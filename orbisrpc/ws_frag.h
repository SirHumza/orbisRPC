#ifndef ORBISRPC_WS_FRAG_H
#define ORBISRPC_WS_FRAG_H

#include <stddef.h>

typedef struct {
    unsigned char *data;
    size_t len;
    int active;
    int discarding;
} ws_frag_t;

/* Feed one data frame. Returns 1 with a complete message, 0 while incomplete,
 * -1 for an invalid sequence, or -2 for a complete oversized message. */
int ws_frag_feed(ws_frag_t *state, int opcode, int fin,
                 const unsigned char *payload, size_t len,
                 const unsigned char **message, size_t *message_len);
void ws_frag_reset(ws_frag_t *state);

#endif
