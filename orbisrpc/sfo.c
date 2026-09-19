/* sfo.c - PARAM.SFO TITLE extractor (bounds-checked, no libc beyond string).
 * Every PS4 game carries sce_sys/param.sfo with a TITLE field. Inside the
 * game process it is reachable via the app0 mount; the plugin tries those
 * paths first because they work on ANY console with zero setup. */
#include "sfo.h"
#include <string.h>
#include <stdint.h>

#pragma pack(push, 1)
typedef struct {
    uint32_t magic;      /* 0x46535000 ("\0PSF" little-endian) */
    uint32_t version;    /* usually 0x0201 */
    uint32_t key_off;
    uint32_t data_off;
    uint32_t count;
} sfo_hdr_t;
typedef struct {
    uint16_t key_off;
    uint16_t fmt;        /* 0x0004 = utf8 string (null-padded), 0x0404 = uint32 */
    uint32_t len;        /* used bytes (incl. null for strings) */
    uint32_t max_len;
    uint32_t data_off;   /* relative to data_off base */
} sfo_entry_t;
#pragma pack(pop)

int sfo_title(const unsigned char *buf, size_t n, char *out, size_t cap){
    if(!buf || !out || cap == 0) return -1;
    out[0] = 0;
    if(n < sizeof(sfo_hdr_t)) return -1;
    const sfo_hdr_t *h = (const sfo_hdr_t *)buf;
    if(h->magic != 0x46535000u) return -1;
    if(h->count == 0 || h->count > 256) return -1;
    if(h->key_off >= n || h->data_off >= n) return -1;
    if(sizeof(sfo_hdr_t) + (size_t)h->count * sizeof(sfo_entry_t) > n) return -1;
    const sfo_entry_t *e = (const sfo_entry_t *)(buf + sizeof(sfo_hdr_t));
    for(uint32_t i = 0; i < h->count; i++){
        size_t ko = (size_t)h->key_off + e[i].key_off;
        if(ko >= n) continue;
        /* keys must be null-terminated INSIDE the buffer: bound the scan */
        size_t kmax = n - ko;
        size_t klen = 0;
        while(klen < kmax && buf[ko + klen]) klen++;
        if(klen >= kmax) continue; /* unterminated key: malformed */
        const char *key = (const char *)(buf + ko);
        /* keys of interest: TITLE first, then language variants TITLE_XX */
        int is_title = (!strcmp(key, "TITLE") ||
            (klen == 8 && !memcmp(key, "TITLE_", 6)));
        if(!is_title) continue;
        if(e[i].fmt != 0x0004 && e[i].fmt != 0x0204) continue;
        size_t dlen = e[i].len;
        if(dlen == 0 || dlen > 256) continue;
        size_t doff = (size_t)h->data_off + e[i].data_off;
        if(doff + dlen > n) continue;
        /* copy up to null, require printable ASCII/UTF-8 start */
        size_t ci = 0;
        for(size_t k = 0; k < dlen && ci < cap - 1; k++){
            unsigned char c = buf[doff + k];
            if(c == 0) break;
            if(c < 0x20 && c != 0x20) break;
            out[ci++] = (char)c;
        }
        out[ci] = 0;
        if(ci > 1) return 0;
        out[0] = 0;
    }
    return -1;
}
