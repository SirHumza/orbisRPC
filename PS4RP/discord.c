/* discord.c - Discord gateway client. */
#include "discord.h"
#include "http.h"
#include "b64.h"
#include "log.h"
#include "jsonlite.h"
#include <orbis/Net.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>

static void make_key(char *out){
    unsigned char b[16];
    int fd=open("/dev/urandom",O_RDONLY);
    if(fd>=0){ read(fd,b,16); close(fd); }
    else { for(int i=0;i<16;i++)b[i]=(unsigned char)(time(NULL)+i*7); }
    b64_encode(b,16,out);
}

int discord_connect(discord_t *d, const char *token, const char *intent){
    (void)intent;
    memset(d,0,sizeof(*d));
    strncpy(d->token, token, sizeof d->token-1);
    /* fetch gateway url (public endpoint, no bot auth needed) */
    char resp[512];
    if(http_get("https://discord.com/api/gateway",resp,sizeof resp)!=0){
        log_msg("gateway fetch failed; using default");
    }
    char url[512]="wss://gateway.discord.gg";
    jl_val_t *r=jl_parse(resp,0);
    if(r){
        const jl_val_t *u=jl_obj_get(r,"url");
        if(u&&u->type==JL_STRING){ strncpy(url,u->str,sizeof url-1); url[sizeof url-1]=0; }
        jl_free(r);
    }
    char key[64]=""; make_key(key);
    const char *res="/?v=10&encoding=json";
    /* host parse: take after https:// or default gateway.discord.gg */
    const char *host="gateway.discord.gg";
    int port=443;
    int rc=ws_connect(&d->ws, host, port, res, key);
    if(rc){ log_msg("ws connect fail %d",rc); return rc; }
    d->connected=1; d->last_heartbeat=time(NULL);
    /* receive HELLO */
    char buf[2048]; int op=0,fin=0;
    int nr=ws_recv_frame(&d->ws,buf,sizeof buf,&op,&fin);
    if(nr<=0||op!=1){log_msg("no HELLO op=%d nr=%d",op,nr);return -1;}
    jl_val_t *h=jl_parse(buf,nr);
    if(h){
        const jl_val_t *dd=jl_obj_get(h,"d");
        if(dd){
            const jl_val_t *hi=jl_obj_get(dd,"heartbeat_interval");
            if(hi&&hi->type==JL_NUMBER) d->hb_interval_ms=(int64_t)hi->num;
        }
        const jl_val_t *s=jl_obj_get(h,"d"); (void)s;
        jl_free(h);
    }
    if(!d->hb_interval_ms) d->hb_interval_ms=45000;
    /* IDENTIFY */
    jl_val_t *root=jl_new_object();
    jl_obj_set(root,"op",jl_new_number(2));
    jl_val_t *dd=jl_new_object();
    jl_obj_set(dd,"token",jl_new_string(token));
    jl_obj_set(dd,"intents",jl_new_number(513));
    jl_obj_set(dd,"properties",jl_new_object());
    jl_obj_set(root,"d",dd);
    char *s=jl_stringify(root); jl_free(root);
    ws_send_text(&d->ws,s,strlen(s)); free(s);
    log_msg("discord: gateway connected, hb=%lds", (long)(d->hb_interval_ms/1000));
    return 0;
}

int discord_set_presence(discord_t *d, const char *state, const char *details,
                         int party_size, int party_max,
                         const char *large_image, const char *large_text){
    (void)party_size; (void)party_max;
    jl_val_t *dd=jl_new_object();
    jl_val_t *act=jl_new_object();
    jl_obj_set(act,"name",jl_new_string(details?details:""));
    jl_obj_set(act,"type",jl_new_number(0)); /* Playing */
    if(state) jl_obj_set(act,"state",jl_new_string(state));
    jl_val_t *assets=jl_new_object();
    if(large_image) jl_obj_set(assets,"large_image",jl_new_string(large_image));
    if(large_text)  jl_obj_set(assets,"large_text", jl_new_string(large_text));
    jl_obj_set(act,"assets",assets);
    jl_obj_set(dd,"activities",jl_new_array());
    jl_arr_push(jl_obj_get(dd,"activities"), act);
    jl_obj_set(dd,"status","online");
    jl_obj_set(dd,"since",jl_new_number((double)time(NULL)));
    jl_obj_set(dd,"afk",jl_new_bool(0));
    jl_val_t *root=jl_new_object();
    jl_obj_set(root,"op",jl_new_number(3));
    jl_obj_set(root,"d",dd);
    char *s=jl_stringify(root); jl_free(root);
    int r=ws_send_text(&d->ws,s,strlen(s)); free(s);
    return (r>=0)?0:-1;
}

int discord_clear_presence(discord_t *d){
    jl_val_t *dd=jl_new_object();
    jl_obj_set(dd,"activities",jl_new_array());
    jl_obj_set(dd,"status","invisible");
    jl_val_t *root=jl_new_object();
    jl_obj_set(root,"op",jl_new_number(3));
    jl_obj_set(root,"d",dd);
    char *s=jl_stringify(root); jl_free(root);
    int r=ws_send_text(&d->ws,s,strlen(s)); free(s);
    return (r>=0)?0:-1;
}

int discord_tick(discord_t *d){
    if(!d->connected) return -1;
    int64_t now=time(NULL);
    if(now - d->last_heartbeat >= d->hb_interval_ms/1000){
        char hb[64]; snprintf(hb,sizeof hb,"{\"op\":1,\"d\":%d}", d->seq);
        ws_send_text(&d->ws,hb,strlen(hb));
        d->last_heartbeat=now;
    }
    /* read any pending server frame */
    char buf[1024]; int op=0,fin=0;
    int nr=ws_recv_frame(&d->ws,buf,sizeof buf,&op,&fin);
    if(nr<=0){ return -1; }
    if(op==0){ /* DISPATCH */
        const char *p=strstr(buf,"\"seq\":");
        if(p){ p+=6; d->seq=(int)strtol(p,NULL,10); }
        else if(strstr(buf,"READY")){ /* capture session id */
            jl_val_t *r=jl_parse(buf,nr);
            if(r){
                const jl_val_t *dd=jl_obj_get(r,"d");
                if(dd){ const jl_val_t *si=jl_obj_get(dd,"session_id");
                    if(si&&si->type==JL_STRING) strncpy(d->session_id,si->str,sizeof d->session_id-1); }
                jl_free(r);
            }
        }
    }
    return 0;
}

int discord_reconnect(discord_t *d){
    ws_close(&d->ws); d->connected=0;
    return discord_connect(d, d->token, NULL);
}
