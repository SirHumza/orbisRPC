/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 SirHumza and CyberMask367 - see NOTICE for attribution terms.
 */
/* daemon.c - orbisRPC core loop (payload ELF only).
 *
 * Auth model: a Discord USER SESSION token pasted into config.json ("token").
 * There is no OAuth flow anymore — OAuth2 access tokens are rejected by the
 * gateway (close 4004), which was why v1 never worked.
 *
 * A `stop` flag is polled so a signal handler can shut the loop down cleanly. */
#include "cfg.h"
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
#include "fingerprint_gen.h"
#include "jsonlite.h"
#include "art.h"
#include "sched.h"
#include "diag.h"
#include "token.h"
#include "goldhen.h"
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <netdb.h>
#include <sys/socket.h>

extern cfg_t g_cfg;

#define DIAG_REQUEST "/data/orbisRPC/diagnose.request"
#define DIAG_PATH "/data/orbisRPC/diagnostic.txt"
#define STOP_PATH "/data/orbisRPC/stop"

#ifndef ORBISRPC_BUILD_HASH
#define ORBISRPC_BUILD_HASH "dev"
#endif

static volatile sig_atomic_t s_stop = 0;
static int64_t s_boot_mono = -1;
static int64_t s_last_post_mono = -1;
static int64_t s_last_diag_check = 0;
/* argv[0] as launched; set by main() before daemon_run(). Used only to make
 * a wrong install location visible in the log (D-17). */
static char s_launch_path[256];

void daemon_set_launch_path(const char *p){
    if(!p) return;
    snprintf(s_launch_path, sizeof s_launch_path, "%s", p);
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

/* D-05: a stop file is the documented way to stop a daemon that was injected
 * without a controlling terminal. Checked once per second, so killing the
 * process no longer requires deleting the lock and rebooting. The file is
 * deliberately left in place: with two daemons alive (a legacy reload), the
 * first to see it must not consume the request before the second polls.
 * daemon_run() clears it at startup, so the next launch starts clean. */
static int stop_file_present(void){
    return access(STOP_PATH, F_OK) == 0;
}

/* Sleep up to `secs` but wake within a second when stop is requested,
 * so we never hang on a long backoff. Returns 1 stopped. */
static void diag_service(void);
static int sleep_stop(int secs){
    for(int i = 0; i < secs; i++){
        if(s_stop) return 1;
        if(stop_file_present()){
            log_msg("stop requested (%s)", STOP_PATH);
            s_stop = 1;
            return 1;
        }
        sleep(1);
        diag_service();
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
    /* An explicit per-title rule always wins: it is the operator saying
     * exactly what this game should say. */
    const cfg_title_rule_t *rule = title_id ? cfg_title_rule(&g_cfg, title_id)
                                            : NULL;
    if(rule && rule->details[0]) return rule->details;
    /* Media apps get their own phrasing: Discord draws a "Watching" context
     * of its own for type 3, so this line says where, not what. */
    if(title_id && title_id[0] && detect_media_type(title_id) == 3)
        return g_cfg.presence_details_media[0] ? g_cfg.presence_details_media
                                               : "Watching on PlayStation 4";
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

/* Shape check with a human-readable reason (D-12). The daemon must never
 * reject a token silently: every failure path logs `why`. */
static int usable_token(const cfg_t *c, char *why, size_t cap){
    if(!c){ if(why && cap) snprintf(why, cap, "config unavailable"); return 0; }
    return token_reason(c->token, why, cap) == 0;
}

/* D-15: a console whose system DNS points at 127.0.0.1 (nanoDNS not running)
 * otherwise retries forever with no diagnosis. One getaddrinfo at startup is
 * enough to tell the user which layer is broken. */
static int dns_resolves(const char *host){
    struct addrinfo hints, *res = NULL;
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    int rc = getaddrinfo(host, "443", &hints, &res);
    if(res) freeaddrinfo(res);
    return rc == 0;
}

/* Playtime ledger: one JSON object per finished session. JSONL keeps the
 * name unambiguous (spaces, quotes) and records start/end so a reader can
 * build daily/weekly totals; the old "<id> <name> <seconds>" lines are
 * still accepted by scripts/playtime_report.py. The daemon only appends,
 * so a corrupt ledger can never break the runtime. */
static void ledger_append(const char *tid, const char *name,
                          int64_t started, int64_t ended){
    if(!tid || !tid[0] || ended <= started || started <= 0) return;
    jl_val_t *r = jl_new_object();
    if(!r) return;
    jl_obj_set(r, "title_id", jl_new_string(tid));
    jl_obj_set(r, "name", jl_new_string((name && name[0]) ? name : "?"));
    jl_obj_set(r, "started", jl_new_int(started));
    jl_obj_set(r, "ended", jl_new_int(ended));
    jl_obj_set(r, "seconds", jl_new_int(ended - started));
    char *s = jl_stringify(r);
    jl_free(r);
    if(!s) return;
    FILE *f = fopen("/data/orbisRPC/playtime.log", "ab");
    if(f){
        fprintf(f, "%s\n", s);
        fclose(f);
    }
    free(s);
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
 * consecutive failures, then the configured slow cadence
 * (reconnect_max_s, default 600 = quiet persistence, never exit).
 * Returns the delay; bumps *fails. */
static int reconnect_delay(int *fails, int base, int max_s, unsigned *jctr){
    int n = ++(*fails);
    int b = base;
    for(int i = 1; i < n && b < 60; i++) b *= 2;
    if(b > 60) b = 60;
    if(n > 10) b = max_s > 60 ? max_s : 60;
    return backoff_jitter(b, jctr);
}

/* A rejected token (close 4004) does not heal by itself: retrying it burns
 * IDENTIFY attempts against the limit Discord actually enforces, which is
 * exactly the behaviour that trips abuse protection. Park until "token" in
 * config.json differs from the rejected one. Config is re-read every 10s so
 * a fix lands without rebooting, and the wait is stop-aware. Returns 1 when
 * stop was requested. */
static int park_until_token_change(const char *rejected){
    if(!rejected) rejected = "";
    log_msg("auth: parked until \"token\" in %s changes", CFG_PATH);
    notify_once_boot("badtoken", "Discord rejected the token - edit config.json");
    for(unsigned waits = 0;; waits++){
        if(sleep_stop(10)) return 1;
        cfg_t next = g_cfg;
        if(cfg_load(CFG_PATH, &next) != 0) continue;   /* keep the old token */
        g_cfg = next;
        if(strcmp(g_cfg.token, rejected) != 0){
            log_msg("auth: token changed; retrying connect");
            return 0;
        }
        if(waits % 60 == 59)   /* ~10 minutes */
            log_msg("auth: still parked (token unchanged)");
    }
}

/* Explicit presence state (logged on every transition; no ambiguous
 * states): NONE -> HOME <-> GAME, with reconnects re-posting. */
typedef enum { PS_NONE = 0, PS_HOME, PS_GAME } pres_state_t;
static const char *pres_name(pres_state_t s){
    return s == PS_GAME ? "GAME" : s == PS_HOME ? "HOME" : "NONE";
}
static void pres_set(pres_state_t *cur, pres_state_t next){
    if(*cur == next) return;
    log_msg("STATE: presence %s -> %s", pres_name(*cur), pres_name(next));
    *cur = next;
}

/* Create /data/orbisRPC/diagnose.request (FTP/SMB) and the daemon writes a
 * redacted bundle within five seconds, then removes the request. Serviced
 * from sleep_stop() too, so it works while parked on a rejected token. */
static void diag_service(void){
    int64_t now = orbis_mono_s();
    if(s_last_diag_check && now - s_last_diag_check < 5) return;
    s_last_diag_check = now;
    if(access(DIAG_REQUEST, F_OK) != 0) return;
    char fw[16] = "unknown";
    fw_version(fw, sizeof fw);
    char ring[64];
    discord_close_ring_text(ring, sizeof ring);
    diag_info_t info;
    info.version = ORBISRPC_VERSION;
    info.firmware = fw;
    info.uptime_s = s_boot_mono > 0 ? now - s_boot_mono : 0;
    info.last_post_age_s = s_last_post_mono > 0 ? now - s_last_post_mono : -1;
    info.close_ring = ring;
    if(diag_write_file(DIAG_PATH, &info, &g_cfg, LOG_PATH) == 0)
        log_msg("diagnostic bundle written: %s", DIAG_PATH);
    else
        log_msg("diagnostic bundle failed");
    remove(DIAG_REQUEST);
}
/* Returns 0 normal stop, 1 config error, 2 auth-fatal (bad token). */
int daemon_run(void){
    s_stop = 0;
    /* A stop file is a request for daemons that were already running. Clear
     * any leftover before this instance starts polling, so a stop sent during
     * the previous session cannot kill the fresh launch. */
    remove(STOP_PATH);
#ifdef SIGTERM
    signal(SIGTERM, daemon_on_signal);
#endif
#ifdef SIGINT
    signal(SIGINT, daemon_on_signal);
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
    s_boot_mono = orbis_mono_s();
    log_msg("orbisRPC daemon start — version %s build %s (%s %s)",
            ORBISRPC_VERSION, ORBISRPC_BUILD_HASH, __DATE__, __TIME__);
    log_msg("log: %s", log_path());
    if(s_launch_path[0])
        log_msg("payload launched as: %s", s_launch_path);
    if(access("/data/payloads", F_OK) != 0){
        log_msg("hint: /data/payloads does not exist — GoldHEN loads payloads "
                "from that exact lowercase path (D-17)");
    }
    {
        char gh[32];
        if(detect_goldhen_version(gh, sizeof gh) == 0)
            log_msg("GoldHEN: %s (Utilities entry requires %s+)", gh, GOLDHEN_MIN);
        else
            log_msg("GoldHEN: version not visible in kern.msgbuf; "
                    "the Utilities payload entry requires %s+", GOLDHEN_MIN);
    }
    /* Config first: the lock timings below are configurable, and a fresh
     * injection must use the operator's stale threshold when deciding whether
     * the previous daemon is wedged. Load is read-only unless the file is
     * missing (first boot template). */
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
    log_set_cap((size_t)g_cfg.log_max_kb * 1024);
    log_set_debug(g_cfg.debug);
    if(g_cfg.debug) log_dbg("debug logging on (config)");
    /* Single writer: a second launch (or a stale pileup from repeated
     * injections) stands down instead of fighting over the gateway. */
    lock_set_stale(g_cfg.lock_stale_s);
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
    /* Boot watchdog: a staged update that never proved itself healthy
     * gets rolled back to .bak before anything runs it. */
    {
        static const char *targets[] = {
            "/data/payloads/orbisrpc.bin",
        };
        for(unsigned ti = 0; ti < sizeof targets/sizeof targets[0]; ti++){
            int vr = health_verify_or_rollback(targets[ti]);
            if(vr == 1)
                log_msg("WARN: %s rolled back to last-good backup", targets[ti]);
            else if(vr == -1)
                log_msg("WARN: %s failed verification (no backup)", targets[ti]);
        }
    }
    /* One installation id per console, generated once and persisted. */
    cfg_ensure_installation_id(&g_cfg);
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
        /* One line per boot: which client this build imitates, and the date
         * that tuple was fetched. No network fetch in 1.1. */
        log_msg("fingerprint: client %s build %s (fetched %s)",
                g_cfg.fp_client_version, g_cfg.fp_client_build,
                ORBISRPC_FP_FETCHED_AT);
    }
    /* LAN kill switch: lets a fresh payload be injected without rebooting the
     * console. Non-fatal if the bind fails. */
    /* Wall-clock correction for Discord timestamps (PSN time sync is
     * typically blocked on jailbroken consoles). Boot sweep is bounded;
     * hourly ticks are single-host attempts (never stall heartbeats). */
    time_sync_all();
    {
        char why[160];
        if(!usable_token(&g_cfg, why, sizeof why)){
            /* Waiting for configuration is a HEALTHY boot, not a crash:
             * mark clean so token-less boots never trip safe mode. */
            health_mark_healthy();
            log_msg("FATAL: \"token\" in %s: %s", CFG_PATH, why);
            notify_once_boot("notoken", "No usable Discord token - edit config.json");
            lock_release();
            log_close();
            return 1;
        }
    }
    /* D-15: name the failing layer before the connect loop spends an hour
     * retrying a DNS server that is not there. Never fatal: DNS may come up
     * later, and the normal retry path already handles it. */
    (void)ws_net_init();
    if(!dns_resolves("discord.com")){
        log_msg("WARN: DNS cannot resolve discord.com — is nanoDNS running? "
                "Set the console DNS to a working resolver and reboot.");
        notify_once_boot("dns", "DNS is not resolving - is nanoDNS running?");
    }

    /* Resolve the configured (static) art once at boot, so no presence POST
     * is the first caller: the op 3 path is cache-only now. Failures just
     * post without art. Idle art falls back to <art_base_url>home.png when
     * neither large_art nor home_art is set, so resolve that URL too. */
    art_prefetch(large_art_url());
    art_prefetch(small_art_url());
    art_prefetch(browser_art_url());
    if(!large_art_url() && g_cfg.art_base_url[0]){
        char u[384];
        int n = snprintf(u, sizeof u, "%shome.png", g_cfg.art_base_url);
        if(n > 0 && (size_t)n < sizeof u) art_prefetch(u);
    }

    /* Self-update once per boot, before first connect. Never fatal:
     * staged artifacts take effect on next launch/injection. */
    if(g_cfg.auto_update && !safe_mode){
        int ur = updater_check_and_stage();
        log_msg("updater: %s (local %s)",
                ur > 0 ? "staged newer build" : ur == 0 ? "already current" : "check failed",
                ORBISRPC_VERSION);
    } else if(safe_mode){
        log_msg("updater: skipped (safe mode)");
    }

    discord_t dc;
    memset(&dc, 0, sizeof dc);
    int base_poll = g_cfg.poll_interval_s;
    if(base_poll < 5) base_poll = 5;
    if(base_poll > 60) base_poll = 60;
    /* Reconnect policy: exponential backoff with deterministic jitter,
     * cap 60s for the first 10 consecutive failures, then 10-minute
     * cadence (quiet persistence, never exit). Success resets. */
    int backoff = base_poll;
    int conn_fails = 0;
    int resume_fails = 0;
    char last_token_why[160] = "";
    unsigned jctr = 0;
    /* Health metrics (hourly HEALTH line). */
    int64_t boot_mono = orbis_mono_s();
    unsigned n_posts = 0, n_reconnects = 0;
    int64_t last_health = 0;
    /* Rest Mode detection spans reconnects: the console can sleep while the
     * gateway is down. Wall and monotonic are sampled together on every tick
     * so the drift is the sleep duration. */
    int64_t last_tick_wall = (int64_t)time(NULL);
    int64_t last_tick_mono = orbis_mono_s();
    for(;;){ /* outer: connect cycles with backoff on failure */
        if(s_stop) break;
        /* re-read config every cycle so token edits land without a reboot.
         * On parse failure keep last-good config instead of stale defaults. */
        {
            cfg_t next = g_cfg;
            if(cfg_load(CFG_PATH, &next) == 0){
                if(next.debug != g_cfg.debug)
                    log_msg("debug logging %s", next.debug ? "on" : "off");
                if(next.log_max_kb != g_cfg.log_max_kb)
                    log_msg("log cap: %d KiB", next.log_max_kb);
                g_cfg = next;
                log_set_debug(g_cfg.debug);
                log_set_cap((size_t)g_cfg.log_max_kb * 1024);
                lock_set_stale(g_cfg.lock_stale_s);
            }
            else log_msg("config reload failed; keeping last-good config");
        }
        {
            char why[160];
            if(!usable_token(&g_cfg, why, sizeof why)){
                /* One line per distinct reason: a config edit that fixes one
                 * field but not another must show the new reason (D-12). */
                if(strcmp(why, last_token_why) != 0){
                    log_msg("no usable token: %s; waiting %ds", why, backoff);
                    snprintf(last_token_why, sizeof last_token_why, "%s", why);
                }
                notify_once_boot("notoken", "No usable Discord token - edit config.json");
                if(sleep_stop(backoff)) break;
                continue;
            }
            last_token_why[0] = 0;
        }
        int rc;
        if(resume_fails >= 3 && discord_can_resume(&dc)){
            /* Three attempts that never reached RESUMED say this session is
             * not coming back. Spending the fourth is worse than one
             * IDENTIFY, and the IDENTIFY budget is finite. */
            log_msg("resume: giving up after %d attempts; fresh identify", resume_fails);
            discord_forget_session(&dc);
            resume_fails = 0;
        }
        if(discord_can_resume(&dc)){
            /* A live session means the gateway kept our presence and the
             * sequence counter; resume instead of spending an IDENTIFY and
             * rebuilding state. A dead session falls back to a fresh one. */
            log_msg("resume: reconnecting to existing session");
            rc = discord_resume(&dc, g_cfg.token);
            if(rc == 0){
                resume_fails = 0;
            } else if(rc == -3){
                resume_fails = 0;
                log_msg("resume: session expired; fresh identify");
                rc = discord_connect(&dc, g_cfg.token);
            } else if(rc == -2){
                discord_forget_session(&dc);
            } else {
                resume_fails++;
            }
        } else {
            rc = discord_connect(&dc, g_cfg.token);
        }
        if(rc == -2){
            /* Rejected token: do NOT exit (that would strand the daemon
             * until the next manual injection), and do NOT backoff-retry
             * (that hammers a credential Discord already refused).
             * Park on config changes; the loop resumes on a new token. */
            char rejected[sizeof g_cfg.token];
            snprintf(rejected, sizeof rejected, "%s", g_cfg.token);
            log_msg("WARN: token rejected by gateway (close 4004)");
            if(park_until_token_change(rejected)) break;
            continue;
        }
        if(rc != 0){
            int wait = reconnect_delay(&conn_fails, base_poll,
                                           g_cfg.reconnect_max_s, &jctr);
            /* Throttled: the backoff already grows, so an unthrottled
             * notification here fires every 13s and then every minute. */
            notify_throttled("connect", "Cannot reach Discord - retrying", 300);
            log_msg("gateway connect failed (attempt %d); retry in %ds",
                    conn_fails, wait);
            if(sleep_stop(wait)) break;
            continue;
        }
        if(conn_fails > 0)
            log_msg("gateway connected after %d failures", conn_fails);
        conn_fails = 0;
        resume_fails = 0;
        n_reconnects++;
        backoff = g_cfg.poll_interval_s; /* success resets backoff */
        if(backoff < 5) backoff = 5;
        if(backoff > 60) backoff = 60;

        /* last/started live OUTSIDE the session loop so the playing timer
         * survives gateway reconnects (same game keeps its original start).
         * A reconnect re-posts presence only when needed, timer intact. */
        static int active = 0;
        static pres_state_t pres = PS_NONE;
        static char last[128] = "";
        static char sess_tid[16] = "";
        static int64_t started = 0;
        static int64_t last_tsync = 0;
        static int home_posted = 0;
        /* Fresh (re)connect invalidates whatever Discord shows: re-post the
         * current state (game via need_post below, home via home_posted
         * reset) so a drop can never leave a stale/blank tile behind.
         * Settings/browser posted flags go with it; they describe what is on
         * the wire, and the wire was just replaced. Without this a reconnect
         * while Settings was open left the tile blank until Settings closed
         * and reopened. */
        home_posted = 0;
        s_settings_posted = 0;
        s_browser_posted = 0;
        /* D-01: no timer continuation and no cross-restart session cache.
         * A title that vanishes and returns is a new session, and so is
         * every daemon start: `started` is process-local by design, so a
         * relaunch, a reboot, Rest Mode, or a reinstall can never resurrect
         * a stale elapsed time. A quick menu hop still keeps the tile via
         * presence_hold_s, but the timer resets.
         *
         * Transition debounce: shell flickers during launches, so a new
         * title (or disappearance) needs consecutive polls. The count is
         * derived from the configured cadence (see sched_transition_hits):
         * two samples at 1s, one at 5s+, because a single long-interval
         * sample already spans the whole flicker window. */
        static char cand_title[16] = "";
        static int cand_hits = 0, miss_hits = 0;
        /* Presence hold (D-11): when a game closes, keep the last tile for
         * presence_hold_s instead of clearing it at once. The session is
         * still ended immediately, so a relaunch starts a fresh timer. */
        static int64_t gone_at = 0;
        static int holding = 0;
        int64_t last_poll = 0;
        int64_t last_detect = 0;
        int64_t last_alive = 0;
        int64_t last_loop_mono = 0;
        int64_t last_lock_mono = 0;
        static int healthy_marked = 0;
        /* re-post after every (re)connect so Discord never sticks on stale */
        int need_post = (active || holding) && last[0];
        while(!s_stop){ /* inner: live session, serviced every second */
            /* Poll every tick, not every detect poll: STOP must land within a
             * second even while a title is being resolved. */
            int64_t now = orbis_mono_s();
            if(stop_file_present()){
                log_msg("stop requested (%s)", STOP_PATH);
                s_stop = 1;
                break;
            }
            /* D-03: a suspended console stops running while wall time keeps
             * advancing. Re-arm the presence and shift the start epoch past
             * the sleep so Rest Mode hours are not counted as playtime. */
            int64_t slept = 0;
            {
                int64_t w = (int64_t)time(NULL);
                slept = sched_suspend_drift(w - last_tick_wall,
                                            now - last_tick_mono, 120);
                last_tick_wall = w;
                last_tick_mono = now;
                if(slept > 0){
                    log_msg("resume: console slept %llds; re-arming presence",
                            (long long)slept);
                    if(active && started > 0){
                        started = sched_rebase_start(started, slept);
                        need_post = 1;
                    }
                    home_posted = 0;
                    holding = 0;
                    gone_at = 0;
                }
            }
            /* A gap with wall and monotonic advancing together is not a
             * suspend: the only thread was parked somewhere that ignored
             * its deadline. Re-arm the tile and record the size, so a
             * residual blocker produces evidence instead of a silently
             * frozen profile. Skipped on the first tick of a session
             * (last_loop_mono == 0) and after a classified sleep. */
            if(slept <= 0 && last_loop_mono > 0 &&
               sched_loop_stall(now - last_loop_mono, 60)){
                log_msg("loop stall: %llds without a tick; re-arming presence",
                        (long long)(now - last_loop_mono));
                if(active && last[0]) need_post = 1;
                home_posted = 0;
            }
            last_loop_mono = now;
            /* Lock liveness. Touching the lock tells a later injection that
             * this daemon is still making progress; if another instance has
             * taken the lock over (this one was wedged past LOCK_STALE_S
             * and has only now woken up), exit instead of running as a
             * second writer against the same gateway. */
            if(last_lock_mono == 0 ||
               now - last_lock_mono >= g_cfg.lock_heartbeat_s){
                last_lock_mono = now;
                lock_refresh();
                if(!lock_owned()){
                    log_msg("lock: reclaimed by another instance; exiting");
                    s_stop = 1;
                    break;
                }
            }
            diag_service();
            if(now - last_tsync >= g_cfg.timesync_interval_s){
                last_tsync = now; time_sync();
            }
            if(now != last_poll){
                last_poll = now;
                if(now - last_alive >= 60){
                    last_alive = now;
                    log_msg("alive: %s", active ? last : "idle");
                }
                /* State reconciliation: re-post current presence every
                 * presence_reconcile_s (default 15 min) so a silently
                 * desynced tile (dropped update, stale cache, missed
                 * reconnect) heals itself without any state change.
                 * Timer/source values are re-read at post time, so this
                 * never disturbs a live session. */
                {
                    static int64_t last_reconcile = 0;
                    if(last_reconcile == 0) last_reconcile = now;
                    if(now - last_reconcile >= g_cfg.presence_reconcile_s){
                        last_reconcile = now;
                        if(active && last[0]) need_post = 1;
                        else home_posted = 0;
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
                if(!healthy_marked && now - boot_mono >= HEALTH_STABLE_SECS){
                    healthy_marked = 1;
                    health_mark_healthy();
                    log_msg("health: boot marked healthy");
                }
                /* Detection cadence. poll_interval_s is the documented
                 * cadence; running the msgbuf/process/app.db probes every
                 * second was pure waste (each scene probe copies the whole
                 * kern.msgbuf ring). sched_poll_due() returns true on the
                 * first tick after a (re)connect, so presence still comes
                 * back immediately. Extended intervals also collapse the
                 * flicker debounce: one 5s+ sample is already stable. */
                int need_hits = sched_transition_hits(g_cfg.poll_interval_s);
                if(sched_poll_due(now, &last_detect, g_cfg.poll_interval_s)){
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
                        else if(dr == -3) log_dbg("system UI in front; holding %s", last);
                        if(scan_unknown) name[0] = 0;
                        }
                        /* Settings screen. Only a definite answer moves the state:
                         * a probe that cannot see kern.msgbuf leaves the last known
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
                                need_post = active && last[0];
                                home_posted = 0;
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
                                need_post = active && last[0];
                                home_posted = 0;
                            }
                        }
                        /* While Settings is up it replaces the whole presence: its
                         * own name, no artwork, no timer, no small icon. The game
                         * underneath keeps `started`, so returning restores the
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
                            int pr = discord_set_presence_ex(&dc, st,
                                "PlayStation 4", details_for_settings(),
                                g_cfg.show_logo ? g_cfg.asset_idle : NULL, NULL,
                                g_cfg.show_logo ? "settings" : NULL,
                                g_cfg.application_id, NULL,
                                g_cfg.show_logo ? large_art_url() : NULL, NULL, 0);
                            if(pr == 0){
                                log_msg("presence: settings");
                                s_settings_posted = 1;
                                n_posts++;
                                s_last_post_mono = orbis_mono_s();
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
                            int pr = discord_set_presence_ex(&dc, st,
                                "PlayStation 4", "Using Web Browser",
                                NULL, NULL,
                                g_cfg.show_logo ? "browser" : NULL,
                                g_cfg.application_id, NULL,
                                g_cfg.show_logo ? browser_art_url() : NULL, NULL, 0);
                            if(pr == 0){
                                log_msg("presence: browser");
                                s_browser_posted = 1;
                                n_posts++;
                                s_last_post_mono = orbis_mono_s();
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
                        if(!strcmp(cur_tid, cand_title)){
                            cand_hits++;
                        } else {
                            strncpy(cand_title, cur_tid, sizeof cand_title-1);
                            cand_title[sizeof cand_title-1] = 0;
                            cand_hits = 1;
                        }
                        miss_hits = 0;
                        if(cand_hits >= need_hits){
                        if(!active || strcmp(cur_tid,sess_tid)!=0){
                            /* Switching away from a live session: bank its time.
                             * (Fresh starts and resumes have nothing to bank;
                             * the clear path already banked ended sessions.) */
                            if(active && sess_tid[0] &&
                               strcmp(cur_tid,sess_tid)!=0)
                                ledger_append(sess_tid, last, started, time_fixed());
                            active = 1;
                            strncpy(sess_tid, cur_tid, sizeof sess_tid-1);
                            strncpy(last, name, sizeof last-1);
                            last[sizeof last-1] = 0;
                            /* D-01: a new game start is always a new timer.
                             * The old 10-minute continuation window is gone;
                             * only a suspend rebases an existing start. */
                            started = time_fixed();
                            holding = 0;
                            gone_at = 0;
                            need_post = 1;
                            log_msg("GAME_DETECTED title=%s name=%s", cur_tid[0]?cur_tid:"?", name);
                            /* No metadata is written to config.json any more.
                             * detect.c persists id+name+cover to games_cache.json
                             * when a title resolves; that file is the only place
                             * the daemon learns anything. config.json keeps only
                             * operator settings and hand-edited overrides. */
                        } else if(strcmp(name,last)!=0){
                            strncpy(last, name, sizeof last-1);
                            last[sizeof last-1] = 0;
                            need_post = 1;
                        }
                        /* A media app or homebrew title that is switched off is
                         * never announced. Sending nothing leaves whatever was
                         * last posted standing, so a game underneath keeps
                         * showing and an empty console simply stays quiet. */
                        const cfg_title_rule_t *srule =
                            cfg_title_rule(&g_cfg, sess_tid);
                        int gate_this_title = gate_media || gate_homebrew ||
                                              (srule && srule->disabled);
                        if(need_post && gate_this_title){
                            /* Suppression is not "send nothing": the title may
                             * already be on the wire from before the rule or
                             * flag changed. Clear so a gated title cannot
                             * linger until the console reboots. Only commit
                             * local state once the clear actually went out,
                             * otherwise keep it armed and retry next tick. */
                            if(discord_clear_presence(&dc) == 0){
                                log_dbg("presence: %s suppressed and cleared", last);
                                pres_set(&pres, PS_NONE);
                                need_post = 0;
                            } else {
                                log_dbg("presence: clear for gated title failed; will retry");
                            }
                        }
                        if(need_post && !gate_this_title){
                            /* name / details / state: what is playing, where it
                             * is, and the firmware. */
                            char gst[64];
                            firmware_state_line(gst, sizeof gst);
                            const char *state = gst[0] ? gst : NULL;
                            const char *details = details_for_game(sess_tid);
                            const char *tart = detect_last_art();
                            if(srule && srule->art[0]) tart = srule->art;
                            /* Playback queue of one: only clear need_post after
                             * the bytes actually go out. A failed send stays
                             * queued and rides the next tick/reconnect. */
                            int pr = discord_set_presence_ex(&dc, state, last, details,
                                NULL, g_cfg.show_logo ? g_cfg.asset_playing : NULL,
                                sess_tid[0]?sess_tid:NULL, g_cfg.application_id,
                                g_cfg.art_base_url, tart,
                                g_cfg.show_logo ? small_art_url() : NULL, started);
                            if(pr == 0){
                                log_msg("presence: %s", last);
                                pres_set(&pres, PS_GAME);
                                n_posts++;
                                s_last_post_mono = orbis_mono_s();
                                active = 1; need_post = 0;
                            } else {
                                log_msg("presence send failed; will retry");
                            }
                        }
                        }
                    }else if(sched_clear_idle_now(gate_idle, active,
                                                  scan_unknown)){
                        /* Home with idle switched off. Clear rather than leave
                         * the previous game up: nothing is running underneath
                         * on the home screen, so holding would strand a closed
                         * game's tile for up to presence_hold_s. This must be
                         * checked before the hold branch below.
                         * A failed clear leaves the state armed; committing
                         * here would drop the only record that the tile is
                         * still up (and double-count the ledger on retry). */
                        if(discord_clear_presence(&dc) == 0){
                            log_msg("presence cleared (home, idle suppressed)");
                            pres_set(&pres, PS_NONE);
                            ledger_append(sess_tid, last, started, time_fixed());
                            last[0]=0; sess_tid[0]=0; active=0; started=0;
                            home_posted = 0;
                            holding = 0;
                            gone_at = 0;
                        } else {
                            log_dbg("presence clear failed (home, idle suppressed); will retry");
                        }
                    }else if((active || holding) && !scan_unknown){
                        cand_title[0] = 0;
                        cand_hits = 0;
                        if(++miss_hits >= need_hits){
                            if(gone_at == 0) gone_at = now;
                            /* End the session immediately (D-01): a relaunch
                             * is a fresh timer even while the old tile is
                             * still inside the hold window. */
                            if(active){
                                ledger_append(sess_tid, last, started, time_fixed());
                                active = 0;
                                sess_tid[0] = 0;
                                started = 0;
                                holding = 1;
                                log_msg("presence held for up to %ds (presence_hold_s)",
                                        g_cfg.presence_hold_s);
                            }
                            if(!holding || g_cfg.presence_hold_s <= 0 ||
                               now - gone_at >= g_cfg.presence_hold_s){
                                /* Hold expired. Only forget the tile once the
                                 * clear reached Discord; a failed send stays
                                 * armed so the next tick retries instead of
                                 * leaving a dead game on the profile. */
                                if(discord_clear_presence(&dc) == 0){
                                    log_msg("presence cleared");
                                    pres_set(&pres, PS_NONE);
                                    last[0]=0; home_posted=0; holding=0; gone_at=0;
                                } else {
                                    log_dbg("presence clear failed at hold expiry; will retry");
                                }
                            }
                        }
                    } else if(sched_home_due(gate_idle, home_posted, holding,
                                             scan_unknown)){
                        /* Home screen support: no game running. Post a timerless
                         * home presence once (instead of bare online), clear it
                         * the moment a game commits. */
                        char hst[64];
                        firmware_state_line(hst, sizeof hst);
                        const char *state = hst[0] ? hst : NULL;
                        if(discord_set_presence_ex(&dc, state, "PlayStation 4",
                            details_for_home(),
                            g_cfg.show_logo ? g_cfg.asset_idle : NULL, NULL,
                            g_cfg.show_logo ? "home" : NULL,
                            g_cfg.application_id, g_cfg.art_base_url,
                            g_cfg.show_logo ? large_art_url() : NULL, NULL, 0) == 0){
                            log_msg("presence: home");
                            pres_set(&pres, PS_HOME);
                            n_posts++;
                            s_last_post_mono = orbis_mono_s();
                            home_posted = 1;
                        } else {
                            log_msg("home presence send failed; will retry");
                        }
                    }
                }
            }

            /* service the gateway every pass (~1s): heartbeats must never be
             * more than a second or two late or the server drops us */
            int tr = discord_tick(&dc);
            if(tr == -2){
                /* Token rejected mid-session: same parking policy as
                 * connect-time 4004. Never re-IDENTIFY a refused token. */
                char rejected[sizeof g_cfg.token];
                snprintf(rejected, sizeof rejected, "%s", g_cfg.token);
                log_msg("WARN: token rejected (close 4004)");
                ws_close(&dc.ws);
                pres_set(&pres, PS_NONE);
                if(park_until_token_change(rejected)) break;
                break;
            }
            if(tr == -3){
                int wait = reconnect_delay(&conn_fails, base_poll,
                                           g_cfg.reconnect_max_s, &jctr);
                log_msg("invalid session; fresh identify (attempt %d) in %ds",
                        conn_fails, wait);
                ws_close(&dc.ws);
                pres_set(&pres, PS_NONE);
                if(sleep_stop(wait)) break;
                break;
            }
            if(tr != 0){
                int wait = reconnect_delay(&conn_fails, base_poll,
                                           g_cfg.reconnect_max_s, &jctr);
                log_msg("gateway dropped (attempt %d); reconnecting in %ds",
                        conn_fails, wait);
                ws_close(&dc.ws);
                pres_set(&pres, PS_NONE);
                if(sleep_stop(wait)) break;
                break;
            }

            for(int i = 0; i < 10 && !s_stop; i++) usleep(100000);
        }
        if(s_stop && dc.connected) discord_clear_presence(&dc);
        ws_close(&dc.ws);
    }
    /* Session file already reflects the live session (saved on every
     * transition), so shutdown is: clear presence (above), mark this boot
     * clean so the next start is not counted as a crash, release the lock so
     * a successor starts immediately, close the log. */
    health_mark_clean();
    lock_release();
    log_close();
    return 0;
}
