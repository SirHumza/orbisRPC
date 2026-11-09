#include "ws_frag.h"
#include "ws.h"
#include <stdlib.h>
#include <string.h>

void ws_frag_reset(ws_frag_t *s){
    if(!s) return;
    if(s->data) memset(s->data, 0, s->len);
    free(s->data);
    memset(s, 0, sizeof *s);
}

int ws_frag_feed(ws_frag_t *s, int opcode, int fin,
                 const unsigned char *payload, size_t len,
                 const unsigned char **message, size_t *message_len){
    if(message) *message = NULL;
    if(message_len) *message_len = 0;
    if(!s || (!payload && len) || !message || !message_len) return -1;

    if(opcode == 1){
        if(s->active) return -1;
        if(fin){
            if(len > WS_BODY_MAX) return -2;
            *message = payload;
            *message_len = len;
            return 1;
        }
        s->active = 1;
        s->len = 0;
        if(len > WS_BODY_MAX){
            s->discarding = 1;
            return 0;
        }
    } else if(opcode == 0){
        if(!s->active) return -1;
    } else {
        return -1;
    }

    if(s->discarding){
        if(fin){ ws_frag_reset(s); return -2; }
        return 0;
    }
    if(len > WS_BODY_MAX - s->len){
        ws_frag_reset(s);
        s->active = !fin;
        s->discarding = !fin;
        if(fin) return -2;
        return 0;
    }
    if(len){
        unsigned char *next = realloc(s->data, s->len + len);
        if(!next){ ws_frag_reset(s); return -1; }
        s->data = next;
        memcpy(s->data + s->len, payload, len);
        s->len += len;
    }
    if(!fin) return 0;
    *message = s->data;
    *message_len = s->len;
    s->active = 0;
    return 1;
}
