/* ws.h - minimal Discord gateway WebSocket (text frames + heartbeat).
 * Non-blocking: recv never blocks the caller; returns 0 when no complete
 * frame is available yet. */
#ifndef WS_H
#define WS_H
#include <stdint.h>
#include <stddef.h>

#define WS_RBUF 16384 /* raw socket buffer (large enough for gateway dispatches) */

typedef struct {
    int32_t sock; int32_t ssl; int32_t ssl_ctx;
    int32_t connected; int32_t fd;
    int nb;                    /* underlying socket non-blocking flag */
    unsigned char rbuf[WS_RBUF]; /* raw bytes from SSL_read */
    size_t rlen;               /* valid bytes in rbuf */
    size_t rpos;               /* consumed parse position */
} ws_t;

int ws_connect(ws_t *w, const char *host, int port, const char *resource, const char *key);
int ws_send_text(ws_t *w, const char *msg, size_t len);
int ws_send_ping(ws_t *w);
int ws_send_pong(ws_t *w);
/* Returns frame payload length (>0), 0 if no complete frame yet, <0 on error.
 * Server frames are unmasked (RFC 6455). */
int ws_recv_frame(ws_t *w, char *buf, size_t cap, int *opcode_out, int *fin_out);
int ws_close(ws_t *w);
#endif
