/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 SirHumza and CyberMask367 - see NOTICE for attribution terms.
 */
/* test_ws.c - host tests for the WebSocket frame parser.
 *
 * A scripted byte stream is fed through a tls_read() stub so the production
 * parser (ws.c) is exercised: single frames, fragmentation, partial reads,
 * interleaved control frames, and protocol violations. No socket is opened.
 */
#define ORBISRPC_SDK_PAYLOAD 1
#include "../orbisrpc/ws.h"
#include "../orbisrpc/log.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

/* --- TLS stub: the parser only needs tls_read; the rest must link. --- */
typedef struct tls_ctx { int unused; } tls_ctx_t;

static const unsigned char *g_in;
static size_t g_in_len, g_in_pos, g_chunk;   /* g_chunk 0 = no cap */

static int g_tls_freed;

tls_ctx_t *tls_start(int fd, const char *host){ (void)fd; (void)host; return NULL; }
void tls_free(tls_ctx_t *t){ if(t) g_tls_freed++; }
int tls_write(tls_ctx_t *t, const void *b, size_t n){ (void)t; (void)b; return (int)n; }int tls_read(tls_ctx_t *t, void *b, size_t cap){
    (void)t;
    size_t avail = g_in_len - g_in_pos;
    if(avail == 0) return 0;
    size_t n = avail < cap ? avail : cap;
    if(g_chunk && n > g_chunk) n = g_chunk;
    memcpy(b, g_in + g_in_pos, n);
    g_in_pos += n;
    return (int)n;
}

/* ws_connect() polls this weak symbol to abort a slow handshake. The parser
 * tests never connect, but Mach-O needs a definition to link. */
int daemon_stop_requested(void){ return 0; }

static ws_t W;

static void begin(const void *data, size_t n, size_t chunk){
    memset(&W, 0, sizeof W);
    W.connected = 1;
    W.rcap = 32;
    W.rbuf = (unsigned char *)malloc(W.rcap);
    assert(W.rbuf);
    W.tls = (void *)&W;
    g_in = (const unsigned char *)data;
    g_in_len = n;
    g_in_pos = 0;
    g_chunk = chunk;
}

static void end(void){
    ws_close(&W);
    g_in = NULL;
    g_in_len = g_in_pos = 0;
    g_chunk = 0;
}

/* Server frames are unmasked; lengths stay under 65536 in these tests. */
static size_t frame(unsigned char *o, int fin, int op, const void *p, size_t n){
    size_t h;
    o[0] = (unsigned char)((fin ? 0x80 : 0) | op);
    if(n < 126){ o[1] = (unsigned char)n; h = 2; }
    else { o[1] = 126; o[2] = (unsigned char)(n >> 8); o[3] = (unsigned char)n; h = 4; }
    if(n) memcpy(o + h, p, n);
    return h + n;
}

static void test_single_frame(void){
    unsigned char f[64];
    size_t n = frame(f, 1, 1, "hello", 5);
    begin(f, n, 0);
    char buf[16]; int op = -1, fin = -1;
    assert(ws_recv_frame(&W, buf, sizeof buf, &op, &fin) == 5);
    assert(op == 1 && fin == 1 && !strcmp(buf, "hello"));
    assert(ws_recv_frame(&W, buf, sizeof buf, &op, &fin) == 0);
    end();
}

/* The caller's buffer is much smaller than the payload (READY is big); the
 * prefix is kept and the rest drained, never returned as garbage. */
static void test_truncated_payload(void){
    unsigned char f[160];
    char payload[100];
    memset(payload, 'x', sizeof payload);
    size_t n = frame(f, 1, 1, payload, sizeof payload);
    begin(f, n, 0);
    char buf[16]; int op = -1, fin = -1;
    assert(ws_recv_frame(&W, buf, sizeof buf, &op, &fin) == 15);
    assert(op == 1 && fin == 1);
    assert(strlen(buf) == 15);
    assert(ws_recv_frame(&W, buf, sizeof buf, &op, &fin) == 0);
    end();
}

/* A split message must be delivered whole. Returning the first fragment
 * (the old behaviour) made READY unparseable and stalled auth. */
static void test_fragmented_message(void){
    unsigned char f[128];
    size_t n = frame(f, 0, 1, "{\"op\":0,", 8);
    n += frame(f + n, 1, 0, "\"t\":\"READY\"}", 13);
    begin(f, n, 0);
    char buf[64]; int op = 0, fin = 0;
    assert(ws_recv_frame(&W, buf, sizeof buf, &op, &fin) == 21);
    assert(op == 1 && fin == 1);
    assert(!strcmp(buf, "{\"op\":0,\"t\":\"READY\"}"));
    assert(ws_recv_frame(&W, buf, sizeof buf, &op, &fin) == 0);
    end();
}

/* One byte per tls_read: the parser must not need a coalesced stream. */
static void test_fragmented_partial_reads(void){
    unsigned char f[128];
    size_t n = frame(f, 0, 2, "AAAA", 4);
    n += frame(f + n, 1, 0, "BBBB", 4);
    begin(f, n, 1);
    char buf[32]; int op = 0, fin = 0;
    assert(ws_recv_frame(&W, buf, sizeof buf, &op, &fin) == 8);
    assert(op == 2 && fin == 1 && !strcmp(buf, "AAAABBBB"));
    end();
}

/* RFC 6455 5.4: control frames may sit between fragments. */
static void test_control_between_fragments(void){
    unsigned char f[128];
    size_t n = frame(f, 0, 1, "ab", 2);
    n += frame(f + n, 1, 9, "P", 1);
    n += frame(f + n, 1, 0, "cd", 2);
    begin(f, n, 0);
    char buf[32]; int op = 0, fin = 0;
    assert(ws_recv_frame(&W, buf, sizeof buf, &op, &fin) == 1);
    assert(op == 9 && !strcmp(buf, "P"));
    assert(ws_recv_frame(&W, buf, sizeof buf, &op, &fin) == 4);
    assert(op == 1 && fin == 1 && !strcmp(buf, "abcd"));
    end();
}

static void test_continuation_without_start(void){
    unsigned char f[64];
    size_t n = frame(f, 1, 0, "orphan", 6);
    begin(f, n, 0);
    char buf[32]; int op = 0, fin = 0;
    assert(ws_recv_frame(&W, buf, sizeof buf, &op, &fin) == -2);
    end();
}

static void test_new_message_inside_fragment(void){
    unsigned char f[128];
    size_t n = frame(f, 0, 1, "first", 5);
    n += frame(f + n, 1, 1, "second", 6);   /* new text frame, not opcode 0 */
    begin(f, n, 0);
    char buf[32]; int op = 0, fin = 0;
    assert(ws_recv_frame(&W, buf, sizeof buf, &op, &fin) == -2);
    end();
}

/* Closing mid-message must release the reassembly buffer and reset state. */
static void test_close_mid_fragment(void){
    unsigned char f[32];
    size_t n = frame(f, 0, 1, "abc", 3);
    begin(f, n, 0);
    char buf[16]; int op = 0, fin = 0;
    assert(ws_recv_frame(&W, buf, sizeof buf, &op, &fin) == 0);
    assert(W.in_frag == 1);
    end();
    assert(W.in_frag == 0);
    assert(W.frag == NULL);
}

/* Reserved control opcodes (0xB-0xF) are a protocol error, not frames to
 * hand to the caller. */
static void test_reserved_control_opcode(void){
    unsigned char f[32];
    size_t n = frame(f, 1, 0xB, "x", 1);
    begin(f, n, 0);
    char buf[16]; int op = 0, fin = 0;
    assert(ws_recv_frame(&W, buf, sizeof buf, &op, &fin) == -2);
    end();
}

/* Zero-length message is consumed and reported as zero bytes, then the next
 * frame parses normally: an empty frame must not wedge the reader. */
static void test_empty_frame(void){
    unsigned char f[64];
    size_t n = frame(f, 1, 1, NULL, 0);
    n += frame(f + n, 1, 1, "ok", 2);
    begin(f, n, 0);
    char buf[16]; int op = 0, fin = 0;
    assert(ws_recv_frame(&W, buf, sizeof buf, &op, &fin) == 0);
    assert(op == 1 && fin == 1);
    assert(ws_recv_frame(&W, buf, sizeof buf, &op, &fin) == 2);
    assert(!strcmp(buf, "ok"));
    end();
}

/* The Upgrade request is the client's first identity claim; the UA must be
 * the same tuple IDENTIFY and REST use, and a native client sends no Origin. */
static void test_handshake_headers(void){
    char req[1024];
    const char *ua = "Mozilla/5.0 (Windows NT 10.0; Win64; x64) "
                     "discord/1.0.9059 Chrome/148.0.7778.280 "
                     "Electron/42.11.18 Safari/537.36";
    size_t n = ws_make_handshake(req, sizeof req, "gateway.discord.gg",
                                 "/?v=10&encoding=json", "dGhlIHNhbXBsZQ==", ua);
    assert(n > 0 && n == strlen(req));
    assert(strstr(req, "GET /?v=10&encoding=json HTTP/1.1\r\n") == req);
    assert(strstr(req, "Host: gateway.discord.gg\r\n") != NULL);
    assert(strstr(req, "Upgrade: websocket\r\n") != NULL);
    assert(strstr(req, "User-Agent: ") != NULL);
    assert(strstr(req, ua) != NULL);      /* byte-for-byte, not a second copy */
    assert(strstr(req, "Origin:") == NULL);
    assert(strstr(req, "Sec-WebSocket-Key: dGhlIHNhbXBsZQ==\r\n") != NULL);
    assert(strstr(req, "Sec-WebSocket-Version: 13\r\n") != NULL);
    /* Too small a buffer must fail, never emit a truncated request. */
    assert(ws_make_handshake(req, 32, "gateway.discord.gg", "/",
                             "dGhlIHNhbXBsZQ==", ua) == 0);
    /* NULL UA falls back to the honest daemon string, not the desktop one. */
    n = ws_make_handshake(req, sizeof req, "h", "/", "k", NULL);
    assert(n > 0 && strstr(req, "User-Agent: orbisRPC\r\n") != NULL);
}

/* READY can be much larger than the old 2 KB stack buffer. Heap mode must
 * return every byte, including session fields near the end. */
static void test_alloc_full_message(void){
    size_t n = 5000;
    unsigned char *f = (unsigned char *)malloc(n + 8);
    char *payload = (char *)malloc(n);
    assert(f && payload);
    memset(payload, 'a', n);
    payload[0] = '{';
    payload[n - 1] = '}';
    size_t total = frame(f, 1, 1, payload, n);
    begin(f, total, 0);
    char *out = NULL; size_t len = 0; int op = 0, fin = 0;
    assert(ws_recv_alloc(&W, &out, &len, &op, &fin) == (int)n);
    assert(out && len == n && op == 1 && fin == 1);
    assert(out[0] == '{' && out[n - 1] == '}');
    assert(out[n] == 0);
    free(out);
    end();
    free(f);
    free(payload);
}

/* A peer drop clears `connected` before the daemon calls ws_close(). The
 * close path must still release the TLS context and fd; the old early return
 * leaked one socket and one TLS context per network flap. */
static void test_close_after_peer_drop(void){
    memset(&W, 0, sizeof W);
    W.connected = 0;
    W.rcap = 32;
    W.rbuf = (unsigned char *)malloc(W.rcap);
    assert(W.rbuf);
    W.tls = (void *)&W;
    int p[2];
    assert(pipe(p) == 0);
    W.fd = p[0];
    W.sock = p[0];
    g_tls_freed = 0;
    assert(ws_close(&W) == 0);
    assert(g_tls_freed == 1);
    assert(fcntl(p[0], F_GETFD) == -1 && errno == EBADF);
    assert(W.rbuf == NULL && W.rcap == 0 && W.tls == NULL && W.fd == 0);
    close(p[1]);
}

int main(void){
    log_init("/dev/null");
    test_handshake_headers();
    test_alloc_full_message();
    test_single_frame();
    test_truncated_payload();
    test_fragmented_message();
    test_fragmented_partial_reads();
    test_control_between_fragments();
    test_continuation_without_start();
    test_new_message_inside_fragment();
    test_close_mid_fragment();
    test_reserved_control_opcode();
    test_empty_frame();
    test_close_after_peer_drop();
    log_close();
    puts("ws tests passed");
    return 0;
}
