/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 SirHumza and CyberMask367 - see NOTICE for attribution terms.
 */
/* discord.c - Discord gateway client using a USER SESSION token.
 *
 * Flow: TLS websocket -> HELLO(op10) -> IDENTIFY(op2) -> READY(dispatch) ->
 * presence updates (op3) + heartbeats (op1/op11).
 *
 * RESUME is used after a READY session: the reconnect tries RESUME first
 * (gateway session id + resume URL, replaying from the last seq) and falls
 * back to a fresh IDENTIFY when Discord rejects the session. The identify
 * budget is 1000/24h, far above our reconnect rate either way. Close frames
 * are parsed so a rejected token (close 4004) is reported as fatal instead
 * of looping forever.
 */
#include "discord.h"
#include "clock.h"
#include "b64.h"
#include "cfg.h"
#include "fingerprint_gen.h"
#include "log.h"
#include "notify.h"
#include "art.h"
#include "jsonlite.h"
#include "detect.h"
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <limits.h>
#include <time.h>

#define GW_HOST "gateway.discord.gg"
#define GW_PORT 443
#define GW_PATH "/?v=10&encoding=json"

/* Keep the token-refresh opt-in off. See discord.h: Discord treats a
 * refreshed auth_token as a replacement and the old token stops being the
 * one the desktop client holds. */
_Static_assert((DISCORD_GW_CAPABILITIES & DISCORD_GW_CAP_AUTH_TOKEN_REFRESH) == 0,
               "IDENTIFY must not request AUTH_TOKEN_REFRESH");

/* Bounded ring of gateway close codes for the diagnostic bundle. Codes are
 * protocol integers; the ring never holds payloads or account data. */
#define CLOSE_RING_N 8
static unsigned s_close_ring[CLOSE_RING_N];
static int s_close_ring_n;

static void note_close(unsigned code){
    if(s_close_ring_n < CLOSE_RING_N){
        s_close_ring[s_close_ring_n++] = code;
    } else {
        memmove(s_close_ring, s_close_ring + 1,
                sizeof(unsigned) * (CLOSE_RING_N - 1));
        s_close_ring[CLOSE_RING_N - 1] = code;
    }
}

void discord_close_ring_text(char *out, size_t cap){
    if(!out || cap == 0) return;
    out[0] = 0;
    size_t o = 0;
    for(int i = 0; i < s_close_ring_n; i++){
        int w = snprintf(out + o, cap - o, "%s%u", i ? "," : "",
                         s_close_ring[i]);
        if(w < 0 || (size_t)w >= cap - o) break;
        o += (size_t)w;
    }
}

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
    while(v < json+len && *v>='0' && *v<='9'){
        /* Gateway ops are 0..11. Cap the accumulator before it can signed-
         * overflow on a malformed payload with a long digit run. */
        if(op > 100000) return -1;
        op = op*10 + (*v-'0'); v++;
    }
    return op;
}
static void gw_seq(discord_t *d, const char *json, size_t len){
    if(!d) return;
    const char *v = top_val(json, len, "s");
    if(!v) return;
    while(v < json+len && (*v==' '||*v=='\t')) v++;
    if(v >= json+len || *v<'0' || *v>'9') return; /* null or missing: not a seq */
    int s = 0;
    while(v < json+len && *v>='0' && *v<='9'){
        int digit = *v - '0';
        /* Keep the last valid sequence instead of overflowing on junk. */
        if(s > (INT_MAX - digit) / 10) return;
        s = s*10 + digit; v++;
    }
    d->seq = s;
}
static int is_event(const char *json, size_t len, const char *name){
    const char *v = top_val(json, len, "t");
    if(!v) return 0;
    while(v < json+len && (*v==' '||*v=='\t')) v++;
    size_t nl = strlen(name);
    return (size_t)(json+len-v) >= nl + 2 && v[0] == '"'
        && !memcmp(v + 1, name, nl) && v[nl + 1] == '"';
}
static int is_ready(const char *json, size_t len){
    return is_event(json, len, "READY");
}

/* Copy a string field out of a raw JSON blob without building a full parse
 * tree (READY can carry megabytes of guild state; only these two fields
 * matter for resume). Escaped or control bytes are refused: both fields are
 * plain URLs/hex ids, so anything else is malformed. */
static int json_str_field(const char *json, size_t len, const char *key,
                          char *out, size_t cap){
    size_t kl = strlen(key);
    if(!out || cap < 2) return 0;
    for(size_t i = 0; i + kl + 3 < len; i++){
        if(json[i] != '"' || memcmp(json + i + 1, key, kl) ||
           json[i + 1 + kl] != '"') continue;
        size_t j = i + kl + 2;
        while(j < len && (json[j] == ' ' || json[j] == '\t')) j++;
        if(j >= len || json[j] != ':') continue;
        j++;
        while(j < len && (json[j] == ' ' || json[j] == '\t')) j++;
        if(j >= len || json[j] != '"') continue;
        j++;
        size_t o = 0;
        int ok = 1;
        while(j < len && json[j] != '"'){
            unsigned char c = (unsigned char)json[j];
            if(c == '\\' || c < 0x20){ ok = 0; break; }
            if(o + 1 >= cap){ ok = 0; break; }
            out[o++] = (char)c;
            j++;
        }
        if(!ok || j >= len) continue;
        out[o] = 0;
        return o > 0;
    }
    return 0;
}

static void capture_ready(discord_t *d, const char *json, size_t len){
    char sid[sizeof d->session_id];
    char url[sizeof d->resume_url];
    if(json_str_field(json, len, "session_id", sid, sizeof sid) &&
       strlen(sid) >= 8){
        snprintf(d->session_id, sizeof d->session_id, "%s", sid);
    } else {
        d->session_id[0] = 0;
        log_dbg("discord: READY had no usable session_id; resume disabled");
    }
    if(json_str_field(json, len, "resume_gateway_url", url, sizeof url) &&
       !strncmp(url, "wss://", 6)){
        snprintf(d->resume_url, sizeof d->resume_url, "%s", url);
    } else {
        d->resume_url[0] = 0;
    }
}

int discord_can_resume(const discord_t *d){
    return d && d->session_id[0] && d->resume_url[0];
}

void discord_forget_session(discord_t *d){
    if(!d) return;
    d->session_id[0] = 0;
    d->resume_url[0] = 0;
}

/* "wss://gateway-us-east1-b.discord.gg" -> host + a v10 gateway path. */
static int parse_resume_url(const char *url, char *host, size_t hcap,
                            char *path, size_t pcap){
    if(!url || strncmp(url, "wss://", 6)) return -1;
    const char *p = url + 6;
    const char *slash = strchr(p, '/');
    size_t hl = slash ? (size_t)(slash - p) : strlen(p);
    if(hl == 0 || hl >= hcap) return -1;
    for(size_t i = 0; i < hl; i++)
        if(p[i] == ':' || p[i] == ' ' || p[i] == '\\') return -1;
    memcpy(host, p, hl);
    host[hl] = 0;
    if(slash){
        if(strlen(slash) >= pcap) return -1;
        snprintf(path, pcap, "%s", slash);
    } else {
        snprintf(path, pcap, "/?v=10&encoding=json");
    }
    return 0;
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

/* Whole-message variant, for READY. The caller frees *out. */
static int rx_alloc(discord_t *d, char **out, size_t *len, int *op, int *fin,
                    int64_t deadline){
    for(;;){
        int nr=ws_recv_alloc(&d->ws,out,len,op,fin);
        if(nr==-3){ log_msg("skipped oversized gateway frame"); continue; }
        if(nr!=0) return nr;
        if(orbis_mono_s()>deadline) return -4;
        usleep(50000);
    }
}

/* HELLO must arrive first on every new gateway connection. */
static int read_hello(discord_t *d, int64_t deadline){
    char buf[2048]; int op=0, fin=0;
    int nr = rx_frame(d, buf, sizeof buf, &op, &fin, deadline);
    if(nr == -4){ log_msg("no HELLO (timeout)"); return -1; }
    if(nr <= 0){ log_msg("no HELLO (nr=%d op=%d)", nr, op); return -1; }
    if(op != 1){ log_msg("no HELLO (op=%d)", op); return -1; }
    jl_val_t *h = jl_parse(buf, nr);
    if(h){
        const jl_val_t *dd = jl_obj_get(h, "d");
        if(dd){
            const jl_val_t *hi = jl_obj_get(dd, "heartbeat_interval");
            if(hi && hi->type == JL_NUMBER){
                /* A double outside int64 range (including inf) is not a
                 * heartbeat interval; casting it would be UB. Clamp the
                 * plausible span and leave zero for junk so the default
                 * below applies. */
                double v = hi->num;
                if(v >= 5000.0 && v <= 300000.0){
                    d->hb_interval_ms = (int64_t)v;
                } else if(v > 300000.0){
                    log_msg("discord: hb_interval %gms too large; clamping to 300000",
                            v);
                    d->hb_interval_ms = 300000;
                } else if(v >= 1.0){
                    log_msg("discord: hb_interval %gms suspiciously small; clamping to 5000",
                            v);
                    d->hb_interval_ms = 5000;
                }
            }
        }
        jl_free(h);
    }
    if(!d->hb_interval_ms) d->hb_interval_ms = 45000;
    return 0;
}

/* Per-process launch id. A real desktop client generates a fresh UUID each
 * time it starts and repeats it in every property blob for that run, so a
 * constant would be a stronger signal than the field is worth. Not secret,
 * so a cheap xorshift over boot time/stack address is enough. */
static const char *launch_id(void){
    static char id[37];
    if(id[0]) return id;
    uint64_t x = (uint64_t)time(NULL) ^ ((uint64_t)orbis_mono_s() << 17)
               ^ (uint64_t)(uintptr_t)&id;
    if(!x) x = 0x9e3779b97f4a7c15ULL;
    char raw[32];
    static const char hex[] = "0123456789abcdef";
    for(int i = 0; i < 32; i++){
        x ^= x << 13; x ^= x >> 7; x ^= x << 17;
        raw[i] = hex[x & 0xf];
    }
    raw[12] = '4';                              /* version 4 */
    raw[16] = "89ab"[(unsigned char)raw[16] & 0x3]; /* RFC 4122 variant */
    snprintf(id, sizeof id, "%.8s-%.4s-%.4s-%.4s-%.12s",
             raw, raw + 8, raw + 12, raw + 16, raw + 20);
    return id;
}

/* The properties object a real desktop client sends in IDENTIFY and in
 * X-Super-Properties. Both must agree or the mismatch is itself a signal,
 * so they are built from g_cfg by this single function. */
static jl_val_t *build_fingerprint(void){
    jl_val_t *p = jl_new_object();
    if(!p) return NULL;
    jl_obj_set(p, "os", jl_new_string(g_cfg.fp_os[0] ? g_cfg.fp_os : "Windows"));
    jl_obj_set(p, "browser", jl_new_string("Discord Client"));
    jl_obj_set(p, "device", jl_new_string(""));
    jl_obj_set(p, "system_locale", jl_new_string(g_cfg.fp_locale[0] ? g_cfg.fp_locale : "en-US"));
    jl_obj_set(p, "client_version", jl_new_string(g_cfg.fp_client_version));
    jl_obj_set(p, "os_version", jl_new_string(g_cfg.fp_os_version));
    jl_obj_set(p, "os_arch", jl_new_string(g_cfg.fp_arch[0] ? g_cfg.fp_arch : "x64"));
    jl_obj_set(p, "app_arch", jl_new_string(g_cfg.fp_arch[0] ? g_cfg.fp_arch : "x64"));
    jl_obj_set(p, "has_client_mods", jl_new_bool(0));
    jl_obj_set(p, "client_build_number", jl_new_int(atoll(g_cfg.fp_client_build)));
    jl_obj_set(p, "client_launch_id", jl_new_string(launch_id()));
    jl_obj_set(p, "release_channel", jl_new_string("stable"));
    jl_obj_set(p, "browser_user_agent", jl_new_string(g_cfg.fp_user_agent));
    jl_obj_set(p, "browser_version", jl_new_string(
        g_cfg.fp_browser_version[0] ? g_cfg.fp_browser_version :
        ORBISRPC_FP_BROWSER_VERSION));
    jl_obj_set(p, "os_sdk_version", jl_new_string(
        g_cfg.fp_os_sdk_version[0] ? g_cfg.fp_os_sdk_version : "10"));
    jl_obj_set(p, "client_event_source", jl_new_null());
    return p;
}

/* Base64 JSON for the X-Super-Properties header. REST calls without it look
 * like automation, because every real client sends it on every request.
 * Returns the encoded length (0 on failure). `out` needs 4*((n+2)/3)+1. */
int discord_super_properties(char *out, size_t cap){
    if(!out || cap < 8) return 0;
    jl_val_t *p = build_fingerprint();
    if(!p) return 0;
    char *json = jl_stringify(p);
    jl_free(p);
    if(!json) return 0;
    size_t n = strlen(json);
    if(cap < 4 * ((n + 2) / 3) + 1){ free(json); return 0; }
    size_t w = b64_encode((const unsigned char *)json, n, out);
    free(json);
    return (int)w;
}

static int send_identify(discord_t *d, const char *token){
    jl_val_t *root=jl_new_object();
    if(!root) return -1;
    jl_obj_set(root,"op",jl_new_int(2));
    jl_val_t *dd=jl_new_object();
    if(!dd){ jl_free(root); return -1; }
    jl_obj_set(dd,"token",jl_new_string(token));
    jl_val_t *pp=build_fingerprint();
    if(!pp){ jl_free(root); jl_free(dd); return -1; }
    jl_obj_set(dd,"properties",pp);
    /* Opt into the same gateway behaviours a real client does, minus
     * AUTH_TOKEN_REFRESH. That capability lets Discord rotate the user
     * token in READY and expect the client to discard the old value; we
     * cannot write the replacement into the desktop client's store, so
     * requesting it logs the user out of Discord. */
    jl_obj_set(dd,"capabilities",jl_new_int(DISCORD_GW_CAPABILITIES));
    jl_obj_set(dd,"compress",jl_new_bool(0));
    /* A real client always carries cache state, even on a cold start. */
    jl_val_t *cs=jl_new_object();
    if(cs){
        jl_obj_set(cs,"guild_versions",jl_new_object());
        jl_obj_set(cs,"highest_last_message_id",jl_new_string("0"));
        jl_obj_set(cs,"read_state_version",jl_new_int(0));
        jl_obj_set(cs,"user_guild_settings_version",jl_new_int(-1));
        jl_obj_set(cs,"user_settings_version",jl_new_int(-1));
        jl_obj_set(cs,"api_code_version",jl_new_int(0));
        jl_obj_set(dd,"client_state",cs);
    }
    jl_val_t *pr=jl_new_object();
    if(!pr){ jl_free(root); jl_free(dd); return -1; }
    jl_obj_set(pr,"status",jl_new_string(
        g_cfg.status[0] ? g_cfg.status : "online"));
    jl_obj_set(pr,"activities",jl_new_array());
    jl_obj_set(pr,"afk",jl_new_bool(0));
    jl_obj_set(pr,"since",jl_new_int(0));
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
    int rc=ws_connect(&d->ws, GW_HOST, GW_PORT, GW_PATH, key,
                      g_cfg.fp_user_agent);
    if(rc){ log_msg("ws connect fail %d",rc); return -1; }
    d->connected=1;
    int64_t now=orbis_mono_s();
    d->last_heartbeat=now; d->last_ack=now;
    if(read_hello(d, now+15) != 0){
        ws_close(&d->ws); d->connected=0;
        return -1;
    }
    if(send_identify(d, token) < 0){
        log_msg("discord: identify send failed");
        ws_close(&d->ws); d->connected=0;
        return -1;
    }
    log_msg("discord: identify sent, hb=%llds",(long long)(d->hb_interval_ms/1000));
    /* READY confirms the token was accepted. `op` is the WebSocket frame
     * type; the gateway event is JSON inside — parse it, don't switch on it.
     * The full message is needed: session_id/resume_gateway_url can sit past
     * the first few KB of a large account's READY. */
    int64_t dl=orbis_mono_s()+20;
    int op=0, fin=0, nr=0;
    for(;;){
        char *msg=NULL; size_t mlen=0;
        nr=rx_alloc(d,&msg,&mlen,&op,&fin,dl);
        if(nr==-4){ log_msg("no READY after identify (timeout)"); break; }
        if(nr<=0){ log_msg("no READY after identify (nr=%d)",nr); free(msg); break; }
        if(op==8){
            unsigned code = (nr>=2) ? (((unsigned char)msg[0]<<8)|((unsigned char)msg[1])) : 0;
            free(msg);
            if(!fin){ log_msg("fragmented close; reconnecting"); ws_close(&d->ws); d->connected=0; return -1; }
            if(code == 0){ log_msg("malformed close (len %d); reconnecting", nr); ws_close(&d->ws); d->connected=0; return -1; }
            log_msg("gateway closed during auth: %u",code);
            note_close(code);
            ws_close(&d->ws); d->connected=0;
            return code==4004 ? -2 : -1;
        }
        if(op==9){ ws_pong(&d->ws); free(msg); continue; }
        if(op!=1 && op!=0){ free(msg); continue; }
        int go = gw_op(msg, mlen);
        if(go==11){ d->last_ack=orbis_mono_s(); free(msg); continue; }
        if(go==0 && is_ready(msg, mlen)){
            gw_seq(d, msg, mlen);
            capture_ready(d, msg, mlen);
            free(msg);
            log_msg("discord: gateway ready%s", d->session_id[0] ? " (resumable)" : "");
            /* Once per boot: a reconnect loop used to re-pop this every few
             * minutes, which testers reported as spam. The log line above is
             * the per-reconnect record instead. */
            notify_once_boot("connected", "Connected to Discord");
            return 0;
        }
        free(msg);
        /* other pre-READY events: ignore */
    }
    ws_close(&d->ws); d->connected=0;
    return -1;
}

int discord_resume(discord_t *d, const char *token){
    if(!d || !token || !token[0]) return -1;
    if(!discord_can_resume(d)) return -3;
    char host[128], path[192];
    if(parse_resume_url(d->resume_url, host, sizeof host,
                        path, sizeof path) != 0){
        log_msg("discord: resume URL malformed; fresh identify");
        discord_forget_session(d);
        return -3;
    }
    char key[64]=""; make_key(key);
    if(ws_connect(&d->ws, host, GW_PORT, path, key, g_cfg.fp_user_agent)){
        log_msg("discord: resume connect failed");
        return -1;
    }
    d->connected=1;
    int64_t now=orbis_mono_s();
    d->last_heartbeat=now; d->last_ack=now; d->sent_hb=0; d->hb_interval_ms=0;
    if(read_hello(d, now+15) != 0){
        ws_close(&d->ws); d->connected=0;
        return -1;
    }
    jl_val_t *root=jl_new_object();
    jl_val_t *dd=jl_new_object();
    if(!root || !dd){ jl_free(root); jl_free(dd); ws_close(&d->ws); d->connected=0; return -1; }
    jl_obj_set(root,"op",jl_new_int(6));
    jl_obj_set(dd,"token",jl_new_string(token));
    jl_obj_set(dd,"session_id",jl_new_string(d->session_id));
    jl_obj_set(dd,"seq",jl_new_int(d->seq > 0 ? d->seq : 0));
    jl_obj_set(root,"d",dd);
    char *s=jl_stringify(root); jl_free(root);
    if(!s){ ws_close(&d->ws); d->connected=0; return -1; }
    int sr = ws_send_text(&d->ws, s, strlen(s));
    free(s);
    if(sr < 0){ log_msg("discord: resume send failed"); ws_close(&d->ws); d->connected=0; return -1; }
    log_msg("discord: resume sent (seq %d)", d->seq);
    int64_t dl=orbis_mono_s()+20;
    int op=0, fin=0;
    for(;;){
        char *msg=NULL; size_t mlen=0;
        int nr=rx_alloc(d,&msg,&mlen,&op,&fin,dl);
        if(nr==-4){ log_msg("no RESUMED (timeout)"); ws_close(&d->ws); d->connected=0; return -1; }
        if(nr<=0){ free(msg); ws_close(&d->ws); d->connected=0; return -1; }
        if(op==8){
            unsigned code = (nr>=2) ? (((unsigned char)msg[0]<<8)|((unsigned char)msg[1])) : 0;
            free(msg);
            log_msg("gateway closed during resume: %u",code);
            note_close(code);
            ws_close(&d->ws); d->connected=0;
            if(code == 4004) return -2;
            if(code == 4007 || code == 4009 || code == 4003 || code == 1000 || code == 1001){
                discord_forget_session(d);
                return -3;
            }
            return -1;
        }
        if(op==9){ ws_pong(&d->ws); free(msg); continue; }
        if(op!=1 && op!=0){ free(msg); continue; }
        int go = gw_op(msg, mlen);
        if(go==11){ d->last_ack=orbis_mono_s(); free(msg); continue; }
        if(go==0 && is_event(msg, mlen, "RESUMED")){
            gw_seq(d, msg, mlen);
            free(msg);
            log_msg("discord: gateway resumed");
            notify_once_boot("connected", "Connected to Discord");
            return 0;
        }
        if(go==0 && is_ready(msg, mlen)){
            /* The gateway may answer a resume with a fresh READY instead of
             * RESUMED; adopt the new session rather than dropping it. */
            gw_seq(d, msg, mlen);
            capture_ready(d, msg, mlen);
            free(msg);
            log_msg("discord: gateway ready (resume answered READY)");
            notify_once_boot("connected", "Connected to Discord");
            return 0;
        }
        if(go==9){
            free(msg);
            log_msg("gateway: resume rejected (invalid session)");
            discord_forget_session(d);
            ws_close(&d->ws); d->connected=0;
            return -3;
        }
        if(go==7){
            free(msg);
            log_msg("gateway: reconnect requested during resume");
            ws_close(&d->ws); d->connected=0;
            return -1;
        }
        free(msg);
    }
}

int discord_set_presence(discord_t *d, const char *state, const char *name,
                         const char *application_id, int64_t started_epoch){
    return discord_set_presence_ex(d, state, name, NULL, NULL, NULL,
                                    NULL, application_id, NULL, NULL,
                                    NULL, started_epoch);
}

/* Lowercase titleId into an uploaded-asset key ("CUSA12345" -> "cusa12345").
 * Returns key length. The icon-pack fallback uses the same key as a file
 * name, so detection and the presence builder must agree on it. */
size_t discord_asset_key(const char *title_id, char *key, size_t cap){
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
                          const char *details,
                          const char *asset_idle, const char *asset_playing,
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
    /* Second visible line. Never the raw title ID: that renders as noise
     * under the name, and twice when the name itself fell back to the ID.
     * Callers pass a short human phrase (what you are doing); the
     * firmware version belongs in `state`, alongside the timer. */
    if(details&&details[0]) jl_obj_set(act,"details",jl_new_string(details));
    if(state&&state[0]) jl_obj_set(act,"state",jl_new_string(state));
    /* details intentionally mirrors the human-readable name, never the raw
     * title ID: sending the ID here renders it as a second line under the
     * game name (and twice when the name itself fell back to the ID).
     * title_id is still used for media-type lookup and artwork below. */
    if(started_epoch>0 && strcmp(g_cfg.timestamp_mode, "none")){
        jl_val_t *ts=jl_new_object();
        if(!ts){ jl_free(act); return NULL; }
        if(started_epoch < 100000000000LL){
            jl_obj_set(ts,"start",jl_new_int(started_epoch*1000LL)); /* integer ms epoch */
            const cfg_title_rule_t *rule = cfg_title_rule(&g_cfg, title_id);
            if(rule && rule->duration_min > 0)
                jl_obj_set(ts,"end",jl_new_int(
                    (started_epoch + (int64_t)rule->duration_min * 60) * 1000LL));
        }
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
    /* "settings" is idle artwork too: Settings shows the same large
     * image as home. Without this the branch fell through and the
     * activity was sent with no assets at all. */
    int idle_art = title_id && title_id[0] &&
                   (!strcmp(title_id,"home") || !strcmp(title_id,"settings") ||
                    !strcmp(title_id,"browser"));
    if(idle_art && application_id && application_id[0]){
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
        /* Prefer the configured idle asset key: already uploaded to the
         * Discord app, so it costs no external-assets round trip and
         * works with no network. art_url stays the fallback so an
         * operator who prefers a URL is not overridden. */
        const char *himg = NULL;
        if(asset_idle && asset_idle[0]){
            himg = asset_idle;
        } else {
        if(hsrc && !strncmp(hsrc,"http",4)){
            if(token && token[0] && art_resolve_mp_cached(hsrc, hmp, sizeof hmp))
                himg = hmp;
        } else if(hsrc){
            himg = hsrc; /* operator-supplied uploaded key: trusted */
        }
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
            if(discord_asset_key(title_id, key, sizeof key) >= 4){
                int n=snprintf(pack_url,sizeof pack_url,"%s%s.png",art_base_url,key);
                if(n>0 && (size_t)n<sizeof pack_url) src_url = pack_url;
            }
        }
        if(src_url && art_resolve_mp_cached(src_url, mp, sizeof mp)){
            jl_val_t *as=jl_new_object();
            if(as){
                jl_obj_set(as,"large_image",jl_new_string(mp));
                /* hover carries the raw title ID (PC-tool pattern):
                 * visible lines stay clean, ID one hover away. */
                jl_obj_set(as,"large_text",jl_new_string(title_id));
                jl_obj_set(act,"assets",as);
            }
        } else if(!src_url){
            /* No configured cover source at all: a bare key is the only
             * possible hope (an operator who uploaded title keys to their
             * own app). When a URL *was* configured but did not resolve,
             * sending the generated key is a dangling-key gamble that drops
             * the whole activity on Discord; omit the art instead. */
            char key[16];
            if(discord_asset_key(title_id, key, sizeof key) >= 4){
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
    int playing_art = title_id && title_id[0] &&
                       strcmp(title_id,"home") != 0 &&
                       strcmp(title_id,"settings") != 0 &&
                       strcmp(title_id,"browser") != 0;
    if(asset_playing && asset_playing[0] && playing_art){
        jl_val_t *as = jl_obj_get(act, "assets");
        if(!as || as->type != JL_OBJECT){
            as = jl_new_object();
            if(as) jl_obj_set(act, "assets", as);
        }
        if(as && as->type == JL_OBJECT){
            jl_obj_set(as, "small_image", jl_new_string(asset_playing));
            jl_obj_set(as, "small_text", jl_new_string("PlayStation 4"));
        }
    } else if(small_art_url && small_art_url[0]){
        char smp[512] = "";
        const char *simg = NULL;
        if(!strncmp(small_art_url, "mp:", 3)){
            simg = small_art_url;
        } else if(!strncmp(small_art_url, "http", 4) && token && token[0] &&
                  art_resolve_mp_cached(small_art_url, smp, sizeof smp)){
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

/* Discord renders nothing for an activity with no usable name, and an
 * envelope with no "activities" array clears the tile instead of setting
 * it. jl_obj_set() drops a NULL value silently, so a failed allocation
 * while building the payload would otherwise produce a structurally valid
 * but empty frame, report success (the caller clears need_post) and blank
 * the profile until the next reconcile. Validate before sending. */
static int activity_named(const jl_val_t *act){
    if(!act || act->type != JL_OBJECT) return 0;
    const jl_val_t *nm = jl_obj_get(act, "name");
    return nm && nm->type == JL_STRING && nm->str && nm->str[0];
}

/* Build one op 3 frame: {"op":3,"d":{"activities":[...],"status":...}}.
 * `activity` may be NULL for a clear. Consumes `activity` on every path
 * (the returned tree owns it). NULL when any load-bearing node is missing. */
static jl_val_t *presence_frame(jl_val_t *activity){
    jl_val_t *dd = jl_new_object();
    jl_val_t *arr = jl_new_array();
    if(!dd || !arr){
        jl_free(dd);
        jl_free(arr);
        jl_free(activity);
        return NULL;
    }
    jl_arr_push(arr, activity);          /* no-op when activity is NULL */
    jl_obj_set(dd, "activities", arr);   /* arr (and activity) now owned by dd */
    if(jl_obj_get(dd, "activities") != arr){
        /* jl_obj_set silently drops a value when its own allocation
         * failed; the array (and the activity inside it) is then still
         * ours to free. */
        jl_free(dd);
        jl_free(arr);
        return NULL;
    }
    jl_obj_set(dd, "status", jl_new_string(
        g_cfg.status[0] ? g_cfg.status : "online"));
    /* NOT idle: Gateway expects null, not 0. Sending 0 asserts
     * "idle since 1970" and can wedge the client timer at 0:00. */
    jl_obj_set(dd, "since", jl_new_null());
    jl_obj_set(dd, "afk", jl_new_bool(0));
    jl_val_t *root = jl_new_object();
    if(!root){ jl_free(dd); return NULL; }
    jl_obj_set(root, "op", jl_new_int(3));
    jl_obj_set(root, "d", dd);
    /* jl_obj_set drops NULL values, so a failed allocation above would
     * otherwise send an op-less or d-less frame. */
    const jl_val_t *opv = jl_obj_get(root, "op");
    const jl_val_t *dv = jl_obj_get(root, "d");
    const jl_val_t *av = dv ? jl_obj_get(dv, "activities") : NULL;
    if(!opv || !dv || !av || av->type != JL_ARRAY ||
       (activity && !av->child)){
        log_msg("presence: payload incomplete (out of memory?); not sending");
        jl_free(root);
        return NULL;
    }
    return root;
}

/* Serialize and send. A failed write may have put part of the frame on the
 * wire (tls_write pumps to its own deadline), so the WebSocket stream can
 * no longer be trusted: more frames after it would be misparsed by the
 * gateway and the presence would stay missing until something else failed.
 * Drop the connection instead; the daemon reconnects and the caller's
 * still-armed need_post posts the same state on a clean stream. */
static int presence_send(discord_t *d, const char *what, jl_val_t *root){
    if(!root) return -1;
    char *s = jl_stringify(root);
    jl_free(root);
    if(!s) return -1;
    int r = ws_send_text(&d->ws, s, strlen(s));
    free(s);
    if(r < 0){
        log_msg("%s send failed; dropping gateway connection", what);
        d->connected = 0;
        return -1;
    }
    return 0;
}

int discord_set_presence_ex(discord_t *d, const char *state, const char *name,
                          const char *details,
                          const char *asset_idle, const char *asset_playing,
                         const char *title_id, const char *application_id,
                         const char *art_base_url, const char *art_url,
                         const char *small_art_url,
                         int64_t started_epoch){
    if(!d || !d->connected || !name) return -1;
    jl_val_t *act = discord_build_activity(state, name, details,
        asset_idle, asset_playing, title_id,
        application_id, art_base_url, art_url, small_art_url, started_epoch, d->token);
    if(!act) return -1;
    if(!activity_named(act)){
        log_msg("presence: activity has no name (out of memory?); not sending");
        jl_free(act);
        return -1;
    }
    return presence_send(d, "presence", presence_frame(act));
}

int discord_clear_presence(discord_t *d){
    if(!d || !d->connected) return -1;
    return presence_send(d, "presence clear", presence_frame(NULL));
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
        if(nr<0){
            /* Without this the drop is invisible: the daemon reconnects and
             * the log jumps from the last heartbeat to "ready" with no cause. */
            log_msg("gateway read failed (rc=%d); reconnecting", nr);
            d->connected=0; return -1;
        }
        if(op==8){ /* WS CLOSE: payload starts with a 2-byte big-endian code */
            if(!fin){ log_msg("fragmented close; reconnecting"); d->connected=0; return -1; }
            if(nr<2){ log_msg("malformed close (len %d); reconnecting", nr); d->connected=0; return -1; }
            unsigned code = (((unsigned char)buf[0]<<8)|((unsigned char)buf[1]));
            log_msg("gateway closed: code=%u",code);
            note_close(code);
            d->connected=0;
            /* These codes mean the session itself is unusable; anything
             * else (network reset, 1006) keeps it for RESUME. */
            if(code == 4004 || code == 4007 || code == 4009 ||
               code == 4003 || code == 1000 || code == 1001)
                discord_forget_session(d);
            return code==4004 ? -2 : -1;
        }
        if(op==9){ /* WS PING -> PONG */
            if(ws_pong(&d->ws) < 0){
                log_msg("pong send failed; reconnecting");
                d->connected = 0;
                return -1;
            }
            continue;
        }
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
            discord_forget_session(d);
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
