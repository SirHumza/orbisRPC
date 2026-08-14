/* main.c - orbisRPC daemon: detect game -> Discord presence. Runs as GoldHEN payload. */
#include "cfg.h"
#include "log.h"
#include "http.h"
#include "ws.h"
#include "discord.h"
#include "detect.h"
#include "jsonlite.h"
#include "b64.h"
#include <orbis/UserService.h>
#include <orbis/libkernel.h>
#include <unistd.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

extern cfg_t g_cfg;

static int ensure_token(void){
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
        "On PS4",                 /* state */
        name,                     /* details = game name */
        1, 1,                     /* party */
        "orbisrpc:playing",          /* large_image key (asset) */
        name);                    /* large_text */
    log_msg("presence: %s", name);
}

int main(int argc, char **argv){
    (void)argc;(void)argv;
    /* GoldHEN payloads: write to /data/orbisRPC */
    cfg_load(CFG_PATH, &g_cfg);
    if(!g_cfg.enabled){ log_msg("disabled in config; exiting"); return 0; }
    if(!g_cfg.client_id[0] || strcmp(g_cfg.client_id,"SET_ME")==0){
        log_msg("FATAL: set client_id + auth_code (or refresh) in %s", CFG_PATH);
        return 1;
    }
    discord_t dc;
    /* token first */
    if(ensure_token()!=0){
        log_msg("no valid token; cannot connect gateway");
        return 2;
    }
    if(discord_connect(&dc, g_cfg.access_token, NULL)!=0){
        log_msg("gateway connect failed");
        return 3;
    }
    char last_name[128]="";
    int64_t last_change = 0;
    for(;;){
        char name[128]=""; char path[128]="";
        int active = detect_foreground_active();
        if(active && detect_current_game(name,sizeof name,path,sizeof path)==0 && name[0]){
            int64_t now=time(NULL);
            if(strcmp(name,last_name)!=0 || now-last_change > 300){
                set_game_presence(&dc, name);
                strncpy(last_name, name, sizeof last_name-1);
                last_change = now;
            }
        } else {
            if(last_name[0] || !active){
                set_game_presence(&dc, NULL); /* clear when nothing foreground */
                last_name[0]=0; last_change=time(NULL);
            }
        }
        if(discord_tick(&dc) != 0){
            log_msg("gateway dropped; reconnecting...");
            discord_reconnect(&dc);
        }
        usleep((useconds_t)(g_cfg.poll_interval_s * 1000000));
    }
    discord_clear_presence(&dc);
    ws_close(&dc.ws);
    log_close();
    return 0;
}
