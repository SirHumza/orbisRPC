/* ws.c - WebSocket client over SceNet + PS4 LibreSSL (OpenSSL-style) TLS.
 * The SDK's libSceLibreSSL.so exports SSL_CTX_new / SSL_new / SSL_connect /
 * SSL_write / SSL_read / SSL_shutdown / SSL_set_fd / SSL_get_error (OpenSSL ABI),
 * which is what we use here for the TLS transport. */
#include "ws.h"
#include "b64.h"
#include "log.h"
#include <orbis/Net.h>
#include <orbis/Sysmodule.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

/* OpenSSL-style SSL objects (opaque). We only use pointers + the exported API. */
typedef struct ssl_ctx_st SSL_CTX;
typedef struct ssl_st SSL;

/* exported from libSceLibreSSL.so (OpenSSL ABI) */
extern SSL_CTX *SSL_CTX_new(const void *method);
extern void     SSL_CTX_free(SSL_CTX *ctx);
extern void     SSL_CTX_set_verify(SSL_CTX *ctx, int mode, void *cb);
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
extern int      SSL_set_tls_host(SSL *s, const char *hostname); /* if exported */

/* net pool id (keep once) */
static int s_net_ready = 0;
static int s_net_mem = 0;

static int net_ensure(void){
    if(s_net_ready) return 0;
    uint32_t ur = sceSysmoduleLoadModuleInternal(ORBIS_SYSMODULE_INTERNAL_NET);
    if((int)ur < 0){ log_msg("load NET fail %d", (int)ur); return -1; }
    /* sceSslInit is NOT needed for LibreSSL path; SceNet init only */
    if(sceNetInit() < 0){ log_msg("sceNetInit fail"); return -2; }
    s_net_mem = (int)sceNetPoolCreate("ps4rpNet", 128*1024, 0);
    if(s_net_mem < 0){ log_msg("net pool fail %d", s_net_mem); return -3; }
    s_net_ready = 1;
    return 0;
}

int ws_connect(ws_t *w, const char *host, int port, const char *resource, const char *key){
    memset(w,0,sizeof(*w));
    if(net_ensure()<0) return -1;
    /* resolve host */
    int32_t rid = sceNetResolverCreate("ps4rpR", 0, 0);
    OrbisNetInAddr in; memset(&in,0,sizeof in);
    int resolved = 0;
    if(rid >= 0){
        int32_t rr = sceNetResolverStartNtoa(rid, host, &in, 5, 3, 0);
        sceNetResolverDestroy(rid);
        resolved = (rr >= 0);
    }
    if(!resolved){
        struct in_addr ia = { .s_addr = inet_addr(host) };
        if(ia.s_addr == 0xffffffff){ log_msg("resolve fail: %s", host); return -4; }
        in.s_addr = ia.s_addr;
    }
    /* TCP socket */
    int32_t fd = sceNetSocket("ps4rpWs", ORBIS_NET_AF_INET, ORBIS_NET_SOCK_STREAM, 0);
    if(fd < 0){ log_msg("socket fail %d",fd); return -5; }
    OrbisNetSockaddr sa; memset(&sa,0,sizeof sa);
    sa.len = (uint8_t)sizeof(sa);
    sa.sa_family = (OrbisNetSaFamily_t)ORBIS_NET_AF_INET;
    sa.sa_data[0] = (char)((port >> 8) & 0xff);
    sa.sa_data[1] = (char)(port & 0xff);
    sa.sa_data[2] = (char)(in.s_addr & 0xff);
    sa.sa_data[3] = (char)((in.s_addr >> 8) & 0xff);
    sa.sa_data[4] = (char)((in.s_addr >> 16) & 0xff);
    sa.sa_data[5] = (char)((in.s_addr >> 24) & 0xff);
    if(sceNetConnect(fd, &sa, sizeof sa) < 0){
        log_msg("connect fail to %s:%d", host, port);
        sceNetSocketClose(fd); return -6;
    }
    w->fd = fd; w->sock = fd; w->connected = 1;
    /* TLS via LibreSSL */
    SSL_CTX *ctx = SSL_CTX_new(SSLv23_client_method());
    if(!ctx){ log_msg("SSL_CTX_new fail"); goto fail; }
    SSL *ssl = SSL_new(ctx);
    if(!ssl){ log_msg("SSL_new fail"); SSL_CTX_free(ctx); goto fail; }
    if(SSL_set_fd(ssl, (int)fd) != 1){ log_msg("SSL_set_fd fail"); SSL_free(ssl); SSL_CTX_free(ctx); goto fail; }
    SSL_set_connect_state(ssl);
    if(SSL_connect(ssl) != 1){ log_msg("SSL_connect fail"); SSL_free(ssl); SSL_CTX_free(ctx); goto fail; }
    w->ssl_ctx = (int32_t)(intptr_t)ctx;   /* stash for teardown */
    w->ssl     = (int32_t)(intptr_t)ssl;
    /* HTTP Upgrade handshake */
    char req[512]; int n=snprintf(req,sizeof req,
        "GET %s HTTP/1.1\r\nHost: %s:%d\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
        "Sec-WebSocket-Key: %s\r\nSec-WebSocket-Version: 13\r\nOrigin: https://discord.com\r\n\r\n",
        resource?resource:"/", host, port, key);
    if(SSL_write(ssl, req, n) <= 0){ log_msg("SSL_write hs fail"); goto fail; }
    char hdr[512]; int hlen=0, rd;
    while(hlen<(int)sizeof hdr-4){
        rd = SSL_read(ssl, hdr+hlen, 1);
        if(rd<=0) break;
        hlen++; hdr[hlen]=0;
        if(hlen>=4 && memcmp(hdr+hlen-4,"\r\n\r\n",4)==0) break;
    }
    if(hlen<12 || !strstr(hdr,"101")){ log_msg("no 101: %.40s", hdr); goto fail; }
    log_msg("ws: connected (handshake ok)");
    return 0;
fail:
    if(w->ssl)   SSL_free((SSL*)(intptr_t)w->ssl);
    if(w->ssl_ctx) SSL_CTX_free((SSL_CTX*)(intptr_t)w->ssl_ctx);
    sceNetSocketClose(w->fd);
    w->connected=0; w->ssl=0; w->ssl_ctx=0; w->fd=0;
    return -9;
}

int ws_send_text(ws_t *w, const char *msg, size_t len){
    if(!w->connected) return -1;
    unsigned char frame[2048]; size_t f=0;
    frame[f++]=0x81;
    if(len<126){ frame[f++]=(unsigned char)(0x80|len); }
    else if(len<65536){ frame[f++]=0x80|126; frame[f++]=(len>>8)&0xff; frame[f++]=len&0xff; }
    else { frame[f++]=0x80|127; for(int i=7;i>=0;i--)frame[f++]=(len>>(i*8))&0xff; }
    unsigned char mk[4]={0x12,0x34,0x56,0x78};
    memcpy(frame+f, mk, 4); f+=4;
    for(size_t i=0;i<len;i++) frame[f+i]=(msg[i]^mk[i%4]);
    f+=len;
    return SSL_write((SSL*)(intptr_t)w->ssl, frame, (int)f);
}

int ws_send_ping(ws_t *w){
    if(!w->connected) return -1;
    unsigned char p[2]={0x89,0x00};
    return SSL_write((SSL*)(intptr_t)w->ssl, p, 2);
}

int ws_recv_frame(ws_t *w, char *buf, size_t cap, int *opcode_out, int *fin_out){
    if(!w->connected) return -1;
    SSL *s=(SSL*)(intptr_t)w->ssl;
    unsigned char h[2];
    if(SSL_read(s, h, 2)!=2) return -1;
    int fin=(h[0]&0x80)!=0, op=h[0]&0x0f, masked=(h[1]&0x80)!=0;
    size_t plen=h[1]&0x7f;
    if(opcode_out)*opcode_out=op; if(fin_out)*fin_out=fin;
    if(plen==126){ unsigned char e[2]; if(SSL_read(s,e,2)!=2)return -1; plen=(e[0]<<8)|e[1]; }
    else if(plen==127){ unsigned char e[8]; if(SSL_read(s,e,8)!=8)return -1; plen=0; for(int i=4;i<8;i++)plen=(plen<<8)|e[i]; }
    if(masked){ unsigned char mk[4]; if(SSL_read(s,mk,4)!=4)return -1; }
    size_t i=0;
    while(i<plen){
        size_t want=plen-i; if(want>cap-1)want=cap-1;
        int rd=SSL_read(s, buf+i, (int)want);
        if(rd<=0) return -1; i+=rd;
    }
    buf[i>cap-1?cap-1:i]=0;
    return (int)i;
}

int ws_close(ws_t *w){
    if(!w->connected) return 0;
    unsigned char c[2]={0x88,0x00};
    SSL_write((SSL*)(intptr_t)w->ssl, c, 2);
    SSL_shutdown((SSL*)(intptr_t)w->ssl);
    SSL_free((SSL*)(intptr_t)w->ssl);
    SSL_CTX_free((SSL_CTX*)(intptr_t)w->ssl_ctx);
    sceNetSocketClose(w->fd);
    w->connected=0; w->sock=0; w->ssl=0; w->ssl_ctx=0; w->fd=0;
    return 0;
}