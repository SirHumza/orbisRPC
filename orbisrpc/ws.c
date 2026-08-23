/* ws.c - WebSocket client over SceNet + PS4 LibreSSL (OpenSSL-style) TLS.
 * Non-blocking socket: every SSL_read/SSL_write return goes through
 * SSL_get_error, so WANT_READ/WANT_WRITE means "retry", never "closed".
 * The receive buffer grows for large server frames (user-account READY
 * payloads are big); frames beyond WS_RBUF_MAX are drained and skipped.
 * All client->server frames are masked per RFC 6455 5.3, control frames too.
 */
#include "ws.h"
#include "log.h"
#include <orbis/Net.h>
#include <orbis/Sysmodule.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>

typedef struct ssl_ctx_st SSL_CTX;
typedef struct ssl_st SSL;

/* exported from libSceLibreSSL.so (OpenSSL ABI) */
extern SSL_CTX *SSL_CTX_new(const void *method);
extern void     SSL_CTX_free(SSL_CTX *ctx);
extern SSL     *SSL_new(SSL_CTX *ctx);
extern void     SSL_free(SSL *s);
extern int      SSL_set_fd(SSL *s, int fd);
extern int      SSL_set_connect_state(SSL *s);
extern int      SSL_connect(SSL *s);
extern int      SSL_write(SSL *s, const void *buf, int num);
extern int      SSL_read(SSL *s, void *buf, int num);
extern int      SSL_shutdown(SSL *s);
extern int      SSL_get_error(SSL *s, int ret);
extern const void *SSLv23_client_method(void);
extern int      SSL_ctrl(SSL *s, int cmd, long larg, void *parg);

#ifndef SOL_SOCKET
#define SOL_SOCKET 0xffff
#endif
#ifndef SO_NBIO
#define SO_NBIO 0x2000
#endif
#define SSL_ERROR_WANT_READ  2
#define SSL_ERROR_WANT_WRITE 3

static int s_net_ready = 0;
static int s_net_mem = 0;

static int net_ensure(void){
    if(s_net_ready) return 0;
    uint32_t ur = sceSysmoduleLoadModuleInternal(ORBIS_SYSMODULE_INTERNAL_NET);
    if((int)ur < 0){ log_msg("load NET fail %d", (int)ur); return -1; }
    if(sceNetInit() < 0){ log_msg("sceNetInit fail"); return -2; }
    s_net_mem = (int)sceNetPoolCreate("orbisrpcNet", 128*1024, 0);
    if(s_net_mem < 0){ log_msg("net pool fail %d", s_net_mem); return -3; }
    s_net_ready = 1;
    return 0;
}

/* Write exactly n bytes over TLS, tolerating WANT_READ/WRITE with deadline. */
static int ws_send_all(ws_t *w, const unsigned char *data, size_t n){
    SSL *ssl = (SSL*)(intptr_t)w->ssl;
    size_t off=0;
    int64_t t0=time(NULL);
    while(off<n){
        int wr = SSL_write(ssl, data+off, (int)(n-off));
        if(wr>0){ off+=(size_t)wr; continue; }
        int e = SSL_get_error(ssl, wr);
        if(e==SSL_ERROR_WANT_READ || e==SSL_ERROR_WANT_WRITE){
            if(time(NULL)-t0 > 10) return -1;
            usleep(10000); continue;
        }
        return -1;
    }
    return (int)off;
}

static void next_mask(unsigned char mk[4]){
    static uint32_t mk_seed;
    if(!mk_seed) mk_seed = (uint32_t)time(NULL) ^ 0x9e3779b9u ^ (uint32_t)(uintptr_t)&mk_seed;
    mk_seed = mk_seed*1664525u + 1013904223u;
    mk[0]=(unsigned char)(mk_seed&0xff);       mk[1]=(unsigned char)((mk_seed>>8)&0xff);
    mk[2]=(unsigned char)((mk_seed>>16)&0xff); mk[3]=(unsigned char)((mk_seed>>24)&0xff);
}

int ws_connect(ws_t *w, const char *host, int port, const char *resource, const char *key){
    memset(w,0,sizeof(*w));
    w->rcap = WS_RBUF_MIN;
    w->rbuf = (unsigned char*)malloc(w->rcap);
    if(!w->rbuf){ log_msg("ws: rbuf alloc fail"); return -1; }
    if(net_ensure()<0) goto fail;
    int32_t rid = sceNetResolverCreate("orbisrpcR", 0, 0);
    OrbisNetInAddr in; memset(&in,0,sizeof in);
    int resolved = 0;
    if(rid >= 0){
        int32_t rr = sceNetResolverStartNtoa(rid, host, &in, 5, 3, 0);
        sceNetResolverDestroy(rid);
        resolved = (rr >= 0);
    }
    if(!resolved){
        struct in_addr ia = { .s_addr = inet_addr(host) };
        if(ia.s_addr == 0xffffffff){ log_msg("resolve fail: %s", host); goto fail; }
        in.s_addr = ia.s_addr;
    }
    int32_t fd = sceNetSocket("orbisrpcWs", ORBIS_NET_AF_INET, ORBIS_NET_SOCK_STREAM, 0);
    if(fd < 0){ log_msg("socket fail %d",fd); goto fail; }
    w->fd = fd; w->sock = fd;
    /* Pack port + IPv4 into sa_data in network byte order.
     * s_addr is a big-endian u32 value; on this LE target its FIRST octet
     * lives in the HIGH byte, so shift down from the top.
     * (v1 packed it low-byte-first -> reversed IPs -> connect() to nowhere.) */
    OrbisNetSockaddr sa; memset(&sa,0,sizeof sa);
    sa.len       = (uint8_t)sizeof sa;
    sa.sa_family = (OrbisNetSaFamily_t)ORBIS_NET_AF_INET;
    sa.sa_data[0] = (char)((port >> 8) & 0xff);
    sa.sa_data[1] = (char)(port & 0xff);
    sa.sa_data[2] = (char)((in.s_addr >> 24) & 0xff);
    sa.sa_data[3] = (char)((in.s_addr >> 16) & 0xff);
    sa.sa_data[4] = (char)((in.s_addr >> 8) & 0xff);
    sa.sa_data[5] = (char)(in.s_addr & 0xff);
    if(sceNetConnect(fd, &sa, sizeof sa) < 0){
        log_msg("connect fail to %s:%d", host, port);
        goto fail;
    }
    int on = 1;
    sceNetSetsockopt(fd, SOL_SOCKET, SO_NBIO, &on, sizeof on);
    w->nb = 1;
    /* socket already non-blocking -> SSL_connect returns WANT_*; poll it */
    SSL_CTX *ctx = SSL_CTX_new(SSLv23_client_method());
    if(!ctx){ log_msg("SSL_CTX_new fail"); goto fail; }
    SSL *ssl = SSL_new(ctx);
    if(!ssl){ log_msg("SSL_new fail"); SSL_CTX_free(ctx); goto fail; }
    w->ssl_ctx = (int32_t)(intptr_t)ctx;   /* stash early: single cleanup path */
    w->ssl     = (int32_t)(intptr_t)ssl;
    if(SSL_set_fd(ssl, (int)fd) != 1){ log_msg("SSL_set_fd fail"); goto fail; }
    /* SNI: SSL_ctrl(s, SSL_CTRL_SET_TLSEXT_HOSTNAME(55), NAMETYPE_host_name(0), host) */
    if(SSL_ctrl(ssl, 55, 0, (void *)host) != 1){ log_msg("SNI warn"); }
    SSL_set_connect_state(ssl);
    int cr=-1; int64_t t0=time(NULL);
    for(;;){
        cr = SSL_connect(ssl);
        if(cr==1) break;
        int e = SSL_get_error(ssl, cr);
        if(e==SSL_ERROR_WANT_READ || e==SSL_ERROR_WANT_WRITE){
            if(time(NULL)-t0 > 10){ log_msg("SSL_connect timeout"); goto fail; }
            usleep(20000); continue;
        }
        log_msg("SSL_connect fail err=%d", e); goto fail;
    }
    /* HTTP Upgrade handshake. Desktop-client UA, NO Origin header (native
     * clients don't send Origin to the gateway). */
    char req[640]; int n=snprintf(req,sizeof req,
        "GET %s HTTP/1.1\r\nHost: %s:%d\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
        "User-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
        "(KHTML, like Gecko) discord/1.0.9175 Chrome/122.0.6261.112 Electron/30.0.8 "
        "Safari/537.36\r\n"
        "Sec-WebSocket-Key: %s\r\nSec-WebSocket-Version: 13\r\n\r\n",
        resource?resource:"/", host, port, key);
    if(ws_send_all(w, (const unsigned char*)req, (size_t)n) < 0){ log_msg("hs write fail"); goto fail; }
    /* Read response headers; bytes past "\r\n\r\n" are the first websocket
     * frame (usually HELLO arriving early) and MUST be kept, not dropped. */
    char hdr[2048]; int hlen=0, rd; t0=time(NULL);
    while(hlen<(int)sizeof hdr-1){
        rd = SSL_read(ssl, hdr+hlen, (int)(sizeof hdr-1-hlen));
        if(rd>0){
            hlen+=rd; hdr[hlen]=0;
            if(hlen>=4 && memcmp(hdr+hlen-4,"\r\n\r\n",4)==0) break;
            continue;
        }
        int e = SSL_get_error(ssl, rd);
        if(e==SSL_ERROR_WANT_READ || e==SSL_ERROR_WANT_WRITE){
            if(time(NULL)-t0 > 10){ log_msg("hs timeout"); goto fail; }
            usleep(20000); continue;
        }
        goto fail;
    }
    if(hlen<12 || !strstr(hdr,"101")){ log_msg("no 101: %.40s", hdr); goto fail; }
    w->connected = 1;
    const char *body = strstr(hdr,"\r\n\r\n");
    if(body){
        body += 4;
        size_t bl = (size_t)(hdr+hlen-body);
        if(bl > w->rcap) bl = w->rcap;
        if(bl){ memcpy(w->rbuf, body, bl); w->rlen = bl; }
    }
    log_msg("ws: connected (handshake ok)");
    return 0;
fail:
    if(w->ssl)     SSL_free((SSL*)(intptr_t)w->ssl);
    if(w->ssl_ctx) SSL_CTX_free((SSL_CTX*)(intptr_t)w->ssl_ctx);
    if(w->fd > 0)  sceNetSocketClose(w->fd);
    free(w->rbuf); w->rbuf=NULL; w->rcap=0;
    w->connected=0; w->ssl=0; w->ssl_ctx=0; w->fd=0; w->sock=0;
    return -9;
}

int ws_send_text(ws_t *w, const char *msg, size_t len){
    if(!w->connected) return -1;
    if(len > 16384){ log_msg("ws frame too big (%zu); refusing", len); return -1; }
    unsigned char mk[4]; next_mask(mk);
    unsigned char hdr[14]; size_t f=0;
    hdr[f++]=0x81; /* FIN | text */
    if(len<126){ hdr[f++]=(unsigned char)(0x80|len); }
    else { hdr[f++]=0x80|126; hdr[f++]=(unsigned char)((len>>8)&0xff); hdr[f++]=(unsigned char)(len&0xff); }
    memcpy(hdr+f, mk, 4); f+=4;
    unsigned char *buf=(unsigned char*)malloc(f+len);
    if(!buf) return -1;
    memcpy(buf,hdr,f);
    for(size_t i=0;i<len;i++) buf[f+i]=(unsigned char)msg[i]^mk[i%4];
    int r = ws_send_all(w, buf, f+len);
    free(buf);
    return r<0?-1:r;
}

/* RFC 6455: client control frames (close/ping/pong) are masked too. */
static int ws_send_control(ws_t *w, unsigned char op){
    if(!w->connected) return -1;
    unsigned char mk[4]; next_mask(mk);
    unsigned char f[6] = { (unsigned char)(0x80|op), 0x80, mk[0], mk[1], mk[2], mk[3] };
    return ws_send_all(w, f, 6);
}

/* Peek at the pending frame header without consuming. 1 = full header ready. */
static int peek_frame(ws_t *w, size_t *hdr_out, uint64_t *plen_out){
    size_t avail = w->rlen - w->rpos;
    const unsigned char *b = w->rbuf + w->rpos;
    if(avail < 2) return 0;
    size_t plen = b[1]&0x7f, hdr = 2;
    if(plen==126){
        if(avail < 4) return 0;
        plen = (size_t)((b[2]<<8)|b[3]); hdr=4;
    } else if(plen==127){
        if(avail < 10) return 0;
        plen=0; for(int i=2;i<10;i++) plen=(plen<<8)|b[i];
        hdr=10;
    }
    *hdr_out=hdr; *plen_out=(uint64_t)plen;
    return 1;
}

int ws_recv_frame(ws_t *w, char *buf, size_t cap, int *opcode_out, int *fin_out){
    if(!w->connected) return -1;
    SSL *ssl = (SSL*)(intptr_t)w->ssl;
    for(;;){
        /* compact consumed bytes to the front */
        if(w->rpos>0){
            memmove(w->rbuf, w->rbuf+w->rpos, w->rlen-w->rpos);
            w->rlen -= w->rpos; w->rpos = 0;
        }
        if(w->skip_left>0){
            /* draining an oversized frame: drop whatever is buffered */
            size_t have = w->rlen - w->rpos;
            size_t take = have < (size_t)w->skip_left ? have : (size_t)w->skip_left;
            w->skip_left -= take; w->rpos += take;
            if(w->rpos >= w->rlen){ w->rpos=0; w->rlen=0; }
            if(w->skip_left==0){
                if(opcode_out)*opcode_out=w->skip_op;
                return -3; /* whole oversized frame skipped */
            }
        } else {
            size_t hdr; uint64_t plen;
            if(peek_frame(w,&hdr,&plen)){
                const unsigned char *b = w->rbuf + w->rpos;
                int fin=(b[0]&0x80)!=0, wire_op=b[0]&0x0f;
                /* absurd length: skip BEFORE arithmetic (hdr+plen could
                 * overflow uint64 and smuggle a tiny "total" past the cap) */
                if(plen > WS_RBUF_MAX){
                    size_t avail = w->rlen - w->rpos;
                    size_t h = (hdr < avail) ? hdr : avail;
                    w->rpos += h;                       /* eat the header */
                    size_t payload_here = avail - h;
                    w->skip_left = plen - (uint64_t)payload_here;
                    w->skip_op = wire_op;
                    if(w->rpos >= w->rlen){ w->rpos=0; w->rlen=0; }
                    continue;
                }
                uint64_t total = (uint64_t)hdr + plen;
                if(total > (uint64_t)w->rcap){
                    size_t ncap=w->rcap;
                    while(ncap < (size_t)total && ncap < WS_RBUF_MAX) ncap*=2;
                    unsigned char *nb=(unsigned char*)realloc(w->rbuf,ncap);
                    if(nb){ w->rbuf=nb; w->rcap=ncap; log_msg("ws: rbuf grown to %zu", ncap); }
                    else { log_msg("ws: rbuf grow fail"); }
                }
                if((uint64_t)(w->rlen-w->rpos) >= total){
                    size_t copy = (size_t)plen;
                    if(copy > cap-1) copy = cap-1;
                    memcpy(buf, b+hdr, copy);
                    buf[copy]=0;
                    w->rpos += (size_t)total;
                    if(w->rpos >= w->rlen){ w->rpos=0; w->rlen=0; }
                    if(opcode_out)*opcode_out=wire_op;
                    if(fin_out)*fin_out=fin;
                    return (int)copy;
                }
            }
            if(w->rlen == w->rcap){ /* full with no parseable frame: give up cleanly */
                w->rpos=0; w->rlen=0; return -2;
            }
        }
        int rd = SSL_read(ssl, w->rbuf+w->rlen, (int)(w->rcap-w->rlen));
        if(rd > 0){ w->rlen += (size_t)rd; continue; }
        int e = SSL_get_error(ssl, rd);
        if(e==SSL_ERROR_WANT_READ || e==SSL_ERROR_WANT_WRITE) return 0; /* no data yet */
        return -1; /* closed / error */
    }
}

int ws_close(ws_t *w){
    if(!w->connected){ free(w->rbuf); w->rbuf=NULL; w->rcap=0; return 0; }
    ws_send_control(w, 0x8); /* best-effort masked CLOSE */
    SSL_shutdown((SSL*)(intptr_t)w->ssl);
    SSL_free((SSL*)(intptr_t)w->ssl);
    SSL_CTX_free((SSL_CTX*)(intptr_t)w->ssl_ctx);
    sceNetSocketClose(w->fd);
    free(w->rbuf); w->rbuf=NULL; w->rcap=0;
    w->connected=0; w->sock=0; w->ssl=0; w->ssl_ctx=0; w->fd=0; w->rlen=0; w->rpos=0;
    w->skip_left=0;
    return 0;
}
