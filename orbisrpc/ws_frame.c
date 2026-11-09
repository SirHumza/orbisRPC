/* Pure server-frame header validation shared with the offline protocol tests. */
#include "ws_frame.h"
#include <stdint.h>
#include <stddef.h>
#include <string.h>

size_t ws_control_frame_encode(unsigned char opcode,
                               const unsigned char *payload, size_t len,
                               const unsigned char mask[4],
                               unsigned char *out, size_t cap){
    if(!out || !mask || (!payload && len) || len > 125 ||
       (opcode != 8 && opcode != 9 && opcode != 10) || cap < 6 + len)
        return 0;
    out[0] = (unsigned char)(0x80 | opcode);
    out[1] = (unsigned char)(0x80 | len);
    memcpy(out + 2, mask, 4);
    for(size_t i = 0; i < len; i++) out[6 + i] = payload[i] ^ mask[i % 4];
    return 6 + len;
}

int ws_frame_header_valid(const unsigned char *b, uint64_t plen){
    int fin = (b[0] & 0x80) != 0;
    int rsv = b[0] & 0x70;
    int op = b[0] & 0x0f;
    int masked = (b[1] & 0x80) != 0;
    if (rsv || masked) return 0;
    if (op != 0 && op != 1 && op != 2 &&
        op != 8 && op != 9 && op != 10) return 0;
    if (op >= 8 && (!fin || plen > 125)) return 0;
    if ((b[1] & 0x7f) == 127 && (b[2] & 0x80)) return 0;
    return 1;
}

static int ws_utf8_valid(const unsigned char *s, size_t n){
    size_t i = 0;
    while(i < n){
        unsigned char a = s[i++];
        if(a < 0x80) continue;
        if(a >= 0xC2 && a <= 0xDF){
            if(i >= n || (s[i++] & 0xC0) != 0x80) return 0;
            continue;
        }
        if(a >= 0xE0 && a <= 0xEF){
            if(n - i < 2) return 0;
            unsigned char b = s[i++], c = s[i++];
            if((b & 0xC0) != 0x80 || (c & 0xC0) != 0x80 ||
               (a == 0xE0 && b < 0xA0) || (a == 0xED && b >= 0xA0))
                return 0;
            continue;
        }
        if(a >= 0xF0 && a <= 0xF4){
            if(n - i < 3) return 0;
            unsigned char b = s[i++], c = s[i++], d = s[i++];
            if((b & 0xC0) != 0x80 || (c & 0xC0) != 0x80 ||
               (d & 0xC0) != 0x80 || (a == 0xF0 && b < 0x90) ||
               (a == 0xF4 && b >= 0x90)) return 0;
            continue;
        }
        return 0;
    }
    return 1;
}

int ws_close_payload_valid(const unsigned char *payload, size_t len,
                           unsigned *code_out){
    if(code_out) *code_out = 0;
    if((!payload && len) || len == 1 || len > 125) return 0;
    if(len == 0) return 1;
    unsigned code = ((unsigned)payload[0] << 8) | payload[1];
    if(code < 1000 || code >= 5000 || code == 1004 || code == 1005 ||
       code == 1006 || code == 1015 || !ws_utf8_valid(payload + 2, len - 2))
        return 0;
    if(code_out) *code_out = code;
    return 1;
}
