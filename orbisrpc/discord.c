/* discord.c - Discord gateway client using a USER SESSION token.
 *
 * Flow: TLS websocket -> HELLO(op10) -> IDENTIFY(op2) -> READY(dispatch) ->
 * presence updates (op3) + heartbeats (op1/op11).
 *
 * Fresh IDENTIFY per connection (no RESUME): the identify budget is 1000/24h,
 * far above our reconnect rate, and skipping RESUME removes session-state
 * bookkeeping entirely. Close frames are parsed so a rejected token
 * (close 4004) is reported as fatal instead of looping forever.
 */
#include "discord.h"
#include "b64.h"
#include "log.h"
#include "jsonlite.h"
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>

#define GW_HOST "gateway.discord.gg"
#define GW_PORT 443
#define GW_PATH "/?v=10&encoding=json"

static void make_key(char *out){
    unsigned char b[16];
    int fd=open("/dev/urandom",O_RDONLY);
    if(fd>=0){ read(fd,b,16); close(fd); }
    else { for(int i=0;i<16;i++) b[i]=(unsigned char)(time(NULL)+i*7); }
    b64_encode(b,16,out);
}

/* recv one frame with a deadline; -4 = timed out. Skipped oversized frames
 * (-3) are logged and retried transparently. */
static int rx_frame(discord_t *d, char *buf, size_t cap, int *op, int *fin, int64_t deadline){
    for(;;){
        int nr=ws_recv_frame(&d->ws,buf,cap,op,fin);
        if(nr==-3){ log_msg("skipped oversized gateway frame"); continue; }
        if(nr!=0) return nr;
        if(time(NULL)>deadline) return -4;
        usleep(50000);
    }
}

static void send_identify(discord_t *d, const char *token){
    jl_val_t *root=jl_new_object();
    jl_obj_set(root,"op",jl_new_number(2));
    jl_val_t *dd=jl_new_object();
    jl_obj_set(dd,"token",jl_new_string(token));
    jl_val_t *pp=jl_new_object();
    jl_obj_set(pp,"os",jl_new_string("windows"));
    jl_obj_set(pp,"browser",jl_new_string("Discord Client"));
    jl_obj_set(pp,"device",jl_new_string(""));
    jl_obj_set(dd,"properties",pp);
    jl_val_t *pr=jl_new_object();
    jl_obj_set(pr,"status",jl_new_string("online"));
    jl_obj_set(pr,"activities",jl_new_array());
    jl_obj_set(pr,"afk",jl_new_bool(0));
    jl_obj_set(pr,"since",jl_new_number(0));
    jl_obj_set(dd,"presence",pr);
    jl_obj_set(root,"d",dd);
    char *s=jl_stringify(root); jl_free(root);
    ws_send_text(&d->ws,s,strlen(s)); free(s);
}

int discord_connect(discord_t *d, const char *token){
    memset(d,0,sizeof(*d));
    strncpy(d->token, token, sizeof d->token-1);
    char key[64]=""; make_key(key);
    int rc=ws_connect(&d->ws, GW_HOST, GW_PORT, GW_PATH, key);
    if(rc){ log_msg("ws connect fail %d",rc); return -1; }
    d->connected=1;
    int64_t now=time(NULL);
    d->last_heartbeat=now; d->last_ack=now;
    /* HELLO */
    char buf[2048]; int op=0,fin=0;
    int nr=rx_frame(d,buf,sizeof buf,&op,&fin,now+15);
    if(nr<=0 || op!=1){
        log_msg("no HELLO (nr=%d op=%d)",nr,op);
        ws_close(&d->ws); d->connected=0;
        return -1;
    }
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
    send_identify(d, token);
    log_msg("discord: identify sent, hb=%llds",(long long)(d->hb_interval_ms/1000));
    /* READY confirms the token was accepted */
    int64_t dl=time(NULL)+20;
    for(;;){
        nr=rx_frame(d,buf,sizeof buf,&op,&fin,dl);
        if(nr<=0){ log_msg("no READY after identify (nr=%d)",nr); break; }
        if(op==11){ d->last_ack=time(NULL); continue; }
        if(op==8){
            unsigned code = nr>=2 ? (((unsigned char)buf[0]<<8)|((unsigned char)buf[1])) : 0;
            log_msg("gateway closed during auth: %u",code);
            ws_close(&d->ws); d->connected=0;
            return code==4004 ? -2 : -1;
        }
        if(op==0 && strstr(buf,"READY")){
            const char *p=strstr(buf,"\"s\":");
            if(p){ p+=4; while(*p==' ')p++; if(*p>='0'&&*p<='9') d->seq=(int)strtol(p,NULL,10); }
            log_msg("discord: gateway ready");
            return 0;
        }
        /* other pre-READY ops: ignore */
    }
    ws_close(&d->ws); d->connected=0;
    return -1;
}

int discord_set_presence(discord_t *d, const char *state, const char *name,
                         const char *application_id, int64_t started_epoch){
    if(!d->connected) return -1;
    jl_val_t *act=jl_new_object();
    jl_obj_set(act,"name",jl_new_string(name?name:""));
    jl_obj_set(act,"type",jl_new_number(0)); /* Playing */
    if(state&&state[0]) jl_obj_set(act,"state",jl_new_string(state));
    if(started_epoch>0){
        jl_val_t *ts=jl_new_object();
        jl_obj_set(ts,"start",jl_new_number((double)started_epoch*1000.0)); /* ms epoch */
        jl_obj_set(act,"timestamps",ts);
    }
    if(application_id&&application_id[0])
        jl_obj_set(act,"application_id",jl_new_string(application_id));
    jl_val_t *dd=jl_new_object();
    jl_obj_set(dd,"activities",jl_new_array());
    jl_arr_push(jl_obj_get(dd,"activities"), act);
    jl_obj_set(dd,"status",jl_new_string("online"));
    jl_obj_set(dd,"since",jl_new_number(0));
    jl_obj_set(dd,"afk",jl_new_bool(0));
    jl_val_t *root=jl_new_object();
    jl_obj_set(root,"op",jl_new_number(3));
    jl_obj_set(root,"d",dd);
    char *s=jl_stringify(root); jl_free(root);
    int r=ws_send_text(&d->ws,s,strlen(s)); free(s);
    return (r>=0)?0:-1;
}

int discord_clear_presence(discord_t *d){
    if(!d->connected) return -1;
    jl_val_t *dd=jl_new_object();
    jl_obj_set(dd,"activities",jl_new_array());
    jl_obj_set(dd,"status",jl_new_string("online")); /* stay visible, just idle */
    jl_obj_set(dd,"since",jl_new_number(0));
    jl_obj_set(dd,"afk",jl_new_bool(0));
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
    long hb_s=(long)(d->hb_interval_ms/1000); if(hb_s<5)hb_s=5;
    /* gateway must ack heartbeats; 2 missed intervals means it's gone */
    if(d->sent_hb && now-d->last_ack > hb_s*2+15){
        log_msg("heartbeat timeout (no ack)");
        d->connected=0;
        return -1;
    }
    /* fire slightly EARLY (tick cadence adds up to ~1s of jitter) so we are
     * always inside the gateway's heartbeat window */
    long fire = hb_s>3 ? hb_s-2 : hb_s;
    if(now-d->last_heartbeat >= fire){
        char hb[64];
        if(d->seq>0) snprintf(hb,sizeof hb,"{\"op\":1,\"d\":%d}",d->seq);
        else         snprintf(hb,sizeof hb,"{\"op\":1,\"d\":null}");
        ws_send_text(&d->ws,hb,strlen(hb));
        d->last_heartbeat=now; d->sent_hb=1;
    }
    /* drain pending server frames (non-blocking); 2048 covers the dispatch
     * envelope ({"t":..,"s":N,..) with margin — big payloads are consumed
     * inside ws_recv_frame regardless of this cap */
    char buf[2048]; int op=0,fin=0;
    for(int i=0;i<32;i++){
        int nr=ws_recv_frame(&d->ws,buf,sizeof buf,&op,&fin);
        if(nr==-3){ log_msg("skipped oversized frame"); continue; }
        if(nr==0) break;
        if(nr<0){ d->connected=0; return -1; }
        switch(op){
        case 0: /* DISPATCH: only the sequence matters to us */
            {
                const char *p=strstr(buf,"\"s\":");
                if(p){
                    p+=4; while(*p==' ')p++;
                    if(*p=='n'){ /* null: not a dispatch seq */ }
                    else if(*p>='0'&&*p<='9') d->seq=(int)strtol(p,NULL,10);
                }
            }
            break;
        case 7: /* RECONNECT requested */
            log_msg("gateway: reconnect requested");
            d->connected=0;
            return -1;
        case 9: /* INVALID_SESSION */
            log_msg("gateway: invalid session");
            d->connected=0;
            return -1;
        case 11:
            d->last_ack=now;
            break;
        case 8: /* CLOSE: payload starts with a 2-byte big-endian code */
            {
                unsigned code = nr>=2 ? (((unsigned char)buf[0]<<8)|((unsigned char)buf[1])) : 0;
                log_msg("gateway closed: code=%u",code);
                d->connected=0;
                return code==4004 ? -2 : -1;
            }
        default: break;
        }
    }
    return 0;
}
