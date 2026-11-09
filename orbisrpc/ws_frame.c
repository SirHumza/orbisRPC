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
