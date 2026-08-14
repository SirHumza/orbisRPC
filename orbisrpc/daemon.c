/* daemon.c - orbisRPC core loop, usable from the ELF payload (main.c) or from a
 * GoldHEN plugin. When `fixed_game_name` is non-NULL the daemon posts presence
 * for that game unconditionally (the plugin runs inside the game process and
 * already knows the title); otherwise it detects the foreground game.
 *
 * A `stop` flag is polled so a plugin_unload() can shut the loop down cleanly.
 */
#include "cfg.h"
#include "log.h"
#include "http.h"
#include "ws.h"
#include "discord.h"
#include "detect.h"
#include <orbis/UserService.h>
#include <orbis/libkernel.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

extern cfg_t g_cfg;

static volatile int s_stop = 0;

/* Tells a running daemon_run() to shut down (called from plugin_unload). */
void daemon_request_stop(void){ s_stop = 1; }
void daemon_clear_stop(void){ s_stop = 0; }

static int ensure_token(void){
    /* Re-read config each attempt: the operator edits auth_code/refresh_token
     * on the console (FTP) while the daemon runs, and cfg_save() persists new
     * tokens after refresh — the disk file is the source of truth. */
    cfg_load(CFG_PATH, &g_cfg);
    int64_t now=time(NULL);
    if(g_cfg.access_token[0] && now < g_cfg.token_expires_at - 120) return 0; /* fresh */
    char at[256]="", rt[256]=""; int64_t exp=0;
    int rc = http_oauth_token(g_cfg.client_id, g_cfg.client_secret,
                              g_cfg.auth_code,
                              g_cfg.refresh_token[0]?g_cfg.refresh_token:NULL,
                              at, sizeof at, g_cfg.refresh_token[0]?rt:NULL, g_cfg.refresh_token[0]?sizeof g_cfg.refresh_token:0,
                              &exp);
    if(rc==0 && at[0]){
        strncpy(g_cfg.access_token, at, sizeof g_cfg.access_token-1);
        if(rt[0]) strncpy(g_cfg.refresh_token, rt, sizeof g_cfg.refresh_token-1);
        g_cfg.token_expires_at = exp;
        memset(g_cfg.auth_code,0,sizeof g_cfg.auth_code); /* one-time use */
        cfg_save(CFG_PATH, &g_cfg);
        log_msg("token refreshed, expires_in=%lds", (long)(exp-now));
        return 0;
    }
    log_msg("oauth failed rc=%d", rc);
    return rc;
}

static void set_game_presence(discord_t *d, const char *name){
    if(!name||!*name){ discord_clear_presence(d); return; }
    discord_set_presence(d,
        g_cfg.presence_state[0]?g_cfg.presence_state:"On PS4", /* state */
        name,                     /* details = game name */
        1, 1,                     /* party */
        "orbisrpc:playing",       /* large_image key (asset) */
        name);                    /* large_text */
    log_msg("presence: %s", name);
}

/* fixed_game_name != NULL -> post presence for that game only, no detection.
 * NULL -> poll the foreground app like the original payload daemon. */
int daemon_run(const char *fixed_game_name){
    s_stop = 0;
    mkdir(DATA_DIR, 0777);          /* plugins/payloads may create their own dir */
    log_init(LOG_PATH);
    cfg_load(CFG_PATH, &g_cfg);
    if(!g_cfg.enabled){ log_msg("disabled in config; exiting"); log_close(); return 0; }
    if(!g_cfg.client_id[0] || strcmp(g_cfg.client_id,"SET_ME")==0){
        log_msg("FATAL: set client_id + auth_code (or refresh) in %s", CFG_PATH);
        log_close();
        return 1;
    }
    discord_t dc;
    /* keep the daemon alive: transient OAuth/gateway failures retry instead
     * of exiting (a GoldHEN plugin only runs while the game process lives) */
    for(;;){ /* outer: token refresh + fresh connect */
        if(s_stop) break;
        if(ensure_token()!=0){
            log_msg("no valid token; retry in %ds", g_cfg.poll_interval_s);
            sleep((unsigned)g_cfg.poll_interval_s);
            continue;
        }
        if(discord_connect(&dc, g_cfg.access_token, 0)!=0){
            log_msg("gateway connect failed; retry in %ds", g_cfg.poll_interval_s);
            sleep((unsigned)g_cfg.poll_interval_s);
            continue;
        }
        char last_name[128]="";
        int64_t last_change = 0;
        while(1){ /* inner: live session */
            if(s_stop) break;
            char name[128]=""; char path[128]="";
            if(fixed_game_name){
                strncpy(name, fixed_game_name, sizeof name-1);
            }else{
                int active = detect_foreground_active();
                if(active && detect_current_game(name,sizeof name,path,sizeof path)==0 && name[0]){
                    /* ok */
                }else{
                    name[0]=0;
                }
            }
            if(name[0]){
                int64_t now=time(NULL);
                if(strcmp(name,last_name)!=0 || now-last_change > 300){
                    set_game_presence(&dc, name);
                    strncpy(last_name, name, sizeof last_name-1);
                    last_change = now;
                }
            } else {
                if(last_name[0] || fixed_game_name==NULL){
                    set_game_presence(&dc, NULL); /* clear when nothing foreground */
                    last_name[0]=0; last_change=time(NULL);
                }
            }
            if(discord_tick(&dc) != 0){
                log_msg("gateway dropped; reconnecting...");
                if(discord_reconnect(&dc)!=0){
                    ws_close(&dc.ws);
                    break; /* outer loop: full retry (token + connect) */
                }
                last_name[0]=0; /* force re-push of the current game */
            }
            int iv = g_cfg.poll_interval_s;
            for(int i=0; i<iv && !s_stop; i++) usleep(1000000); /* interruptible poll */
        }
        ws_close(&dc.ws);
    }
    discord_clear_presence(&dc);
    ws_close(&dc.ws);
    log_close();
    return 0;
}