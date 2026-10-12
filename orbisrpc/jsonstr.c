/* jsonstr.c - see jsonstr.h. Moved verbatim from retro.c so custom.c can share
 * one parser instead of carrying a second copy that could drift. */
#include "jsonstr.h"
#include <string.h>

size_t json_unescape(const char *src, size_t len, char *dst, size_t cap){
    size_t o = 0;
    for(size_t i = 0; i < len && o + 1 < cap; i++){
        if(src[i] != '\\'){ dst[o++] = src[i]; continue; }
        if(i + 1 >= len) break;
        char c = src[++i];
        switch(c){
        case 'n': dst[o++] = '\n'; break;
        case 't': dst[o++] = '\t'; break;
        case 'r': dst[o++] = '\r'; break;
        case 'b': dst[o++] = '\b'; break;
        case 'f': dst[o++] = '\f'; break;
        case 'u': {
            if(i + 4 >= len){ i = len; break; }
            unsigned cp = 0;
            for(int k = 1; k <= 4; k++){
                char h = src[i + k];
                cp <<= 4;
                if(h >= '0' && h <= '9') cp |= (unsigned)(h - '0');
                else if(h >= 'a' && h <= 'f') cp |= (unsigned)(h - 'a' + 10);
                else if(h >= 'A' && h <= 'F') cp |= (unsigned)(h - 'A' + 10);
            }
            i += 4;
            if(cp < 0x80) dst[o++] = (char)cp;
            else if(cp < 0x800 && o + 2 < cap){
                dst[o++] = (char)(0xC0 | (cp >> 6));
                dst[o++] = (char)(0x80 | (cp & 0x3F));
            } else if(cp < 0x10000 && o + 3 < cap){
                dst[o++] = (char)(0xE0 | (cp >> 12));
                dst[o++] = (char)(0x80 | ((cp >> 6) & 0x3F));
                dst[o++] = (char)(0x80 | (cp & 0x3F));
            }
            break;
        }
        default: dst[o++] = c; break;   /* covers \" \\ \/ */
        }
    }
    dst[o] = 0;
    return o;
}

int json_grab_string(const char *p, const char *end, char *dst, size_t cap){
    if(!p || p >= end || *p != '"') return 0;
    p++;
    const char *q = p;
    while(q < end){
        if(*q == '\\'){ if(q + 1 >= end) return 0; q += 2; continue; }
        if(*q == '"') break;
        q++;
    }
    if(q >= end) return 0;
    json_unescape(p, (size_t)(q - p), dst, cap);
    return 1;
}

int json_grab_value(const char *key, const char *end, char *dst, size_t cap){
    if(!key || key >= end) return 0;
    const char *c = key;
    while(c < end && *c != ':') c++;
    if(c >= end) return 0;
    return json_grab_string(c + 1, end, dst, cap);
}