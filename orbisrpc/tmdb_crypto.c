/* tmdb_crypto.c - self-contained SHA1 + HMAC-SHA1 + Sony TMDB URL builder
 * and response parser. No platform headers: host-unit-testable.
 *
 * Name/icon resolution idea follows bshar1865/zorua98741
 * PS4-Rich-Presence-for-Discord (TMDB key/hash scheme, Tustin's hash
 * construction): title metadata comes from Sony's own
 * tmdb.np.dl.playstation.net service. Our implementation here (parser,
 * transport, caching, presence wiring) is original. */
#include "tmdb_crypto.h"
#include "jsonlite.h"
#include <string.h>
#include <stdint.h>
#include <stdio.h>

/* ---- SHA1 (small, public-domain style) ---- */
typedef struct { uint32_t h[5]; uint64_t len; unsigned char buf[64]; size_t n; } sha1_t;
static uint32_t rol(uint32_t x, int n){ return (x<<n)|(x>>(32-n)); }
static void sha1_block(sha1_t *c, const unsigned char *p){
    uint32_t w[80];
    for(int i=0;i<16;i++) w[i]=(uint32_t)p[4*i]<<24|(uint32_t)p[4*i+1]<<16|(uint32_t)p[4*i+2]<<8|p[4*i+3];
    for(int i=16;i<80;i++) w[i]=rol(w[i-3]^w[i-8]^w[i-14]^w[i-16],1);
    uint32_t a=c->h[0],b=c->h[1],d=c->h[2],e=c->h[3],f=c->h[4];
    for(int i=0;i<80;i++){
        uint32_t t;
        if(i<20) t=((b&d)|((~b)&e))+0x5A827999u;
        else if(i<40) t=(b^d^e)+0x6ED9EBA1u;
        else if(i<60) t=((b&d)|(b&e)|(d&e))+0x8F1BBCDCu;
        else t=(b^d^e)+0xCA62C1D6u;
        t+=rol(a,5)+f+w[i]; f=e; e=d; d=rol(b,30); b=a; a=t;
    }
    c->h[0]+=a; c->h[1]+=b; c->h[2]+=d; c->h[3]+=e; c->h[4]+=f;
}
static void sha1_init(sha1_t *c){
    c->h[0]=0x67452301u; c->h[1]=0xEFCDAB89u; c->h[2]=0x98BADCFEu;
    c->h[3]=0x10325476u; c->h[4]=0xC3D2E1F0u; c->len=0; c->n=0;
}
static void sha1_update(sha1_t *c, const unsigned char *p, size_t n){
    c->len+=n;
    while(n){
        size_t t=64-c->n; if(t>n) t=n;
        memcpy(c->buf+c->n,p,t); c->n+=t; p+=t; n-=t;
        if(c->n==64){ sha1_block(c,c->buf); c->n=0; }
    }
}
static void sha1_final(sha1_t *c, unsigned char out[20]){
    uint64_t bits=c->len*8;
    unsigned char z=0x80, zz=0x00;
    sha1_update(c,&z,1);
    while(c->n!=56) sha1_update(c,&zz,1);
    unsigned char lb[8];
    for(int i=0;i<8;i++) lb[i]=(unsigned char)(bits>>((7-i)*8));
    memcpy(c->buf+c->n,lb,8); sha1_block(c,c->buf);
    for(int i=0;i<5;i++){
        out[4*i]=(unsigned char)(c->h[i]>>24); out[4*i+1]=(unsigned char)(c->h[i]>>16);
        out[4*i+2]=(unsigned char)(c->h[i]>>8); out[4*i+3]=(unsigned char)c->h[i];
    }
}
void tmdb_sha1(const unsigned char *p, size_t n, unsigned char out[20]){
    sha1_t c; sha1_init(&c); sha1_update(&c,p,n); sha1_final(&c,out);
}
void tmdb_hmac_sha1(const unsigned char *key, size_t klen,
                    const unsigned char *msg, size_t mlen,
                    unsigned char out[20]){
    unsigned char k[64];
    if(klen>64){ tmdb_sha1(key,klen,k); memset(k+20,0,44); klen=20; }
    else { memcpy(k,key,klen); memset(k+klen,0,64-klen); }
    unsigned char ipad[64], opad[64];
    for(int i=0;i<64;i++){ ipad[i]=k[i]^0x36; opad[i]=k[i]^0x5C; }
    sha1_t c; unsigned char inner[20];
    sha1_init(&c); sha1_update(&c,ipad,64); sha1_update(&c,msg,mlen); sha1_final(&c,inner);
    sha1_init(&c); sha1_update(&c,opad,64); sha1_update(&c,inner,20); sha1_final(&c,out);
}

/* Sony TMDB service key (same key their own tooling uses). */
static const unsigned char tmdb_key[64] = {
    0xF5,0xDE,0x66,0xD2,0x68,0x0E,0x25,0x5B,0x2D,0xF7,0x9E,0x74,0xF8,0x90,0xEB,0xF3,
    0x49,0x26,0x2F,0x61,0x8B,0xCA,0xE2,0xA9,0xAC,0xCD,0xEE,0x51,0x56,0xCE,0x8D,0xF2,
    0xCD,0xF2,0xD4,0x8C,0x71,0x17,0x3C,0xDC,0x25,0x94,0x46,0x5B,0x87,0x40,0x5D,0x19,
    0x7C,0xF1,0xAE,0xD3,0xB7,0xE9,0x67,0x1E,0xEB,0x56,0xCA,0x67,0x53,0xC2,0xE6,0xB0
};

/* Path (no scheme/host): /tmdb2/<TID>_00_<UPPERHEX>/<TID>_00.json Const-size. */
int tmdb_path(const char *titleId, char *out, size_t cap){
    static const char hex[] = "0123456789ABCDEF";
    char tid[16];
    size_t i = 0;
    if(!titleId || !out || cap == 0) return -1;
    while(titleId[i] && i < sizeof tid - 1){
        char c = titleId[i];
        if(!((c>='A'&&c<='Z')||(c>='0'&&c<='9'))) return -1;
        tid[i] = c; i++;
    }
    tid[i] = 0;
    if(i != 9) return -1;
    char msg[16];
    memcpy(msg, tid, 9); memcpy(msg+9, "_00", 4); /* includes null */
    unsigned char mac[20];
    tmdb_hmac_sha1(tmdb_key, sizeof tmdb_key, (unsigned char*)msg, 12, mac);
    /* need: /tmdb2/XXXXXXXXX_00_40HEX/XXXXXXXXX_00.json = 7+9+3+40+1+9+5+1 */
    if(cap < 80) return -1;
    int n = snprintf(out, cap, "/tmdb2/%s_00_", tid);
    for(int k = 0; k < 20; k++){ out[n++] = hex[mac[k]>>4]; out[n++] = hex[mac[k]&15]; }
    n += snprintf(out+n, cap-(size_t)n, "/%s_00.json", tid);
    (void)n;
    return 0;
}

/* Parse a TMDB JSON body: names[0].name + icons[0].icon. */
int tmdb_parse(const char *body, size_t len,
               char *name, size_t name_cap, char *icon, size_t icon_cap){
    if(!body || !name || name_cap == 0) return -1;
    name[0] = 0; if(icon && icon_cap) icon[0] = 0;
    jl_val_t *r = jl_parse(body, len);
    if(!r) return -1;
    int rc = -1;
    const jl_val_t *names = jl_obj_get(r, "names");
    const jl_val_t *n0 = (names && names->type == JL_ARRAY) ? jl_arr_at(names, 0) : NULL;
    const jl_val_t *nm = n0 ? jl_obj_get(n0, "name") : NULL;
    if(nm && nm->type == JL_STRING && nm->str[0]){
        strncpy(name, nm->str, name_cap-1); name[name_cap-1] = 0;
        rc = 0;
        if(icon && icon_cap){
            const jl_val_t *icons = jl_obj_get(r, "icons");
            const jl_val_t *i0 = (icons && icons->type == JL_ARRAY) ? jl_arr_at(icons, 0) : NULL;
            const jl_val_t *iu = i0 ? jl_obj_get(i0, "icon") : NULL;
            if(iu && iu->type == JL_STRING && iu->str[0]){
                /* only http(s) URLs are usable as external assets.
                 * Sony serves the same bytes over TLS: prefer https so
                 * mixed-content rules can never silently drop the art. */
                const char *u = iu->str;
                if(!strncmp(u, "http://", 7)){
                    if(strstr(u, ".playstation.net/")){
                        char tmp[256];
                        snprintf(tmp, sizeof tmp, "https://%s", u+7);
                        strncpy(icon, tmp, icon_cap-1); icon[icon_cap-1] = 0;
                    } else {
                        strncpy(icon, u, icon_cap-1); icon[icon_cap-1] = 0;
                    }
                } else if(!strncmp(u, "https://", 8)){
                    strncpy(icon, u, icon_cap-1); icon[icon_cap-1] = 0;
                }
            }
        }
    }
    jl_free(r);
    return rc;
}
