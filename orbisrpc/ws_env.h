/* ws_env.h - streaming envelope extraction for oversized WebSocket frames.
 *
 * A Discord READY frame is ~12 MB, almost all of it the "d" object. The
 * daemon only ever reads three top-level fields from a gateway frame:
 *
 *     {"op":0,"s":41,"t":"READY","d":{ ...12 MB... }}
 *              ^    ^      ^
 *
 * Buffering the whole frame just to read those made the receive buffer grow
 * from 64 KB to 16 MB by repeated doubling (65536 * 2^8 >= 11695459), and
 * that 16 MB is held for the life of the session. With more than one payload
 * instance alive the console runs out of memory and malloc fails -- observed
 * 2026-10-09 as "ws: rbuf grow fail" followed by "cannot connect to
 * discord", repeating on every retry.
 *
 * So oversized frames are streamed instead of buffered: these three fields
 * are pulled out of a small rolling window while the rest of the payload is
 * discarded. A 12 MB READY then costs the same memory as a 2 KB heartbeat.
 *
 * Pure logic -- no socket, no TLS, no PS4 headers -- so the host tests can
 * pin it. Same pattern as ws_skip.c: included by ws.c, compiled directly by
 * tests, so the two can never drift.
 */
#ifndef ORBISRPC_WS_ENV_H
#define ORBISRPC_WS_ENV_H

#include <stddef.h>

/* Bytes of payload prefix retained for scanning. op/s/t are a few bytes and
 * sit at the front of every gateway frame, so this only has to exceed the
 * envelope's own length; 2 KB leaves large margin for a reordering while
 * keeping the footprint fixed and tiny. */
#define WS_ENV_WINDOW 2048

typedef struct {
    char win[WS_ENV_WINDOW];   /* payload prefix */
    size_t winlen;             /* valid bytes in win */
    int have_op, have_s, have_t;
    char op[32];
    char s[32];
    char t[64];
    /* Captured independently while streaming: READY may be much larger than
     * the retained envelope prefix, and can contain a rotated session token. */
    int have_auth_token;
    char auth_token[512];
    int have_qos_token;
    char qos_token[512];
    int json_depth, in_string, escaped;
    int string_depth, string_overflow;
    int auth_state; /* 0 scanning, 1 expect ':', 2 expect value, 3 value */
    int capture_kind; /* 1 auth_token, 2 qos_token */
    size_t string_len, auth_len;
    size_t qos_len;
    char string[32];
} ws_env_t;

void ws_env_init(ws_env_t *e);
/* Feed a chunk of frame payload. Any chunking is fine, including a field
 * split across two calls: the window is rescanned in full each time. */
void ws_env_feed(ws_env_t *e, const unsigned char *p, size_t n);
/* 1 once the envelope is usable, or the window can take no more.
 *
 * op and t are what callers actually need: gw_op() reads "op" and is_ready()
 * reads "t". "s" is optional -- gw_seq() returns quietly when it is missing,
 * and requiring it here meant one unmatched field stalled the whole report. */
int ws_env_complete(const ws_env_t *e);
/* Token fields are accepted only from the gateway READY dispatch. */
int ws_env_is_ready(const ws_env_t *e);
/* Copy and consume a captured READY credential. Returns 1 if copied, 0 if
 * absent or the envelope is not READY, and -1 if the output is too small. */
int ws_env_take_auth_token(ws_env_t *e, char *out, size_t cap);
int ws_env_take_qos_token(ws_env_t *e, char *out, size_t cap);
/* Render a minimal JSON envelope holding only the fields that were found,
 * shaped so discord.c's existing top_val() reads it unchanged.
 * Returns bytes written (excluding NUL), or 0 if nothing was captured. */
size_t ws_env_render(const ws_env_t *e, char *out, size_t cap);

#endif /* ORBISRPC_WS_ENV_H */
