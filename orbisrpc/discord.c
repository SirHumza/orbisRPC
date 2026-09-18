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

/* Gateway events live INSIDE the JSON text payload ("op":N) — not in the
 * WebSocket frame header (1=text, 8=close, 9=ping). Scan top-level keys
 * with string-awareness so a nested `"op":` inside a string value can't
 * spoof the event type. Works on truncated buffers too (op/s/t sit at the
 * payload prefix, while the megabyte-long "d" blob may be cut off). */
static const char *top_val(const char *json, size_t len, const char *key){
    size_t kl = strlen(key);
    int depth = 0, instr = 0, esc = 0;
    for(size_t i = 0; i < len; i++){
        char c = json[i];
        if(instr){
            if(esc) esc = 0;
            else if(c == '\\') esc = 1;
            else if(c == '"') instr = 0;
            continue;
        }
        if(c == '"'){
            /* possible key: must be at depth 1 followed by optional ws + ':' */
            size_t j = i + 1, k = 0;
            while(j < len && k < kl && json[j] == key[k]){ j++; k++; }
            if(k == kl && j < len && json[j] == '"'){
                j++;
                while(j < len && (json[j]==' '||json[j]=='\t')) j++;
                if(j < len && json[j] == ':'){
                    if(depth == 1) return json + j + 1;
                    /* nested same-name key: skip its string opener below */
                    i = j; continue;
                }
            }
            instr = 1; continue;
        }
        if(c == '{' || c == '[') depth++;
        else if(c == '}' || c == ']') depth--;
    }
    return NULL;
}
static int gw_op(const char *json, size_t len){
    const char *v = top_val(json, len, "op");
    if(!v) return -1;
    while(v < json+len && (*v==' '||*v=='\t')) v++;
    if(v >= json+len || *v<'0' || *v>'9') return -1;
    int op = 0;
    while(v < json+len && *v>='0' && *v<='9'){ op = op*10 + (*v-'0'); v++; }
    return op;
}
static void gw_seq(discord_t *d, const char *json, size_t len){
    if(!d) return;
    const char *v = top_val(json, len, "s");
    if(!v) return;
    while(v < json+len && (*v==' '||*v=='\t')) v++;
    if(v >= json+len || *v<'0' || *v>'9') return; /* null or missing: not a seq */
    int s = 0;
    while(v < json+len && *v>='0' && *v<='9'){ s = s*10 + (*v-'0'); v++; }
    d->seq = s;
}
static int is_ready(const char *json, size_t len){
    const char *v = top_val(json, len, "t");
    if(!v) return 0;
    while(v < json+len && (*v==' '||*v=='\t')) v++;
    return (size_t)(json+len-v) >= 7 && !memcmp(v, "\"READY\"", 7);
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

static int send_identify(discord_t *d, const char *token){
    jl_val_t *root=jl_new_object();
    if(!root) return -1;
    jl_obj_set(root,"op",jl_new_number(2));
    jl_val_t *dd=jl_new_object();
    if(!dd){ jl_free(root); return -1; }
    jl_obj_set(dd,"token",jl_new_string(token));
    jl_val_t *pp=jl_new_object();
    if(!pp){ jl_free(root); return -1; }
    jl_obj_set(pp,"os",jl_new_string("windows"));
    jl_obj_set(pp,"browser",jl_new_string("Discord Client"));
    jl_obj_set(pp,"device",jl_new_string(""));
    jl_obj_set(dd,"properties",pp);
    jl_val_t *pr=jl_new_object();
    if(!pr){ jl_free(root); return -1; }
    jl_obj_set(pr,"status",jl_new_string("online"));
    jl_obj_set(pr,"activities",jl_new_array());
    jl_obj_set(pr,"afk",jl_new_bool(0));
    jl_obj_set(pr,"since",jl_new_number(0));
    jl_obj_set(dd,"presence",pr);
    jl_obj_set(root,"d",dd);
    char *s=jl_stringify(root); jl_free(root);
    if(!s) return -1;
    int rc = ws_send_text(&d->ws,s,strlen(s));
    free(s);
    return rc < 0 ? -1 : 0;
}

int discord_connect(discord_t *d, const char *token){
    if(!d || !token || !token[0]) return -1;
    memset(d,0,sizeof(*d));
    strncpy(d->token, token, sizeof d->token-1);
    d->token[sizeof d->token-1] = 0;
    char key[64]=""; make_key(key);
    int rc=ws_connect(&d->ws, GW_HOST, GW_PORT, GW_PATH, key);
    if(rc){ log_msg("ws connect fail %d",rc); return -1; }
    d->connected=1;
    int64_t now=time(NULL);
    d->last_heartbeat=now; d->last_ack=now;
    /* HELLO (text frame carrying {"op":10,...}) */
    char buf[2048]; int op=0,fin=0;
    int nr=rx_frame(d,buf,sizeof buf,&op,&fin,now+15);
    if(nr<=0 || (op!=1 && op!=0)){
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
    if(send_identify(d, token) < 0){
        log_msg("discord: identify send failed");
        ws_close(&d->ws); d->connected=0;
        return -1;
    }
    log_msg("discord: identify sent, hb=%llds",(long long)(d->hb_interval_ms/1000));
    /* READY confirms the token was accepted. `op` is the WebSocket frame
     * type; the gateway event is JSON inside — parse it, don't switch on it. */
    int64_t dl=time(NULL)+20;
    for(;;){
        nr=rx_frame(d,buf,sizeof buf,&op,&fin,dl);
        if(nr<=0){ log_msg("no READY after identify (nr=%d)",nr); break; }
        if(op==8){
            if(!fin){ log_msg("fragmented close; reconnecting"); ws_close(&d->ws); d->connected=0; return -1; }
            unsigned code = nr>=2 ? (((unsigned char)buf[0]<<8)|((unsigned char)buf[1])) : 0;
            log_msg("gateway closed during auth: %u",code);
            ws_close(&d->ws); d->connected=0;
            return code==4004 ? -2 : -1;
        }
        if(op==9){ ws_pong(&d->ws); continue; }
        if(op!=1 && op!=0) continue;
        int go = gw_op(buf, (size_t)nr);
        if(go==11){ d->last_ack=time(NULL); continue; }
        if(go==0 && is_ready(buf, (size_t)nr)){
            gw_seq(d, buf, (size_t)nr);
            log_msg("discord: gateway ready");
            return 0;
        }
        /* other pre-READY events: ignore */
    }
    ws_close(&d->ws); d->connected=0;
    return -1;
}

int discord_set_presence(discord_t *d, const char *state, const char *name,
                         const char *application_id, int64_t started_epoch){
    return discord_set_presence_ex(d, state, name, NULL, application_id, started_epoch);
}

int discord_set_presence_ex(discord_t *d, const char *state, const char *name,
                         const char *title_id, const char *application_id,
                         int64_t started_epoch){
    if(!d || !d->connected || !name) return -1;
    jl_val_t *act=jl_new_object();
    if(!act) return -1;
    jl_obj_set(act,"name",jl_new_string(name?name:""));
    jl_obj_set(act,"type",jl_new_number(0)); /* Playing */
    if(state&&state[0]) jl_obj_set(act,"state",jl_new_string(state));
    if(started_epoch>0){
        jl_val_t *ts=jl_new_object();
        if(!ts){ jl_free(act); return -1; }
        jl_obj_set(ts,"start",jl_new_number((double)started_epoch*1000.0)); /* ms epoch */
        jl_obj_set(act,"timestamps",ts);
    }
    if(application_id&&application_id[0])
        jl_obj_set(act,"application_id",jl_new_string(application_id));
    if(title_id&&title_id[0]&&application_id&&application_id[0]){
        /* asset key: lowercase titleId, exactly how the icon is uploaded */
        char key[16]; size_t ki=0;
        for(size_t i=0; title_id[i] && ki<sizeof key-1; i++){
            char c=title_id[i];
            if(c>='A'&&c<='Z') c+='a'-'A';
            if((c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='_') key[ki++]=c;
        }
        key[ki]=0;
        if(ki>=4){
            jl_val_t *as=jl_new_object();
            if(as){
                jl_obj_set(as,"large_image",jl_new_string(key));
                jl_obj_set(as,"large_text",jl_new_string(name?name:""));
                jl_obj_set(act,"assets",as);
            }
        }
    }
    jl_val_t *dd=jl_new_object();
    jl_obj_set(dd,"activities",jl_new_array());
    jl_arr_push(jl_obj_get(dd,"activities"), act);
    jl_obj_set(dd,"status",jl_new_string("online"));
    jl_obj_set(dd,"since",jl_new_number(0));
    jl_obj_set(dd,"afk",jl_new_bool(0));
    jl_val_t *root=jl_new_object();
    if(!root){ jl_free(dd); return -1; }
    jl_obj_set(root,"op",jl_new_number(3));
    jl_obj_set(root,"d",dd);
    char *s=jl_stringify(root); jl_free(root);
    if(!s) return -1;
    int r=ws_send_text(&d->ws,s,strlen(s)); free(s);
    return (r>=0)?0:-1;
}

int discord_clear_presence(discord_t *d){
    if(!d || !d->connected) return -1;
    jl_val_t *dd=jl_new_object();
    jl_obj_set(dd,"activities",jl_new_array());
    jl_obj_set(dd,"status",jl_new_string("online")); /* stay visible, just idle */
    jl_obj_set(dd,"since",jl_new_number(0));
    jl_obj_set(dd,"afk",jl_new_bool(0));
    jl_val_t *root=jl_new_object();
    if(!root){ jl_free(dd); return -1; }
    jl_obj_set(root,"op",jl_new_number(3));
    jl_obj_set(root,"d",dd);
    char *s=jl_stringify(root); jl_free(root);
    if(!s) return -1;
    int r=ws_send_text(&d->ws,s,strlen(s)); free(s);
    return (r>=0)?0:-1;
}

int discord_tick(discord_t *d){
    if(!d || !d->connected) return -1;
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
        if(op==8){ /* WS CLOSE: payload starts with a 2-byte big-endian code */
            if(!fin){ log_msg("fragmented close; reconnecting"); d->connected=0; return -1; }
            unsigned code = nr>=2 ? (((unsigned char)buf[0]<<8)|((unsigned char)buf[1])) : 0;
            log_msg("gateway closed: code=%u",code);
            d->connected=0;
            return code==4004 ? -2 : -1;
        }
        if(op==9){ ws_pong(&d->ws); continue; } /* WS PING -> PONG */
        if(op!=1 && op!=0) continue;
        switch(gw_op(buf, (size_t)nr)){
        case 0: /* DISPATCH: only the sequence matters to us */
            gw_seq(d, buf, (size_t)nr);
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
        default: break;
        }
    }
    return 0;
}
