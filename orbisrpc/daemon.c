/* daemon.c - orbisRPC core loop (payload ELF only).
 *
 * Auth model: a Discord USER SESSION token pasted into config.json ("token").
 * There is no OAuth flow anymore — OAuth2 access tokens are rejected by the
 * gateway (close 4004), which was why v1 never worked.
 *
 * A `stop` flag is polled so a signal handler can shut the loop down cleanly. */
#include "cfg.h"
#include "auth_rotation.h"
#include "daemon.h"
#include "clock.h"
#include "lock.h"
#include "timesync.h"
#include "health.h"
#include "log.h"
#include "ws.h"
#include "discord.h"
#include "detect.h"
#include "fw.h"
#include "notify.h"
#include "updater.h"
#include "version.h"
#include "jsonlite.h"
#include "art.h"
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include <string.h>
#include <time.h>

extern cfg_t g_cfg;

static volatile sig_atomic_t s_stop = 0;

/* Session/detection state, hoisted to file scope. These were function
 * statics inside daemon_run, which meant a supervisor retry — a second
 * daemon_run call after a fault — inherited the faulted run's leftovers.
 * daemon_sess_reset() (supervise.c) clears them at every daemon_run
 * entry. */
static daemon_sess_t s_sess;

/* The live connection, so the crash hook can close it from normal
 * context. File scope (not a daemon_run local): after a fault the old
 * frame is gone, and the hook must never read an unwound stack. */
static discord_t s_dc;
/* Gate for the hook: 1 only while s_dc is a provably zeroed, fully
 * constructed connection. Makes the hook one-shot and refusable — a
 * second fault inside the hook must not recurse. */
static volatile sig_atomic_t s_dc_armed = 0;

int daemon_crash_cleanup(void){
    /* The pid lock is released unconditionally and first: it is held
     * from before the connection exists, so even a pre-arm fault must
     * not leave the retry standing down against its own stale lock. */
    lock_release();
    if(!s_dc_armed) return 1;   /* nothing else provably exists yet */
    s_dc_armed = 0;             /* one-shot: never touch s_dc twice */
    if(daemon_stop_requested()) return 0;  /* stop asked mid-fault */
    if(s_dc.connected) discord_clear_presence(&s_dc);
    ws_close(&s_dc.ws);
    return 1;
}

/* Tells a running daemon_run() to shut down. */
void daemon_request_stop(void){ s_stop = 1; }
void daemon_clear_stop(void){ s_stop = 0; }
int daemon_stop_requested(void){ return s_stop; }

/* SIGTERM/SIGINT (payload managers, kill): set the flag only — the loops
 * unwind through their normal cleanup (presence clear, session save,
 * socket close) instead of dying mid-write. */
static void daemon_on_signal(int sig){
    (void)sig;
    s_stop = 1;
}

/* sigaction with SA_RESTART instead of signal(): signal() has
 * implementation-defined semantics (handler reset, interrupted syscalls),
 * sigaction pins ours — flag store only, syscalls resume. */
static void daemon_arm_signal(int sig){
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = daemon_on_signal;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    sigaction(sig, &sa, NULL);
}

/* Sleep up to `secs` but wake within a second when stop is requested,
 * so we never hang on a long backoff. Returns 1 stopped. */
static int sleep_stop(int secs){
    for(int i = 0; i < secs; i++){
        if(s_stop) return 1;
        sleep(1);
    }
    return s_stop;
}

/* Last known Settings-screen state: 1 in Settings, 0 elsewhere, -1 never
 * determined. Settings overlays a running game without closing it, so while it
 * is up the presence is REPLACED by a dedicated "In Settings" one rather than
 * the game carrying a changed state line. -1 is sticky: an unknown probe
 * result must not overwrite a real reading. */
static int s_in_settings = -1;
/* 1 once the Settings presence is on the wire, so a poll storm does not
 * re-post it; cleared when Settings closes so returning re-posts the game. */
static int s_settings_posted = 0;
/* Same for the web browser. */
static int s_in_browser = -1;
static int s_browser_posted = 0;
/* If a rotated token is accepted in memory but storage is temporarily
 * unavailable, keep it across reconnects. A later manual config edit wins. */
static auth_rotation_t s_token_rotation;
static char s_pending_qos[512];
static char s_pending_qos_owner[512];

/* Shown when the gateway closes with 4004 (token rejected). */
#define TOKEN_REJECTED_MSG "Discord rejected login - check account recovery prompts"

/* "Firmware 13.52" for the Settings presence. Reads through the same helper the
 * rest of the daemon uses, so an unavailable firmware string degrades to
 * "unknown" instead of stalling the post. */
static void firmware_state_line(char *out, size_t cap){
    if(!g_cfg.show_firmware){ out[0] = 0; return; }  /* "" omits `state` */
    char fw[16] = "unknown";
    fw_version(fw, sizeof fw);
    snprintf(out, cap, "Firmware %s", fw);
}

/* The second visible line, per context. Beside the firmware helper so all
 * three lines of a presence are chosen in one place. */
static const char *details_for_home(void){
    return g_cfg.presence_details_home[0] ? g_cfg.presence_details_home
                                           : "Idling on Home Menu";
}
static const char *details_for_game(const char *title_id){
    /* Media apps get their own phrasing: Discord draws a "Watching" context
     * of its own for type 3, so this line says where, not what. */
    if(title_id && title_id[0] && detect_media_type(title_id) == 3)
        return "Watching on PlayStation 4";
    /* presence_state is legacy ("On PS4"). If the operator has changed it from
     * that default it wins, so an existing customisation is not silently lost
     * now that presence_details_game exists. */
    if(g_cfg.presence_state[0] && strcmp(g_cfg.presence_state, "On PS4") != 0)
        return g_cfg.presence_state;
    return g_cfg.presence_details_game[0] ? g_cfg.presence_details_game
                                           : "Playing on PlayStation 4";
}
static const char *details_for_settings(void){
    return g_cfg.presence_settings_text[0] ? g_cfg.presence_settings_text
                                            : "In Settings";
}

/* Effective artwork URLs, most specific first.
 * large: large_art -> home_art. small: small_art -> large.
 * Returns NULL when nothing is configured, which makes discord.c
 * omit the assets rather than send a dangling key. */
static const char *large_art_url(void){
    if(g_cfg.large_art[0]) return g_cfg.large_art;
    if(g_cfg.home_art[0])   return g_cfg.home_art;
    return NULL;
}
static const char *small_art_url(void){
    if(g_cfg.small_art[0]) return g_cfg.small_art;
    return large_art_url();
}

/* Browser image: its own key, falling back to the idle logo so a console
 * without a browser.png still renders something sensible. */
static const char *browser_art_url(void){
    if(g_cfg.browser_art[0]) return g_cfg.browser_art;
    return large_art_url();
}

static int have_token(const cfg_t *c){
    if(!c || !c->token[0]) return 0;
    if(strcmp(c->token,"SET_ME")==0) return 0;
    /* reject whitespace-only / trivially short tokens */
    size_t n = strlen(c->token);
    if(n < 16) return 0;
    for(size_t i = 0; i < n; i++){
        if(c->token[i]!=' ' && c->token[i]!='\t' && c->token[i]!='\r' && c->token[i]!='\n')
            return 1;
    }
    return 0;
}

static void remember_pending_token(const char *next, const char *old){
    auth_rotation_remember(&s_token_rotation, next, old);
}

static void remember_pending_qos(const char *qos, const char *owner){
    strncpy(s_pending_qos, qos, sizeof s_pending_qos - 1);
    s_pending_qos[sizeof s_pending_qos - 1] = 0;
    strncpy(s_pending_qos_owner, owner, sizeof s_pending_qos_owner - 1);
    s_pending_qos_owner[sizeof s_pending_qos_owner - 1] = 0;
}

static void persist_qos_token(void){
    char qos[512] = {0};
    int got = discord_take_qos_token(&s_dc, qos, sizeof qos);
    if(got == 0) return;
    if(got < 0){
        log_msg("WARN: gateway supplied an unusable QoS token; keeping current value");
        return;
    }
    cfg_t disk = g_cfg;
    if(cfg_load(CFG_PATH, &disk) != 0){
        remember_pending_qos(qos, s_dc.token);
        strncpy(g_cfg.qos_token, qos, sizeof g_cfg.qos_token - 1);
        g_cfg.qos_token[sizeof g_cfg.qos_token - 1] = 0;
        log_msg("WARN: QoS token held in memory; config could not be read");
    } else if(!auth_rotation_matches(&s_token_rotation, disk.token, s_dc.token)){
        log_msg("config token changed during session; ignoring stale QoS token");
    } else if(strcmp(disk.qos_token, qos) != 0 || strcmp(disk.token, s_dc.token) != 0){
        /* The disk may still hold the original generation after a failed
         * rotation save. Persist its replacement together with signed QoS;
         * never restore the obsolete disk credential into live config. */
        strncpy(disk.token, s_dc.token, sizeof disk.token - 1);
        disk.token[sizeof disk.token - 1] = 0;
        strncpy(disk.qos_token, qos, sizeof disk.qos_token - 1);
        disk.qos_token[sizeof disk.qos_token - 1] = 0;
        if(cfg_save(CFG_PATH, &disk) == 0){
            g_cfg = disk;
            memset(&s_token_rotation, 0, sizeof s_token_rotation);
            s_pending_qos[0] = s_pending_qos_owner[0] = 0;
            log_msg("Discord QoS token saved");
        } else {
            remember_pending_qos(qos, s_dc.token);
            strncpy(g_cfg.qos_token, qos, sizeof g_cfg.qos_token - 1);
            g_cfg.qos_token[sizeof g_cfg.qos_token - 1] = 0;
            log_msg("WARN: QoS token active in memory; config save will retry");
        }
    }
    memset(qos, 0, sizeof qos);
}

static void persist_rotated_token(void){
    char rotated[512] = {0};
    int got = discord_take_rotated_token(&s_dc, rotated, sizeof rotated);
    if(got == 0) return;
    if(got < 0){
        log_msg("WARN: gateway supplied an unusable rotated token; keeping current config");
        return;
    }
    if(strcmp(rotated, s_dc.token) == 0){
        memset(rotated, 0, sizeof rotated);
        return;
    }

    cfg_t disk = g_cfg;
    if(cfg_load(CFG_PATH, &disk) != 0){
        remember_pending_token(rotated, s_dc.token);
        strncpy(g_cfg.token, rotated, sizeof g_cfg.token - 1);
        g_cfg.token[sizeof g_cfg.token - 1] = 0;
        strncpy(s_dc.token, rotated, sizeof s_dc.token - 1);
        s_dc.token[sizeof s_dc.token - 1] = 0;
        g_cfg.qos_token[0] = 0;
        s_pending_qos[0] = s_pending_qos_owner[0] = 0;
        log_msg("WARN: token rotation held in memory; config could not be read");
    } else if(!auth_rotation_matches(&s_token_rotation, disk.token, s_dc.token)){
        /* Do not replace a token the user changed while this connection lived. */
        log_msg("config token changed during session; keeping manual value");
    } else {
        char old[sizeof s_dc.token];
        strncpy(old, s_dc.token, sizeof old - 1);
        old[sizeof old - 1] = 0;
        strncpy(disk.token, rotated, sizeof disk.token - 1);
        disk.token[sizeof disk.token - 1] = 0;
        disk.qos_token[0] = 0; /* the signed QoS state is bound to the old token */
        if(cfg_save(CFG_PATH, &disk) == 0){
            g_cfg = disk;
            strncpy(s_dc.token, rotated, sizeof s_dc.token - 1);
            s_dc.token[sizeof s_dc.token - 1] = 0;
            memset(&s_token_rotation, 0, sizeof s_token_rotation);
            s_pending_qos[0] = s_pending_qos_owner[0] = 0;
            log_msg("Discord token rotation saved");
        } else {
            remember_pending_token(rotated, old);
            g_cfg = disk;
            strncpy(g_cfg.token, rotated, sizeof g_cfg.token - 1);
            g_cfg.token[sizeof g_cfg.token - 1] = 0;
            strncpy(s_dc.token, rotated, sizeof s_dc.token - 1);
            s_dc.token[sizeof s_dc.token - 1] = 0;
            g_cfg.qos_token[0] = 0;
            s_pending_qos[0] = s_pending_qos_owner[0] = 0;
            log_msg("WARN: token rotation active in memory; config save will retry");
            notify_throttled("token-save", "Discord token update could not be saved", 300);
        }
    }
    memset(rotated, 0, sizeof rotated);
}

/* Persist the live session (atomic tmp+rename). Called on every
 * transition and on clear so a restart resumes instead of resetting. */
static void sess_save(const char *tid, const char *name, int64_t started){    jl_val_t *r = jl_new_object();
    if(!r) return;
    jl_obj_set(r, "title_id", jl_new_string(tid ? tid : ""));
    jl_obj_set(r, "name", jl_new_string(name ? name : ""));
    jl_obj_set(r, "started", jl_new_number((double)started));
    jl_obj_set(r, "saved_at", jl_new_number((double)time(NULL)));
    char *s = jl_stringify(r);
    jl_free(r);
    if(!s) return;
    FILE *f = fopen("/data/orbisRPC/session.json.new", "wb");
    if(f){
        int ok = (fputs(s, f) >= 0) && (fflush(f) == 0);
        /* force bytes to disk before rename (same as cfg_save) */
        if(ok){ int fd = fileno(f); if(fd < 0 || fsync(fd) != 0) ok = 0; }
        if(fclose(f) != 0) ok = 0;
        if(ok && rename("/data/orbisRPC/session.json.new",
                        "/data/orbisRPC/session.json") != 0){
            log_msg("session save failed; will retry on next transition");
            ok = 0;
        }
        if(!ok) remove("/data/orbisRPC/session.json.new");
    }
    free(s);
}

/* Playtime ledger: append-only "<title_id> <name> <seconds>" per finished
 * session. Totals are computed by readers (app/docs); the daemon only
 * appends, so a corrupt ledger can never break the runtime. Bounded by
 * one rotation generation: ~40 bytes per line means 64KB is years of
 * sessions, and readers that want full history sum both files. */
#define LEDGER_CAP (64*1024)
#define LEDGER_PATH "/data/orbisRPC/playtime.log"
#define LEDGER_PREV "/data/orbisRPC/playtime.log.1"
static void ledger_append(const char *tid, const char *name,
                          int64_t started, int64_t ended){
    if(!tid || !tid[0] || ended <= started || started <= 0) return;
    struct stat st;
    if(stat(LEDGER_PATH, &st) == 0 && (uint64_t)st.st_size > LEDGER_CAP){
        remove(LEDGER_PREV);
        rename(LEDGER_PATH, LEDGER_PREV);
    }
    FILE *f = fopen(LEDGER_PATH, "ab");
    if(!f) return;
    fprintf(f, "%s %s %lld\n", tid, (name && name[0]) ? name : "?",
            (long long)(ended - started));
    fclose(f);
}

/* Deterministic jitter ±20% without RNG state: hash of mono time and a
 * counter. Keeps reconnect storms decorrelated across restarts. */
static int backoff_jitter(int base, unsigned *ctr){
    unsigned x = (unsigned)orbis_mono_s() * 2654435761u + (++(*ctr)) * 40503u;
    x ^= x >> 15; x *= 0x2c1b3c6du; x ^= x >> 12;
    int pct = 80 + (int)(x % 41); /* 80..120 */
    return (base * pct) / 100;
}

/* Next reconnect delay: exponential from base, cap 60s for the first 10
 * consecutive failures, then 10-minute cadence (quiet persistence, never
 * exit). Returns the delay; bumps *fails. */
static int reconnect_delay(int *fails, int base, unsigned *jctr){
    int n = ++(*fails);
    int b = base;
    for(int i = 1; i < n && b < 60; i++) b *= 2;
    if(b > 60) b = 60;
    if(n > 10) b = 600;
    return backoff_jitter(b, jctr);
}

/* Explicit presence state (logged on every transition; no ambiguous
 * states): NONE -> HOME <-> GAME, with reconnects re-posting. */
/* pres_state_t lives in daemon.h (session struct needs it). */
static const char *pres_name(pres_state_t s){
    return s == PS_GAME ? "GAME" : s == PS_HOME ? "HOME" : "NONE";
}
static void pres_set(pres_state_t *cur, pres_state_t next){
    if(*cur == next) return;
    log_msg("STATE: presence %s -> %s", pres_name(*cur), pres_name(next));
    *cur = next;
}
/* Returns 0 normal stop, 1 config error, 2 auth-fatal (bad token). */
int daemon_run(void){
    /* A supervisor retry enters here again after a fault: start from a
     * clean session state instead of the faulted run's leftovers.
     * s_stop is deliberately NOT cleared: a stop requested while the
     * supervisor was backing off must stick. */
    daemon_sess_reset(&s_sess);
    memset(&s_dc, 0, sizeof s_dc);
    s_dc_armed = 1;
#ifdef SIGTERM
    daemon_arm_signal(SIGTERM);
#endif
#ifdef SIGINT
    daemon_arm_signal(SIGINT);
#endif
    if(mkdir(DATA_DIR, 0777) != 0){
        /* EEXIST is fine; anything else means payload can't persist config/log */
        struct stat st;
        if(stat(DATA_DIR, &st) != 0){
            log_init(LOG_PATH);
            log_msg("FATAL: cannot create %s; check /data writable", DATA_DIR);
            log_close();
            return 1;
        }
    }
    log_init(LOG_PATH);
    log_msg("orbisRPC daemon start — build %s %s", __DATE__, __TIME__);
    /* Single writer: a second launch (or a stale pileup from repeated
     * injections) stands down instead of fighting over the gateway. */
    {
        int lr = lock_acquire();
        if(lr == 1){ log_msg("another daemon holds the lock; standing down"); log_close(); return 0; }
        if(lr != 0) log_msg("WARN: lock error; continuing without guard");
    }
    /* Crash recovery: unclean-boot marker + consecutive-failure counter.
     * Only boots that die before going healthy count; normal reboots and
     * token-less idles never trip safe mode. */
    int safe_mode = health_boot_note_crash();
    if(safe_mode)
        log_msg("WARN: repeated unclean boots; safe mode (updates off)");
    if(cfg_load(CFG_PATH, &g_cfg) != 0){
        /* First boot: drop a template so FTP edit is the only step */
        FILE *probe = fopen(CFG_PATH, "rb");
        if(!probe){
            cfg_defaults(&g_cfg);
            if(cfg_save(CFG_PATH, &g_cfg) != 0)
                log_msg("WARN: template config unwritable; token edits will not persist");
            else
                log_msg("created template %s; edit \"token\" then reboot", CFG_PATH);
        } else fclose(probe);
        log_msg("config load failed; running on defaults until valid config appears");
        notify_once_boot("cfgfail", "config.json unreadable - using defaults");
    }
    if(!g_cfg.enabled){ log_msg("disabled in config; exiting"); health_mark_clean(); log_close(); return 0; }
    log_set_debug(g_cfg.debug);
    if(g_cfg.debug) log_dbg("debug logging on (config)");
    /* Boot census. MUST come after cfg_load(): an earlier revision printed this
     * before the load and so reported the zero-initialised global, which made a
     * correctly enabled pkgzone_enabled read as "off". */
    {
        char fw[16] = "unknown";
        fw_version(fw, sizeof fw);
        int st = detect_settings_active();
        log_msg("env: firmware=%s pkgzone=%s settings_probe=%s debug=%d",
                fw, g_cfg.pkgzone_enabled ? "on" : "off",
                st == 1 ? "in-settings" : (st == 0 ? "ok" : "unavailable"),
                g_cfg.debug);
    }
    /* LAN kill switch: lets a fresh payload be injected without rebooting the
     * console. Non-fatal if the bind fails. */
    /* Wall-clock correction for Discord timestamps (PSN time sync is
     * typically blocked on jailbroken consoles). Boot sweep is bounded;
     * hourly ticks are single-host attempts (never stall heartbeats).
     * A fresh sync also steps the system clock itself so TLS cert
     * verification never fails after an RTC loss. */
    if(time_sync_all() == 0) time_apply_offset();
    if(!have_token(&g_cfg)){
        /* Waiting for configuration is a HEALTHY boot, not a crash:
         * mark clean so token-less boots never trip safe mode. */
        health_mark_healthy();
        log_msg("FATAL: put your Discord user token in %s as \"token\":\"...\"", CFG_PATH);
        notify_once_boot("notoken", "No Discord token - edit config.json");
        log_close();
        return 1;
    }

    /* Update check once per boot, before first connect. Never fatal. Does not
     * download or install anything; only notifies when a newer signed
     * release exists, so the user can run the setup app. */
    if(g_cfg.auto_update && !safe_mode){
        /* Update CHECK only. Nothing is downloaded or installed here -- the
         * setup app applies updates. A verified newer release just raises a
         * notification so the user knows to run it. */
        char newer[32] = "";
        int ur = updater_check_notify(newer, sizeof newer);
        if(ur > 0){
            char msg[96];
            snprintf(msg, sizeof msg,
                     "Update %s available - use setup app to update", newer);
            notify_once_boot("update", msg);
            log_msg("updater: notification sent for %s (local %s)",
                    newer, ORBISRPC_VERSION);
        } else {
            log_msg("updater: %s (local %s)",
                    ur == 0 ? "already current" : "check failed",
                    ORBISRPC_VERSION);
        }
    } else if(safe_mode){
        log_msg("updater: skipped (safe mode)");
    }

    int base_poll = g_cfg.poll_interval_s;
    if(base_poll < 5) base_poll = 5;
    if(base_poll > 60) base_poll = 60;
    /* Reconnect policy: exponential backoff with deterministic jitter,
     * cap 60s for the first 10 consecutive failures, then 10-minute
     * cadence (quiet persistence, never exit). Success resets. */
    int backoff = base_poll;
    int conn_fails = 0;
    char rejected_token[sizeof g_cfg.token] = {0};
    unsigned jctr = 0;
    /* "Connected to Discord" policy: once on the first successful connect,
     * then only again if a connect attempt actually failed in between.
     *
     * Deliberately NOT counting mid-session drops. The gateway can drop and
     * come straight back (heartbeat ACKs, a brief outage) and treating that
     * as a failure would re-notify on every cycle, which is the spam this
     * replaces. Only a failed connect attempt -- rc != 0 from
     * discord_connect -- counts as "we could not reach Discord". */
    int announced_connected = 0;
    int connect_failed_since_ok = 0;
    /* Health metrics (hourly HEALTH line). */
    int64_t boot_mono = orbis_mono_s();
    unsigned n_posts = 0, n_reconnects = 0;
    int64_t last_health = 0;
    for(;;){ /* outer: connect cycles with backoff on failure */
        if(s_stop) break;
        /* re-read config every cycle so token edits land without a reboot.
         * On parse failure keep s_sess.last-good config instead of stale defaults. */
        {
            cfg_t next = g_cfg;
            if(cfg_load(CFG_PATH, &next) == 0){
                auth_rotation_action_t rotation_action =
                    auth_rotation_disk_action(&s_token_rotation, next.token);
                if(rotation_action == AUTH_ROTATION_APPLY){
                    strncpy(next.token, s_token_rotation.value, sizeof next.token - 1);
                    next.token[sizeof next.token - 1] = 0;
                    next.qos_token[0] = 0;
                    if(cfg_save(CFG_PATH, &next) == 0){
                        memset(&s_token_rotation, 0, sizeof s_token_rotation);
                        log_msg("pending Discord token rotation saved");
                    } else {
                        log_msg("WARN: pending token rotation still cannot be saved");
                    }
                } else if(rotation_action == AUTH_ROTATION_DISCARD){
                    memset(&s_token_rotation, 0, sizeof s_token_rotation);
                    log_msg("pending Discord token rotation discarded after config token changed");
                }
                if(s_pending_qos[0]){
                    if(strcmp(next.token, s_pending_qos_owner) == 0){
                        strncpy(next.qos_token, s_pending_qos, sizeof next.qos_token - 1);
                        next.qos_token[sizeof next.qos_token - 1] = 0;
                        if(cfg_save(CFG_PATH, &next) == 0){
                            s_pending_qos[0] = s_pending_qos_owner[0] = 0;
                            log_msg("pending Discord QoS token saved");
                        } else {
                            log_msg("WARN: pending QoS token still cannot be saved");
                        }
                    } else {
                        s_pending_qos[0] = s_pending_qos_owner[0] = 0;
                    }
                }
                if(strcmp(next.token, g_cfg.token) != 0 && next.qos_token[0]){
                    next.qos_token[0] = 0;
                    s_pending_qos[0] = s_pending_qos_owner[0] = 0;
                    if(cfg_save(CFG_PATH, &next) != 0)
                        log_msg("WARN: stale QoS token cleared in memory but not on disk");
                    else
                        log_msg("cleared QoS token after account token changed");
                }
                if(next.debug != g_cfg.debug)
                    log_msg("debug logging %s", next.debug ? "on" : "off");
                g_cfg = next;
                log_set_debug(g_cfg.debug);
            }
            else log_msg("config reload failed; keeping last-good config");
        }
        if(!have_token(&g_cfg)){
            log_msg("no token in config; waiting %ds", backoff);
            notify_once_boot("notoken", "No Discord token - edit config.json");
            if(sleep_stop(backoff)) break;
            continue;
        }
        if(auth_token_wait_for_change(rejected_token, g_cfg.token)){
            /* 4004 is an explicit auth rejection, not a transient network
             * error. Repeated IDENTIFY with the same credential cannot heal
             * it and may make recovery noisier. Poll config on normal cadence. */
            if(sleep_stop(base_poll)) break;
            continue;
        }
        if(rejected_token[0]) memset(rejected_token, 0, sizeof rejected_token);
        int rc = discord_connect(&s_dc, g_cfg.token, g_cfg.qos_token);
        if(rc == -2){
            /* Keep the daemon alive, but do not identify repeatedly with a
             * credential the gateway has explicitly rejected. */
            snprintf(rejected_token, sizeof rejected_token, "%s", g_cfg.token);
            discord_clear_credentials(&s_dc);
            log_msg("WARN: token rejected by gateway (close 4004). "
                    "Update \"token\" in %s to retry", CFG_PATH);
            notify_throttled("token4004", TOKEN_REJECTED_MSG, 300);
            connect_failed_since_ok = 1;
            if(sleep_stop(base_poll)) break;
            continue;
        }
        if(rc != 0){
            discord_clear_credentials(&s_dc);
            int wait = reconnect_delay(&conn_fails, base_poll, &jctr);
            /* Throttled: the backoff already grows, so an unthrottled
             * notification here fires every 13s and then every minute. */
            notify_throttled("connect", "Cannot reach Discord - retrying", 300);
            connect_failed_since_ok = 1;
            log_msg("gateway connect failed (attempt %d); retry in %ds",
                    conn_fails, wait);
            if(sleep_stop(wait)) break;
            continue;
        }
        persist_rotated_token();
        persist_qos_token();
        if(!announced_connected || connect_failed_since_ok){
            notify_show("Connected to Discord");
            announced_connected = 1;
            connect_failed_since_ok = 0;
            log_msg("notify: %s",
                    conn_fails > 0 ? "connected after failed attempts"
                                   : "connected (first)");
        }
        if(conn_fails > 0)
            log_msg("gateway connected after %d failures", conn_fails);
        conn_fails = 0;
        n_reconnects++;
        backoff = g_cfg.poll_interval_s; /* success resets backoff */
        if(backoff < 5) backoff = 5;
        if(backoff > 60) backoff = 60;

        /* s_sess.last/s_sess.started live OUTSIDE the session loop so the playing timer
         * survives gateway reconnects (same game keeps its original start).
         * A reconnect re-posts presence only when needed, timer intact.
         * (State itself lives in file-scope s_sess; see daemon.h.) */
        /* Fresh (re)connect invalidates whatever Discord shows: re-post the
         * current state (game via need_post below, home via s_sess.home_posted
         * reset) so a drop can never leave a stale/blank tile behind. */
        s_sess.home_posted = 0;
        /* Resume window: if the same title vanishes briefly (detection
         * flicker, quick menu hop) and returns within 10 minutes, the
         * timer resumes instead of resetting to 0:00.
         * (prev_* live in file-scope s_sess; see daemon.h.) */
        /* Cross-restart resume: a previous run's session (saved on every
         * transition) seeds the resume window, so reboots and updates
         * keep the timer instead of resetting it. */
        {
            if(!s_sess.sess_restored){
                s_sess.sess_restored = 1;
                FILE *sf = fopen("/data/orbisRPC/session.json", "rb");
                if(sf){
                    fseek(sf, 0, SEEK_END);
                    long ssz = ftell(sf);
                    fseek(sf, 0, SEEK_SET);
                    if(ssz > 0 && ssz < 1024){
                        char *sb = (char*)malloc((size_t)ssz + 1);
                        if(sb && fread(sb, 1, (size_t)ssz, sf) == (size_t)ssz){
                            sb[ssz] = 0;
                            jl_val_t *sr = jl_parse(sb, (size_t)ssz);
                            if(sr && sr->type == JL_OBJECT){
                                const jl_val_t *t = jl_obj_get(sr, "title_id");
                                const jl_val_t *s2 = jl_obj_get(sr, "started");
                                if(t && t->type == JL_STRING && t->str &&
                                   s2 && s2->type == JL_NUMBER){
                                    /* Validate before trusting: 9-char id,
                                     * sane epoch (2020-2100). A corrupt file
                                     * must never seed a bogus timer. */
                                    int64_t st = (int64_t)s2->num;
                                    if(strlen(t->str) == 9 &&
                                       st >= 1577836800LL &&
                                       st <= 4102444800LL){
                                        strncpy(s_sess.prev_tid, t->str, sizeof s_sess.prev_tid-1);
                                        s_sess.prev_started = st;
                                        /* Wall saved_at can't mix with the
                                         * monotonic resume window: start the
                                         * window at boot. Same title seen
                                         * within minutes of boot resumes. */
                                        s_sess.prev_end = orbis_mono_s();
                                        log_msg("session restored: %s (timer may resume)",
                                                s_sess.prev_tid);
                                    } else {
                                        log_msg("session file failed validation; starting fresh");
                                    }
                                }
                                jl_free(sr);
                            }
                            free(sb);
                        } else if(sb) free(sb);
                    }
                    fclose(sf);
                }
            }
        }
        /* Transition debounce: shell flickers during launches, so a new
         * title (or disappearance) needs consecutive polls (2 at ~1s).
         * (Counters live in file-scope s_sess; see daemon.h.) */
        int64_t last_poll = 0;
        int64_t last_alive = 0;
        /* re-post after every (re)connect so Discord never sticks on stale */
        int need_post = s_sess.active && s_sess.last[0];
        while(!s_stop){ /* inner: live session, serviced every second */
            /* Poll every tick, not every detect poll: STOP must land within a
             * second even while a title is being resolved. */
            int64_t now = orbis_mono_s();
            if(now - s_sess.last_tsync >= 3600){ s_sess.last_tsync = now; if(time_sync() == 0) time_apply_offset(); }
            if(now != last_poll){
                last_poll = now;
                if(now - last_alive >= 60){
                    last_alive = now;
                    log_msg("alive: %s", s_sess.active ? s_sess.last : "idle");
                }
                /* State reconciliation: re-post current presence every
                 * 15 min so a silently desynced tile (dropped update,
                 * stale cache, missed reconnect) heals itself without
                 * any state change. Timer/source values are re-read at
                 * post time, so this never disturbs a live session. */
                {
                    if(s_sess.last_reconcile == 0) s_sess.last_reconcile = now;
                    if(now - s_sess.last_reconcile >= 900){
                        s_sess.last_reconcile = now;
                        if(s_sess.active && s_sess.last[0]) need_post = 1;
                        else s_sess.home_posted = 0;
                        log_msg("reconcile: reposting current state");
                    }
                }
                /* Hourly health metrics: uptime, posts, reconnects, fails. */
                if(now - last_health >= 3600){
                    last_health = now;
                    log_msg("HEALTH: %lldh uptime, %u posts, %u reconnects, %d recent fails",
                            (long long)((now - boot_mono) / 3600),
                            n_posts, n_reconnects, conn_fails);
                }
                /* A boot that survives HEALTH_STABLE_SECS of runtime was
                 * healthy: clear the unclean-boot marker so only real
                 * crashes count toward safe mode. */
                if(!s_sess.healthy_marked && now - boot_mono >= HEALTH_STABLE_SECS){
                    s_sess.healthy_marked = 1;
                    health_mark_healthy();
                    log_msg("health: boot marked healthy");
                }
                char name[128] = "";
                int scan_unknown = 0;
                    {
                    int dr = detect_current_game(name, sizeof name, NULL, 0);
                    /* -2 uncertain and -3 system app covering a live game both
                     * mean hold. They are logged apart because only one says
                     * the daemon has lost its view of the console. Both must
                     * suppress the clear path below: neither is a close. */
                    scan_unknown = (dr == -2 || dr == -3);
                    if(dr == -2) log_dbg("detect read failed; holding");
                    else if(dr == -3) log_dbg("system UI in front; holding %s", s_sess.last);
                    if(scan_unknown) name[0] = 0;
                    }
                    /* Settings screen. Only a definite answer moves the state:
                     * a probe that cannot see kern.msgbuf leaves the s_sess.last known
                     * value alone instead of flapping the presence off and on. */
                    {
                    int sstate = detect_settings_active();
                    if(sstate >= 0 && sstate != s_in_settings){
                        s_in_settings = sstate;
                        log_msg("settings screen: %s", sstate ? "in" : "out");
                        /* Leaving Settings always hands control back to the
                         * normal path below: re-arm the game post, and re-arm
                         * the home post so returning to the home screen is
                         * allowed to speak again. Previously this only set
                         * need_post, which the home-screen branch below never
                         * looks at, so leaving Settings wedged the daemon. */
                        if(!sstate){
                            s_settings_posted = 0;
                            need_post = s_sess.active && s_sess.last[0];
                            s_sess.home_posted = 0;
                        }
                    }
                    }

                    /* Browser: same shape as Settings. Probed only when
                     * Settings is not in front, so Settings keeps priority
                     * when both would match. */
                    int bstate = (s_in_settings == 1) ? -1 : detect_browser_active();
                    if(bstate >= 0 && bstate != s_in_browser){
                        s_in_browser = bstate;
                        log_msg("browser screen: %s", bstate ? "in" : "out");
                        if(!bstate){
                            s_browser_posted = 0;
                            need_post = s_sess.active && s_sess.last[0];
                            s_sess.home_posted = 0;
                        }
                    }
                    /* While Settings is up it replaces the whole presence: its
                     * own name, no artwork, no timer, no small icon. The game
                     * underneath keeps `s_sess.started`, so returning restores the
                     * original elapsed time rather than restarting the clock.
                     * It also counts as "hold" below -- Settings must never
                     * look like a game closing. */
                    int in_settings = (s_in_settings == 1);
                    int in_browser = (s_in_settings != 1 && s_in_browser == 1);
                    /* Neither may look like a game closing. */
                    if(in_settings || in_browser) scan_unknown = 1;
                    /* Visibility gates. Detection always runs; this only
                     * decides whether Discord is told about it.
                     *
                     * "Don't send" is safe wherever something could be
                     * running underneath -- Settings, the browser, a media app,
                     * homebrew. Home is the exception: nothing is underneath
                     * there, so a closed game must be cleared rather than held,
                     * otherwise the tile would linger forever. */
                    const char *cur_tid_now = detect_last_titleid();
                    int gate_idle    = g_cfg.show_idle    == 0;
                    int gate_media   = (g_cfg.show_media   == 0) &&
                                       cur_tid_now && detect_media_type(cur_tid_now) == 3;
                    int gate_homebrew = (g_cfg.show_homebrew == 0) &&
                                        is_homebrew_id(cur_tid_now);
                    if(in_settings && gate_idle) log_dbg("presence: settings suppressed");
                    else if(in_settings && !s_settings_posted){
                        char st[64];
                        firmware_state_line(st, sizeof st);
                        int pr = discord_set_presence_ex(&s_dc, st,
                            "PlayStation 4", details_for_settings(),
                            g_cfg.asset_idle, NULL,
                            "settings", g_cfg.application_id, NULL,
                            large_art_url(), NULL, 0);
                        if(pr == 0){
                            log_msg("presence: settings");
                            s_settings_posted = 1;
                            n_posts++;
                        } else {
                            log_msg("settings presence send failed; will retry");
                        }
                    } else if(in_browser && gate_idle) log_dbg("presence: browser suppressed");
                    else if(in_browser && !s_browser_posted){
                        /* Standalone presence: its own name, line and artwork,
                         * no timer and no badge. A game may be running
                         * underneath; it comes back on its own when the
                         * browser closes. */
                        char st[64];
                        firmware_state_line(st, sizeof st);
                        int pr = discord_set_presence_ex(&s_dc, st,
                            "PlayStation 4", "Using Web Browser",
                            NULL, NULL,
                            "browser", g_cfg.application_id, NULL,
                            browser_art_url(), NULL, 0);
                        if(pr == 0){
                            log_msg("presence: browser");
                            s_browser_posted = 1;
                            n_posts++;
                        } else {
                            log_msg("browser presence send failed; will retry");
                        }
                    }

                /* Settings or the browser has already spoken for this poll:
                 * skip the whole game/home/clear decision below rather than
                 * letting a stale name or a pending miss counter fight it. */
                else if(name[0]){
                    const char *cur_tid = detect_last_titleid();
                    if(!cur_tid) cur_tid = "";
                    if(!strcmp(cur_tid, s_sess.cand_title)){
                        s_sess.cand_hits++;
                    } else {
                        strncpy(s_sess.cand_title, cur_tid, sizeof s_sess.cand_title-1);
                        s_sess.cand_title[sizeof s_sess.cand_title-1] = 0;
                        s_sess.cand_hits = 1;
                    }
                    s_sess.miss_hits = 0;
                    if(s_sess.cand_hits >= 2){
                    if(!s_sess.active || strcmp(cur_tid,s_sess.sess_tid)!=0){
                        /* Switching away from a live session: bank its time.
                         * (Fresh starts and resumes have nothing to bank;
                         * the clear path already banked ended sessions.) */
                        if(s_sess.active && s_sess.sess_tid[0] &&
                           strcmp(cur_tid,s_sess.sess_tid)!=0)
                            ledger_append(s_sess.sess_tid, s_sess.last, s_sess.started, time_fixed());
                        s_sess.active = 1;
                        strncpy(s_sess.sess_tid, cur_tid, sizeof s_sess.sess_tid-1);
                        strncpy(s_sess.last, name, sizeof s_sess.last-1);
                        s_sess.last[sizeof s_sess.last-1] = 0;
                        if(!strcmp(cur_tid, s_sess.prev_tid) &&
                           s_sess.prev_started > 0 &&
                           now - s_sess.prev_end < 600){
                            s_sess.started = s_sess.prev_started;
                            log_msg("SESSION_RESUMED title=%s (gap %llds, timer kept)",
                                    cur_tid[0]?cur_tid:"?", (long long)(now - s_sess.prev_end));
                        } else {
                            s_sess.started = time_fixed();
                        }
                        need_post = 1;
                        log_msg("GAME_DETECTED title=%s name=%s", cur_tid[0]?cur_tid:"?", name);
                        art_cache_clear();
                        sess_save(cur_tid, name, s_sess.started);
                        /* No metadata is written to config.json any more.
                         * detect.c persists id+name+cover to games_cache.json
                         * when a title resolves; that file is the only place
                         * the daemon learns anything. config.json keeps only
                         * operator settings and hand-edited overrides. */
                    } else if(strcmp(name,s_sess.last)!=0){
                        strncpy(s_sess.last, name, sizeof s_sess.last-1);
                        s_sess.last[sizeof s_sess.last-1] = 0;
                        need_post = 1;
                    }
                    /* A media app or homebrew title that is switched off is
                     * never announced. Sending nothing leaves whatever was
                     * s_sess.last posted standing, so a game underneath keeps
                     * showing and an empty console simply stays quiet. */
                    int gate_this_title = gate_media || gate_homebrew;
                    if(need_post && gate_this_title){
                        log_dbg("presence: %s suppressed", s_sess.last);
                        need_post = 0;
                    }
                    if(need_post){
                        /* name / details / state: what is playing, where it
                         * is, and the firmware. */
                        char gst[64];
                        firmware_state_line(gst, sizeof gst);
                        const char *state = gst[0] ? gst : NULL;
                        const char *details = details_for_game(s_sess.sess_tid);
                        const char *tart = detect_last_art();
                        /* Playback queue of one: only clear need_post after
                         * the bytes actually go out. A failed send stays
                         * queued and rides the next tick/reconnect. */
                        int pr = discord_set_presence_ex(&s_dc, state, s_sess.last, details,
                            NULL, g_cfg.asset_playing,
                            s_sess.sess_tid[0]?s_sess.sess_tid:NULL, g_cfg.application_id,
                            g_cfg.art_base_url, tart,
                            small_art_url(), s_sess.started);
                        if(pr == 0){
                            log_msg("presence: %s", s_sess.last);
                            pres_set(&s_sess.pres, PS_GAME);
                            n_posts++;
                            s_sess.active = 1; need_post = 0;
                        } else {
                            log_msg("presence send failed; will retry");
                        }
                    }
                    }
                }else if(s_sess.active && !scan_unknown){
                    s_sess.cand_title[0] = 0;
                    s_sess.cand_hits = 0;
                    if(++s_sess.miss_hits >= 2){
                        discord_clear_presence(&s_dc);
                        log_msg("presence cleared");
                        pres_set(&s_sess.pres, PS_NONE);
                        ledger_append(s_sess.sess_tid, s_sess.last, s_sess.started, time_fixed());
                        strncpy(s_sess.prev_tid, s_sess.sess_tid, sizeof s_sess.prev_tid-1);
                        s_sess.prev_started = s_sess.started;
                        s_sess.prev_end = now;
                        remove("/data/orbisRPC/session.json");
                        s_sess.last[0]=0; s_sess.sess_tid[0]=0; s_sess.active=0; s_sess.started=0;
                        s_sess.home_posted = 0;
                    }
                } else if(!gate_idle && !s_sess.home_posted && !scan_unknown){
                    /* Home screen support: no game running. Post a timerless
                     * home presence once (instead of bare online), clear it
                     * the moment a game commits. */
                    char hst[64];
                    firmware_state_line(hst, sizeof hst);
                    const char *state = hst[0] ? hst : NULL;
                    if(discord_set_presence_ex(&s_dc, state, "PlayStation 4",
                        details_for_home(),
                        g_cfg.asset_idle, NULL, "home",
                        g_cfg.application_id, g_cfg.art_base_url,
                        large_art_url(), NULL, 0) == 0){
                        log_msg("presence: home");
                        pres_set(&s_sess.pres, PS_HOME);
                        n_posts++;
                        s_sess.home_posted = 1;
                    } else {
                        log_msg("home presence send failed; will retry");
                    }
                }
            }

            /* service the gateway every pass (~1s): heartbeats must never be
             * more than a second or two late or the server drops us */
            int tr = discord_tick(&s_dc);
            persist_rotated_token();
            persist_qos_token();
            if(tr == -2){
                /* Token rejected mid-session: wait for a credential change. */
                snprintf(rejected_token, sizeof rejected_token, "%s", g_cfg.token);
                log_msg("WARN: token rejected (close 4004). Update %s to retry", CFG_PATH);
                notify_throttled("token4004", TOKEN_REJECTED_MSG, 300);
                ws_close(&s_dc.ws);
                pres_set(&s_sess.pres, PS_NONE);
                if(sleep_stop(reconnect_delay(&conn_fails, base_poll, &jctr))) break;
                break;
            }
            if(tr == -3){
                int wait = reconnect_delay(&conn_fails, base_poll, &jctr);
                log_msg("invalid session; fresh identify (attempt %d) in %ds",
                        conn_fails, wait);
                ws_close(&s_dc.ws);
                pres_set(&s_sess.pres, PS_NONE);
                if(sleep_stop(wait)) break;
                break;
            }
            if(tr != 0){
                int wait = reconnect_delay(&conn_fails, base_poll, &jctr);
                log_msg("gateway dropped (attempt %d); reconnecting in %ds",
                        conn_fails, wait);
                ws_close(&s_dc.ws);
                pres_set(&s_sess.pres, PS_NONE);
                if(sleep_stop(wait)) break;
                break;
            }

            for(int i = 0; i < 10 && !s_stop; i++) usleep(100000);
        }
        if(s_stop && s_dc.connected) discord_clear_presence(&s_dc);
        discord_clear_credentials(&s_dc);
        ws_close(&s_dc.ws);
    }
    /* Session file already reflects the live session (saved on every
     * transition), so shutdown is: clear presence (above), release the
     * lock so a successor starts immediately, close the log. */
    lock_release();
    memset(g_cfg.token, 0, sizeof g_cfg.token);
    memset(g_cfg.qos_token, 0, sizeof g_cfg.qos_token);
    memset(rejected_token, 0, sizeof rejected_token);
    memset(&s_token_rotation, 0, sizeof s_token_rotation);
    memset(s_pending_qos, 0, sizeof s_pending_qos);
    memset(s_pending_qos_owner, 0, sizeof s_pending_qos_owner);
    discord_clear_credentials(&s_dc);
    log_close();
    return 0;
}
