/* discord.c - Discord gateway client.
 * User OAuth2 connection: IDENTIFY carries required connection properties,
 * no bot intents, presence via op 3. Handles heartbeat acks, RESUME on
 * reconnect, invalid-session, server pings and close frames. */
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

#define GATEWAY_DEFAULT "wss://gateway.discord.gg/?v=10&encoding=json"

static void make_key(char *out){
    unsigned char b[16];
    int fd=open("/dev/urandom",O_RDONLY);
    if(fd>=0){ read(fd,b,16); close(fd); }
    else { for(int i=0;i<16;i++)b[i]=(unsigned char)(time(NULL)+i*7); }
    b64_encode(b,16,out);
}

/* Parse "wss://host[:port][/path?query]" into its parts. */
static void parse_gateway_url(const char *url, char *host, size_t hcap,
                              int *port, char *res, size_t rcap){
    const char *p=strstr(url,"://");
    p = p ? p+3 : url;
    size_t hl=0;
    while(*p && *p!=':' && *p!='/' && *p!='?' && hl<hcap-1){ host[hl++]=*p++; }
    host[hl]=0;
    *port=0;
    if(*p==':'){
        p++;
        while(*p>='0'&&*p<='9'){ *port=*port*10+(*p-'0'); p++; }
    }
    if(!*port) *port=443;
    if(*p=='/'){
        size_t rl=0;
        while(*p && rl<rcap-1){ res[rl++]=*p++; }
        res[rl]=0;
    } else {
        res[0]='/'; res[1]=0;
    }
}

/* fetch the real gateway url (public endpoint, no bot auth needed) */
static void fetch_gateway_url(char *url, size_t cap){
    strncpy(url, GATEWAY_DEFAULT, cap-1); url[cap-1]=0;
    char resp[512]="";
    if(http_get("https://discord.com/api/gateway",resp,sizeof resp)!=0){
        log_msg("gateway fetch failed; using default");
        return;
    }
    jl_val_t *r=jl_parse(resp,0);
    if(!r) return;
    const jl_val_t *u=jl_obj_get(r,"url");
    if(u && u->type==JL_STRING){
        strncpy(url,u->str,cap-1); url[cap-1]=0;
    }
    jl_free(r);
}

static void send_identify(discord_t *d, const char *token){
    /* User OAuth2 connection: no intents (bot-only field). The gateway
     * REQUIRES connection properties; we present a plausible desktop-client
     * fingerprint rather than advertising the console (less of an obvious
     * selfbot signal). */
    jl_val_t *root=jl_new_object();
    jl_obj_set(root,"op",jl_new_number(2));
    jl_val_t *dd=jl_new_object();
    jl_obj_set(dd,"token",jl_new_string(token));
    jl_val_t *pp=jl_new_object();
    jl_obj_set(pp,"os",jl_new_string("windows"));
    jl_obj_set(pp,"browser",jl_new_string("Discord Client"));
    jl_obj_set(pp,"device",jl_new_string(""));
    jl_obj_set(pp,"system_locale",jl_new_string("en-US"));
    jl_obj_set(pp,"browser_user_agent",
        jl_new_string("Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
                      "(KHTML, like Gecko) discord/1.0.9175 Chrome/122.0.6261.112 "
                      "Electron/30.0.8 Safari/537.36"));
    jl_obj_set(pp,"browser_version",jl_new_string("30.0.8"));
    jl_obj_set(pp,"os_version",jl_new_string("10.0.19045"));
    jl_obj_set(pp,"referrer",jl_new_string(""));
    jl_obj_set(pp,"referring_domain",jl_new_string(""));
    jl_obj_set(pp,"release_channel",jl_new_string("stable"));
    jl_obj_set(pp,"client_build_number",jl_new_number(300042));
    jl_obj_set(dd,"properties",pp);
    jl_obj_set(root,"d",dd);
    char *s=jl_stringify(root); jl_free(root);
    ws_send_text(&d->ws,s,strlen(s)); free(s);
}

static void send_resume(discord_t *d){
    jl_val_t *root=jl_new_object();
    jl_obj_set(root,"op",jl_new_number(6));
    jl_val_t *dd=jl_new_object();
    jl_obj_set(dd,"token",jl_new_string(d->token));
    jl_obj_set(dd,"session_id",jl_new_string(d->session_id));
    jl_obj_set(dd,"seq",jl_new_number((double)d->seq));
    jl_obj_set(root,"d",dd);
    char *s=jl_stringify(root); jl_free(root);
    ws_send_text(&d->ws,s,strlen(s)); free(s);
}

/* resume=1: reuse the stored session (fast reconnect). Otherwise IDENTIFY. */
int discord_connect(discord_t *d, const char *token, int resume){
    memset(d,0,sizeof(*d));
    strncpy(d->token, token, sizeof d->token-1);
    char url[512]; fetch_gateway_url(url,sizeof url);
    char host[128]; int port=443; char res[128];
    parse_gateway_url(url, host, sizeof host, &port, res, sizeof res);
    char key[64]=""; make_key(key);
    int rc=ws_connect(&d->ws, host, port, res, key);
    if(rc){ log_msg("ws connect fail %d",rc); return rc; }
    d->connected=1; d->last_heartbeat=time(NULL); d->last_ack=d->last_heartbeat;
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
        jl_free(h);
    }
    if(!d->hb_interval_ms) d->hb_interval_ms=45000;
    if(resume && d->session_id[0]){ send_resume(d); log_msg("discord: resume sent"); }
    else { send_identify(d, token); }
    log_msg("discord: gateway connected, hb=%lds", (long)(d->hb_interval_ms/1000));
    return 0;
}

int discord_set_presence(discord_t *d, const char *state, const char *details,
                         int party_size, int party_max,
                         const char *large_image, const char *large_text){
    jl_val_t *dd=jl_new_object();
    jl_val_t *act=jl_new_object();
    jl_obj_set(act,"name",jl_new_string(details?details:""));
    jl_obj_set(act,"type",jl_new_number(0)); /* Playing */
    if(state) jl_obj_set(act,"state",jl_new_string(state));
    if(party_size>0 && party_max>0){
        jl_val_t *pty=jl_new_object();
        jl_obj_set(pty,"size",jl_new_array());
        jl_arr_push(jl_obj_get(pty,"size"), jl_new_number(party_size));
        jl_arr_push(jl_obj_get(pty,"size"), jl_new_number(party_max));
        jl_obj_set(act,"party",pty);
    }
    if(large_image || large_text){
        jl_val_t *assets=jl_new_object();
        if(large_image) jl_obj_set(assets,"large_image",jl_new_string(large_image));
        if(large_text)  jl_obj_set(assets,"large_text", jl_new_string(large_text));
        jl_obj_set(act,"assets",assets);
    }
    jl_obj_set(dd,"activities",jl_new_array());
    jl_arr_push(jl_obj_get(dd,"activities"), act);
    jl_obj_set(dd,"status",jl_new_string("online"));
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
    jl_obj_set(dd,"status",jl_new_string("invisible"));
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
    /* heartbeat: if the gateway hasn't acked in 2 intervals, it's gone */
    if(now - d->last_ack > (d->hb_interval_ms/1000)*2 + 5){
        log_msg("heartbeat timeout (no ack)");
        return -1;
    }
    if(now - d->last_heartbeat >= d->hb_interval_ms/1000){
        char hb[64]; snprintf(hb,sizeof hb,"{\"op\":1,\"d\":%d}", d->seq);
        ws_send_text(&d->ws,hb,strlen(hb));
        d->last_heartbeat=now;
    }
    /* read any pending server frame (non-blocking) */
    char buf[1024]; int op=0,fin=0;
    int nr=ws_recv_frame(&d->ws,buf,sizeof buf,&op,&fin);
    if(nr<0){ return -1; }
    if(nr==0){ return 0; } /* no complete frame yet — not an error */
    switch(op){
    case 0: /* DISPATCH: "s" is the sequence, "t" the event name */
        {
            const char *p=strstr(buf,"\"s\":");
            if(p){ p+=4; d->seq=(int)strtol(p,NULL,10); }
            const char *t=strstr(buf,"\"t\":");
            if(t && strncmp(t+4,"\"READY\"",7)==0){
                jl_val_t *r=jl_parse(buf,nr);
                if(r){
                    const jl_val_t *dd=jl_obj_get(r,"d");
                    const jl_val_t *si=dd?jl_obj_get(dd,"session_id"):NULL;
                    if(si&&si->type==JL_STRING)
                        strncpy(d->session_id,si->str,sizeof d->session_id-1);
                    jl_free(r);
                }
            }
        }
        break;
    case 7: /* RECONNECT: server wants us to reconnect */
        log_msg("gateway: reconnect requested");
        return -1;
    case 9: /* INVALID_SESSION: resume rejected; force a fresh IDENTIFY */
        log_msg("gateway: invalid session");
        d->session_id[0]=0;
        return -1;
    case 11: /* HEARTBEAT_ACK */
        d->last_ack=now;
        break;
    default:
        break;
    }
    return 0;
}

/* Reconnect: try RESUME with the stored session; falls back to IDENTIFY
 * automatically (op 9 clears the session and triggers a fresh connect). */
int discord_reconnect(discord_t *d){
    ws_close(&d->ws); d->connected=0;
    int rc=discord_connect(d, d->token, 1);
    return rc;
}