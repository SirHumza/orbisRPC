/* daemon.c - orbisRPC core loop, usable from the ELF payload (main.c) or from a
 * GoldHEN plugin. When `fixed_game_name` is non-NULL the daemon posts presence
 * for that game unconditionally (the plugin runs inside the game process and
 * already knows the title); otherwise it detects the foreground game.
 *
 * Auth model: a Discord USER SESSION token pasted into config.json ("token").
 * There is no OAuth flow anymore — OAuth2 access tokens are rejected by the
 * gateway (close 4004), which was why v1 never worked.
 *
 * A `stop` flag is polled so plugin_unload() can shut the loop down cleanly. */
#include "cfg.h"
#include "clock.h"
#include "lock.h"
#include "timesync.h"
#include "health.h"
#include "log.h"
#include "ws.h"
#include "discord.h"
#include "detect.h"
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

/* Tells a running daemon_run() to shut down (called from plugin_unload). */
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

/* Sleep up to `secs` but wake within a second when the plugin asks us to
 * stop, so plugin_unload() never hangs on a long backoff. Returns 1 stopped. */
static int sleep_stop(int secs){
    for(int i = 0; i < secs; i++){
        if(s_stop) return 1;
        sleep(1);
    }
    return s_stop;
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
        if(fclose(f) != 0) ok = 0;
        if(ok) rename("/data/orbisRPC/session.json.new",
                      "/data/orbisRPC/session.json");
        else remove("/data/orbisRPC/session.json.new");
    }
    free(s);
}

/* Playtime ledger: append-only "<title_id> <name> <seconds>" per finished
 * session. Totals are computed by readers (app/docs); the daemon only
 * appends, so a corrupt ledger can never break the runtime. */
static void ledger_append(const char *tid, const char *name,
                          int64_t started, int64_t ended){
    if(!tid || !tid[0] || ended <= started || started <= 0) return;
    FILE *f = fopen("/data/orbisRPC/playtime.log", "ab");
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
typedef enum { PS_NONE = 0, PS_HOME, PS_GAME } pres_state_t;
static const char *pres_name(pres_state_t s){
    return s == PS_GAME ? "GAME" : s == PS_HOME ? "HOME" : "NONE";
}
static void pres_set(pres_state_t *cur, pres_state_t next){
    if(*cur == next) return;
    log_msg("STATE: presence %s -> %s", pres_name(*cur), pres_name(next));
    *cur = next;
}
/* fixed_game_name != NULL -> post presence for that game only, no detection.
 * NULL -> poll the foreground app like the payload daemon does.
 * Returns 0 normal stop, 1 config error, 2 auth-fatal (bad token). */
int daemon_run(const char *fixed_game_name){
    s_stop = 0;
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
    /* Boot watchdog: a staged update that never proved itself healthy
     * gets rolled back to .bak before anything runs it. */
    {
        static const char *targets[] = {
            "/data/payloads/orbisrpc.bin",
            "/data/GoldHEN/plugins/orbisrpc_plugin.prx",
        };
        for(unsigned ti = 0; ti < sizeof targets/sizeof targets[0]; ti++){
            int vr = health_verify_or_rollback(targets[ti]);
            if(vr == 1)
                log_msg("WARN: %s rolled back to last-good backup", targets[ti]);
            else if(vr == -1)
                log_msg("WARN: %s failed verification (no backup)", targets[ti]);
        }
    }
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
    }
    if(!g_cfg.enabled){ log_msg("disabled in config; exiting"); health_mark_clean(); log_close(); return 0; }
    log_set_debug(g_cfg.debug);
    if(g_cfg.debug) log_dbg("debug logging on (config)");
    /* Wall-clock correction for Discord timestamps (PSN time sync is
     * typically blocked on jailbroken consoles). Boot sweep is bounded;
     * hourly ticks are single-host attempts (never stall heartbeats). */
    time_sync_all();
    if(!have_token(&g_cfg)){
        /* Waiting for configuration is a HEALTHY boot, not a crash:
         * mark clean so token-less boots never trip safe mode. */
        health_mark_healthy();
        log_msg("FATAL: put your Discord user token in %s as \"token\":\"...\"", CFG_PATH);
        log_close();
        return 1;
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
    int base_poll = g_cfg.poll_interval_s;
    if(base_poll < 5) base_poll = 5;
    if(base_poll > 60) base_poll = 60;
    /* Reconnect policy: exponential backoff with deterministic jitter,
     * cap 60s for the first 10 consecutive failures, then 10-minute
     * cadence (quiet persistence, never exit). Success resets. */
    int backoff = base_poll;
    int conn_fails = 0;
    unsigned jctr = 0;
    /* Health metrics (hourly HEALTH line). */
    int64_t boot_mono = orbis_mono_s();
    unsigned n_posts = 0, n_reconnects = 0;
    int64_t last_health = 0;
    for(;;){ /* outer: connect cycles with backoff on failure */
        if(s_stop) break;
        /* re-read config every cycle so token edits land without a reboot.
         * On parse failure keep last-good config instead of stale defaults. */
        {
            cfg_t next = g_cfg;
            if(cfg_load(CFG_PATH, &next) == 0){
                if(next.debug != g_cfg.debug)
                    log_msg("debug logging %s", next.debug ? "on" : "off");
                g_cfg = next;
                log_set_debug(g_cfg.debug);
            }
            else log_msg("config reload failed; keeping last-good config");
        }
        if(!have_token(&g_cfg)){
            log_msg("no token in config; waiting %ds", backoff);
            if(sleep_stop(backoff)) break;
            continue;
        }
        int rc = discord_connect(&dc, g_cfg.token);
        if(rc == -2){
            /* Rejected token: do NOT exit (that would strand the daemon
             * until the next manual injection). The per-cycle config
             * reload picks up a fixed token on its own. */
            log_msg("WARN: token rejected by gateway (close 4004). "
                    "Fix \"token\" in %s; retrying", CFG_PATH);
            int wait = reconnect_delay(&conn_fails, base_poll, &jctr);
            if(sleep_stop(wait)) break;
            continue;
        }
        if(rc != 0){
            int wait = reconnect_delay(&conn_fails, base_poll, &jctr);
            log_msg("gateway connect failed (attempt %d); retry in %ds",
                    conn_fails, wait);
            if(sleep_stop(wait)) break;
            continue;
        }
        if(conn_fails > 0)
            log_msg("gateway connected after %d failures", conn_fails);
        conn_fails = 0;
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
         * reset) so a drop can never leave a stale/blank tile behind. */
        home_posted = 0;
        /* Resume window: if the same title vanishes briefly (detection
         * flicker, quick menu hop) and returns within 10 minutes, the
         * timer resumes instead of resetting to 0:00. */
        static char prev_tid[16] = "";
        static int64_t prev_started = 0, prev_end = 0;
        /* Cross-restart resume: a previous run's session (saved on every
         * transition) seeds the resume window, so reboots and updates
         * keep the timer instead of resetting it. */
        {
            static int sess_restored = 0;
            if(!sess_restored){
                sess_restored = 1;
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
                                        strncpy(prev_tid, t->str, sizeof prev_tid-1);
                                        prev_started = st;
                                        /* Wall saved_at can't mix with the
                                         * monotonic resume window: start the
                                         * window at boot. Same title seen
                                         * within minutes of boot resumes. */
                                        prev_end = orbis_mono_s();
                                        log_msg("session restored: %s (timer may resume)",
                                                prev_tid);
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
         * title (or disappearance) needs consecutive polls (2 at ~1s). */
        static char cand_title[16] = "";
        static int cand_hits = 0, miss_hits = 0;
        int64_t last_poll = 0;
        int64_t last_alive = 0;
        static int healthy_marked = 0;
        /* re-post after every (re)connect so Discord never sticks on stale */
        int need_post = active && last[0];
        while(!s_stop){ /* inner: live session, serviced every second */
            int64_t now = orbis_mono_s();
            if(now - last_tsync >= 3600){ last_tsync = now; time_sync(); }
            if(now != last_poll){
                last_poll = now;
                if(now - last_alive >= 60){
                    last_alive = now;
                    log_msg("alive: %s", active ? last : "idle");
                }
                /* State reconciliation: re-post current presence every
                 * 15 min so a silently desynced tile (dropped update,
                 * stale cache, missed reconnect) heals itself without
                 * any state change. Timer/source values are re-read at
                 * post time, so this never disturbs a live session. */
                {
                    static int64_t last_reconcile = 0;
                    if(last_reconcile == 0) last_reconcile = now;
                    if(now - last_reconcile >= 900){
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
                char name[128] = "";
                int scan_unknown = 0;
#ifdef ORBISRPC_SDK_PAYLOAD
                /* Launch/close shortcut: the eboot set changing means the
                 * foreground game changed RIGHT NOW. Reset debounce so the
                 * switch commits within ~2 polls. Deliberately NOT instant:
                 * the title scan needs one poll for fresh atime data, and
                 * committing a stale scan instantly would lock the wrong
                 * title with a full session. */
                {
                    static int last_eboots = -1;
                    int nboots = detect_eboot_count();
                    if(nboots >= 0 && nboots != last_eboots){
                        if(last_eboots >= 0)
                            log_msg("eboot set %d -> %d; fast-switching",
                                    last_eboots, nboots);
                        last_eboots = nboots;
                        cand_title[0] = 0;
                        cand_hits = 0;
                        miss_hits = 0;
                    }
                }
#endif
                if(fixed_game_name){
                    strncpy(name, fixed_game_name, sizeof name-1);
                    name[sizeof name-1] = 0;
                }else{
                    /* detect_current_game already checks foreground-active
                     * internally; don't double-call it. -2 means the scan
                     * itself failed: hold position, touch nothing. */
                    scan_unknown = (detect_current_game(name, sizeof name, NULL, 0) == -2);
                    if(scan_unknown){
                        log_dbg("detect scan failed; holding");
                        name[0] = 0;
                    }
                }

                if(name[0]){
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
                    if(fixed_game_name || cand_hits >= 2){
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
                        if(!strcmp(cur_tid, prev_tid) &&
                           prev_started > 0 &&
                           now - prev_end < 600){
                            started = prev_started;
                            log_msg("SESSION_RESUMED title=%s (gap %llds, timer kept)",
                                    cur_tid[0]?cur_tid:"?", (long long)(now - prev_end));
                        } else {
                            started = time_fixed();
                        }
                        need_post = 1;
                        log_msg("GAME_DETECTED title=%s name=%s", cur_tid[0]?cur_tid:"?", name);
                        art_cache_clear();
                        sess_save(cur_tid, name, started);
                        /* Self-learning map: persist authoritatively resolved
                         * names so later boots resolve instantly, even when
                         * every live source is unreachable. Raw-ID fallbacks
                         * (ok=0) are never learned. */
                        if(cur_tid[0] && detect_last_ok() && cfg_learn(&g_cfg, cur_tid, name)){
                            if(cfg_save(CFG_PATH, &g_cfg) != 0)
                                log_msg("config: learn save failed for %s", cur_tid);
                            else
                                log_msg("config: learned %s", cur_tid);
                        }
                    } else if(strcmp(name,last)!=0){
                        strncpy(last, name, sizeof last-1);
                        last[sizeof last-1] = 0;
                        need_post = 1;
                    }
                    if(need_post){
                        const char *state = g_cfg.presence_state[0] ? g_cfg.presence_state : NULL;
                        const char *tart = detect_last_art();
                        /* Playback queue of one: only clear need_post after
                         * the bytes actually go out. A failed send stays
                         * queued and rides the next tick/reconnect. */
                        int pr = discord_set_presence_ex(&dc, state, last, sess_tid[0]?sess_tid:NULL, g_cfg.application_id, g_cfg.art_base_url, tart, g_cfg.home_art[0]?g_cfg.home_art:NULL, started);
                        if(pr == 0){
                            log_msg("presence: %s", last);
                            pres_set(&pres, PS_GAME);
                            n_posts++;
                            active = 1; need_post = 0;
                        } else {
                            log_msg("presence send failed; will retry");
                        }
                    }
                    }
                }else if(active && !scan_unknown){
                    cand_title[0] = 0;
                    cand_hits = 0;
                    if(++miss_hits >= 2){
                        discord_clear_presence(&dc);
                        log_msg("presence cleared");
                        pres_set(&pres, PS_NONE);
                        ledger_append(sess_tid, last, started, time_fixed());
                        strncpy(prev_tid, sess_tid, sizeof prev_tid-1);
                        prev_started = started;
                        prev_end = now;
                        remove("/data/orbisRPC/session.json");
                        last[0]=0; sess_tid[0]=0; active=0; started=0;
                        home_posted = 0;
                    }
                } else if(!home_posted && !scan_unknown){
                    /* Home screen support: no game running. Post a timerless
                     * home presence once (instead of bare online), clear it
                     * the moment a game commits. */
                    const char *state = g_cfg.presence_state[0] ? g_cfg.presence_state : NULL;
                    if(discord_set_presence_ex(&dc, state, "PlayStation 4", "home",
                                            g_cfg.application_id, g_cfg.art_base_url,
                                            g_cfg.home_art[0] ? g_cfg.home_art : NULL, NULL, 0) == 0){
                        log_msg("presence: home");
                        pres_set(&pres, PS_HOME);
                        n_posts++;
                        home_posted = 1;
                    } else {
                        log_msg("home presence send failed; will retry");
                    }
                }
            }

            /* service the gateway every pass (~1s): heartbeats must never be
             * more than a second or two late or the server drops us */
            int tr = discord_tick(&dc);
            if(tr == -2){
                /* Token rejected mid-session: same policy as connect-time
                 * 4004 (never strand the daemon). Drop to the outer loop:
                 * backoff + config reload picks up a fixed token alone. */
                log_msg("WARN: token rejected (close 4004). Fix %s; retrying", CFG_PATH);
                ws_close(&dc.ws);
                pres_set(&pres, PS_NONE);
                if(sleep_stop(reconnect_delay(&conn_fails, base_poll, &jctr))) break;
                break;
            }
            if(tr == -3){
                int wait = reconnect_delay(&conn_fails, base_poll, &jctr);
                log_msg("invalid session; fresh identify (attempt %d) in %ds",
                        conn_fails, wait);
                ws_close(&dc.ws);
                pres_set(&pres, PS_NONE);
                if(sleep_stop(wait)) break;
                break;
            }
            if(tr != 0){
                int wait = reconnect_delay(&conn_fails, base_poll, &jctr);
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
     * transition), so shutdown is: clear presence (above), release the
     * lock so a successor starts immediately, close the log. */
    lock_release();
    log_close();
    return 0;
}
