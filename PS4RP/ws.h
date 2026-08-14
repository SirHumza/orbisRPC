/* ws.h - minimal Discord gateway WebSocket (text frames + heartbeat). */
#ifndef WS_H
#define WS_H
#include <stdint.h>
#include <stddef.h>
/* opaque socket handle */
typedef struct { int32_t sock; int32_t ssl; int32_t ssl_ctx; int32_t connected; int32_t fd; } ws_t;
int ws_connect(ws_t *w, const char *host, int port, const char *resource, const char *key);
int ws_send_text(ws_t *w, const char *msg, size_t len);
int ws_send_ping(ws_t *w);
int ws_recv_frame(ws_t *w, char *buf, size_t cap, int *opcode_out, int *fin_out);
int ws_close(ws_t *w);
#endif
