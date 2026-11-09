/* Pure server-frame header validation shared with the offline protocol tests. */
#include <stdint.h>

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
