/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 SirHumza and CyberMask367 - see NOTICE for attribution terms.
 */
/* ws.h - minimal Discord gateway WebSocket (text frames + heartbeat).
 * Non-blocking: recv never blocks the caller; returns 0 when no complete
 * frame is available yet. */
#ifndef WS_H
#define WS_H
#include <stdint.h>
#include <stddef.h>

#define WS_RBUF_MIN 65536          /* initial raw socket buffer (READY is big) */
#define WS_RBUF_MAX (8*1024*1024)  /* grow limit; bigger frames are skipped */

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
    /* Fragmented message reassembly (RFC 6455 5.4). A message split across
     * frames arrives as one text/binary frame with FIN=0 followed by
     * continuation frames; the caller must see one message, not fragments. */
    unsigned char *frag;
    size_t frag_len, frag_cap;
    int frag_op;                /* opcode of the first frame (1 text, 2 binary) */
    int in_frag;
    int frag_drop;              /* message exceeded WS_MSG_MAX; discard to FIN */
} ws_t;

/* user_agent is the same string the client reports in its properties; NULL
 * falls back to a plain orbisRPC UA (tests only). */
int ws_connect(ws_t *w, const char *host, int port, const char *resource,
               const char *key, const char *user_agent);
/* Pure request builder (no sockets) so the handshake headers are testable.
 * Returns the request length, 0 when it does not fit. */
size_t ws_make_handshake(char *out, size_t cap, const char *host,
                         const char *resource, const char *key,
                         const char *user_agent);
int ws_send_text(ws_t *w, const char *msg, size_t len);
/* Returns frame payload length (>0), 0 if no complete frame yet, <0 on error,
 * or -3 if an oversized frame was drained and skipped (payload lost). */
int ws_recv_frame(ws_t *w, char *buf, size_t cap, int *opcode_out, int *fin_out);
/* Same, but the whole message is malloc'd (caller frees *out). Needed for
 * READY: its session fields can sit past the first few KB. */
int ws_recv_alloc(ws_t *w, char **out, size_t *out_len,
                  int *opcode_out, int *fin_out);
int ws_pong(ws_t *w); /* answer a server PING (call when recv gives opcode 9) */
int ws_close(ws_t *w);
/* Initialise the platform network stack (SceNet on the app build; a no-op
 * for the payload SDK). Safe to call repeatedly. Needed before getaddrinfo
 * outside a connection attempt (startup DNS probe). */
int ws_net_init(void);
#endif
