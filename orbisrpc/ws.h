/* ws.h - minimal Discord gateway WebSocket (text frames + heartbeat).
 * Non-blocking: recv never blocks the caller; returns 0 when no complete
 * frame is available yet. */
#ifndef WS_H
#define WS_H
#include <stdint.h>
#include <stddef.h>
#include "ws_env.h"

#define WS_RBUF_MIN 65536          /* initial raw socket buffer (READY is big) */
/* Grow limit. Now a backstop rather than the working size: any frame with a
 * payload larger than WS_BODY_MAX is envelope-extracted and never buffered,
 * so rbuf stays at WS_RBUF_MIN for a whole session.
 *
 * History, kept because the reason is not obvious from the code: this was
 * 8 MB, raised to 32 MB on 2026-10-06 because a real account's READY
 * measured 11.7 MB. But 32 MB only moved the failure -- the buffer doubles
 * to 16 MB to hold a 12 MB frame, and two live payload instances exhausted
 * the console's memory (observed 2026-10-09 as "ws: rbuf grow fail" and a
 * permanent connect failure). WS_BODY_MAX is the actual fix. */
#define WS_RBUF_MAX (32*1024*1024)
/* Largest frame payload ever buffered whole. Above this the payload is
 * streamed and only op/s/t are kept (see ws_env.h), so an oversized READY
 * costs a fixed 2 KB window instead of tens of megabytes. Callers cap
 * payloads at 2 KB anyway. */
#define WS_BODY_MAX 65536

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
    int skip_fin;               /* FIN of the frame being drained */
    /* Envelope scraped while draining an oversized frame. Bounded: only the
     * first WS_ENV_WINDOW bytes of payload are retained. */
    ws_env_t env;
    int env_active;             /* 1 while an oversized frame is being drained */
    int env_reported;          /* envelope already handed back for this frame */
    int grow_warned;            /* rbuf realloc already logged a failure */
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
/* How many payload bytes the drain still has to discard. payload_here is the
 * count ALREADY REMOVED from rbuf, never the count sitting in it -- the drain
 * consumes buffered bytes too, so passing those here double-counts and leaves
 * the stream mid-frame (seen 2026-10-09). ws_recv_frame() removes none and
 * passes 0. Returns the value written. */
uint64_t ws_skip_plan(uint64_t plen, size_t payload_here, uint64_t *skip_left_out);
#endif
