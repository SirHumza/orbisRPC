/* daemon.c - orbisRPC core loop, usable from the ELF payload (main.c) or from a
 * GoldHEN plugin. When `fixed_game_name` is non-NULL the daemon posts presence
 * for that game unconditionally (the plugin runs inside the game process and
 * already knows the title); otherwise it detects the foreground game.
 *
 * Auth model: a Discord USER SESSION token pasted into config.json ("token").
 * There is no OAuth flow anymore — OAuth2 access tokens are rejected by the
 * gateway (close 4004), which was why v1 never worked.
 *
 * A `stop` flag is polled so plugin_unload() can shut the loop down cleanly.
 */
#include "cfg.h"
#include "log.h"
#include "ws.h"
#include "discord.h"
#include "detect.h"
#include <sys/stat.h>
#include <unistd.h>
#include <string.h>
#include <time.h>

extern cfg_t g_cfg;

static volatile int s_stop = 0;

/* Tells a running daemon_run() to shut down (called from plugin_unload). */
void daemon_request_stop(void){ s_stop = 1; }
void daemon_clear_stop(void){ s_stop = 0; }

static int have_token(const cfg_t *c){
    return c->token[0] && strcmp(c->token,"SET_ME")!=0;
}

/* fixed_game_name != NULL -> post presence for that game only, no detection.
 * NULL -> poll the foreground app like the payload daemon does.
 * Returns 0 normal stop, 1 config error, 2 auth-fatal (bad token). */
int daemon_run(const char *fixed_game_name){
    s_stop = 0;
    mkdir(DATA_DIR, 0777);
    log_init(LOG_PATH);
    cfg_load(CFG_PATH, &g_cfg);
    if(!g_cfg.enabled){ log_msg("disabled in config; exiting"); log_close(); return 0; }
    if(!have_token(&g_cfg)){
        log_msg("FATAL: put your Discord user token in %s as \"token\":\"...\"", CFG_PATH);
        log_close();
        return 1;
    }

    discord_t dc;
    int backoff = g_cfg.poll_interval_s;
    for(;;){ /* outer: connect cycles with backoff on failure */
        if(s_stop) break;
        /* re-read config every cycle so token edits land without a reboot */
        cfg_load(CFG_PATH, &g_cfg);
        if(!have_token(&g_cfg)){
            log_msg("no token in config; waiting %ds", backoff);
            sleep((unsigned)backoff);
            continue;
        }
        int rc = discord_connect(&dc, g_cfg.token);
        if(rc == -2){
            log_msg("FATAL: token rejected by gateway (close 4004). "
                    "Fix \"token\" in %s", CFG_PATH);
            return 2;
        }
        if(rc != 0){
            log_msg("gateway connect failed; retry in %ds", backoff);
            sleep((unsigned)backoff);
            backoff *= 2; if(backoff > 300) backoff = 300;
            continue;
        }
        backoff = g_cfg.poll_interval_s; /* success resets backoff */

        int active = 0;
        char last[128] = "";
        int64_t started = 0;
        int64_t last_poll = 0;
        while(!s_stop){ /* inner: live session, serviced every second */
            int64_t now = time(NULL);
            if(now != last_poll){
                last_poll = now;
                char name[128] = "";
                if(fixed_game_name){
                    strncpy(name, fixed_game_name, sizeof name-1);
                    name[sizeof name-1] = 0;
                }else{
                    if(detect_foreground_active())
                        detect_current_game(name, sizeof name, NULL, 0);
                }

                if(name[0]){
                    if(!active || strncmp(name,last,sizeof last)!=0){
                        started = time(NULL);
                        const char *state = g_cfg.presence_state[0] ? g_cfg.presence_state : NULL;
                        discord_set_presence(&dc, state, name, g_cfg.application_id, started);
                        log_msg("presence: %s", name);
                        strncpy(last, name, sizeof last-1);
                        last[sizeof last-1] = 0;
                        active = 1;
                    }
                }else if(active){
                    discord_clear_presence(&dc);
                    log_msg("presence cleared");
                    last[0]=0; active=0;
                }
            }

            /* service the gateway every pass (~1s): heartbeats must never be
             * more than a second or two late or the server drops us */
            int tr = discord_tick(&dc);
            if(tr == -2){
                log_msg("FATAL: token rejected (close 4004). Fix %s", CFG_PATH);
                ws_close(&dc.ws);
                log_close();
                return 2;
            }
            if(tr != 0){ log_msg("gateway dropped; reconnecting"); break; }

            usleep(1000000);
        }
        if(s_stop && dc.connected) discord_clear_presence(&dc);
        ws_close(&dc.ws);
    }
    log_close();
    return 0;
}
