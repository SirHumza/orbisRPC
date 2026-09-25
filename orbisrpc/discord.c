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
#include "clock.h"
#include "b64.h"
#include "log.h"
#include "art.h"
#include "jsonlite.h"
#include "detect.h"
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
    memset(b, 0, sizeof b);
    int fd=open("/dev/urandom",O_RDONLY);
    if(fd>=0){
        size_t got = 0;
        while(got < sizeof b){
            long r = read(fd, (char *)b + got, sizeof b - got);
            if(r <= 0) break;
            got += (size_t)r;
        }
        close(fd);
        if(got < sizeof b){
            /* short read: mix time in rather than leaving stack bytes */
            uint32_t t = (uint32_t)time(NULL);
            for(size_t i = got; i < sizeof b; i++, t = t*1664525u + 1013904223u)
                b[i] ^= (unsigned char)(t >> 24);
        }
    }
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
        if(orbis_mono_s()>deadline) return -4;
        usleep(50000);
    }
}

static int send_identify(discord_t *d, const char *token){
    jl_val_t *root=jl_new_object();
    if(!root) return -1;
    jl_obj_set(root,"op",jl_new_int(2));
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
    jl_obj_set(pr,"since",jl_new_null());
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
    int64_t now=orbis_mono_s();
    d->last_heartbeat=now; d->last_ack=now;
    /* HELLO (text frame carrying {"op":10,...}) */
    char buf[2048]; int op=0,fin=0;
    int nr=rx_frame(d,buf,sizeof buf,&op,&fin,now+15);
    if(nr==-4){ log_msg("no HELLO (timeout)"); ws_close(&d->ws); d->connected=0; return -1; }
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
    if(d->hb_interval_ms < 5000){
        log_msg("discord: hb_interval %lldms suspiciously small; clamping to 5000",
                (long long)d->hb_interval_ms);
        d->hb_interval_ms = 5000;
    }
    if(send_identify(d, token) < 0){
        log_msg("discord: identify send failed");
        ws_close(&d->ws); d->connected=0;
        return -1;
    }
    log_msg("discord: identify sent, hb=%llds",(long long)(d->hb_interval_ms/1000));
    /* READY confirms the token was accepted. `op` is the WebSocket frame
     * type; the gateway event is JSON inside — parse it, don't switch on it. */
    int64_t dl=orbis_mono_s()+20;
    for(;;){
        nr=rx_frame(d,buf,sizeof buf,&op,&fin,dl);
        if(nr==-4){ log_msg("no READY after identify (timeout)"); break; }
        if(nr<=0){ log_msg("no READY after identify (nr=%d)",nr); break; }
        if(op==8){
            if(!fin){ log_msg("fragmented close; reconnecting"); ws_close(&d->ws); d->connected=0; return -1; }
            if(nr<2){ log_msg("malformed close (len %d); reconnecting", nr); ws_close(&d->ws); d->connected=0; return -1; }
            unsigned code = (((unsigned char)buf[0]<<8)|((unsigned char)buf[1]));
            log_msg("gateway closed during auth: %u",code);
            ws_close(&d->ws); d->connected=0;
            return code==4004 ? -2 : -1;
        }
        if(op==9){ ws_pong(&d->ws); continue; }
        if(op!=1) continue;
        int go = gw_op(buf, (size_t)nr);
        if(go==11){ d->last_ack=orbis_mono_s(); continue; }
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
    return discord_set_presence_ex(d, state, name, NULL, application_id, NULL, NULL, NULL, started_epoch);
}

/* Lowercase titleId into an asset-key buffer. Returns key length. */
static size_t asset_key(const char *title_id, char *key, size_t cap){
    size_t ki = 0;
    if(!title_id || !key || cap == 0) return 0;
    for(size_t i = 0; title_id[i] && ki < cap-1; i++){
        char c = title_id[i];
        if(c>='A'&&c<='Z') c += 'a'-'A';
        if((c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='_') key[ki++]=c;
    }
    key[ki] = 0;
    return ki;
}

/* Test seam: pure activity-JSON builder (no sockets). token may be ""
 * to skip the mp: proxy (deterministic offline tests). NULL on OOM. */
jl_val_t *discord_build_activity(const char *state, const char *name,
                          const char *title_id, const char *application_id,
                          const char *art_base_url, const char *art_url,
                          const char *small_art_url,
                          int64_t started_epoch, const char *token){
    if(!name) return NULL;
    jl_val_t *act=jl_new_object();
    if(!act) return NULL;
    jl_obj_set(act,"name",jl_new_string(name?name:""));
    {
        /* Media apps (Netflix/YouTube/...) post Watching/Listening. */
        int atype = 0;
        if(title_id && title_id[0]){
            atype = detect_media_type(title_id);
            if(atype != 0 && atype != 2 && atype != 3) atype = 0;
        }
        jl_obj_set(act,"type",jl_new_int(atype)); /* 0 Playing */
    }
    if(state&&state[0]) jl_obj_set(act,"state",jl_new_string(state));
    /* details intentionally mirrors the human-readable name, never the raw
     * title ID: sending the ID here renders it as a second line under the
     * game name (and twice when the name itself fell back to the ID).
     * title_id is still used for media-type lookup and artwork below. */
    if(started_epoch>0){
        jl_val_t *ts=jl_new_object();
        if(!ts){ jl_free(act); return NULL; }
        if(started_epoch > 0 && started_epoch < 100000000000LL)
            jl_obj_set(ts,"start",jl_new_int(started_epoch*1000LL)); /* integer ms epoch */
        jl_obj_set(act,"timestamps",ts);
    }
    if(application_id&&application_id[0])
        jl_obj_set(act,"application_id",jl_new_string(application_id));
    else if(title_id&&title_id[0]&&!(art_base_url&&art_base_url[0])&&!(art_url&&art_url[0])){
        /* No artwork will appear without a shared app or art URL pack:
         * say so once instead of failing silently every update. */
        static int art_warned = 0;
        if(!art_warned){ art_warned = 1;
            log_msg("art: no application_id or art_base_url; presence sends without artwork"); }
    }
    /* Artwork (verified on hardware 2026-09-23): raw https URLs and
     * dangling keys drop the whole activity. Two working forms, tried in
     * order: (1) mp: proxy resolved via external-assets for the URL we
     * have (Sony CDN or art_base_url pack); (2) uploaded asset key. */
    if(title_id&&!strcmp(title_id,"home")&&application_id&&application_id[0]){
        /* Idle tile: PlayStation logo. home_art (arrives as art_url):
         * http(s) URL -> mp: proxy; bare value -> uploaded asset key
         * used as-is; empty -> <art_base_url>home.png when the pack
         * carries it. A URL that fails mp: sends NO assets (never a
         * dangling key: those drop the whole activity). */
        const char *hsrc = (art_url&&art_url[0]) ? art_url : NULL;
        char home_pack[320] = "";
        if(hsrc && strchr(hsrc, '"')) hsrc = NULL; /* config garbage fails
            soft to the pack default, never a broken JSON POST */
        if(hsrc && strncmp(hsrc, "http", 4) != 0){
            for(const char *q = hsrc; *q; q++){
                char ch = *q;
                if(!((ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '_')){
                    hsrc = NULL; /* not a valid asset key: pack default */
                    break;
                }
            }
        }
        if(!hsrc && art_base_url&&art_base_url[0]){
            int n=snprintf(home_pack,sizeof home_pack,"%shome.png",art_base_url);
            if(n>0 && (size_t)n<sizeof home_pack) hsrc = home_pack;
        }
        char hmp[512] = "";
        const char *himg = NULL;
        if(hsrc && !strncmp(hsrc,"http",4)){
            if(token && token[0] && art_resolve_mp(application_id, token, hsrc, hmp, sizeof hmp))
                himg = hmp;
        } else if(hsrc){
            himg = hsrc; /* operator-supplied uploaded key: trusted */
        }
        if(himg){
            jl_val_t *as=jl_new_object();
            if(as){
                jl_obj_set(as,"large_image",jl_new_string(himg));
                jl_obj_set(as,"large_text",jl_new_string(name?name:""));
                jl_obj_set(act,"assets",as);
            }
        }
    } else if(title_id&&title_id[0]&&application_id&&application_id[0]){
        char mp[512] = "";
        const char *src_url = (art_url&&art_url[0]) ? art_url : NULL;
        char pack_url[320] = "";
        if(!src_url && art_base_url&&art_base_url[0]&&title_id){
            char key[16];
            if(asset_key(title_id, key, sizeof key) >= 4){
                int n=snprintf(pack_url,sizeof pack_url,"%s%s.png",art_base_url,key);
                if(n>0 && (size_t)n<sizeof pack_url) src_url = pack_url;
            }
        }
        if(src_url && token && token[0] &&
           art_resolve_mp(application_id, token, src_url, mp, sizeof mp)){
            jl_val_t *as=jl_new_object();
            if(as){
                jl_obj_set(as,"large_image",jl_new_string(mp));
                /* hover carries the raw title ID (PC-tool pattern):
                 * visible lines stay clean, ID one hover away. */
                jl_obj_set(as,"large_text",jl_new_string(title_id));
                jl_obj_set(act,"assets",as);
            }
        } else {
            char key[16];
            if(asset_key(title_id, key, sizeof key) >= 4){
                jl_val_t *as=jl_new_object();
                if(as){
                    jl_obj_set(as,"large_image",jl_new_string(key));
                    jl_obj_set(as,"large_text",jl_new_string(title_id));
                    jl_obj_set(act,"assets",as);
                }
            }
        }
    }
    /* System badge (PC-tool pattern): small corner tile, e.g. the
     * PlayStation logo next to game art. mp: URLs resolve through the
     * disk-cached proxy; anything unresolvable is omitted, never sent
     * raw (bad small images drop the whole activity). */
    if(small_art_url && small_art_url[0]){
        char smp[512] = "";
        const char *simg = NULL;
        if(!strncmp(small_art_url, "mp:", 3)){
            simg = small_art_url;
        } else if(!strncmp(small_art_url, "http", 4) && token && token[0] &&
                  art_resolve_mp(application_id, token, small_art_url, smp, sizeof smp)){
            simg = smp;
        }
        if(simg){
            jl_val_t *as = jl_obj_get(act, "assets");
            if(as && as->type == JL_OBJECT){
                jl_obj_set(as, "small_image", jl_new_string(simg));
                jl_obj_set(as, "small_text", jl_new_string("PlayStation 4"));
            }
        }
    }
    return act;
}

int discord_set_presence_ex(discord_t *d, const char *state, const char *name,
                         const char *title_id, const char *application_id,
                         const char *art_base_url, const char *art_url,
                         const char *small_art_url,
                         int64_t started_epoch){
    if(!d || !d->connected || !name) return -1;
    jl_val_t *act = discord_build_activity(state, name, title_id,
        application_id, art_base_url, art_url, small_art_url, started_epoch, d->token);
    if(!act) return -1;
    jl_val_t *dd=jl_new_object();
    jl_obj_set(dd,"activities",jl_new_array());
    jl_arr_push(jl_obj_get(dd,"activities"), act);
    jl_obj_set(dd,"status",jl_new_string("online"));
    /* NOT idle: Gateway expects null, not 0. Sending 0 asserts
     * "idle since 1970" and can wedge the client timer at 0:00. */
    jl_obj_set(dd,"since",jl_new_null());
    jl_obj_set(dd,"afk",jl_new_bool(0));
    jl_val_t *root=jl_new_object();
    if(!root){ jl_free(dd); return -1; }
    jl_obj_set(root,"op",jl_new_int(3));
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
    jl_obj_set(dd,"since",jl_new_null());
    jl_obj_set(dd,"afk",jl_new_bool(0));
    jl_val_t *root=jl_new_object();
    if(!root){ jl_free(dd); return -1; }
    jl_obj_set(root,"op",jl_new_int(3));
    jl_obj_set(root,"d",dd);
    char *s=jl_stringify(root); jl_free(root);
    if(!s) return -1;
    int r=ws_send_text(&d->ws,s,strlen(s)); free(s);
    return (r>=0)?0:-1;
}

int discord_tick(discord_t *d){
    if(!d || !d->connected) return -1;
    int64_t now=orbis_mono_s();
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
        if(ws_send_text(&d->ws,hb,strlen(hb)) < 0){
            log_msg("heartbeat send failed; reconnecting");
            d->connected=0;
            return -1;
        }
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
            if(nr<2){ log_msg("malformed close (len %d); reconnecting", nr); d->connected=0; return -1; }
            unsigned code = (((unsigned char)buf[0]<<8)|((unsigned char)buf[1]));
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
        case 9: /* INVALID_SESSION: session dead, fresh IDENTIFY needed.
                   * Distinct code so the caller retries promptly instead of
                   * doubling into a long backoff. */
            log_msg("gateway: invalid session");
            d->connected=0;
            return -3;
        case 11:
            d->last_ack=now;
            break;
        default: break;
        }
    }
    return 0;
}
