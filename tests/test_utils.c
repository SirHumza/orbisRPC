/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 SirHumza and CyberMask367 - see NOTICE for attribution terms.
 */
#include <string.h>
#include <stdlib.h>
#include "../orbisrpc/jsonlite.h"
#include "../orbisrpc/b64.h"
#include "../orbisrpc/sfo.h"
#include "../orbisrpc/tmdb_crypto.h"
#include "../orbisrpc/updater.h"
#include "../orbisrpc/updater_http.h"
#include "../orbisrpc/art.h"
#include "../orbisrpc/health.h"
#include "../orbisrpc/manifest.h"
#include "../orbisrpc/cfg.h"
#include "../orbisrpc/fingerprint_gen.h"
#include "../orbisrpc/appdb.h"
#include "../orbisrpc/discord.h"
#include "../orbisrpc/detect.h"
#include "../orbisrpc/procwalk.h"
#include "../orbisrpc/bigapp.h"
#include "../orbisrpc/retro.h"
#include "../orbisrpc/pkgzone.h"
#include "../orbisrpc/gamecache.h"
#include "../orbisrpc/fw.h"
#include "../orbisrpc/log.h"
#include "../orbisrpc/sched.h"
#include "../orbisrpc/clock.h"
#include "../orbisrpc/diag.h"
#include "../orbisrpc/token.h"
#include "../orbisrpc/builtin_titles.h"
#include "../orbisrpc/goldhen.h"
#include "../orbisrpc/lock.h"
#include "../orbisrpc/notify.h"
#include "../orbisrpc/gamecache.h"
#include "../orbisrpc/pkgzone.h"
#include "../installer/icfg.h"
#include "sqlite3.h"
#include <string.h>
#include <dirent.h>
#include <time.h>

/* discord.c host stubs: builder tests never touch the wire. The replay
 * driver below switches them into a scripted-frame mode. */
typedef struct { int wsop; int fin; char payload[2048]; int len; } replay_frame_t;
static replay_frame_t s_rf[64];
static int s_rf_n = 0, s_rf_i = 0;
static int s_replay_on = 0;
static int s_pong_n = 0;
static int s_sent_op[32];
static char s_sent_json[32][4096];
static int s_sent_n = 0;
static int s_fail_send = 0;

static void replay_begin(replay_frame_t *f, int n){
    memcpy(s_rf, f, sizeof(replay_frame_t) * (size_t)n);
    s_rf_n = n; s_rf_i = 0; s_sent_n = 0; s_pong_n = 0; s_replay_on = 1;
}

static void replay_end(void){ s_replay_on = 0; }
int detect_media_type(const char *t){
    if(t && !strcmp(t, "CUSA00127")) return 3;
    return 0;
}
static char s_last_host[128];
int ws_connect(ws_t *w, const char *h, int p, const char *r, const char *k,
               const char *ua){
    (void)w; (void)p; (void)r; (void)k; (void)ua;
    snprintf(s_last_host, sizeof s_last_host, "%s", h ? h : "");
    return s_replay_on ? 0 : -1;
}
int ws_send_text(ws_t *w, const char *m, size_t n){
    (void)w;
    if(!s_replay_on || s_fail_send) return -1;
    jl_val_t *r = jl_parse(m, n);
    int op = -1;
    if(r){ const jl_val_t *v = jl_obj_get(r, "op"); if(v && v->type == JL_NUMBER) op = (int)v->num; jl_free(r); }
    if(s_sent_n < 32){
        size_t copy = n < sizeof s_sent_json[0] - 1 ? n : sizeof s_sent_json[0] - 1;
        memcpy(s_sent_json[s_sent_n], m, copy);
        s_sent_json[s_sent_n][copy] = 0;
        s_sent_op[s_sent_n++] = op;
    }
    return 0;
}
int ws_recv_frame(ws_t *w, char *b, size_t c, int *o, int *f){
    (void)w;
    if(!s_replay_on || s_rf_i >= s_rf_n) return 0;
    replay_frame_t *fr = &s_rf[s_rf_i++];
    size_t n = (size_t)fr->len;
    if(n >= c) n = c - 1;
    memcpy(b, fr->payload, n);
    b[n] = 0;
    if(o) *o = fr->wsop;
    if(f) *f = fr->fin;
    return (int)n;
}
int ws_recv_alloc(ws_t *w, char **out, size_t *out_len, int *o, int *f){
    (void)w;
    if(!s_replay_on || s_rf_i >= s_rf_n) return 0;
    replay_frame_t *fr = &s_rf[s_rf_i++];
    char *p = (char*)malloc((size_t)fr->len + 1);
    if(!p) return -1;
    memcpy(p, fr->payload, (size_t)fr->len);
    p[fr->len] = 0;
    *out = p;
    if(out_len) *out_len = (size_t)fr->len;
    if(o) *o = fr->wsop;
    if(f) *f = fr->fin;
    return fr->len;
}
int ws_pong(ws_t *w){ (void)w; s_pong_n++; return s_replay_on ? 0 : -1; }
int ws_close(ws_t *w){ (void)w; return s_replay_on ? 0 : -1; }
#include <mbedtls/ecdsa.h>
#include <mbedtls/ecp.h>
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/entropy.h>
#include <mbedtls/sha256.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>

/* Portable temp dir (mkdtemp needs feature macros this toolchain lacks). */
static int make_tmpdir(char *out, size_t cap){
    static int seq = 0;
    snprintf(out, cap, "/tmp/orx_test_%d_%d", (int)getpid(), seq++);
    if(mkdir(out, 0700) != 0) return -1;
    return 0;
}

/* Read a whole file into a NUL-terminated buffer; 0 when unreadable. */
static size_t read_text_file(const char *path, char *out, size_t cap){
    if(!path || !out || cap < 2) return 0;
    FILE *f = fopen(path, "rb");
    if(!f) return 0;
    size_t n = fread(out, 1, cap - 1, f);
    fclose(f);
    out[n] = 0;
    return n;
}

static void test_json(void) {
    const char input[] = "{\"name\":\"A\\u00e9\",\"items\":[true,2,null]}";
    jl_val_t *root = jl_parse(input, sizeof(input) - 1);
    assert(root && root->type == JL_OBJECT);
    assert(strcmp(jl_obj_get(root, "name")->str, "A\xc3\xa9") == 0);
    assert(jl_arr_at(jl_obj_get(root, "items"), 0)->num == 1);
    assert(jl_arr_at(jl_obj_get(root, "items"), 1)->num == 2);
    assert(jl_arr_at(jl_obj_get(root, "items"), 2)->type == JL_NULL);
    jl_free(root);

    /* Surrogate pairs are one 4-byte UTF-8 code point, not two invalid
     * 3-byte sequences. A lone surrogate becomes U+FFFD. */
    {
        const char pair[] = "{\"n\":\"\\ud83c\\udfae\"}";
        jl_val_t *p = jl_parse(pair, sizeof(pair) - 1);
        assert(p != NULL);
        const jl_val_t *pn = jl_obj_get(p, "n");
        assert(pn && pn->type == JL_STRING);
        assert(!strcmp(pn->str, "\xF0\x9F\x8E\xAE"));
        jl_free(p);

        const char lone[] = "{\"n\":\"\\ud83c\"}";
        jl_val_t *l = jl_parse(lone, sizeof(lone) - 1);
        assert(l != NULL);
        const jl_val_t *ln = jl_obj_get(l, "n");
        assert(ln && ln->type == JL_STRING);
        assert(!strcmp(ln->str, "\xEF\xBF\xBD"));
        jl_free(l);
    }

    assert(jl_parse("{} trailing", 11) == NULL);
    assert(jl_parse("[1,]", 4) == NULL);
    assert(jl_parse("\"unterminated", 13) == NULL);
}

static void test_gateway_op_spoof(void) {
    /* string value containing `"op":` must not spoof the real op */
    const char input[] = "{\"note\":\"x \\\"op\\\":99 y\",\"op\":10,\"s\":42}";
    jl_val_t *r = jl_parse(input, sizeof(input)-1);
    assert(r && r->type == JL_OBJECT);
    const jl_val_t *op = jl_obj_get(r, "op");
    const jl_val_t *s = jl_obj_get(r, "s");
    assert(op && op->type == JL_NUMBER && (int)op->num == 10);
    assert(s && s->type == JL_NUMBER && (int)s->num == 42);
    jl_free(r);
    /* missing op -> NULL, not crash */
    const char no_op[] = "{\"t\":\"READY\"}";
    jl_val_t *r2 = jl_parse(no_op, sizeof(no_op)-1);
    assert(r2 && jl_obj_get(r2, "op") == NULL);
    jl_free(r2);
}

/* The handshake, IDENTIFY and REST must all report one identity tuple.
 * A stale or contradictory field is exactly what marks a hand-rolled
 * client, so this test reads the real IDENTIFY payload. */
static void test_identity_consistency(void) {
    static const char *const frames[] = {
        "{\"op\":10,\"d\":{\"heartbeat_interval\":45000}}",
        "{\"op\":0,\"t\":\"READY\",\"s\":1,\"d\":{}}"
    };
    replay_frame_t f[2];
    memset(f, 0, sizeof f);
    for(int i = 0; i < 2; i++){
        f[i].wsop = 1; f[i].fin = 1;
        f[i].len = (int)strlen(frames[i]);
        memcpy(f[i].payload, frames[i], (size_t)f[i].len);
    }
    cfg_t saved = g_cfg;
    cfg_defaults(&g_cfg);
    snprintf(g_cfg.status, sizeof g_cfg.status, "dnd");
    replay_begin(f, 2);
    discord_t d;
    memset(&d, 0, sizeof d);
    assert(discord_connect(&d, "REPLAY-TOKEN") == 0);
    replay_end();
    assert(s_sent_n >= 1 && s_sent_op[0] == 2);
    jl_val_t *id = jl_parse(s_sent_json[0], strlen(s_sent_json[0]));
    assert(id && id->type == JL_OBJECT);
    const jl_val_t *dd = jl_obj_get(id, "d");
    const jl_val_t *identify_presence = dd ? jl_obj_get(dd, "presence") : NULL;
    const jl_val_t *identify_status = identify_presence
        ? jl_obj_get(identify_presence, "status") : NULL;
    assert(identify_status && identify_status->type == JL_STRING
           && !strcmp(identify_status->str, "dnd"));
    const jl_val_t *p = dd ? jl_obj_get(dd, "properties") : NULL;
    assert(p && p->type == JL_OBJECT);
    const jl_val_t *cv = jl_obj_get(p, "client_version");
    const jl_val_t *cb = jl_obj_get(p, "client_build_number");
    const jl_val_t *ua = jl_obj_get(p, "browser_user_agent");
    const jl_val_t *bv = jl_obj_get(p, "browser_version");
    const jl_val_t *lid = jl_obj_get(p, "client_launch_id");
    assert(cv && cv->type == JL_STRING && !strcmp(cv->str, ORBISRPC_FP_VERSION));
    assert(cb && cb->type == JL_NUMBER && (long)cb->num == ORBISRPC_FP_BUILD);
    assert(ua && ua->type == JL_STRING);
    /* The UA is the reviewed tuple, not a second hardcoded string. */
    assert(!strcmp(ua->str, ORBISRPC_FP_USER_AGENT));
    assert(strstr(ua->str, "discord/" ORBISRPC_FP_VERSION) != NULL);
    assert(strstr(ua->str, "Chrome/" ORBISRPC_FP_CHROME) != NULL);
    assert(strstr(ua->str, "Electron/" ORBISRPC_FP_ELECTRON) != NULL);
    /* browser_version is the Chromium version, same as the UA's Chrome/. */
    assert(bv && bv->type == JL_STRING && !strcmp(bv->str, ORBISRPC_FP_CHROME));
    /* Per-launch UUID, not a constant. */
    assert(lid && lid->type == JL_STRING && strlen(lid->str) == 36);
    assert(lid->str[8] == '-' && lid->str[13] == '-' && lid->str[18] == '-'
           && lid->str[23] == '-');
    jl_free(id);
    g_cfg = saved;
}

static void test_identify_capabilities(void) {
    static const char *const frames[] = {
        "{\"op\":10,\"d\":{\"heartbeat_interval\":45000}}",
        "{\"op\":0,\"t\":\"READY\",\"s\":1,\"d\":{}}"
    };
    replay_frame_t f[2];
    memset(f, 0, sizeof f);
    for(int i = 0; i < 2; i++){
        f[i].wsop = 1;
        f[i].fin = 1;
        f[i].len = (int)strlen(frames[i]);
        assert((size_t)f[i].len < sizeof f[i].payload);
        memcpy(f[i].payload, frames[i], (size_t)f[i].len);
    }
    replay_begin(f, 2);
    discord_t d;
    memset(&d, 0, sizeof d);
    int rc = discord_connect(&d, "REPLAY-TOKEN");
    replay_end();
    assert(rc == 0);
    assert(s_sent_n >= 1 && s_sent_op[0] == 2);
    jl_val_t *id = jl_parse(s_sent_json[0], strlen(s_sent_json[0]));
    assert(id && id->type == JL_OBJECT);
    const jl_val_t *dd = jl_obj_get(id, "d");
    const jl_val_t *caps = dd ? jl_obj_get(dd, "capabilities") : NULL;
    assert(caps && caps->type == JL_NUMBER);
    /* AUTH_TOKEN_REFRESH would let Discord replace the token and invalidate
     * the copy the desktop client holds; orbisRPC cannot write it back. */
    assert(((unsigned)caps->num & DISCORD_GW_CAP_AUTH_TOKEN_REFRESH) == 0);
    jl_free(id);
}

/* READY -> RESUME round trip through the real gateway code. The replay
 * transport never truncates, so this checks payload construction and state
 * transitions; test_ws.c checks that a >2 KB READY survives the parser. */
static void test_resume_flow(void){
    static const char *const hello = "{\"op\":10,\"d\":{\"heartbeat_interval\":45000}}";
    static const char *const ready =
        "{\"op\":0,\"t\":\"READY\",\"s\":7,\"d\":{"
        "\"session_id\":\"0123456789abcdef0123456789abcdef\","
        "\"resume_gateway_url\":\"wss://gateway-us-east1-b.discord.gg\","
        "\"user\":{\"id\":\"1\"}}}";
    replay_frame_t f[2];
    memset(f, 0, sizeof f);
    f[0].wsop = 1; f[0].fin = 1; f[0].len = (int)strlen(hello);
    memcpy(f[0].payload, hello, (size_t)f[0].len);
    f[1].wsop = 1; f[1].fin = 1; f[1].len = (int)strlen(ready);
    memcpy(f[1].payload, ready, (size_t)f[1].len);
    replay_begin(f, 2);
    discord_t d;
    memset(&d, 0, sizeof d);
    assert(discord_connect(&d, "REPLAY-TOKEN") == 0);
    replay_end();
    assert(discord_can_resume(&d));
    assert(!strcmp(d.session_id, "0123456789abcdef0123456789abcdef"));
    assert(!strcmp(d.resume_url, "wss://gateway-us-east1-b.discord.gg"));
    assert(d.seq == 7);

    /* Successful resume: op 6 carries token, session and last sequence. */
    replay_frame_t r[2];
    memset(r, 0, sizeof r);
    r[0].wsop = 1; r[0].fin = 1; r[0].len = (int)strlen(hello);
    memcpy(r[0].payload, hello, (size_t)r[0].len);
    const char *resumed = "{\"op\":0,\"t\":\"RESUMED\",\"s\":8,\"d\":{}}";
    r[1].wsop = 1; r[1].fin = 1; r[1].len = (int)strlen(resumed);
    memcpy(r[1].payload, resumed, (size_t)r[1].len);
    replay_begin(r, 2);
    assert(discord_resume(&d, "REPLAY-TOKEN") == 0);
    replay_end();
    assert(s_sent_n == 1 && s_sent_op[0] == 6);
    assert(!strcmp(s_last_host, "gateway-us-east1-b.discord.gg"));
    jl_val_t *rq = jl_parse(s_sent_json[0], strlen(s_sent_json[0]));
    assert(rq && rq->type == JL_OBJECT);
    const jl_val_t *rd = jl_obj_get(rq, "d");
    const jl_val_t *tk = rd ? jl_obj_get(rd, "token") : NULL;
    const jl_val_t *si = rd ? jl_obj_get(rd, "session_id") : NULL;
    const jl_val_t *sq = rd ? jl_obj_get(rd, "seq") : NULL;
    assert(tk && tk->type == JL_STRING && !strcmp(tk->str, "REPLAY-TOKEN"));
    assert(si && si->type == JL_STRING &&
           !strcmp(si->str, "0123456789abcdef0123456789abcdef"));
    assert(sq && sq->type == JL_NUMBER && (int)sq->num == 7);
    jl_free(rq);

    /* Invalid session: gateway says the session is dead; the next connect
     * must be a fresh IDENTIFY, not another doomed RESUME. */
    replay_frame_t bad[2];
    memset(bad, 0, sizeof bad);
    bad[0].wsop = 1; bad[0].fin = 1; bad[0].len = (int)strlen(hello);
    memcpy(bad[0].payload, hello, (size_t)bad[0].len);
    const char *inv = "{\"op\":9,\"d\":false}";
    bad[1].wsop = 1; bad[1].fin = 1; bad[1].len = (int)strlen(inv);
    memcpy(bad[1].payload, inv, (size_t)bad[1].len);
    replay_begin(bad, 2);
    assert(discord_resume(&d, "REPLAY-TOKEN") == -3);
    replay_end();
    assert(!discord_can_resume(&d));
}

/* Presence updates are all-or-nothing. The frame must be op 3 with a named
 * activity (an anonymous activity renders as nothing), and a failed write
 * must invalidate the WebSocket: tls_write may already have put part of the
 * frame on the wire, so reusing that stream would corrupt every later
 * frame and the presence would silently stay missing. */
static void test_presence_send(void){
    static const char *const frames[] = {
        "{\"op\":10,\"d\":{\"heartbeat_interval\":45000}}",
        "{\"op\":0,\"t\":\"READY\",\"s\":1,\"d\":{}}"
    };
    replay_frame_t f[2];
    memset(f, 0, sizeof f);
    for(int i = 0; i < 2; i++){
        f[i].wsop = 1; f[i].fin = 1;
        f[i].len = (int)strlen(frames[i]);
        memcpy(f[i].payload, frames[i], (size_t)f[i].len);
    }
    cfg_t saved = g_cfg;
    cfg_defaults(&g_cfg);
    replay_begin(f, 2);
    discord_t d;
    memset(&d, 0, sizeof d);
    assert(discord_connect(&d, "REPLAY-TOKEN") == 0);

    assert(discord_set_presence(&d, "firmware 9.00", "Terraria", NULL, 0) == 0);
    assert(d.connected == 1);
    assert(s_sent_n >= 2 && s_sent_op[s_sent_n-1] == 3);
    jl_val_t *p = jl_parse(s_sent_json[s_sent_n-1],
                           strlen(s_sent_json[s_sent_n-1]));
    assert(p && p->type == JL_OBJECT);
    const jl_val_t *pd = jl_obj_get(p, "d");
    const jl_val_t *acts = pd ? jl_obj_get(pd, "activities") : NULL;
    const jl_val_t *a0 = (acts && acts->type == JL_ARRAY)
                         ? jl_arr_at(acts, 0) : NULL;
    const jl_val_t *nm = a0 ? jl_obj_get(a0, "name") : NULL;
    assert(acts && acts->type == JL_ARRAY && acts->count == 1);
    assert(nm && nm->type == JL_STRING && !strcmp(nm->str, "Terraria"));
    jl_free(p);

    /* Clear is the same envelope with an empty activities array. */
    assert(discord_clear_presence(&d) == 0);
    assert(s_sent_n >= 3 && s_sent_op[s_sent_n-1] == 3);
    p = jl_parse(s_sent_json[s_sent_n-1], strlen(s_sent_json[s_sent_n-1]));
    assert(p && p->type == JL_OBJECT);
    pd = jl_obj_get(p, "d");
    acts = pd ? jl_obj_get(pd, "activities") : NULL;
    assert(acts && acts->type == JL_ARRAY && acts->count == 0);
    jl_free(p);

    s_fail_send = 1;
    assert(discord_set_presence(&d, "firmware 9.00", "Terraria", NULL, 0) == -1);
    assert(d.connected == 0);
    s_fail_send = 0;
    assert(discord_clear_presence(&d) == -1); /* no live session left */
    replay_end();
    g_cfg = saved;
}

static void test_json_oom_safe(void) {
    jl_val_t *t = jl_parse("true", 4); assert(t != NULL); jl_free(t);
    jl_val_t *f = jl_parse("false", 5); assert(f != NULL); jl_free(f);
    jl_val_t *z = jl_parse("null", 4); assert(z != NULL); jl_free(z);
    jl_val_t *n = jl_parse("123.5", 5);
    assert(n && n->type == JL_NUMBER);
    jl_free(n);
    /* incomplete pair must fail cleanly, no leak/crash */
    assert(jl_parse("{\"a\":", 5) == NULL);
    assert(jl_parse("{\"a\":1", 6) == NULL);
    /* oversize input refused before any allocation */
    assert(jl_parse("[]", 5u*1024u*1024u) == NULL);
}
static void test_json_hostile(void) {
    /* 200-deep nesting must be rejected, not stack-smash */
    char deep[420];
    memset(deep, '[', 200);
    memset(deep+200, ']', 200);
    deep[400] = 0;
    assert(jl_parse(deep, 400) == NULL);
    /* 60-deep is fine */
    char okd[130];
    memset(okd, '[', 60);
    memset(okd+60, ']', 60);
    okd[120] = 0;
    jl_val_t *r = jl_parse(okd, 120);
    assert(r && r->type == JL_ARRAY);
    jl_free(r);
    /* number running exactly to buffer end (no NUL past it) */
    char num[16];
    memcpy(num, "{\"s\":41250}", 11);
    jl_val_t *r2 = jl_parse(num, 11);
    assert(r2);
    assert(jl_obj_get(r2, "s")->num == 41250);
    jl_free(r2);
    /* absurd number token fails cleanly */
    char big[80];
    memset(big, '9', 70);
    big[70] = 0;
    assert(jl_parse(big, 70) == NULL);
}
static void test_sfo(void) {
    /* minimal synthetic param.sfo: header + 1 entry (TITLE="Terraria") */
    unsigned char sfo[128];
    char out[64];
    memset(sfo, 0, sizeof sfo);
    sfo[0]=0x00; sfo[1]='P'; sfo[2]='S'; sfo[3]='F';
    sfo[4]=0x01; sfo[5]=0x02;
    sfo[8]=36; sfo[12]=48; sfo[16]=1;   /* keys@36 data@48 */
    sfo[20]=0; sfo[21]=0; sfo[22]=0x04; sfo[23]=0; /* key_off=0 fmt=0x0004 */
    sfo[24]=9; sfo[28]=16; sfo[32]=0;   /* len=9 max=16 data_off=0 */
    memcpy(sfo+36, "TITLE", 6);
    memcpy(sfo+48, "Terraria", 9);
    assert(sfo_title(sfo, 64, out, sizeof out) == 0);
    assert(strcmp(out, "Terraria") == 0);
    /* malformed: bad magic, truncated, insane count, OOB offsets */
    unsigned char bad[64];
    memset(bad, 0, sizeof bad);
    assert(sfo_title(bad, sizeof bad, out, sizeof out) != 0);
    assert(sfo_title(sfo, 10, out, sizeof out) != 0);
    sfo[16]=200; /* count overflow */
    assert(sfo_title(sfo, 64, out, sizeof out) != 0);
    sfo[16]=1; sfo[8]=200; /* key_off OOB */
    assert(sfo_title(sfo, 64, out, sizeof out) != 0);
    sfo[8]=36;
    /* tiny output buffer still safe */
    assert(sfo_title(sfo, 64, out, 4) == 0);
    assert(out[3] == 0);
    /* unterminated key region: must fail, not read OOB */
    memset(sfo, 0x41, sizeof sfo);
    sfo[0]=0x00; sfo[1]='P'; sfo[2]='S'; sfo[3]='F';
    sfo[8]=36; sfo[12]=48; sfo[16]=1;
    sfo[20]=0; sfo[22]=0x04; sfo[24]=9; sfo[28]=16; sfo[32]=0;
    assert(sfo_title(sfo, 64, out, sizeof out) != 0);
}
static void test_tmdb(void) {
    unsigned char dig[20];
    char hex[41];
    int i;
    /* SHA1("abc") = a9993e364706816aba3e25717850c26c9cd0d4d */
    tmdb_sha1((const unsigned char *)"abc", 3, dig);
    for(i=0;i<20;i++) snprintf(hex+2*i, 3, "%02x", dig[i]);
    assert(strcmp(hex, "a9993e364706816aba3e25717850c26c9cd0d89d") == 0);
    /* HMAC-SHA1 RFC 2202 case 1 */
    {
        unsigned char key[20];
        memset(key, 0x0b, 20);
        tmdb_hmac_sha1(key, 20, (const unsigned char *)"Hi There", 8, dig);
        for(i=0;i<20;i++) snprintf(hex+2*i, 3, "%02x", dig[i]);
        assert(strcmp(hex, "b617318655057264e28bc0b6fb378c8ef146be00") == 0);
    }
    /* URL path must match the hash Sony's live service accepts */
    {
        char path[128];
        assert(tmdb_path("CUSA00740", path, sizeof path) == 0);
        assert(strcmp(path, "/tmdb2/CUSA00740_00_95C83DE844D155477CB77C577A24D735F2E9AC08/CUSA00740_00.json") == 0);
        assert(tmdb_path("junk!", path, sizeof path) != 0);
        assert(tmdb_path("CUSA00740", path, 10) != 0);
    }
    /* response parse: name + icon URL.
     * Sony's icon CDN still answers in http on some titles; Discord silently
     * drops an activity whose artwork is a non-https external URL, so the
     * scheme is upgraded rather than the asset being discarded. */
    {
        const char body[] = "{\"names\":[{\"name\":\"Terraria\"}],\"icons\":[{\"icon\":\"http://x/y/icon0.png\",\"type\":\"512x512\"}]}";
        char name[64], icon[128];
        assert(tmdb_parse(body, sizeof(body)-1, name, sizeof name, icon, sizeof icon) == 0);
        assert(strcmp(name, "Terraria") == 0);
        assert(strcmp(icon, "https://x/y/icon0.png") == 0);
        /* already https: untouched */
        const char body3[] = "{\"names\":[{\"name\":\"T\"}],\"icons\":[{\"icon\":\"https://x/y/i.png\"}]}";
        assert(tmdb_parse(body3, sizeof(body3)-1, name, sizeof name, icon, sizeof icon) == 0);
        assert(strcmp(icon, "https://x/y/i.png") == 0);
        /* gs2 CDN form from the reference client */
        const char body4[] = "{\"names\":[{\"name\":\"T\"}],\"icons\":[{\"icon\":\"http://gs2-sec.ww.prod.dl.playstation.net/a/b.png\"}]}";
        assert(tmdb_parse(body4, sizeof(body4)-1, name, sizeof name, icon, sizeof icon) == 0);
        assert(strcmp(icon, "https://gs2-sec.ww.prod.dl.playstation.net/a/b.png") == 0);
        /* non-URL icon rejected, name still wins */
        const char body2[] = "{\"names\":[{\"name\":\"X\"}],\"icons\":[{\"icon\":\"not a url\"}]}";
        assert(tmdb_parse(body2, sizeof(body2)-1, name, sizeof name, icon, sizeof icon) == 0);
        assert(icon[0] == 0);
        /* an http URL too long to upgrade is dropped, not sent in a form
         * Discord ignores */
        {
            char body5[640], path[300];
            memset(path, 'a', sizeof path - 1); path[sizeof path - 1] = 0;
            snprintf(body5, sizeof body5,
                     "{\"names\":[{\"name\":\"Long\"}],\"icons\":[{\"icon\":\"http://x/%s.png\"}]}",
                     path);
            assert(tmdb_parse(body5, strlen(body5), name, sizeof name,
                              icon, sizeof icon) == 0);
            assert(!strcmp(name, "Long"));
            assert(icon[0] == 0);
        }
        /* no names -> fail */
        assert(tmdb_parse("{\"icons\":[]}", 12, name, sizeof name, icon, sizeof icon) != 0);
    }
}
static void test_updater(void) {
    assert(updater_cmp("0.4.0", "0.4.0") == 0);
    assert(updater_cmp("v0.4.1", "0.4.0") > 0);
    assert(updater_cmp("0.4.0", "v0.4.1") < 0);
    assert(updater_cmp("0.10.0", "0.9.9") > 0);
    assert(updater_cmp("1.0", "1.0.0") == 0);
    assert(updater_cmp(NULL, "0.1") < 0);
    unsigned char elf[64];
    memset(elf, 0, sizeof elf);
    assert(updater_elf_ok(elf, sizeof elf) == 0);
    assert(updater_elf_ok(NULL, 100) == 0);
    assert(updater_elf_ok(elf, 10) == 0);
    elf[0]=0x7f; elf[1]='E'; elf[2]='L'; elf[3]='F';
    elf[4]=2; elf[5]=1; elf[18]=62; elf[19]=0;
    assert(updater_elf_ok(elf, sizeof elf) == 1);
    elf[18]=99;
    assert(updater_elf_ok(elf, sizeof elf) == 0);
}
/* --- gateway regression replay (T0.12) -------------------------------
 * Recorded gateway exchanges as JSONL, one scenario per line:
 *   {"name":"...","connect":[{"json":"..."}],"connect_rc":0,
 *    "tick":[{"json":"..."}],"tick_rc":-1,"pre":{"sent_hb":1},
 *    "out":[2]}
 * Frames are sanitized: no token, no account data, fixed timings. The
 * driver feeds them to the real discord_connect/discord_tick through the
 * ws stubs, so a protocol regression fails `make test`. */
static int replay_load_frames(const jl_val_t *arr, replay_frame_t *out, int cap){
    int n = 0;
    if(!arr) return 0;
    for(size_t i = 0; ; i++){
        const jl_val_t *e = jl_arr_at(arr, i);
        if(!e) break;
        if(e->type != JL_OBJECT || n >= cap) return -1;
        replay_frame_t *f = &out[n];
        memset(f, 0, sizeof *f);
        f->wsop = 1; f->fin = 1;
        const jl_val_t *op = jl_obj_get(e, "op");
        const jl_val_t *fin = jl_obj_get(e, "fin");
        const jl_val_t *js = jl_obj_get(e, "json");
        const jl_val_t *code = jl_obj_get(e, "code");
        if(op && op->type == JL_NUMBER) f->wsop = (int)op->num;
        if(fin && fin->type == JL_NUMBER) f->fin = (int)fin->num;
        if(js && js->type == JL_STRING && js->str){
            size_t l = strlen(js->str);
            if(l >= sizeof f->payload) return -1;
            memcpy(f->payload, js->str, l);
            f->len = (int)l;
        } else if(code && code->type == JL_NUMBER){
            f->wsop = 8;
            f->payload[0] = (char)((int)code->num >> 8);
            f->payload[1] = (char)((int)code->num & 0xff);
            f->len = 2;
        } else return -1;
        n++;
    }
    return n;
}

static int replay_check_out(const jl_val_t *exp, const char *name, const char *phase){
    int n = 0;
    if(!exp) return 0;
    for(size_t i = 0; ; i++){
        const jl_val_t *v = jl_arr_at(exp, i);
        if(!v) break;
        if(v->type != JL_NUMBER || n >= s_sent_n || s_sent_op[n] != (int)v->num){
            printf("replay: %s %s out[%d] expected %d got %d\n", name, phase, n,
                   v->type == JL_NUMBER ? (int)v->num : -1,
                   n < s_sent_n ? s_sent_op[n] : -1);
            return -1;
        }
        n++;
    }
    if(n != s_sent_n){
        printf("replay: %s %s sent %d frames, expected %d\n", name, phase, s_sent_n, n);
        return -1;
    }
    return 0;
}

static void replay_apply_pre(discord_t *d, const jl_val_t *pre){
    if(!pre) return;
    int64_t now = orbis_mono_s();
    const jl_val_t *v;
    if((v = jl_obj_get(pre, "sent_hb")) && v->type == JL_NUMBER)
        d->sent_hb = (int)v->num;
    if((v = jl_obj_get(pre, "ack_age_s")) && v->type == JL_NUMBER)
        d->last_ack = now - (int64_t)v->num;
    if((v = jl_obj_get(pre, "hb_age_s")) && v->type == JL_NUMBER)
        d->last_heartbeat = now - (int64_t)v->num;
}

static int replay_one(const jl_val_t *sc, const char *file){
    const jl_val_t *nm = jl_obj_get(sc, "name");
    const char *name = (nm && nm->type == JL_STRING) ? nm->str : file;
    replay_frame_t frames[64];
    int n = replay_load_frames(jl_obj_get(sc, "connect"), frames, 64);
    if(n < 0){ printf("replay: %s bad connect frames\n", name); return -1; }

    /* Silence expected log noise from the negative scenarios; the driver is
     * asserting return codes and frame counts, not log text. */
    replay_begin(frames, n);
    discord_t d;
    memset(&d, 0, sizeof d);
    int rc = discord_connect(&d, "REPLAY-TOKEN");
    int want = 0;
    const jl_val_t *rcv = jl_obj_get(sc, "connect_rc");
    if(rcv && rcv->type == JL_NUMBER) want = (int)rcv->num;
    int fail = 0;
    if(rc != want){ printf("replay: %s connect rc %d want %d\n", name, rc, want); fail = 1; }
    {
        const jl_val_t *hbm = jl_obj_get(sc, "hb_ms");
        if(hbm && hbm->type == JL_NUMBER &&
           d.hb_interval_ms != (int64_t)hbm->num){
            printf("replay: %s hb_ms %lld want %lld\n", name,
                   (long long)d.hb_interval_ms, (long long)hbm->num);
            fail = 1;
        }
    }
    if(replay_check_out(jl_obj_get(sc, "out"), name, "connect") != 0) fail = 1;

    const jl_val_t *tick = jl_obj_get(sc, "tick");
    if(tick && tick->type == JL_ARRAY){
        int tn = replay_load_frames(tick, frames, 64);
        if(tn < 0){ printf("replay: %s bad tick frames\n", name); return -1; }
        replay_apply_pre(&d, jl_obj_get(sc, "pre"));
        replay_begin(frames, tn);
        rc = discord_tick(&d);
        want = 0;
        rcv = jl_obj_get(sc, "tick_rc");
        if(rcv && rcv->type == JL_NUMBER) want = (int)rcv->num;
        if(rc != want){ printf("replay: %s tick rc %d want %d\n", name, rc, want); fail = 1; }
        {
            const jl_val_t *sq = jl_obj_get(sc, "seq");
            if(sq && sq->type == JL_NUMBER && d.seq != (int)sq->num){
                printf("replay: %s seq %d want %d\n", name, d.seq, (int)sq->num);
                fail = 1;
            }
        }
        if(replay_check_out(jl_obj_get(sc, "tick_out"), name, "tick") != 0) fail = 1;
        const jl_val_t *pong = jl_obj_get(sc, "pong");
        if(pong && pong->type == JL_NUMBER && s_pong_n != (int)pong->num){
            printf("replay: %s pong %d want %d\n", name, s_pong_n, (int)pong->num);
            fail = 1;
        }
    }
    replay_end();
    return fail ? -1 : 0;
}

static int test_replay(void){
    DIR *dp = opendir("fixtures/gateway");
    if(!dp){ printf("replay: fixtures/gateway missing\n"); return -1; }
    int files = 0, scenarios = 0, bad = 0;
    struct dirent *de;
    while((de = readdir(dp)) != NULL){
        size_t l = strlen(de->d_name);
        if(l < 6 || strcmp(de->d_name + l - 6, ".jsonl") != 0) continue;
        char path[512];
        snprintf(path, sizeof path, "fixtures/gateway/%s", de->d_name);
        FILE *f = fopen(path, "rb");
        if(!f){ printf("replay: cannot open %s\n", path); bad++; continue; }
        files++;
        char *line = NULL;
        size_t cap = 0;
        ssize_t got;
        while((got = getline(&line, &cap, f)) >= 0){
            while(got > 0 && (line[got-1] == '\n' || line[got-1] == '\r')) line[--got] = 0;
            if(got == 0 || line[0] == '#') continue;
            jl_val_t *sc = jl_parse(line, (size_t)got);
            if(!sc || sc->type != JL_OBJECT){
                printf("replay: %s malformed line\n", de->d_name);
                if(sc) jl_free(sc);
                bad++;
                continue;
            }
            scenarios++;
            if(replay_one(sc, de->d_name) != 0) bad++;
            jl_free(sc);
        }
        free(line);
        fclose(f);
    }
    closedir(dp);
    printf("replay: %d fixtures, %d scenarios, %d failed\n", files, scenarios, bad);
    return bad ? -1 : 0;
}

static void test_diag(void){
    char dir[64];
    assert(make_tmpdir(dir, sizeof dir) == 0);
    char logp[256], out[256];
    snprintf(logp, sizeof logp, "%s/log.txt", dir);
    snprintf(out, sizeof out, "%s/diagnostic.txt", dir);
    const char *secret = "SECRET.TOKEN.VALUE";
    FILE *f = fopen(logp, "wb");
    assert(f);
    fprintf(f, "boot ok\ntoken %s\n", secret);
    fclose(f);

    cfg_t c;
    cfg_defaults(&c);
    snprintf(c.token, sizeof c.token, "%s", secret);
    snprintf(c.application_id, sizeof c.application_id, "123456789");
    diag_info_t info = { "1.1.0", "9.00", 3600, 12, "4004,1006" };
    char *b = diag_build(&info, &c, "boot ok token SECRET.TOKEN.VALUE", 30);
    assert(b);
    assert(strstr(b, secret) == NULL);
    assert(strstr(b, "[redacted]") != NULL);
    assert(strstr(b, "1.1.0") != NULL);
    assert(strstr(b, "4004,1006") != NULL);
    assert(strstr(b, "123456789") != NULL);
    assert(strstr(b, "token: [redacted] len=") != NULL);
    free(b);

    assert(diag_write_file(out, &info, &c, logp) == 0);
    f = fopen(out, "rb");
    assert(f);
    char buf[4096];
    size_t n = fread(buf, 1, sizeof buf - 1, f);
    fclose(f);
    buf[n] = 0;
    assert(strstr(buf, secret) == NULL);
    assert(strstr(buf, "log tail:") != NULL);
    struct stat st;
    assert(stat(out, &st) == 0);
    assert((st.st_mode & 0777) == 0600);
}

static void test_updater_http(void) {
    int status = 0;
    size_t len = 0;
    char loc[64];
    const char ok[] = "HTTP/1.1 200 OK\r\nContent-Length: 5\r\n\r\nhello";
    char *b = upd_parse_response(ok, sizeof ok - 1, 64, &status, &len, loc, sizeof loc);
    assert(b && status == 200 && len == 5 && !memcmp(b, "hello", 5) && loc[0] == 0);
    free(b);

    /* A short read against a declared Content-Length is a failed download,
     * not a valid body. This is the truncation check. */
    const char cut[] = "HTTP/1.1 200 OK\r\nContent-Length: 10\r\n\r\nhello";
    b = upd_parse_response(cut, sizeof cut - 1, 64, &status, &len, loc, sizeof loc);
    assert(b == NULL && status == 200 && len == 0);

    /* Chunked decoding still works after the truncation gate. */
    const char chunked[] = "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n"
                           "5\r\nhello\r\n0\r\n\r\n";
    b = upd_parse_response(chunked, sizeof chunked - 1, 64, &status, &len, loc, sizeof loc);
    assert(b && status == 200 && len == 5 && !memcmp(b, "hello", 5));
    free(b);

    /* Redirect target is captured for the updater's allowlist check. */
    const char redir[] = "HTTP/1.1 302 Found\r\nLocation: https://github.com/x\r\nContent-Length: 0\r\n\r\n";
    b = upd_parse_response(redir, sizeof redir - 1, 64, &status, &len, loc, sizeof loc);
    assert(b && status == 302 && len == 0 && !strcmp(loc, "https://github.com/x"));
    free(b);

    /* No header terminator: unusable, never a guessed body. */
    const char junk[] = "HTTP/1.1 200 OK\r\nContent-Length: 1\r\n";
    assert(upd_parse_response(junk, sizeof junk - 1, 64, &status, &len, loc, sizeof loc) == NULL);
    /* Body larger than the caller's cap is refused, not silently truncated. */
    const char big[] = "HTTP/1.1 200 OK\r\nContent-Length: 5\r\n\r\nhello";
    assert(upd_parse_response(big, sizeof big - 1, 4, &status, &len, loc, sizeof loc) == NULL);
    /* Numeric header/status fields are parsed with strtol, so an
     * unrepresentable value neither overflows nor reads as success. */
    const char hugecl[] = "HTTP/1.1 200 OK\r\nContent-Length: 99999999999999999999999999999999\r\n\r\nhello";
    assert(upd_parse_response(hugecl, sizeof hugecl - 1, 64, &status, &len,
                              loc, sizeof loc) == NULL);
    const char hugest[] = "HTTP/1.1 99999999999999999999999999999999 OK\r\nContent-Length: 0\r\n\r\n";
    b = upd_parse_response(hugest, sizeof hugest - 1, 64, &status, &len,
                           loc, sizeof loc);
    assert(b && status != 200 && len == 0);
    free(b);
}

static void test_art_parse(void) {
    /* URLs are interpolated into a JSON POST body and a request line. Quote,
     * backslash, control, and non-http values must be refused before they can
     * break the request or smuggle in another JSON field. */
    assert(art_url_ok("https://example.com/a.png") == 1);
    assert(art_url_ok("http://example.com/a.png?x=1&y=2") == 1);
    assert(art_url_ok("https://example.com/a\"b.png") == 0);
    assert(art_url_ok("https://example.com/a\\b.png") == 0);
    assert(art_url_ok("https://example.com/a\nb.png") == 0);
    assert(art_url_ok("https://example.com/a\tb.png") == 0);
    assert(art_url_ok("ftp://example.com/a.png") == 0);
    assert(art_url_ok("cusa12345") == 0);
    assert(art_url_ok("") == 0);
    assert(art_url_ok(NULL) == 0);
    {
        char longurl[600];
        memset(longurl, 'a', sizeof longurl - 1);
        memcpy(longurl, "https://", 8);
        longurl[sizeof longurl - 1] = 0;
        assert(art_url_ok(longurl) == 0);
    }
    /* Real external-assets response shape (verified against live API). */
    const char *body = "[{\"url\":\"https://example.com/a.png\","
        "\"external_asset_path\":\"external/ABC/https/example.com/a.png\"},"
        "{\"url\":\"https://example.com/b.png\","
        "\"external_asset_path\":\"external/DEF/https/example.com/b.png\"}]";
    char out[128] = {0};
    assert(art_parse_mp(body, strlen(body),
                        "https://example.com/b.png", out, sizeof out) == 1);
    assert(strcmp(out, "mp:external/DEF/https/example.com/b.png") == 0);
    assert(art_resolve_mp_cached("mp:external/DEF/x", out, sizeof out) == 1);
    assert(strcmp(out, "mp:external/DEF/x") == 0);
    assert(art_resolve_mp_cached("https://example.com/zzz.png", out, sizeof out) == 0);
    art_prefetch("mp:external/DEF/x");   /* already resolved: no transport */
    /* The per-boot cache is multi-entry. Boot prefetches three static images
     * (large, small, browser); with a single slot the last resolve evicted
     * the rest, so the idle tile and the small badge silently lost their
     * art and the builder is cache-only now. */
    art_cache_clear();
    art_cache_put("https://example.com/large.png", "mp:external/AAA/large.png");
    art_cache_put("https://example.com/small.png", "mp:external/BBB/small.png");
    art_cache_put("https://example.com/browser.png", "mp:external/CCC/browser.png");
    assert(art_resolve_mp_cached("https://example.com/large.png", out, sizeof out) == 1);
    assert(strcmp(out, "mp:external/AAA/large.png") == 0);
    assert(art_resolve_mp_cached("https://example.com/small.png", out, sizeof out) == 1);
    assert(strcmp(out, "mp:external/BBB/small.png") == 0);
    assert(art_resolve_mp_cached("https://example.com/browser.png", out, sizeof out) == 1);
    /* re-resolving one URL updates in place instead of duplicating it */
    art_cache_put("https://example.com/small.png", "mp:external/BBB/small2.png");
    assert(art_resolve_mp_cached("https://example.com/small.png", out, sizeof out) == 1);
    assert(strcmp(out, "mp:external/BBB/small2.png") == 0);
    /* only resolved mp: values are stored: a raw URL must never be echoed */
    art_cache_put("https://example.com/bad.png", "https://example.com/bad.png");
    assert(art_resolve_mp_cached("https://example.com/bad.png", out, sizeof out) == 0);
    art_cache_clear();
    assert(art_resolve_mp_cached("https://example.com/large.png", out, sizeof out) == 0);
    assert(art_parse_mp(body, strlen(body),
                        "https://example.com/zzz.png", out, sizeof out) == 0);
    assert(art_parse_mp("[]", 2, "x", out, sizeof out) == 0);
    assert(art_parse_mp(NULL, 0, "x", out, sizeof out) == 0);
    assert(art_resolve_mp("1", "t", "https://example.com/a.png", out, sizeof out) == 0);
}

static void test_health_safe_mode(void) {
    /* Unclean-boot marker semantics: normal reboots never count. */
    char dir[64];
    assert(make_tmpdir(dir, sizeof dir) == 0);
    health_set_base(dir);
    health_mark_clean();
    /* clean boot x3: counter stays 0, never safe mode */
    assert(health_boot_note_crash() == 0);
    health_mark_healthy();
    assert(health_boot_note_crash() == 0);
    health_mark_healthy();
    assert(health_boot_note_crash() == 0);
    /* now crash repeatedly WITHOUT going healthy: 1, 2, then safe */
    assert(health_boot_note_crash() == 0); /* count=1 */
    assert(health_boot_note_crash() == 0); /* count=2 */
    assert(health_boot_note_crash() == 1); /* count=3 -> safe mode */
    /* recovery clears */
    health_mark_healthy();
    assert(health_boot_note_crash() == 0);
    health_mark_healthy();
}

static void test_health_stage_activate(void) {
    /* Atomic staging: bad .new never touches live; rollback restores. */
    char dir[64];
    assert(make_tmpdir(dir, sizeof dir) == 0);
    health_set_base(dir);
    char live[256], tmp[256], bak[256];
    snprintf(live, sizeof live, "%s/live.bin", dir);
    snprintf(tmp, sizeof tmp, "%s/live.bin.new", dir);
    snprintf(bak, sizeof bak, "%s/live.bin.bak", dir);
    /* live = valid ELF stand-in (>=64B, ELF magic via updater_image_ok?
     * use real check: write 64 zero bytes won't pass; stage path only
     * needs .new validation, so craft minimal ELF header). */
    unsigned char elf[128];
    memset(elf, 0, sizeof elf);
    elf[0] = 0x7f; elf[1] = 'E'; elf[2] = 'L'; elf[3] = 'F';
    elf[4] = 2; elf[5] = 1; elf[18] = 62;
    FILE *f = fopen(live, "wb");
    assert(f); assert(fwrite(elf, 1, sizeof elf, f) == sizeof elf); fclose(f);
    /* corrupt .new is refused, live untouched */
    f = fopen(tmp, "wb");
    assert(f); assert(fwrite("garbage-not-elf-at-all......................"
                            "..............................", 1, 64, f) == 64);
    fclose(f);
    assert(health_stage_activate(live) != 0);
    f = fopen(live, "rb");
    assert(f);
    unsigned char chk[8];
    assert(fread(chk, 1, 4, f) == 4);
    fclose(f);
    assert(chk[0] == 0x7f && chk[1] == 'E');
    /* valid .new activates, backup created, rollback restores */
    f = fopen(tmp, "wb");
    assert(f);
    elf[7] = 0x42;
    assert(fwrite(elf, 1, sizeof elf, f) == sizeof elf);
    fclose(f);
    assert(health_stage_activate(live) == 0);
    f = fopen(bak, "rb");
    assert(f); fclose(f);
    assert(health_verify_or_rollback(live) == 0);
    /* corrupt live + backup present -> rollback */
    f = fopen(live, "wb");
    assert(f); assert(fwrite("XX", 1, 2, f) == 2); fclose(f);
    assert(health_verify_or_rollback(live) == 1);
    f = fopen(live, "rb");
    assert(f);
    assert(fread(chk, 1, 4, f) == 4);
    fclose(f);
    assert(chk[0] == 0x7f);

    /* A corrupt backup must never replace the live file. The old code
     * renamed .bak over .bin before checking it, so a bad backup turned
     * one broken binary into two. */
    f = fopen(bak, "wb");
    assert(f); assert(fwrite("BAD-BACKUP", 1, 10, f) == 10); fclose(f);
    f = fopen(live, "wb");
    assert(f); assert(fwrite("BAD-LIVE", 1, 8, f) == 8); fclose(f);
    assert(health_verify_or_rollback(live) == -1);
    f = fopen(live, "rb");
    assert(f);
    memset(chk, 0, sizeof chk);
    assert(fread(chk, 1, 8, f) == 8);
    fclose(f);
    assert(!memcmp(chk, "BAD-LIVE", 8));
}

static void test_manifest(void) {
    const char *json = "{\"version\":\"1.0.0\",\"channel\":\"stable\","
        "\"min_version\":\"0.9.0\",\"platform\":\"ps4-goldhen\","
        "\"assets\":[{\"name\":\"orbisrpc.bin\","
        "\"sha256\":\"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef\"}]}";
    manifest_t m;
    assert(manifest_parse(json, strlen(json), &m) == 0);
    assert(strcmp(m.version, "1.0.0") == 0);
    assert(strcmp(m.channel, "stable") == 0);
    char hex[65] = {0};
    assert(manifest_find(&m, "orbisrpc.bin", hex) == 0);
    assert(!strcmp(hex, "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"));
    assert(manifest_find(&m, "nope.bin", hex) != 0);
    assert(manifest_gate(&m) == 1);
    assert(manifest_min_ok(&m, "1.0.0") == 1);
    assert(manifest_min_ok(&m, "0.9.0") == 1);
    assert(manifest_min_ok(&m, "0.8.0") == 0);
    assert(manifest_min_ok(&m, "") == 0);
    assert(manifest_is_newer(&m, "0.9.0") == 1);
    assert(manifest_is_newer(&m, "1.0.0") == 0);
    /* wrong channel / platform refused */
    const char *bad = "{\"version\":\"9.9.9\",\"channel\":\"beta\","
        "\"platform\":\"ps5\",\"assets\":[{\"name\":\"x.bin\","
        "\"sha256\":\"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef\"}]}";
    manifest_t m2;
    assert(manifest_parse(bad, strlen(bad), &m2) == 0);
    assert(manifest_gate(&m2) == 0);
    /* malformed rejected */
    assert(manifest_parse("{}", 2, &m) != 0);
    assert(manifest_parse("not json", 8, &m) != 0);
    /* hash check: real sha256 of "abc" must match */
    const char *jh = "{\"version\":\"1\",\"assets\":[{\"name\":\"a\","
        "\"sha256\":\"ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad\"}]}";
    manifest_t m3;
    assert(manifest_parse(jh, strlen(jh), &m3) == 0);
    assert(manifest_min_ok(&m3, "0.0.1") == 1);   /* no minimum declared */
    assert(manifest_check(&m3, "a", (const unsigned char *)"abc", 3) == 0);
    assert(manifest_check(&m3, "a", (const unsigned char *)"abd", 3) != 0);
    /* Release alias rule: a payload published as orbisrpc_sdk.elf has the
     * same bytes as the manifest's orbisrpc.bin entry. The alias must verify
     * against that canonical entry, and a missing fallback must still fail. */
    assert(manifest_check_alias(&m3, "orbisrpc_sdk.elf", "a",
                                (const unsigned char *)"abc", 3) == 0);
    assert(manifest_check_alias(&m3, "orbisrpc_sdk.elf", "a",
                                (const unsigned char *)"abd", 3) != 0);
    assert(manifest_check_alias(&m3, "orbisrpc_sdk.elf", NULL,
                                (const unsigned char *)"abc", 3) != 0);
}

static void test_manifest_sig(void) {
    /* Full round trip with a fresh keypair: sign via mbedTLS, verify via
     * our public-API-only manifest_verify_sig. Tampered bytes must fail.
     * Uses only public 3.x APIs (raw group + MPIs, no context internals). */
    static const unsigned char msg[] = "{\"version\":\"9.9.9\"}";
    mbedtls_entropy_context ent;
    mbedtls_entropy_init(&ent);
    mbedtls_ctr_drbg_context rng;
    mbedtls_ctr_drbg_init(&rng);
    assert(mbedtls_ctr_drbg_seed(&rng, mbedtls_entropy_func, &ent,
                                  (const unsigned char *)"test", 4) == 0);
    mbedtls_ecp_group grp;
    mbedtls_ecp_group_init(&grp);
    assert(mbedtls_ecp_group_load(&grp, MBEDTLS_ECP_DP_SECP256R1) == 0);
    mbedtls_mpi d, r, s;
    mbedtls_mpi_init(&d); mbedtls_mpi_init(&r); mbedtls_mpi_init(&s);
    mbedtls_ecp_point Q;
    mbedtls_ecp_point_init(&Q);
    assert(mbedtls_ecp_gen_keypair(&grp, &d, &Q,
                                    mbedtls_ctr_drbg_random, &rng) == 0);
    unsigned char hash[32], sig[64], rawpub[64];
    {
        mbedtls_sha256_context sc;
        mbedtls_sha256_init(&sc);
        assert(mbedtls_sha256_starts(&sc, 0) == 0);
        assert(mbedtls_sha256_update(&sc, msg, sizeof msg - 1) == 0);
        assert(mbedtls_sha256_finish(&sc, hash) == 0);
        mbedtls_sha256_free(&sc);
    }
    assert(mbedtls_ecdsa_sign(&grp, &r, &s, &d, hash, sizeof hash,
                               mbedtls_ctr_drbg_random, &rng) == 0);
    assert(mbedtls_mpi_write_binary(&r, sig, 32) == 0);
    assert(mbedtls_mpi_write_binary(&s, sig + 32, 32) == 0);
    /* export X||Y via the public point-write API */
    {
        unsigned char uncomp[65];
        size_t olen = 0;
        assert(mbedtls_ecp_point_write_binary(&grp, &Q,
               MBEDTLS_ECP_PF_UNCOMPRESSED, &olen, uncomp, sizeof uncomp) == 0);
        assert(olen == 65 && uncomp[0] == 0x04);
        memcpy(rawpub, uncomp + 1, 64);
    }
    assert(manifest_verify_sig(msg, sizeof msg - 1, sig, rawpub) == 0);
    sig[10] ^= 0x01;
    assert(manifest_verify_sig(msg, sizeof msg - 1, sig, rawpub) != 0);
    sig[10] ^= 0x01;
    unsigned char bad[sizeof msg];
    memcpy(bad, msg, sizeof bad);
    bad[5] ^= 0x01;
    assert(manifest_verify_sig(bad, sizeof bad - 1, sig, rawpub) != 0);
    assert(manifest_verify_sig(NULL, 0, sig, rawpub) != 0);
    mbedtls_mpi_free(&d); mbedtls_mpi_free(&r); mbedtls_mpi_free(&s);
    mbedtls_ecp_point_free(&Q);
    mbedtls_ecp_group_free(&grp);
    mbedtls_ctr_drbg_free(&rng);
    mbedtls_entropy_free(&ent);
}

static void test_base64(void) {
    char out[32];
    assert(b64_encode((const unsigned char *)"", 0, out) == 0);
    assert(strcmp(out, "") == 0);
    assert(b64_encode((const unsigned char *)"f", 1, out) == 4);
    assert(strcmp(out, "Zg==") == 0);
    assert(b64_encode((const unsigned char *)"fo", 2, out) == 4);
    assert(strcmp(out, "Zm8=") == 0);
    assert(b64_encode((const unsigned char *)"foo", 3, out) == 4);
    assert(strcmp(out, "Zm9v") == 0);
}

static void test_cfg_titles(void) {
    char dir[64], path[96], path2[96];
    assert(make_tmpdir(dir, sizeof dir) == 0);
    snprintf(path, sizeof path, "%s/cfg.json", dir);
    snprintf(path2, sizeof path2, "%s/cfg2.json", dir);
    FILE *f = fopen(path, "wb");
    assert(f);
    fputs("{\"token\":\"t\",\"titles\":{\"CUSA11995\":\"Marvel's Spider-Man\","
          "\"BAD KEY!\": \"junk\", \"CUSA00001\": \"x\", \"TOOLONGTITLEID12345\": \"y\","
          "\"CUSA00002\": 42}}", f);
    fclose(f);
    cfg_t c;
    assert(cfg_load(path, &c) == 0);
    assert(c.n_titles == 1); /* only the well-formed entry survives */
    char name[128];
    assert(cfg_title(&c, "CUSA11995", name, sizeof name) == 0);
    assert(!strcmp(name, "Marvel's Spider-Man"));
    assert(cfg_title(&c, "CUSA99999", name, sizeof name) != 0);
    assert(cfg_title(&c, "", name, sizeof name) != 0);
    assert(cfg_title(NULL, "CUSA11995", name, sizeof name) != 0);
    /* round-trip: save preserves overrides + home_art */
    strncpy(c.home_art, "pslogo", sizeof c.home_art - 1);
    assert(cfg_save(path2, &c) == 0);
    cfg_t c2;
    assert(cfg_load(path2, &c2) == 0);
    assert(c2.n_titles == 1);
    assert(cfg_title(&c2, "CUSA11995", name, sizeof name) == 0);
    assert(!strcmp(name, "Marvel's Spider-Man"));
    assert(!strcmp(c2.home_art, "pslogo"));
    /* missing titles object: zero overrides, lookup misses */
    f = fopen(path, "wb");
    assert(f);
    fputs("{\"token\":\"t\"}", f);
    fclose(f);
    assert(cfg_load(path, &c) == 0);
    assert(c.n_titles == 0);
    /* home_art is the legacy fallback; large_art is what actually gets used.
     * Both default to the operator-hosted idle logo. */
    assert(!strcmp(c.home_art, "https://retro-games.cybermask.dpdns.org/images/ps-logo-full.png"));
    assert(!strcmp(c.large_art, "https://retro-games.cybermask.dpdns.org/images/ps-logo-full.png"));
    assert(!strcmp(c.small_art, "https://retro-games.cybermask.dpdns.org/images/ps-logo-blue.png"));
    assert(!strcmp(c.browser_art, "https://retro-games.cybermask.dpdns.org/images/web_browser.png"));
}

static void test_cfg_title_rules(void){
    char dir[64], path[96], path2[96];
    assert(make_tmpdir(dir, sizeof dir) == 0);
    snprintf(path, sizeof path, "%s/cfg.json", dir);
    snprintf(path2, sizeof path2, "%s/cfg2.json", dir);
    FILE *f = fopen(path, "wb");
    assert(f);
    fputs("{\"token\":\"t\",\"status\":\"dnd\",\"timestamp_mode\":\"none\",\"title_rules\":{"
          "\"CUSA11995\":{\"disabled\":true,\"details\":\"Swinging\",\"duration_min\":90,"
          "\"art\":\"https://example.invalid/spidey.png\"},"
          "\"CUSA00001\":{\"disabled\":false},"
          "\"CUSA00002\":{\"details\":\"x\"},"
          "\"bad key\":{\"disabled\":true},"
          "\"CUSA00003\":42,"
          "\"CUSA00004\":{\"disabled\":\"yes\"},"
          "\"CUSA00005\":{\"art\":\"\"}}}", f);
    fclose(f);
    cfg_t c;
    assert(cfg_load(path, &c) == 0);
    assert(!strcmp(c.status, "dnd"));
    assert(!strcmp(c.timestamp_mode, "none"));
    /* disabled + details + art, one details-only; empty rules are dropped
     * and bad keys/types are ignored. A string "disabled" is not a bool. */
    assert(c.n_rules == 2);
    const cfg_title_rule_t *r = cfg_title_rule(&c, "CUSA11995");
    assert(r && r->disabled && !strcmp(r->details, "Swinging"));
    assert(r->duration_min == 90);
    assert(!strcmp(r->art, "https://example.invalid/spidey.png"));
    r = cfg_title_rule(&c, "CUSA00002");
    assert(r && !r->disabled && !strcmp(r->details, "x") && !r->art[0]);
    assert(cfg_title_rule(&c, "CUSA00001") == NULL);
    assert(cfg_title_rule(&c, "CUSA00004") == NULL);
    assert(cfg_title_rule(&c, "CUSA00005") == NULL);
    assert(cfg_title_rule(&c, "bad key") == NULL);
    assert(cfg_title_rule(&c, "CUSA99999") == NULL);
    assert(cfg_title_rule(NULL, "CUSA11995") == NULL);
    /* save/load preserves the rules (and their absence stays absence). */
    assert(cfg_save(path2, &c) == 0);
    cfg_t c2;
    assert(cfg_load(path2, &c2) == 0);
    assert(!strcmp(c2.status, "dnd"));
    assert(!strcmp(c2.timestamp_mode, "none"));
    assert(c2.n_rules == 2);
    r = cfg_title_rule(&c2, "CUSA11995");
    assert(r && r->disabled && !strcmp(r->details, "Swinging"));
    assert(r->duration_min == 90);
    assert(!strcmp(r->art, "https://example.invalid/spidey.png"));
}

static void test_cfg_permissions(void) {
    /* config.json carries a user token. It must never be group/world
     * readable, including after a load of an older 0644 file. */
    char dir[64], path[96];
    assert(make_tmpdir(dir, sizeof dir) == 0);
    snprintf(path, sizeof path, "%s/cfg.json", dir);
    FILE *f = fopen(path, "wb");
    assert(f);
    fputs("{\"token\":\"test.token.fixture.value.for.host.tests.only\"}", f);
    fclose(f);
    assert(chmod(path, 0644) == 0);
    cfg_t c;
    assert(cfg_load(path, &c) == 0);
    struct stat st;
    assert(stat(path, &st) == 0);
    assert((st.st_mode & 0777) == 0600);
    assert(cfg_save(path, &c) == 0);
    assert(stat(path, &st) == 0);
    assert((st.st_mode & 0777) == 0600);
}

/* cfg_save used to rebuild the object from scratch, silently deleting every
 * key it did not know (including ones written by a newer build or by hand). */
static void test_cfg_preserves_unknown(void) {
    char dir[64], path[96];
    assert(make_tmpdir(dir, sizeof dir) == 0);
    snprintf(path, sizeof path, "%s/cfg.json", dir);
    FILE *f = fopen(path, "wb");
    assert(f);
    fputs("{\"token\":\"old-token\",\"future_key\":{\"a\":1},\"note\":\"keep me\"}", f);
    fclose(f);
    cfg_t c;
    assert(cfg_load(path, &c) == 0);
    strncpy(c.token, "new-token", sizeof c.token - 1);
    c.token[sizeof c.token - 1] = 0;
    assert(cfg_save(path, &c) == 0);
    FILE *g = fopen(path, "rb");
    assert(g);
    char buf[2048];
    size_t n = fread(buf, 1, sizeof buf - 1, g);
    fclose(g);
    buf[n] = 0;
    assert(strstr(buf, "\"future_key\"") != NULL);
    assert(strstr(buf, "\"keep me\"") != NULL);
    assert(strstr(buf, "\"new-token\"") != NULL);
    assert(strstr(buf, "\"old-token\"") == NULL);
    /* exactly one token pair: a duplicate would let the stale one win */
    int hits = 0;
    for(const char *p = buf; (p = strstr(p, "\"token\"")) != NULL; p += 7) hits++;
    assert(hits == 1);
}

static void test_cfg_migrate(void) {
    char dir[64], path[96], bak[120], vbak[120], buf[2048];
    assert(make_tmpdir(dir, sizeof dir) == 0);
    snprintf(path, sizeof path, "%s/config.json", dir);
    snprintf(bak, sizeof bak, "%s.bak", path);
    snprintf(vbak, sizeof vbak, "%s.v1.bak", path);
    const char *orig = "{\"token\":\"test-token-v1\","
                       "\"titles\":{\"CUSA1\":\"Game\"},\"future_key\":42}";
    FILE *f = fopen(path, "wb");
    assert(f); fputs(orig, f); fclose(f);

    cfg_t c;
    assert(cfg_load(path, &c) == 0);
    assert(c.schema_version == CFG_SCHEMA_VERSION);
    assert(c.n_titles == 1 && !strcmp(c.title_ids[0], "CUSA1"));
    assert(cfg_save(path, &c) == 0);
    assert(read_text_file(path, buf, sizeof buf) > 0);
    assert(strstr(buf, "\"schema_version\":2") != NULL);
    assert(strstr(buf, "\"future_key\"") != NULL);
    assert(strstr(buf, "test-token-v1") != NULL);
    /* pre-save bytes are kept, including a one-time pristine v1 copy */
    assert(read_text_file(bak, buf, sizeof buf) > 0);
    assert(!strcmp(buf, orig));
    assert(read_text_file(vbak, buf, sizeof buf) > 0);
    assert(!strcmp(buf, orig));
    struct stat st;
    assert(stat(vbak, &st) == 0 && (st.st_mode & 0777) == 0600);

    /* A second save rotates .bak but must never consume the v1 copy. */
    strncpy(c.token, "test-token-v2", sizeof c.token - 1);
    c.token[sizeof c.token - 1] = 0;
    assert(cfg_save(path, &c) == 0);
    assert(read_text_file(vbak, buf, sizeof buf) > 0);
    assert(!strcmp(buf, orig));
    assert(read_text_file(bak, buf, sizeof buf) > 0);
    assert(strstr(buf, "\"schema_version\":2") != NULL);
    assert(strstr(buf, "test-token-v1") != NULL);
    assert(strstr(buf, "test-token-v2") == NULL);

    /* A document from a newer build is not rewritten. */
    f = fopen(path, "wb");
    assert(f); fputs("{\"schema_version\":99,\"token\":\"keep\"}", f); fclose(f);
    assert(cfg_save(path, &c) != 0);
    assert(read_text_file(path, buf, sizeof buf) > 0);
    assert(strstr(buf, "\"schema_version\":99") != NULL);
    assert(cfg_migrate(NULL, 1) != 0);
}

/* Nonsense cadences must degrade to the documented bounds, not to a busy
 * poll or a dead feature. */
static void test_cfg_clamp(void) {
    char dir[64], path[96];
    assert(make_tmpdir(dir, sizeof dir) == 0);
    snprintf(path, sizeof path, "%s/cfg.json", dir);
    cfg_t c;
    FILE *f = fopen(path, "wb");
    assert(f);
    fputs("{\"token\":\"t\",\"poll_interval_s\":1,\"presence_reconcile_s\":5,"
          "\"presence_hold_s\":-5,\"lock_heartbeat_s\":1,\"lock_stale_s\":1,"
          "\"reconnect_max_s\":1,\"timesync_interval_s\":1,\"log_max_kb\":1}", f);
    fclose(f);
    assert(cfg_load(path, &c) == 0);
    assert(c.poll_interval_s == 5);
    assert(c.presence_reconcile_s == 60);
    assert(c.presence_hold_s == 0);
    assert(c.lock_heartbeat_s == 30);
    assert(c.lock_stale_s == 120);
    assert(c.reconnect_max_s == 60);
    assert(c.timesync_interval_s == 300);
    assert(c.log_max_kb == 64);

    f = fopen(path, "wb");
    assert(f);
    fputs("{\"token\":\"t\",\"poll_interval_s\":9999,\"presence_reconcile_s\":99999,"
          "\"presence_hold_s\":99999,\"lock_heartbeat_s\":9999,"
          "\"lock_stale_s\":99999,\"reconnect_max_s\":99999,"
          "\"timesync_interval_s\":99999,\"log_max_kb\":99999}", f);
    fclose(f);
    assert(cfg_load(path, &c) == 0);
    assert(c.poll_interval_s == 300);
    assert(c.presence_reconcile_s == 3600);
    assert(c.presence_hold_s == 1800);
    assert(c.lock_heartbeat_s == 600);
    assert(c.lock_stale_s == 86400);
    assert(c.reconnect_max_s == 3600);
    assert(c.timesync_interval_s == 86400);
    assert(c.log_max_kb == 8192);

    /* stale must leave at least two heartbeats of margin */
    f = fopen(path, "wb");
    assert(f);
    fputs("{\"token\":\"t\",\"lock_heartbeat_s\":300,\"lock_stale_s\":400}", f);
    fclose(f);
    assert(cfg_load(path, &c) == 0);
    assert(c.lock_heartbeat_s == 300);
    assert(c.lock_stale_s == 600);

    f = fopen(path, "wb");
    assert(f);
    fputs("{\"token\":\"t\",\"poll_interval_s\":12,\"presence_reconcile_s\":900}", f);
    fclose(f);
    assert(cfg_load(path, &c) == 0);
    assert(c.poll_interval_s == 12 && c.presence_reconcile_s == 900);
    /* D-11: the five-minute hold is the documented default. */
    assert(c.presence_hold_s == 300);
    /* Omitted recovery knobs keep the 1.1 built-ins. */
    assert(c.lock_heartbeat_s == 60);
    assert(c.lock_stale_s == 600);
    assert(c.reconnect_max_s == 600);
    assert(c.timesync_interval_s == 3600);
    assert(c.log_max_kb == 512);
    assert(strcmp(c.presence_details_media, "Watching on PlayStation 4") == 0);
}

/* Pure scheduling predicates the daemon uses for detection cadence,
 * transition debounce, retry windows, and Rest Mode. */
static void test_sched(void) {
    int64_t last = 0;
    /* first tick after a (re)connect always polls */
    assert(sched_poll_due(100, &last, 12) == 1);
    assert(last == 100);
    /* inside the interval: no poll, and the deadline is not moved */
    assert(sched_poll_due(105, &last, 12) == 0);
    assert(last == 100);
    /* exactly at the interval: poll */
    assert(sched_poll_due(112, &last, 12) == 1);
    assert(last == 112);
    /* a backwards jump must not stall detection forever */
    assert(sched_poll_due(50, &last, 12) == 1);
    assert(last == 50);
    /* nonsense interval is treated as 1s, never 0 (no busy poll) */
    last = 0;
    assert(sched_poll_due(10, &last, 0) == 1);
    assert(sched_poll_due(10, &last, 0) == 0);
    assert(sched_poll_due(11, &last, 0) == 1);

    /* debounce collapses at long intervals so latency is not doubled */
    assert(sched_transition_hits(1) == 2);
    assert(sched_transition_hits(2) == 2);
    assert(sched_transition_hits(5) == 1);
    assert(sched_transition_hits(300) == 1);

    /* Idle suppression must beat the post-close hold, but an uncertain
     * detection still holds: a live game may be underneath. */
    assert(sched_clear_idle_now(1, 1, 0) == 1);
    assert(sched_clear_idle_now(1, 1, 1) == 0);
    assert(sched_clear_idle_now(1, 0, 0) == 0);
    assert(sched_clear_idle_now(0, 1, 0) == 0);
    /* Home presence must not be posted with show_idle off. */
    assert(sched_home_due(0, 0, 0, 0) == 1);
    assert(sched_home_due(1, 0, 0, 0) == 0);
    assert(sched_home_due(0, 1, 0, 0) == 0);
    assert(sched_home_due(0, 0, 1, 0) == 0);
    assert(sched_home_due(0, 0, 0, 1) == 0);

    /* Best-effort lookups with no mapping are retried on a window, not on
     * every poll: a dead network must not stall detection every 12s. */
    int64_t rt = 0;
    assert(sched_retry_due(1000, &rt, 900) == 1);
    assert(rt == 1000);
    assert(sched_retry_due(1500, &rt, 900) == 0);
    assert(rt == 1000);
    assert(sched_retry_due(1900, &rt, 900) == 1);
    /* a backwards clock jump retries immediately rather than waiting */
    assert(sched_retry_due(1800, &rt, 900) == 1);

    /* Rest Mode (D-03): wall runs ahead of monotonic only while suspended.
     * Normal ticks have near-zero drift; a 2h sleep shows ~7200s. */
    assert(sched_suspend_drift(1, 1, 120) == 0);
    assert(sched_suspend_drift(2, 1, 120) == 0);
    assert(sched_suspend_drift(119, 0, 120) == 0);
    assert(sched_suspend_drift(120, 0, 120) == 120);
    assert(sched_suspend_drift(7200, 0, 120) == 7200);
    /* a backwards/system-clock correction is not a suspend */
    assert(sched_suspend_drift(-3600, 1, 120) == 0);
    assert(sched_suspend_drift(0, 0, 120) == 0);
    /* monotonic advancing with wall (no way to tell) is not a suspend */
    assert(sched_suspend_drift(7200, 7200, 120) == 0);

    /* Rebasing preserves elapsed playtime and drops the sleep hours. */
    assert(sched_rebase_start(1000, 3600) == 4600);
    assert(sched_rebase_start(1000, 0) == 1000);
    assert(sched_rebase_start(0, 3600) == 0);

    /* A pure loop stall (no wall/monotonic drift) is a different event
     * from Rest Mode; threshold is 60s and must not fire on normal ticks. */
    assert(sched_loop_stall(0, 60) == 0);
    assert(sched_loop_stall(1, 60) == 0);
    assert(sched_loop_stall(60, 60) == 0);
    assert(sched_loop_stall(61, 60) == 1);
    assert(sched_loop_stall(3600, 60) == 1);
    /* nonsense threshold never turns every tick into a stall */
    assert(sched_loop_stall(0, 0) == 0);
    assert(sched_loop_stall(1, 0) == 0);
    assert(sched_loop_stall(2, 0) == 1);
}

/* A long-running daemon previously only rotated the log at open, so the file
 * grew without bound. Rotation must happen during the run and keep one
 * previous generation. */
static void test_log_rotation(void) {
    char dir[64], path[96], old[120];
    assert(make_tmpdir(dir, sizeof dir) == 0);
    snprintf(path, sizeof path, "%s/log.txt", dir);
    snprintf(old, sizeof old, "%s.1", path);
    log_close();
    log_set_cap(2048);
    log_init(path);
    char line[81];
    memset(line, 'x', sizeof line - 1);
    line[sizeof line - 1] = 0;
    for(int i = 0; i < 200; i++) log_msg("%s", line);
    log_close();
    struct stat st;
    assert(stat(path, &st) == 0);
    assert(st.st_size <= 2048 + 128);   /* bounded: one line of overshoot */
    assert(stat(old, &st) == 0);
    assert(st.st_size <= 2048 + 128);
    log_set_cap(0);                     /* restore default for later tests */
}

/* Test-only VFS shim. sqlite's DbPath-based xFullPathname can fail with a
 * silent SQLITE_CANTOPEN (no syscall, no errno, nothing logged) on some
 * runners. Our test paths are always absolute, so bypass it with a plain
 * copy and delegate everything else to the real unix VFS. */
static sqlite3_vfs g_orx_wrap;
static int g_orx_installed = 0;

static int orx_fullpath(sqlite3_vfs *p, const char *z, int n, char *o){
    size_t len = strlen(z) + 1;
    (void)p;
    if (len > (size_t)n) return SQLITE_CANTOPEN;
    memcpy(o, z, len);
    return SQLITE_OK;
}

static void orx_vfs_install(void){
    sqlite3_vfs *under;
    if (g_orx_installed) return;
    g_orx_installed = 1;
    (void)sqlite3_initialize();
    under = sqlite3_vfs_find(0);
    if (!under) return;
    g_orx_wrap = *under; /* inherit szOsFile, mxPathname, all methods */
    g_orx_wrap.pNext = 0; /* must not link into the old registry list */
    g_orx_wrap.zName = "orxtest";
    g_orx_wrap.xFullPathname = orx_fullpath;
    (void)sqlite3_vfs_register(&g_orx_wrap, 1); /* default for this process */
}

static void test_appdb(void) {
    char dir[64], db[96], meta[96], pj[160];
    orx_vfs_install();
    assert(make_tmpdir(dir, sizeof dir) == 0);
    snprintf(db, sizeof db, "%s/app.db", dir);
    /* Brand-new dir, so a pre-existing app.db is a stale artifact. sqlite
     * opens with O_NOFOLLOW and fails ELOOP on a symlink where plain
     * open() would follow it — clear the path before opening. */
    unlink(db);
    /* Pre-create so sqlite never exercises its CREATE branch; the test
     * validates appdb reads/writes on a real file either way. */
    { int prefd = open(db, O_RDWR|O_CREAT|O_TRUNC, 0600); if (prefd >= 0) close(prefd); }
    snprintf(meta, sizeof meta, "%s/appmeta", dir);
    assert(mkdir(meta, 0700) == 0);
    sqlite3 *s = NULL;
    (void)sqlite3_initialize();
    int rc = sqlite3_open_v2(db, &s, SQLITE_OPEN_READWRITE|SQLITE_OPEN_CREATE, NULL);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "DEBUG sqlite3_open_v2 rc=%d db=%s err=%s\n", rc, db, s ? sqlite3_errmsg(s) : "null");
        sqlite3_close(s);
    }
    assert(rc == SQLITE_OK);
    assert(sqlite3_exec(s, "CREATE TABLE tbl_appbrowse(titleId TEXT, titleName TEXT);", 0, 0, 0) == SQLITE_OK);
    assert(sqlite3_exec(s, "INSERT INTO tbl_appbrowse VALUES('CUSA11995','Marvel''s Spider-Man');", 0, 0, 0) == SQLITE_OK);
    assert(sqlite3_exec(s, "CREATE TABLE tbl_appinfo(titleId TEXT, key TEXT, val TEXT);", 0, 0, 0) == SQLITE_OK);
    assert(sqlite3_exec(s, "INSERT INTO tbl_appinfo VALUES('CUSA00001','TITLE_01','Lang One');", 0, 0, 0) == SQLITE_OK);
    assert(sqlite3_exec(s, "INSERT INTO tbl_appinfo VALUES('CUSA00001','TITLE','Base Name');", 0, 0, 0) == SQLITE_OK);
    assert(sqlite3_exec(s, "INSERT INTO tbl_appinfo VALUES('CUSA00002','TITLE','');", 0, 0, 0) == SQLITE_OK);
    sqlite3_close(s);
    char name[128];
    /* tier 1: browse name wins */
    assert(appdb_title_from(db, meta, "CUSA11995", name, sizeof name) == 0);
    assert(!strcmp(name, "Marvel's Spider-Man"));
    /* tier 2: bare TITLE preferred over TITLE_01 */
    assert(appdb_title_from(db, meta, "CUSA00001", name, sizeof name) == 0);
    assert(!strcmp(name, "Base Name"));
    /* empty val rows never match; no param.json staged -> miss */
    assert(appdb_title_from(db, meta, "CUSA00002", name, sizeof name) != 0);
    assert(appdb_title_from(db, meta, "CUSA99999", name, sizeof name) != 0);
    /* tier 3: staged param.json */
    char gdir[128];
    snprintf(gdir, sizeof gdir, "%s/CUSA00002", meta);
    assert(mkdir(gdir, 0700) == 0);
    snprintf(pj, sizeof pj, "%s/param.json", gdir);
    FILE *f = fopen(pj, "wb");
    assert(f);
    fputs("{\"titleId\":\"CUSA00002\",\"titleName\":\"JSON Game\"}", f);
    fclose(f);
    assert(appdb_title_from(db, meta, "CUSA00002", name, sizeof name) == 0);
    assert(!strcmp(name, "JSON Game"));
    /* invalid inputs fail soft */
    assert(appdb_title_from(db, meta, NULL, name, sizeof name) != 0);
    assert(appdb_title_from(db, meta, "short", name, sizeof name) != 0);
    assert(appdb_title_from(db, meta, "cusa11995", name, sizeof name) != 0);
    assert(appdb_title_from(db, meta, "CUSA1199!", name, sizeof name) != 0);
    assert(appdb_title_from("/nonexistent/app.db", meta, "CUSA11995", name, sizeof name) != 0);
    assert(appdb_title_from(db, meta, "CUSA11995", NULL, 0) != 0);
}

static void test_discord_builder(void) {
    cfg_t saved = g_cfg;
    cfg_defaults(&g_cfg);
    /* game presence: name shown, raw ID nowhere visible, hover has the ID */
    jl_val_t *a = discord_build_activity("On PS4", "Marvel's Spider-Man","Playing on PlayStation 4","ps_logo_blue",NULL,
        "CUSA11995", "1536977374795538532", "https://x/icons/", NULL, NULL,
        1700000000LL, "");
    assert(a);
    const jl_val_t *v = jl_obj_get(a, "name");
    assert(v && v->type == JL_STRING && !strcmp(v->str, "Marvel's Spider-Man"));
    v = jl_obj_get(a, "details");
    assert(v && v->type == JL_STRING && !strcmp(v->str, "Playing on PlayStation 4"));
    assert(!strstr(v->str, "CUSA")); /* never the raw ID */
    v = jl_obj_get(a, "state");
    assert(v && v->type == JL_STRING && !strcmp(v->str, "On PS4"));
    v = jl_obj_get(a, "type");
    assert(v && v->type == JL_NUMBER && (int)v->num == 0);
    const jl_val_t *ts = jl_obj_get(a, "timestamps");
    assert(ts && ts->type == JL_OBJECT);
    v = jl_obj_get(ts, "start");
    assert(v && v->type == JL_NUMBER && v->inum == 1700000000LL * 1000LL);
    /* art_base_url is configured, so the pack URL is the intended source.
     * With no token it cannot resolve; the builder must omit art rather
     * than send the generated key (a dangling key drops the whole activity). */
    const jl_val_t *as = jl_obj_get(a, "assets");
    assert(as == NULL);
    jl_free(a);
    snprintf(g_cfg.timestamp_mode, sizeof g_cfg.timestamp_mode, "none");
    a = discord_build_activity("On PS4", "Game", "Playing", NULL, NULL,
        "CUSA11995", "app", NULL, NULL, NULL, 1700000000LL, "");
    assert(a && jl_obj_get(a, "timestamps") == NULL);
    jl_free(a);
    snprintf(g_cfg.timestamp_mode, sizeof g_cfg.timestamp_mode, "elapsed");
    g_cfg.n_rules = 1;
    snprintf(g_cfg.title_rules[0].title_id,
             sizeof g_cfg.title_rules[0].title_id, "CUSA11995");
    g_cfg.title_rules[0].duration_min = 90;
    a = discord_build_activity("On PS4", "Game", "Playing", NULL, NULL,
        "CUSA11995", "app", NULL, NULL, NULL, 1700000000LL, "");
    assert(a);
    ts = jl_obj_get(a, "timestamps");
    assert(ts && jl_obj_get(ts, "start") && jl_obj_get(ts, "end"));
    assert(jl_obj_get(ts, "end")->inum == (1700000000LL + 90 * 60) * 1000LL);
    jl_free(a);
    /* system badge: mp: URL used as-is with platform hover text */
    a = discord_build_activity("On PS4", "Marvel's Spider-Man","Playing on PlayStation 4","ps_logo_blue",NULL,
        "CUSA11995", "1536977374795538532", NULL, NULL, "mp:1/2/logo",
        1700000000LL, "");
    assert(a);
    as = jl_obj_get(a, "assets");
    assert(as && as->type == JL_OBJECT);
    v = jl_obj_get(as, "small_image");
    assert(v && v->type == JL_STRING && !strcmp(v->str, "mp:1/2/logo"));
    v = jl_obj_get(as, "small_text");
    assert(v && v->type == JL_STRING && !strcmp(v->str, "PlayStation 4"));
    jl_free(a);
    /* media type mapping survives */
    a = discord_build_activity(NULL, "YouTube","Watching on PlayStation 4","ps_logo_blue",NULL, "CUSA01015", "app",
        NULL, NULL, NULL, 0, "");
    assert(a);
    v = jl_obj_get(a, "type");
    assert(v && (int)v->num == 0); /* stub maps only CUSA00127 */
    jl_free(a);
    a = discord_build_activity(NULL, "Netflix","Watching on PlayStation 4","ps_logo_blue",NULL, "CUSA00127", "app",
        NULL, NULL, NULL, 0, "");
    assert(a);
    v = jl_obj_get(a, "type");
    assert(v && (int)v->num == 3);
    jl_free(a);
    /* home + uploaded key: trusted key used as-is */
    a = discord_build_activity("On PS4", "PlayStation 4","Idling on Home Menu",NULL,NULL, "home", "app",
        NULL, "pslogo", NULL, 0, "");
    assert(a);
    as = jl_obj_get(a, "assets");
    assert(as && as->type == JL_OBJECT);
    v = jl_obj_get(as, "large_image");
    assert(v && !strcmp(v->str, "pslogo"));
    v = jl_obj_get(as, "large_text");
    assert(v && !strcmp(v->str, "PlayStation 4"));
    jl_free(a);
    /* home with nothing: no assets block at all (never dangling) */
    a = discord_build_activity("On PS4", "PlayStation 4","Idling on Home Menu",NULL,NULL, "home", "app",
        NULL, NULL, NULL, 0, "");
    assert(a);
    assert(jl_obj_get(a, "assets") == NULL);
    jl_free(a);
    /* NULL name rejected */
    assert(discord_build_activity(NULL, NULL,"Idling on Home Menu",NULL,NULL, "home", "app", NULL, NULL, NULL, 0, "") == NULL);
    g_cfg = saved;
}

static void test_cfg_learn(void) {
    cfg_t c;
    cfg_defaults(&c);
    assert(c.n_titles == 0);
    assert(cfg_learn(&c, "CUSA11995", "Marvel's Spider-Man") == 1);
    assert(cfg_learn(&c, "CUSA11995", "Marvel's Spider-Man") == 0); /* dup */
    assert(cfg_learn(&c, "CUSA11995", "CUSA11995") == 0); /* ID echo refused */
    assert(cfg_learn(&c, "CUSA11995", "x") == 0); /* too short */
    assert(cfg_learn(&c, "bad key!", "Name") == 0);
    assert(cfg_learn(&c, "CUSA11995", "Spider-Man 2") == 1); /* update */
    char name[128];
    assert(cfg_title(&c, "CUSA11995", name, sizeof name) == 0);
    assert(!strcmp(name, "Spider-Man 2"));
    assert(cfg_learn(NULL, "CUSA11995", "N") == 0);
    assert(cfg_learn(&c, NULL, "N") == 0);
    /* full map refuses */
    c.n_titles = CFG_MAX_TITLES;
    assert(cfg_learn(&c, "CUSA00001", "Full Game") == 0);
}

static void test_installer_cfg(void) {
    char dir[64], path[96];
    assert(make_tmpdir(dir, sizeof dir) == 0);
    snprintf(path, sizeof path, "%s/config.json", dir);
    /* token validation */
    assert(token_valid("MTIz.ABCdef_0123456789-xYz.ABCDEFGHijklmnopQRSTUVwxyz") == 1);
    assert(token_valid("short") == 0);
    assert(token_valid("SET_ME") == 0);
    assert(token_valid("has space.in.it.aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa") == 0);
    assert(token_valid("nodots") == 0);
    assert(token_valid(NULL) == 0);
    /* Same validator as the daemon: a 151..200-char token is legal, and
     * empty segments are not (they used to pass the installer's copy). */
    {
        char longtok[201];
        int i = 0, j;
        for(j = 0; j < 60; j++) longtok[i++] = 'A';
        longtok[i++] = '.';
        for(j = 0; j < 60; j++) longtok[i++] = 'B';
        longtok[i++] = '.';
        while(i < 180) longtok[i++] = 'C';
        longtok[i] = 0;
        assert(strlen(longtok) == 180);
        assert(token_valid(longtok) == 1);
    }
    assert(token_valid(".abc.def.ghijklmnopqrstuvwxyz0123456789") == 0);
    assert(token_valid("abc.def.ghijklmnopqrstuvwxyz0123456789.") == 0);
    assert(token_valid("abc..ghijklmnopqrstuvwxyz0123456789abc") == 0);
    assert(token_valid("YOUR_TOKEN_HERE") == 0);
    /* save creates, preserves, round-trips */
    assert(icfg_token_save(path, "MTIz.ABCdef_0123456789-xYz.ABCDEFGHijklmnopQRSTUVwxyz") == 0);
    {
        struct stat stb;
        assert(stat(path, &stb) == 0);
        assert((stb.st_mode & 0777) == 0600);   /* token file is not group/world readable */
    }
    assert(icfg_set_str(path, "presence_state", "On PS4") == 0);
    assert(icfg_set_int(path, "poll_interval_s", 12) == 0);
    /* invalid token refused, file untouched */
    assert(icfg_token_save(path, "junk") == -1);
    char tok[160], st[64];
    long n = 0;
    assert(icfg_token_load(path, tok, sizeof tok) == 0);
    assert(!strcmp(tok, "MTIz.ABCdef_0123456789-xYz.ABCDEFGHijklmnopQRSTUVwxyz"));
    assert(icfg_get_str(path, "presence_state", st, sizeof st) == 0);
    assert(!strcmp(st, "On PS4"));
    assert(icfg_get_int(path, "poll_interval_s", &n) == 0 && n == 12);
    /* missing file: loads fail soft, save still works */
    assert(icfg_get_str("/nonexistent/x.json", "k", st, sizeof st) != 0);
}

/* --- procwalk: process-table record parsing ---------------------------
 * detect.c used to hard-code "eboot.bin" at byte 447 and reject records
 * under 479 bytes. Those are 9.00 numbers; on a firmware that shifts
 * kinfo_proc the scan reported "no game running" forever and the daemon
 * posted nothing. procwalk keeps the self-describing ki_structsize framing
 * but searches for the name instead of trusting an offset, so these tests
 * pin that down: the 9.00 layout must keep working, and shifted layouts
 * must now be found. */

/* Write a synthetic kinfo_proc record: leading ki_structsize, then `comm`
 * (NUL-terminated) at comm_off, zero padding out to recsz. Sizes are native
 * endian ints -- PS4 and the host runner are both little-endian. The comm is
 * only written when it fits entirely inside the record, which is how we build
 * the deliberately-truncated-name case. */
static size_t put_rec(unsigned char *buf, size_t at, int recsz,
                      size_t comm_off, const char *comm) {
    memset(buf + at, 0, (size_t)recsz);
    memcpy(buf + at, &recsz, sizeof recsz);
    if (comm && comm_off + strlen(comm) + 1 <= (size_t)recsz)
        memcpy(buf + at + comm_off, comm, strlen(comm) + 1);
    return at + (size_t)recsz;
}

static void put_i32(unsigned char *p, int v) { memcpy(p, &v, sizeof v); }

static void test_procwalk(void) {
    unsigned char buf[4096];
    size_t at;

    /* The 9.00 layout this code was written against. Regression guard. */
    at = put_rec(buf, 0, 479, 447, "eboot.bin");
    assert(procwalk_count_comm(buf, at, PROCWALK_EBOOT) == 1);
    assert(procwalk_has_comm(buf, at, PROCWALK_EBOOT) == 1);
    assert(procwalk_first_structsize(buf, at) == 479);

    /* A firmware that grew the record and moved the name. The old 447/479
     * check missed every one of these and reported "no game running". */
    at = put_rec(buf, 0, 512, 300, "SystemUI");
    at = put_rec(buf, at, 736, 611, "eboot.bin");
    at = put_rec(buf, at, 640, 200, "eapServer");
    assert(procwalk_count_comm(buf, at, PROCWALK_EBOOT) == 1);
    assert(procwalk_has_comm(buf, at, PROCWALK_EBOOT) == 1);
    assert(procwalk_first_structsize(buf, at) == 512);

    /* Name shoved right up against the end of the record, terminator in the
     * very last byte, still found. */
    at = put_rec(buf, 0, 22, 12, "eboot.bin");
    assert(procwalk_count_comm(buf, at, PROCWALK_EBOOT) == 1);

    /* Two games up -> count 2. */
    at = put_rec(buf, 0, 479, 447, "eboot.bin");
    at = put_rec(buf, at, 479, 447, "eboot.bin");
    assert(procwalk_count_comm(buf, at, PROCWALK_EBOOT) == 2);

    /* No match at all is 0 (an error), not -1 (unknown). */
    at = put_rec(buf, 0, 479, 447, "SystemUI");
    at = put_rec(buf, at, 600, 500, "webShell");
    assert(procwalk_count_comm(buf, at, PROCWALK_EBOOT) == 0);
    assert(procwalk_has_comm(buf, at, PROCWALK_EBOOT) == 0);

    /* Empty buffer. */
    assert(procwalk_count_comm(buf, 0, PROCWALK_EBOOT) == 0);
    assert(procwalk_has_comm(buf, 0, PROCWALK_EBOOT) == 0);
    assert(procwalk_first_structsize(buf, 0) == 0);

    /* Broken framing must report "unknown" (-1), never 0. Callers read 0 as
     * "no game running" and would blank an active presence. */
    memset(buf, 0, 16);
    assert(procwalk_count_comm(buf, 16, PROCWALK_EBOOT) == -1);
    assert(procwalk_has_comm(buf, 16, PROCWALK_EBOOT) == -1);
    put_i32(buf, 479);
    put_i32(buf + 479, -8);
    at = put_rec(buf, 0, 479, 447, "eboot.bin");
    put_i32(buf + at, -8);
    assert(procwalk_count_comm(buf, at + 4, PROCWALK_EBOOT) == -1);
    memset(buf, 0, 16);
    put_i32(buf, 0);
    assert(procwalk_count_comm(buf, 16, PROCWALK_EBOOT) == -1);

    /* A final record cut short by the buffer is reported as unknown, never as
     * the count of the records before it and never read past the end. */
    at = put_rec(buf, 0, 479, 447, "eboot.bin");
    at = put_rec(buf, at, 479, 447, "eboot.bin");
    size_t second = at - 479;
    assert(procwalk_count_comm(buf, at, PROCWALK_EBOOT) == 2);
    assert(procwalk_count_comm(buf, second + 300, PROCWALK_EBOOT) == -1);
    assert(procwalk_count_comm(buf, second + 4, PROCWALK_EBOOT) == -1);

    /* A name too long for its own record is not a match: it must not alias
     * into whatever record follows it. */
    at = put_rec(buf, 0, 16, 12, "eboot.bin");
    assert(procwalk_count_comm(buf, at, PROCWALK_EBOOT) == 0);

    /* Prefix of the needle is not a match ("eboot" != "eboot.bin"). */
    at = put_rec(buf, 0, 479, 447, "eboot");
    assert(procwalk_count_comm(buf, at, PROCWALK_EBOOT) == 0);

    /* A record claiming to be larger than the buffer is malformed, not a
     * clean truncation, so it reports unknown. */
    at = put_rec(buf, 0, 479, 447, "eboot.bin");
    put_i32(buf, 999999);
    assert(procwalk_count_comm(buf, at, PROCWALK_EBOOT) == -1);
    assert(procwalk_first_structsize(buf, at) == 0);

    /* Bad arguments rejected rather than crashed on. */
    assert(procwalk_count_comm(NULL, 16, PROCWALK_EBOOT) == -1);
    assert(procwalk_count_comm(buf, 16, NULL) == -1);
    assert(procwalk_count_comm(buf, 16, "") == -1);
    assert(procwalk_first_structsize(NULL, 16) == 0);
}

/* --- procwalk: record lookup + offset-free pid search -----------------
 * lock.c and evict.c used to read the process name at byte 447 and the pid at
 * byte 72 of every kinfo_proc record. Those are 9.00 offsets, so on newer
 * firmware they addressed the wrong bytes and every live payload looked dead:
 * evict deleted the lock while the daemon kept running, and lock.c let a
 * second daemon start alongside the first. procwalk narrows by exact name
 * first, then searches the record for the pid. */
static void test_procwalk_find_and_pid(void) {
    unsigned char buf[4096];
    size_t at, recsz;
    const unsigned char *rec;

    /* the layout lock.c/evict.c used to assume still works */
    at = put_rec(buf, 0, 479, 447, "Payload");
    put_i32(buf + 72, 1234);
    assert(procwalk_find_comm(buf, at, "Payload", &rec, &recsz) == 0);
    assert(rec == buf && recsz == 479);
    assert(procwalk_record_has_i32(rec, recsz, 1234) == 1);
    assert(procwalk_record_has_i32(rec, recsz, 9999) == 0);

    /* shifted firmware: record grew, name and pid both moved */
    at = put_rec(buf, 0, 512, 300, "SystemUI");
    at = put_rec(buf, at, 736, 611, "Payload");
    put_i32(buf + 479 + 91, 1234);
    assert(procwalk_find_comm(buf, at, "Payload", &rec, &recsz) == 0);
    assert(rec == buf + 512 && recsz == 736);
    assert(procwalk_record_has_i32(rec, recsz, 1234) == 1);
    assert(procwalk_record_has_i32(rec, recsz, 4321) == 0);

    /* prefix names must not match: PayloadHelper is not Payload, and kill()
     * on the wrong pid is the failure this guards */
    at = put_rec(buf, 0, 479, 447, "PayloadHelper");
    assert(procwalk_find_comm(buf, at, "Payload", &rec, &recsz) == -2);
    at = put_rec(buf, 0, 479, 447, "Payload");
    assert(procwalk_find_comm(buf, at, "Payload", &rec, &recsz) == 0);

    /* nothing matches: -2, distinct from malformed (-1), outputs cleared */
    at = put_rec(buf, 0, 479, 447, "SystemUI");
    at = put_rec(buf, at, 600, 500, "eboot.bin");
    assert(procwalk_find_comm(buf, at, "Payload", &rec, &recsz) == -2);
    assert(rec == NULL && recsz == 0);

    /* malformed table -> -1 */
    memset(buf, 0, 16);
    assert(procwalk_find_comm(buf, 16, "Payload", &rec, &recsz) == -1);
    put_i32(buf, 479);
    put_i32(buf + 479, -3);
    assert(procwalk_find_comm(buf, 483, "Payload", &rec, &recsz) == -1);

    /* empty and bad args */
    assert(procwalk_find_comm(buf, 0, "Payload", &rec, &recsz) == -2);
    assert(procwalk_find_comm(NULL, 16, "Payload", &rec, &recsz) == -1);
    assert(procwalk_find_comm(buf, 16, NULL, &rec, &recsz) == -1);
    assert(procwalk_find_comm(buf, 16, "Payload", NULL, &recsz) == -1);

    /* the int must lie wholly inside the record */
    at = put_rec(buf, 0, 16, 0, "Payload");
    put_i32(buf + 12, 77);
    assert(procwalk_record_has_i32(buf, 16, 77) == 1);
    at = put_rec(buf, 0, 15, 0, "Payload");
    put_i32(buf + 12, 77);
    assert(procwalk_record_has_i32(buf, 15, 77) == 0);

    /* degenerate args */
    assert(procwalk_record_has_i32(NULL, 100, 1) == 0);
    assert(procwalk_record_has_i32(buf, 0, 1) == 0);
    assert(procwalk_record_has_i32(buf, 3, 1) == 0);

    /* Caveat, pinned down: records are zero-padded and a process name ends in
     * a NUL, so a record routinely contains the int32 0 in its padding. A hit
     * is corroboration, not proof -- which is why callers reject pid <= 0
     * first. Recorded so this is a documented decision, not a surprise. */
    at = put_rec(buf, 0, 64, 0, "Payload");
    assert(procwalk_record_has_i32(buf, 64, 0) == 1);
}

/* --- bigapp: foreground-app classification ----------------------------
 * The daemon's entire "is a game running" signal. The distinctions that matter
 * are the ones a single boolean would throw away:
 *   -1 no big app          -> a game may genuinely have closed
 *    0 System UI in front  -> a game is probably still running underneath
 *   -1 unreadable          -> hold whatever we had, never clear
 * Getting "system UI" or "unreadable" wrong is how presence disappeared while
 * a game was still open. */
static void test_bigapp(void) {
    char out[16];

    /* nothing in front: the only reading that may clear presence */
    assert(bigapp_classify(-1, -1, "", out, sizeof out) == BIGAPP_NONE);
    assert(out[0] == 0);

    /* System UI reported as app id 0, and the NPXS namespace: hold, not close */
    assert(bigapp_classify(0, 0, "", out, sizeof out) == BIGAPP_SYSTEM);
    assert(bigapp_classify(1, 0, "NPXS20001", out, sizeof out) == BIGAPP_SYSTEM);
    assert(out[0] == 0);

    /* a real title */
    assert(bigapp_classify(1, 0, "CUSA00740", out, sizeof out) == BIGAPP_GAME);
    assert(strcmp(out, "CUSA00740") == 0);
    /* PS5 backports and PS2 titles use other prefixes; accept all [A-Z0-9] */
    assert(bigapp_classify(1, 0, "PPSA01234", out, sizeof out) == BIGAPP_GAME);
    assert(bigapp_classify(1, 0, "ELUA01234", out, sizeof out) == BIGAPP_GAME);

    /* a failed title lookup is unknown, not "no game" */
    assert(bigapp_classify(1, -1, "", out, sizeof out) == BIGAPP_UNKNOWN);
    assert(bigapp_classify(1, 0x80990003, "CUSA00740", out, sizeof out) == BIGAPP_UNKNOWN);
    assert(out[0] == 0);

    /* malformed title ids: wrong length, lowercase, punctuation, empty */
    assert(bigapp_classify(1, 0, "", out, sizeof out) == BIGAPP_UNKNOWN);
    assert(bigapp_classify(1, 0, "CUSA0074", out, sizeof out) == BIGAPP_UNKNOWN);
    assert(bigapp_classify(1, 0, "CUSA007400", out, sizeof out) == BIGAPP_UNKNOWN);
    assert(bigapp_classify(1, 0, "cusa00740", out, sizeof out) == BIGAPP_UNKNOWN);
    assert(bigapp_classify(1, 0, "CUSA-0740", out, sizeof out) == BIGAPP_UNKNOWN);
    assert(bigapp_classify(1, 0, "CUSA0074\x80", out, sizeof out) == BIGAPP_UNKNOWN);
    assert(out[0] == 0);

    /* a negative app id other than -1: unknown, never "nothing running" */
    assert(bigapp_classify(-2, 0, "", out, sizeof out) == BIGAPP_UNKNOWN);
    assert(bigapp_classify((int32_t)0x80990003, 0, "", out, sizeof out) == BIGAPP_UNKNOWN);

    /* NULL title must not crash */
    assert(bigapp_classify(1, 0, NULL, out, sizeof out) == BIGAPP_UNKNOWN);
    assert(bigapp_classify(1, 0, NULL, NULL, 0) == BIGAPP_UNKNOWN);

    /* a real title but nowhere to write it: unknown rather than a silent
     * partial copy */
    assert(bigapp_classify(1, 0, "CUSA00740", NULL, 0) == BIGAPP_UNKNOWN);
    assert(bigapp_classify(1, 0, "CUSA00740", out, 4) == BIGAPP_UNKNOWN);

    /* validator directly */
    assert(bigapp_titleid_valid("CUSA00740") == 1);
    assert(bigapp_titleid_valid("CUSA0074") == 0);
    assert(bigapp_titleid_valid("cusa00740") == 0);
    assert(bigapp_titleid_valid(NULL) == 0);
}

/* --- pkgzone: the homebrew name scrape ---------------------------------
 * pkg-zone.com is scraped, not queried, so the parsing is pinned down here
 * against synthetic pages. A layout change upstream must degrade to "no name"
 * (the caller keeps the raw title id), never to a garbled presence. */
static void test_pkgzone(void) {
    char name[128], url[256];

    /* --- which titles are even worth asking about --- */
    /* retail: TMDB already owns these, never spend a request on them */
    assert(pkgzone_wants("CUSA00740") == 0);
    /* retro: deferred, deliberately excluded */
    assert(pkgzone_wants("SLUS20001") == 0);
    assert(pkgzone_wants("SCES50361") == 0);
    assert(pkgzone_wants("ELUA01234") == 0);
    /* The regression that sent SCUS97399 to pkg-zone at 302/500 every poll:
     * SCUS is in the retro tables (ps1 + ps2) and must be excluded here. */
    assert(pkgzone_wants("SCUS97399") == 0);
    /* and the exclusion is delegated to retro_wants(), not re-listed, so the
     * two tables cannot drift apart again */
    assert(retro_wants("SCUS97399") == 1);
    assert(pkgzone_wants("KOEI12345") == 0);
    assert(pkgzone_wants("UCUS12345") == 0);
    /* homebrew prefixes: 4 letters + 5 digits, e.g. LAPY20009 */
    assert(pkgzone_wants("LAPY20009") == 1);
    assert(pkgzone_wants("HT0000001") == 1);
    assert(pkgzone_wants("JBUA20001") == 1);
    /* shape violations: lowercase, wrong length, non-alnum */
    assert(pkgzone_wants("lapy20009") == 0);
    assert(pkgzone_wants("LAPY2000") == 0);
    assert(pkgzone_wants("LAPY200099") == 0);
    assert(pkgzone_wants("LAPY-2009") == 0);
    /* malformed */
    assert(pkgzone_wants("CUSA") == 0);
    assert(pkgzone_wants("") == 0);
    assert(pkgzone_wants(NULL) == 0);

    /* --- a realistic page: boilerplate heading is skipped --- */
    {
        const char *html =
            "<html><head><title>pkg-zone</title></head><body>"
            "<h1>Install HB-Store on your Playstation</h1>"
            "<div class=\"image\" style=\"background-image: "
            "url(/images/LAPY20009/cover.png)\"></div>"
            "<h1>Some Homebrew App</h1>"
            "</body></html>";
        assert(pkgzone_parse(html, strlen(html), "LAPY20009",
                             name, sizeof name, url, sizeof url) == 0);
        assert(strcmp(name, "Some Homebrew App") == 0);
        assert(strcmp(url, "https://pkg-zone.com/images/LAPY20009/cover.png") == 0);
    }

    /* --- boilerplate only: no usable name --- */
    {
        const char *html = "<h1>Install HB-Store on your Playstation</h1>";
        assert(pkgzone_parse(html, strlen(html), "LAPY20009",
                             name, sizeof name, url, sizeof url) == -1);
        assert(name[0] == 0);
    }

    /* --- whitespace and entities are normalised --- */
    {
        const char *html = "<h1>  Retro   Tool &amp; Co  </h1>";
        assert(pkgzone_parse(html, strlen(html), "LAPY20009",
                             name, sizeof name, url, sizeof url) == 0);
        assert(strcmp(name, "Retro Tool & Co") == 0);
    }

    /* --- unclosed / absent tags must not read past the buffer --- */
    {
        const char *html = "<h1>No closing tag here";
        assert(pkgzone_parse(html, strlen(html), "LAPY20009",
                             name, sizeof name, url, sizeof url) == -1);
        const char *nohead = "<body><p>no headings</p></body>";
        assert(pkgzone_parse(nohead, strlen(nohead), "LAPY20009",
                             name, sizeof name, url, sizeof url) == -1);
        /* an empty <h1> must be skipped in favour of the next real one */
        const char *empty_then_real = "<h1></h1><h1>Real Title</h1>";
        assert(pkgzone_parse(empty_then_real, strlen(empty_then_real), "LAPY20009",
                             name, sizeof name, url, sizeof url) == 0);
        assert(strcmp(name, "Real Title") == 0);
    }

    /* --- bad arguments --- */
    assert(pkgzone_parse(NULL, 10, "LAPY20009", name, sizeof name, url, sizeof url) == -1);
    assert(pkgzone_parse("<h1>x</h1>", 11, NULL, name, sizeof name, url, sizeof url) == -1);
    assert(pkgzone_parse("<h1>x</h1>", 11, "LAPY20009", NULL, 0, url, sizeof url) == -1);
    /* title id must be 9 chars: the cover URL is built from it */
    assert(pkgzone_parse("<h1>x</h1>", 11, "SHORT", name, sizeof name, url, sizeof url) == -1);
}

/* --- games_cache: the persistent id -> {name, cover} store -------------
 * Replaces both the config.json titles map and artwork_cache.json. Cover art
 * was never persisted before this, so a reboot lost it and the presence fell
 * back to a pack URL that may not exist. */
static void test_gamecache(void) {
    gamecache_entry_t e[8];
    const int64_t now = 1700000000;

    /* --- id validation --- */
    assert(gamecache_id_valid("CUSA32836") == 1);
    assert(gamecache_id_valid("LAPY20009") == 1);
    assert(gamecache_id_valid("cusa32836") == 0);   /* lowercase */
    assert(gamecache_id_valid("CUSA-3283") == 0);   /* punctuation */
    assert(gamecache_id_valid("CUSA3283612345678") == 0); /* too long */
    assert(gamecache_id_valid("") == 0);
    assert(gamecache_id_valid(NULL) == 0);

    /* --- parse a realistic document --- */
    {
        const char *doc =
            "{\"schema\":1,\"games\":["
            "{\"id\":\"CUSA32836\",\"name\":\"Some Game\",\"art\":\"https://cdn/x.png\",\"at\":1699999000},"
            "{\"id\":\"LAPY20009\",\"name\":\"Homebrew\",\"art\":\"\",\"at\":1699999000}"
            "]}";
        int n = gamecache_parse(doc, strlen(doc), e, 8, now);
        assert(n == 2);
        assert(!strcmp(e[0].id, "CUSA32836"));
        assert(!strcmp(e[0].name, "Some Game"));
        assert(!strcmp(e[0].art, "https://cdn/x.png"));
        assert(!strcmp(e[1].id, "LAPY20009"));
        assert(e[1].art[0] == 0);   /* empty art is allowed */
    }

    /* --- malformed rows are skipped, not fatal --- */
    {
        const char *doc =
            "{\"games\":["
            "{\"id\":\"CUSA32836\",\"name\":\"Good\",\"at\":1699999000},"
            "{\"id\":\"bad-id\",\"name\":\"Bad\",\"at\":1699999000},"
            "{\"id\":\"CUSA11111\"},"
            "{\"name\":\"NoId\",\"at\":1699999000},"
            "\"notanobject\","
            "{\"id\":\"CUSA22222\",\"name\":\"Also Good\",\"at\":1699999000}"
            "]}";
        int n = gamecache_parse(doc, strlen(doc), e, 8, now);
        assert(n == 2);
        assert(!strcmp(e[0].id, "CUSA32836"));
        assert(!strcmp(e[1].id, "CUSA22222"));
    }

    /* --- unusable documents --- */
    assert(gamecache_parse("not json", 8, e, 8, now) == -1);
    assert(gamecache_parse("[]", 2, e, 8, now) == -1);       /* array at top */
    assert(gamecache_parse("{}", 2, e, 8, now) == 0);         /* no games key */
    assert(gamecache_parse(NULL, 10, e, 8, now) == -1);
    assert(gamecache_parse("{}", 2, NULL, 8, now) == -1);
    assert(gamecache_parse("{}", 2, e, 0, now) == -1);

    /* --- staleness: old rows stay on disk; get() ignores them --- */
    {
        char doc[256];
        snprintf(doc, sizeof doc,
            "{\"games\":[{\"id\":\"CUSA32836\",\"name\":\"Old\",\"at\":%lld}]}",
            (long long)(now - GAMECACHE_TTL_SECS - 60));
        assert(gamecache_parse(doc, strlen(doc), e, 8, now) == 1);
        assert(e[0].at == now - GAMECACHE_TTL_SECS - 60);
        /* fresh entry survives */
        snprintf(doc, sizeof doc,
            "{\"games\":[{\"id\":\"CUSA32836\",\"name\":\"New\",\"at\":%lld}]}",
            (long long)(now - 10));
        assert(gamecache_parse(doc, strlen(doc), e, 8, now) == 1);
    }
    /* a missing timestamp parses but is treated as stale by get() */
    {
        const char *doc = "{\"games\":[{\"id\":\"CUSA32836\",\"name\":\"NoAt\"}]}";
        assert(gamecache_parse(doc, strlen(doc), e, 8, now) == 1);
    }

    /* --- round trip, including quotes and backslashes in the name --- */
    {
        gamecache_entry_t in[2], out[8];
        memset(in, 0, sizeof in);
        strcpy(in[0].id, "CUSA32836");
        strcpy(in[0].name, "He said \"hi\" \\ back");
        strcpy(in[0].art, "https://cdn/a.png");
        in[0].at = now;
        strcpy(in[1].id, "LAPY20009");
        strcpy(in[1].name, "Homebrew Tool");
        in[1].at = now;

        char *json = gamecache_serialize(in, 2);
        assert(json != NULL);
        int n = gamecache_parse(json, strlen(json), out, 8, now);
        assert(n == 2);
        assert(!strcmp(out[0].id, "CUSA32836"));
        assert(!strcmp(out[0].name, "He said \"hi\" \\ back"));
        assert(!strcmp(out[0].art, "https://cdn/a.png"));
        assert(!strcmp(out[1].name, "Homebrew Tool"));
        assert(out[1].art[0] == 0);
        free(json);
    }

    /* Control bytes must not leak into the serialized JSON. retro names
     * decode \n and \t escapes, and a raw newline made the file invalid on
     * the next boot, silently dropping every cached title. */
    {
        gamecache_entry_t in[1], out[1];
        char *json;
        memset(in, 0, sizeof in);
        strcpy(in[0].id, "CUSA33333");
        strcpy(in[0].name, "Line\nBreak\tTool");
        in[0].at = now;
        json = gamecache_serialize(in, 1);
        assert(json != NULL);
        assert(strchr(json, '\n') == NULL && strchr(json, '\t') == NULL);
        assert(gamecache_parse(json, strlen(json), out, 1, now) == 1);
        assert(!strcmp(out[0].name, "Line Break Tool"));
        free(json);
    }

    /* --- serialise edges --- */
    assert(gamecache_serialize(NULL, 1) == NULL);
    assert(gamecache_serialize(e, 0) == NULL);

    /* --- `max` really caps --- */
    {
        const char *doc =
            "{\"games\":["
            "{\"id\":\"CUSA00001\",\"name\":\"A\",\"at\":1699999000},"
            "{\"id\":\"CUSA00002\",\"name\":\"B\",\"at\":1699999000},"
            "{\"id\":\"CUSA00003\",\"name\":\"C\",\"at\":1699999000}"
            "]}";
        assert(gamecache_parse(doc, strlen(doc), e, 2, now) == 2);
    }

    /* --- live store: put then get, and art round-trips --- */
    gamecache_reset();
    {
        char nm[64], ar[128];
        assert(gamecache_get("CUSA32836", nm, sizeof nm, ar, sizeof ar) == 0);
        assert(gamecache_put("CUSA32836", "Cached Game",
                             "https://cdn/cover.png", "tmdb") == 1);
        assert(gamecache_get("CUSA32836", nm, sizeof nm, ar, sizeof ar) == 1);
        assert(!strcmp(nm, "Cached Game"));
        assert(!strcmp(ar, "https://cdn/cover.png"));
        /* id echo is not a name */
        assert(gamecache_put("CUSA99999", "CUSA99999", "", "tmdb") == 0);
        /* bad id rejected */
        assert(gamecache_put("bad id", "X", "", "tmdb") == 0);
        /* update replaces */
        assert(gamecache_put("CUSA32836", "Renamed", "", "pkgzone") == 1);
        assert(gamecache_get("CUSA32836", nm, sizeof nm, ar, sizeof ar) == 1);
        assert(!strcmp(nm, "Renamed"));
        /* identical content right after a write is a no-op */
        assert(gamecache_put("CUSA32836", "Renamed", "", "pkgzone") == 0);
        /* a persisted mp: mapping wins over the raw URL on the next read */
        assert(gamecache_set_mp("CUSA32836", "mp:external/x/y.png") == 1);
        assert(gamecache_get("CUSA32836", nm, sizeof nm, ar, sizeof ar) == 1);
        assert(!strcmp(ar, "mp:external/x/y.png"));
        assert(gamecache_set_mp("CUSA32836", "not-mp") == 0);
        assert(gamecache_set_mp("MISSING01", "mp:x") == 0);
    }
    gamecache_reset();
}

/* --- firmware version normalisation ---------------------------------
 * The presence card shows this string, so a bad trim is user-visible.
 * Sony packs the minor as three digits with a trailing sub-patch zero,
 * which is why "13.520.001" has to render as "13.52", not "13.520". */
/* fw.c host stub: the only thing it needs from the console is the
 * firmware string. fw_normalize() itself is pure and fully covered. */
typedef struct {
    unsigned long long reserved;
    char version_string[0x1c];
    unsigned int version;
} fw_info_stub_t;
/* What the stub reports. Overridden by the padded case below so fw_version()
 * can be driven end-to-end through the whole path, not just fw_normalize(). */
static const char *s_stub_fw_string = "13.520.001";
int sceKernelGetSystemSwVersion(fw_info_stub_t *info){
    if(!info) return -1;
    memset(info, 0, sizeof *info);
    snprintf(info->version_string, sizeof info->version_string, "%s",
             s_stub_fw_string);
    return 0;
}

static void test_fw_normalize(void) {
    static const struct { const char *in; const char *want; } cases[] = {
        { "13.520.001", "13.52" },
        { "13.520",     "13.52" },
        { "13.52",      "13.52" },
        { "9.000",      "9.00" },
        { "9.000.001",  "9.00" },
        { "8.500",      "8.50" },
        { "11.020",     "11.02" },
        { "12.000",     "12.00" },
        { "5.050",      "5.05" },
        { "7.550",      "7.55" },
        { "13",         "13" },      /* no minor: left alone */
        /* Padded by Sony on some firmware -- 9.00 sends " 9.008.031",
         * verified on console 2026-10-05. digits_ok() rejected the space and
         * every poll logged "unrecognised version string", so the presence
         * fell back to "Firmware unknown". */
        { " 9.008.031", "9.00" },
        { "  9.000",    "9.00" },    /* more than one space */
        { " 5.050.123", "5.05" },    /* padded single-digit major */
        { "\t9.008.031", "9.00" },   /* tab padding */
        { "9.008.031 ", "9.00" },    /* trailing padding */
    };
    for (unsigned i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        char v[32];
        snprintf(v, sizeof v, "%s", cases[i].in);
        fw_normalize(v);
        assert(!strcmp(v, cases[i].want));
    }
    /* end-to-end: fw_version() must render "13.52" for the console the
     * stub above describes -- exactly what the presence card shows. */
    {
        char fw[16];
        assert(fw_version(fw, sizeof fw) == 0);
        assert(!strcmp(fw, "13.52"));
    }
    /* The regression that reported "unrecognised version string" forever on
     * 9.00. fw_version() normalises before validating, so a padded string
     * reaches digits_ok() trimmed. */
    {
        static const struct { const char *raw; const char *want; } pad[] = {
            { " 9.008.031", "9.00" },
            { "9.008.031",  "9.00" },
            { " 5.050.123", "5.05" },
            { " 13.520.001", "13.52" },
        };
        for (unsigned i = 0; i < sizeof pad / sizeof pad[0]; i++) {
            char fw[16];
            s_stub_fw_string = pad[i].raw;
            assert(fw_version(fw, sizeof fw) == 0);
            assert(!strcmp(fw, pad[i].want));
        }
        s_stub_fw_string = "13.520.001";
    }
    /* Still rejects genuine garbage rather than passing it through. */
    {
        char fw[16];
        s_stub_fw_string = "not-a-version";
        assert(fw_version(fw, sizeof fw) != 0);
        assert(!strcmp(fw, "unknown"));
        s_stub_fw_string = "13.520.001";
    }
}

/* --- NPXS is system; app/media ids resolve like anything else -------
 * Console klog 2026-10-03: NPXS20001 backs SceShellUI plus
 * SecureUIProcess/SecureWebProcess and is where every AppFocusChanged
 * event starts, so it must never be posted as a game. ItemzFlow is
 * ITEM00001, a different namespace, and does resolve. */
static void test_bigapp_namespaces(void) {
    char out[16];

    /* NPXS is never a playing game. */
    assert(bigapp_classify(1, 0, "NPXS20001", out, sizeof out) == BIGAPP_SYSTEM);
    assert(bigapp_classify(1, 0, "NPXS21002", out, sizeof out) == BIGAPP_SYSTEM);
    assert(bigapp_classify(1, 0, "NPXS10001", out, sizeof out) == BIGAPP_SYSTEM);
    assert(out[0] == 0); /* nothing written for a system id */

    /* App and media containers post normally. */
    assert(bigapp_classify(1, 0, "ITEM00001", out, sizeof out) == BIGAPP_GAME);
    assert(!strcmp(out, "ITEM00001"));
    assert(bigapp_classify(1, 0, "CUSA00127", out, sizeof out) == BIGAPP_GAME);
    assert(bigapp_classify(1, 0, "LAPY20009", out, sizeof out) == BIGAPP_GAME);
    assert(bigapp_classify(1, 0, "CUSA03887", out, sizeof out) == BIGAPP_GAME);
}


/* --- asset keys land in the right fields ---------------------------
 * Every parameter here is const char*, so a mis-ordered signature still
 * compiles: it shipped once and put "ps_logo_full" in the details line.
 * Assert on values, not on arity. */
static void test_activity_assets(void) {
    /* home: idle asset as large image, no badge */
    jl_val_t *a = discord_build_activity("Firmware 13.52", "PlayStation 4",
        "Idling on Home Menu", "ps_logo_full", NULL,
        "home", "app", NULL, NULL, NULL, 0, "");
    assert(a);
    const jl_val_t *v = jl_obj_get(a, "details");
    assert(v && v->type == JL_STRING && !strcmp(v->str, "Idling on Home Menu"));
    const jl_val_t *as = jl_obj_get(a, "assets");
    assert(as && as->type == JL_OBJECT);
    v = jl_obj_get(as, "large_image");
    assert(v && v->type == JL_STRING && !strcmp(v->str, "ps_logo_full"));
    assert(jl_obj_get(as, "small_image") == NULL); /* no badge on home */
    jl_free(a);

    /* playing: game icon large, playing asset as the badge */
    a = discord_build_activity("Firmware 13.52", "Some Game",
        "Playing on PlayStation 4", NULL, "ps_logo_blue",
        "CUSA11995", "app", NULL, NULL, NULL, 1700000000LL, "");
    assert(a);
    v = jl_obj_get(a, "details");
    assert(v && v->type == JL_STRING && !strcmp(v->str, "Playing on PlayStation 4"));
    as = jl_obj_get(a, "assets");
    assert(as && as->type == JL_OBJECT);
    v = jl_obj_get(as, "large_image");
    assert(v && v->type == JL_STRING && !strcmp(v->str, "cusa11995"));
    v = jl_obj_get(as, "small_image");
    assert(v && v->type == JL_STRING && !strcmp(v->str, "ps_logo_blue"));
    jl_free(a);

    /* an empty key falls back rather than sending an empty asset */
    a = discord_build_activity("Firmware 13.52", "Some Game",
        "Playing on PlayStation 4", "", "",
        "CUSA11995", "app", NULL, NULL, NULL, 0, "");
    assert(a);
    as = jl_obj_get(a, "assets");
    assert(as && as->type == JL_OBJECT);
    assert(jl_obj_get(as, "small_image") == NULL);
    jl_free(a);

    /* A configured cover URL that has not resolved must NOT fall back to a
     * title-id asset key: an uploaded-key miss drops the whole activity, so
     * the old fallback removed presence whenever external-assets was down.
     * Sending no art keeps the game tile visible. */
    art_cache_clear();
    a = discord_build_activity("Firmware 13.52", "Some Game",
        "Playing on PlayStation 4", NULL, NULL,
        "CUSA11995", "app", NULL, "https://example.com/unresolved.png",
        NULL, 0, "");
    assert(a);
    as = jl_obj_get(a, "assets");
    assert(as == NULL || jl_obj_get(as, "large_image") == NULL);
    jl_free(a);

    /* Same rule when the configured URL produced the pack fallback. */
    a = discord_build_activity("Firmware 13.52", "Some Game",
        "Playing on PlayStation 4", NULL, NULL,
        "CUSA11995", "app", "https://example.com/pack/", NULL,
        NULL, 0, "");
    assert(a);
    as = jl_obj_get(a, "assets");
    assert(as == NULL || jl_obj_get(as, "large_image") == NULL);
    jl_free(a);
}

/* --- retro: prefix routing and the entry extractor -----------------
 * Pure logic only; the fetch half is payload-only. Driven against the real
 * index shapes verified on console 2026-10-05: compact, BOM-free, a bare
 * array of flat {"id","name","cover"} entries. */
static void test_retro(void) {
    unsigned char c[3];

    /* PS1-only prefixes: one file, no wasted fetches */
    assert(retro_wants("SND00123") == 1);
    assert(retro_candidates_for("SND00123", c) == 1 && c[0] == 0);
    /* LSD is a 3-character prefix: comparing a fixed 4 bytes would miss it */
    assert(retro_wants("LSD00123") == 1);
    assert(retro_candidates_for("LSD00123", c) == 1 && c[0] == 0);
    /* PS2-only */
    assert(retro_wants("KOEI12345") == 1);
    assert(retro_candidates_for("KOEI12345", c) == 1 && c[0] == 1);
    /* PSP-only */
    assert(retro_wants("UCUS12345") == 1);
    assert(retro_candidates_for("UCUS12345", c) == 1 && c[0] == 2);

    /* shared prefixes: SLPM and PBPX are in both PS1 and PS2, and must
     * come out in check order so the first hit is deterministic */
    assert(retro_candidates_for("SLPM55265", c) == 2);
    assert(c[0] == 0 && c[1] == 1);
    assert(retro_candidates_for("PBPX12345", c) == 2);
    assert(c[0] == 0 && c[1] == 1);

    /* retail and unknowns are never routed here */
    assert(retro_wants("CUSA32836") == 0);
    assert(retro_wants("LAPY20009") == 0);
    assert(retro_wants("ITEM00001") == 0);
    assert(retro_wants("") == 0);
    assert(retro_wants(NULL) == 0);

    /* extraction from a real index body */
    {
        const char *body =
            "[{\"id\":\"SLPM87404\",\"name\":\"A GAME\",\"cover\":\"https://x/a.jpg\"},"
             "{\"id\":\"SLPM55146\",\"name\":\"OTHER GAME\",\"cover\":\"https://x/b.jpg\"}]";
        char name[RETRO_MAX_NAME], url[RETRO_MAX_URL];
        assert(retro_extract(body, strlen(body), "\"id\":\"SLPM55146\"",
                             name, sizeof name, url, sizeof url) == 1);
        assert(!strcmp(name, "OTHER GAME"));
        assert(!strcmp(url, "https://x/b.jpg"));
        /* a miss must not invent a name */
        name[0] = 'X';
        assert(retro_extract(body, strlen(body), "\"id\":\"NOPE00000\"",
                             name, sizeof name, url, sizeof url) == 0);
        assert(name[0] == 0);
    }

    /* The streaming window is a fixed byte buffer, not a C string. An
     * exact-size heap copy makes the old strstr() an ASAN heap-overflow;
     * the bounded find must stay inside len. */
    {
        const char *frag =
            "[{\"id\":\"SLPM87404\",\"name\":\"NO NUL\",\"cover\":\"https://x/n.jpg\"}]";
        size_t n = strlen(frag);
        char *nb = malloc(n);
        assert(nb);
        memcpy(nb, frag, n);
        char name[RETRO_MAX_NAME], url[RETRO_MAX_URL];
        assert(retro_extract(nb, n, "\"id\":\"SLPM87404\"",
                             name, sizeof name, url, sizeof url) == 1);
        assert(!strcmp(name, "NO NUL"));
        assert(!strcmp(url, "https://x/n.jpg"));
        free(nb);
    }

    /* names carry quotes and backslashes; raw bytes would show \\ and \" */
    {
        const char *body =
            "[{\"id\":\"ABCD12345\",\"name\":\"He said \\\"hi\\\" \\\\ away\",\"cover\":\"\"}]";
        char name[RETRO_MAX_NAME], url[RETRO_MAX_URL];
        assert(retro_extract(body, strlen(body), "\"id\":\"ABCD12345\"",
                             name, sizeof name, url, sizeof url) == 1);
        assert(!strcmp(name, "He said \"hi\" \\ away"));
        assert(url[0] == 0);              /* empty cover is valid, not fatal */
    }

    /* the 164-char name in ps2.json must survive intact */
    {
        const char *body =
            "[{\"id\":\"SLPM00001\",\"name\":\"Kidou Senshi Gundam Giren no Yabou - Zeon Dokuritsu Sensouden & Kidou Senshi Gundam Giren no Yabou - Zeon Dokuritsu Sensouden - Kouryaku Shireisho (Gundam The Best)\",\"cover\":\"https://x/g.jpg\"}]";
        char name[RETRO_MAX_NAME], url[RETRO_MAX_URL];
        assert(retro_extract(body, strlen(body), "\"id\":\"SLPM00001\"",
                             name, sizeof name, url, sizeof url) == 1);
        assert(strlen(name) == 164);
        assert(strstr(name, "Gundam The Best") != NULL);
    }

    /* D-08: the local file path resolves before any network fetch, including
     * a match that spans the reader's chunk boundary. */
    {
        char dir[64], path[96];
        assert(make_tmpdir(dir, sizeof dir) == 0);
        snprintf(path, sizeof path, "%s/ps1.json", dir);
        FILE *f = fopen(path, "wb");
        assert(f);
        fputs("[{\"id\":\"SLPM87404\",\"name\":\"LOCAL GAME\",\"cover\":\"https://x/l.jpg\"}]", f);
        fclose(f);
        char name[RETRO_MAX_NAME], url[RETRO_MAX_URL];
        assert(retro_local_scan(path, "\"id\":\"SLPM87404\"",
                                name, sizeof name, url, sizeof url) == 1);
        assert(!strcmp(name, "LOCAL GAME"));
        assert(!strcmp(url, "https://x/l.jpg"));
        assert(retro_local_scan(path, "\"id\":\"NOPE00000\"",
                                name, sizeof name, url, sizeof url) == 0);
        assert(name[0] == 0);
        remove(path);
        assert(retro_local_scan(path, "\"id\":\"SLPM87404\"",
                                name, sizeof name, url, sizeof url) == -1);

        /* A match that spans the reader's chunk boundary must survive the
         * tail carry-over; a fresh index is 1.2 MB, so this is the normal
         * case, not an edge case. */
        f = fopen(path, "wb");
        assert(f);
        for(int i = 0; i < 66200; i++) fputc(' ', f);
        fputs("[{\"id\":\"SLPM55146\",\"name\":\"BOUNDARY GAME\",\"cover\":\"\"}]", f);
        fclose(f);
        assert(retro_local_scan(path, "\"id\":\"SLPM55146\"",
                                name, sizeof name, url, sizeof url) == 1);
        assert(!strcmp(name, "BOUNDARY GAME"));
        assert(url[0] == 0);
        remove(path);
    }
}

/* --- visibility flags + homebrew classification -------------------
 * The four show_* flags are a daemon-side decision, so what is testable
 * here is that they default to on and that is_homebrew_id puts the right
 * ids in the right bucket -- in particular that retro is never homebrew. */
static void test_show_flags(void) {
    cfg_t c;
    cfg_defaults(&c);
    /* all on by default: a config that predates the keys keeps today's
     * behaviour rather than silently hiding everything */
    assert(c.show_firmware == 1);
    assert(c.show_logo     == 1);
    assert(c.show_idle     == 1);
    assert(c.show_media    == 1);
    assert(c.show_homebrew == 1);

    /* is_homebrew_id: the pkg-zone namespace */
    assert(is_homebrew_id("LAPY20009") == 1);   /* payload-dir homebrew */
    assert(is_homebrew_id("HT0000001") == 1);
    assert(is_homebrew_id("ITEM00001") == 1);   /* app container */

    /* retail and system are never homebrew */
    assert(is_homebrew_id("CUSA32836") == 0);
    assert(is_homebrew_id("NPXS20001") == 0);

    /* the point of the exclusion: retro must survive show_homebrew:false */
    assert(is_homebrew_id("SLPM55146") == 0);
    assert(is_homebrew_id("UCUS12345") == 0);
    assert(is_homebrew_id("KOEI12345") == 0);

    /* degenerate input */
    assert(is_homebrew_id(NULL) == 0);
    assert(is_homebrew_id("") == 0);
    assert(is_homebrew_id("AB") == 0);

    char dir[64], path[96];
    assert(make_tmpdir(dir, sizeof dir) == 0);
    snprintf(path, sizeof path, "%s/cfg.json", dir);
    FILE *f = fopen(path, "wb");
    assert(f);
    fputs("{\"token\":\"t\",\"show_logo\":false}", f);
    fclose(f);
    assert(cfg_load(path, &c) == 0 && c.show_logo == 0);
    assert(cfg_save(path, &c) == 0);
    cfg_t reloaded;
    assert(cfg_load(path, &reloaded) == 0 && reloaded.show_logo == 0);
    remove(path);
}

/* D-12: the token check must explain every rejection. */
static void test_token_shape(void) {
    char why[160];
    const char *good = "test.token.fixture.value.for.host.tests.only";
    assert(token_reason(good, why, sizeof why) == 0);
    assert(why[0] == 0);
    /* The format is undocumented; 3+ non-empty segments are accepted so a
     * real token with an extra segment is never refused. */
    {
        char extra[128];
        snprintf(extra, sizeof extra, "%s.extra", good);
        assert(token_reason(extra, why, sizeof why) == 0);
    }

    assert(token_reason("", why, sizeof why) == -1);
    assert(strstr(why, "empty") != NULL);
    assert(token_reason(NULL, why, sizeof why) == -1);

    assert(token_reason("SET_ME", why, sizeof why) == -1);
    assert(strstr(why, "placeholder") != NULL);

    assert(token_reason("short.token.here", why, sizeof why) == -1);
    assert(strstr(why, "too short") != NULL);

    char longs[220];
    memset(longs, 'a', sizeof longs - 1);
    longs[sizeof longs - 1] = 0;
    assert(token_reason(longs, why, sizeof why) == -1);
    assert(strstr(why, "too long") != NULL);

    /* two segments */
    char nodots[80];
    snprintf(nodots, sizeof nodots, "%s.%s",
             "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
             "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb");
    assert(token_reason(nodots, why, sizeof why) == -1);
    assert(strstr(why, "dot-separated") != NULL);

    /* leading dot -> empty first segment, reported as such */
    char lead[80];
    snprintf(lead, sizeof lead, ".%s", good);
    assert(token_reason(lead, why, sizeof why) == -1);
    assert(strstr(why, "empty token segment") != NULL);

    /* illegal character names the position */
    char bad[80];
    snprintf(bad, sizeof bad, "%s", good);
    bad[5] = '!';
    assert(token_reason(bad, why, sizeof why) == -1);
    assert(strstr(why, "illegal character") != NULL);
    assert(strstr(why, "position 5") != NULL);
}

/* D-09: homebrew titles no index carries still get a human name. */
static void test_builtin_titles(void) {
    assert(builtin_title_name("ELYS00001") != NULL);
    assert(strcmp(builtin_title_name("ELYS00001"), "Elysium") == 0);
    assert(builtin_title_name("CUSA00001") == NULL);
    assert(builtin_title_name("") == NULL);
    assert(builtin_title_name(NULL) == NULL);
}

/* D-18: GoldHEN's kernel-log banner is the only version source a payload has. */
static void test_goldhen(void) {
    int v[4], min[4];
    assert(goldhen_parse_ver("2.4b18.10", v) == 0);
    assert(v[0] == 2 && v[1] == 4 && v[2] == 18 && v[3] == 10);
    assert(goldhen_parse_ver(GOLDHEN_MIN, min) == 0);
    assert(goldhen_ver_cmp(v, min) == 0);

    assert(goldhen_parse_ver("2.4b18.8", v) == 0);
    assert(goldhen_ver_cmp(v, min) < 0);
    assert(goldhen_parse_ver("2.4b19.0", v) == 0);
    assert(goldhen_ver_cmp(v, min) > 0);
    assert(goldhen_parse_ver("junk", v) == -1);

    char out[32];
    const char *log =
        "boot\nGoldHEN v2.4b18.8 loaded\n"
        "later: GoldHEN v2.4b18.10 ready\n";
    assert(goldhen_version_from_log(log, out, sizeof out) == 0);
    assert(strcmp(out, "2.4b18.10") == 0);
    assert(goldhen_version_from_log("no banner here", out, sizeof out) == -1);
}

/* D-04: a lock from before this boot is stale even if its pid was recycled. */
static void test_lock_stale(void) {
    /* previous-boot mtime: reclaim regardless of pid liveness */
    assert(lock_entry_stale(1000, 2000, 42, 1) == 1);
    /* same boot, live holder: stand down */
    assert(lock_entry_stale(2500, 2000, 42, 1) == 0);
    /* same boot, dead holder: reclaim */
    assert(lock_entry_stale(2500, 2000, 42, 0) == 1);
    /* no pid in the file: reclaim */
    assert(lock_entry_stale(2500, 2000, 0, 1) == 1);
    /* boot time unknown: fall back to liveness only */
    assert(lock_entry_stale(1000, 0, 42, 1) == 0);
    assert(lock_entry_stale(1000, 0, 42, 0) == 1);

    /* Heartbeat: a live pid that stopped touching the lock is wedged, not
     * busy. Only a gap strictly beyond the threshold reclaims, and a
     * backwards clock or unknown mtime never does. */
    assert(lock_heartbeat_stale(1000, 1060, 600) == 0);
    assert(lock_heartbeat_stale(1000, 1600, 600) == 0);
    assert(lock_heartbeat_stale(1000, 1601, 600) == 1);
    assert(lock_heartbeat_stale(1000, 10000, 600) == 1);
    assert(lock_heartbeat_stale(0, 10000, 600) == 0);   /* unknown mtime */
    assert(lock_heartbeat_stale(5000, 1000, 600) == 0); /* clock went back */
    assert(lock_heartbeat_stale(1000, 1601, 0) == 0);   /* disabled */

    /* Payload Guest runs every load with the same pid (46), so a reload
     * looks like "our own stale lock". A per-instance token tells a live
     * peer from a leftover: a fresh lock with another token must win. */
    assert(lock_entry_reclaim(2500, 2000, 2501, 600, 1, 0xAAAA, 0xBBBB, 1) == 0);
    assert(lock_entry_reclaim(2500, 2000, 2501, 600, 1, 0xAAAA, 0xAAAA, 0) == 0);
    assert(lock_entry_reclaim(2500, 2000, 3101, 600, 1, 0xAAAA, 0xBBBB, 1) == 1);
    assert(lock_entry_reclaim(1000, 2000, 2501, 600, 1, 0xAAAA, 0xBBBB, 1) == 1);
    /* the configured stale window is honoured */
    assert(lock_entry_reclaim(2500, 2000, 2619, 120, 1, 0xAAAA, 0xBBBB, 1) == 0);
    assert(lock_entry_reclaim(2500, 2000, 2621, 120, 1, 0xAAAA, 0xBBBB, 1) == 1);
    /* legacy pid-only locks keep the old liveness rule */
    assert(lock_entry_reclaim(2500, 2000, 2501, 600, 0, 0, 0xBBBB, 1) == 0);
    assert(lock_entry_reclaim(2500, 2000, 2501, 600, 0, 0, 0xBBBB, 0) == 1);

    /* Ownership: same pid is not enough. The daemon must not release or
     * keep serving on a lock another instance wrote. */
    assert(lock_token_owned(2, 46, 0xAAAA, 46, 0xAAAA) == 1);
    assert(lock_token_owned(2, 46, 0xBBBB, 46, 0xAAAA) == 0);
    assert(lock_token_owned(2, 46, 0xAAAA, 47, 0xAAAA) == 0);
    assert(lock_token_owned(1, 46, 0, 46, 0xAAAA) == 1);      /* legacy */
    assert(lock_token_owned(1, 46, 0, 47, 0xAAAA) == 0);
    assert(lock_token_owned(0, 0, 0, 46, 0xAAAA) == 0);
}

/* D-14: a pasted token arrives quoted/padded, or bare and breaks JSON. */
static void test_cfg_token_parsing(void) {
    char dir[64], path[96];
    assert(make_tmpdir(dir, sizeof dir) == 0);
    snprintf(path, sizeof path, "%s/cfg.json", dir);
    cfg_t c;
    const char *tok = "test.token.fixture.value.for.host.tests.only";

    FILE *f = fopen(path, "wb");
    assert(f);
    fprintf(f, "{\"token\":\"  \\\"%s\\\"  \"}", tok);
    fclose(f);
    assert(cfg_load(path, &c) == 0);
    assert(strcmp(c.token, tok) == 0);

    /* bare value: repair the one field instead of resetting everything */
    f = fopen(path, "wb");
    assert(f);
    fprintf(f, "{\"token\": %s , \"poll_interval_s\": 7}", tok);
    fclose(f);
    assert(cfg_load(path, &c) == 0);
    assert(strcmp(c.token, tok) == 0);
    assert(c.poll_interval_s == 7);

    /* single quotes around the token are trimmed too */
    f = fopen(path, "wb");
    assert(f);
    fprintf(f, "{\"token\":\"'%s'\"}", tok);
    fclose(f);
    assert(cfg_load(path, &c) == 0);
    assert(strcmp(c.token, tok) == 0);
}

static void test_notify(void){
#ifdef NOTIFY_TESTABLE
    /* "Connected to Discord" was throttled to once per 5 minutes, so a
     * reconnecting daemon popped the same toast all day (testers reported it
     * every few minutes). It is once per boot now: the once-map must make the
     * second and third call silent. */
    notify_test_reset();
    notify_once_boot("connected", "Connected to Discord");
    notify_once_boot("connected", "Connected to Discord");
    notify_once_boot("connected", "Connected to Discord");
    assert(notify_test_count() == 1);
    /* Keys are independent: a different once-key still fires. */
    notify_once_boot("boot", "orbisRPC loaded");
    assert(notify_test_count() == 2);
    /* The failure toast is rate-limited, not once-only: the first call fires,
     * an immediate repeat inside the window is dropped. */
    notify_throttled("connect", "Cannot reach Discord - retrying", 300);
    assert(notify_test_count() == 3);
    notify_throttled("connect", "Cannot reach Discord - retrying", 300);
    assert(notify_test_count() == 3);
    notify_test_reset();
    assert(notify_test_count() == 0);
#endif
}

int main(void) {
    /* First: pure parsing, no I/O, no fixtures. The pre-existing
     * test_appdb failure aborts the binary, so anything listed after it
     * would never run. */
    test_activity_assets();
    test_bigapp();
    test_retro();
    test_show_flags();
    test_bigapp_namespaces();
    test_pkgzone();
    test_gamecache();
    test_fw_normalize();
    test_procwalk();
    test_procwalk_find_and_pid();
    test_json();
    test_gateway_op_spoof();
    test_identity_consistency();
    test_identify_capabilities();
    test_resume_flow();
    test_presence_send();
    test_json_oom_safe();
    test_json_hostile();
    test_tmdb();
    test_updater();
    test_updater_http();
    test_diag();
    assert(test_replay() == 0);
    test_sfo();
    test_base64();
    test_art_parse();
    test_health_safe_mode();
    test_health_stage_activate();
    test_manifest();
    test_manifest_sig();
    test_cfg_titles();
    test_cfg_title_rules();
    test_cfg_permissions();
    test_cfg_preserves_unknown();
    test_cfg_migrate();
    test_cfg_clamp();
    test_sched();
    test_notify();
    test_token_shape();
    test_builtin_titles();
    test_goldhen();
    test_lock_stale();
    test_cfg_token_parsing();
    test_log_rotation();
    test_cfg_learn();
    test_installer_cfg();
    test_appdb();
    test_discord_builder();
    puts("utility tests passed");
    return 0;
}
