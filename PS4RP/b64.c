/* b64.c - standard base64 encoder */
#include "b64.h"
static const char b64t[]="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
size_t b64_encode(const unsigned char *in, size_t len, char *out){
    size_t o=0, i;
    for(i=0;i<len;i+=3){
        unsigned char b0=in[i], b1=(i+1<len)?in[i+1]:0, b2=(i+2<len)?in[i+2]:0;
        unsigned v=((unsigned)b0<<16)|((unsigned)b1<<8)|b2;
        out[o++]=b64t[(v>>18)&0x3f];
        out[o++]=b64t[(v>>12)&0x3f];
        out[o++]=i+1<len?b64t[(v>>6)&0x3f]:'=';
        out[o++]=i+2<len?b64t[v&0x3f]:'=';
    }
    return o;
}
