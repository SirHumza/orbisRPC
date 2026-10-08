/* ws.h - minimal Discord gateway WebSocket (text frames + heartbeat).
 * Non-blocking: recv never blocks the caller; returns 0 when no complete
 * frame is available yet. */
#ifndef WS_H
#define WS_H
#include <stdint.h>
#include <stddef.h>

#define WS_RBUF_MIN 65536          /* initial raw socket buffer (READY is big) */
/* Grow limit; anything larger is drained and skipped.
 *
 * 32 MB, raised from 8 MB on 2026-10-06: a real account's READY measured
 * 11.7 MB across four attempts (11695449 / 11694320 / 11694256 / 11706895),
 * well past the old cap, so identify never completed and the gateway
 * reported a connect failure. The value is not stable frame-to-frame, so
 * this is sized with real headroom rather than matched to one observation.
 * rbuf doubles from 64 KB, so a full READY holds ~32 MB of PS4 heap for
 * the life of the session. */
#define WS_RBUF_MAX (32*1024*1024)

typedef struct {
    int32_t sock; int32_t connected; int32_t fd;
    int nb;                     /* underlying socket non-blocking flag */
    void *tls;                  /* tls_ctx_t* (opaque: mbedTLS session) */
    unsigned char *rbuf;        /* raw bytes from TLS layer (heap, grows) */
    size_t rcap;                /* allocated size of rbuf */
    size_t rlen;                /* valid bytes in rbuf */
    size_t rpos;                /* consumed parse position */
    uint64_t skip_left;         /* bytes left of an oversized frame being drained */
    int skip_op;                /* opcode of the frame being drained */
} ws_t;

int ws_connect(ws_t *w, const char *host, int port, const char *resource, const char *key);
int ws_send_text(ws_t *w, const char *msg, size_t len);
/* Returns frame payload length (>0), 0 if no complete frame yet, <0 on error,
 * or -3 if an oversized frame was drained and skipped (payload lost). */
int ws_recv_frame(ws_t *w, char *buf, size_t cap, int *opcode_out, int *fin_out);
int ws_pong(ws_t *w); /* answer a server PING (call when recv gives opcode 9) */
int ws_close(ws_t *w);
/* How many payload bytes remain to discard after an oversized frame's header
 * has been consumed. Saturates at 0 rather than wrapping when payload_here
 * already covers plen. Pure, so the host tests can pin it: the drain state
 * lives in the ws_t and a bad value is only visible on the console as a
 * desynced frame stream. */
void ws_skip_plan(uint64_t plen, size_t payload_here, uint64_t *skip_left_out);
#endif
