/* ws.c - WebSocket client over SceNet + BearSSL TLS.
 * Non-blocking friendly: tls_read() returns 0 when no data is pending, and
 * the receive buffer grows for large server frames (user-account READY
 * payloads are big); frames beyond WS_RBUF_MAX are drained and skipped.
 * All client->server frames are masked per RFC 6455 5.3, control frames too.
 */
#include "ws.h"
#include "tls.h"
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

#ifndef SOL_SOCKET
#define SOL_SOCKET 0xffff
#endif
#ifndef SO_NBIO
#define SO_NBIO 0x2000
#endif

static int s_net_ready = 0;
static int s_net_mem = 0;

static int net_ensure(void){
    if(s_net_ready) return 0;
    uint32_t ur = sceSysmoduleLoadModuleInternal(ORBIS_SYSMODULE_INTERNAL_NET);
    if((int)ur < 0){ log_msg("load NET fail %d", (int)ur); return -1; }
    if(sceNetInit() < 0){ log_msg("sceNetInit fail"); return -2; }
    s_net_mem = (int)sceNetPoolCreate("orbisrpcNet", 128*1024, 0);
    if(s_net_mem < 0){ log_msg("net pool fail %d", s_net_mem); return -3; }
    /* NOTE: deliberately NO sceNetCtlInit here — this runs inside a game
     * process, and initializing app-level NetCtl state under a live game is
     * a crash recipe. The kernel resolver works fine with just the pool. */
    s_net_ready = 1;
    return 0;
}

/* Write exactly n bytes of plaintext through the TLS engine. */
static int ws_send_all(ws_t *w, const unsigned char *data, size_t n){
    return tls_write((tls_ctx_t*)w->tls, data, n) < 0 ? -1 : (int)n;
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
    /* memid = our net pool: passing 0 here fails with EBADF (0x80410109) */
    int32_t rid = sceNetResolverCreate("orbisrpcR", s_net_mem, 0);
    OrbisNetInAddr in; memset(&in,0,sizeof in);
    int resolved = 0;
    if(rid < 0){
        log_msg("resolver create fail %d", rid);
    }else{
        int32_t rr = sceNetResolverStartNtoa(rid, host, &in, 5000000, 3, 0);
        if(rr < 0) log_msg("resolver %s err %d", host, rr);
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
    /* Pack port + IPv4 into sa_data. The resolver's OrbisNetInAddr already
     * stores the octets in wire-memory order, so copy the 4 bytes VERBATIM
     * (any >>24-style arithmetic reverses the IP — v2 dialed mirrored IPs). */
    unsigned char ipb[4];
    memcpy(ipb, &in.s_addr, 4);
    OrbisNetSockaddr sa; memset(&sa,0,sizeof sa);
    sa.len       = (uint8_t)sizeof sa;
    sa.sa_family = (OrbisNetSaFamily_t)ORBIS_NET_AF_INET;
    sa.sa_data[0] = (char)((port >> 8) & 0xff);
    sa.sa_data[1] = (char)(port & 0xff);
    sa.sa_data[2] = (char)ipb[0];
    sa.sa_data[3] = (char)ipb[1];
    sa.sa_data[4] = (char)ipb[2];
    sa.sa_data[5] = (char)ipb[3];
    log_msg("dial %s -> %u.%u.%u.%u:%d", host,
            (unsigned char)sa.sa_data[2], (unsigned char)sa.sa_data[3],
            (unsigned char)sa.sa_data[4], (unsigned char)sa.sa_data[5], port);
    if(sceNetConnect(fd, &sa, sizeof sa) < 0){
        log_msg("connect fail (syscall) to %s:%d", host, port);
        goto fail;
    }
    int on = 1;
    sceNetSetsockopt(fd, SOL_SOCKET, SO_NBIO, &on, sizeof on);
    w->nb = 1;
    log_msg("tcp established");
    /* TLS handshake over the established connection (BearSSL, no external
     * module needed). Socket is NBIO; tls_start pumps it with a deadline. */
    w->tls = tls_start(fd, host);
    if(!w->tls){ goto fail; }
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
    char hdr[2048]; int hlen=0, rd; int64_t t0=time(NULL);
    int header_end = -1;
    while(hlen<(int)sizeof hdr-1){
        rd = tls_read(w->tls, hdr+hlen, sizeof hdr-1-(size_t)hlen);
        if(rd>0){
            hlen+=rd; hdr[hlen]=0;
            for(int i=3; i<hlen; i++){
                if(hdr[i-3]=='\r' && hdr[i-2]=='\n' && hdr[i-1]=='\r' && hdr[i]=='\n'){
                    header_end = i + 1;
                    break;
                }
            }
            if(header_end >= 0) break;
            continue;
        }
        if(rd==0){
            if(time(NULL)-t0 > 10){ log_msg("hs timeout"); goto fail; }
            usleep(20000); continue;
        }
        goto fail;
    }
    if(header_end < 0 || hlen < 12 || strncmp(hdr,"HTTP/1.1 101 ",12)!=0){ log_msg("no 101: %.40s", hdr); goto fail; }
    w->connected = 1;
    if(header_end < hlen){
        const char *body = hdr + header_end;
        size_t bl = (size_t)(hdr+hlen-body);
        if(bl > w->rcap) bl = w->rcap;
        if(bl){ memcpy(w->rbuf, body, bl); w->rlen = bl; }
    }
    log_msg("ws: connected (handshake ok)");
    return 0;
fail:
    tls_free((tls_ctx_t*)w->tls); w->tls=NULL;
    if(w->fd > 0)  sceNetSocketClose(w->fd);
    free(w->rbuf); w->rbuf=NULL; w->rcap=0;
    w->connected=0; w->tls=NULL; w->fd=0; w->sock=0;
    return -9;
}

int ws_send_text(ws_t *w, const char *msg, size_t len){
    if(!w || !w->connected || (!msg && len)) return -1;
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

static int valid_frame_header(const unsigned char *b, size_t avail, uint64_t plen){
    int fin = (b[0] & 0x80) != 0;
    int rsv = b[0] & 0x70;
    int op = b[0] & 0x0f;
    int masked = (b[1] & 0x80) != 0;
    if (rsv || masked || op >= 3 || (op == 0 && fin)) return 0;
    if (op >= 8 && (!fin || plen > 125)) return 0;
    if ((b[1] & 0x7f) == 127 && (b[2] & 0x80)) return 0;
    if (op == 0 && avail >= 2 && !fin) return 0;
    return 1;
}

int ws_recv_frame(ws_t *w, char *buf, size_t cap, int *opcode_out, int *fin_out){
    if(!w || !w->connected || !buf || cap==0) return -1;
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
                if(!valid_frame_header(b, w->rlen-w->rpos, plen)){
                    log_msg("ws: protocol violation in frame header");
                    return -2;
                }
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
        int rd = tls_read((tls_ctx_t*)w->tls, (char*)w->rbuf+w->rlen, w->rcap-w->rlen);
        if(rd > 0){ w->rlen += (size_t)rd; continue; }
        if(rd == 0) return 0; /* no data yet */
        return -1; /* closed / error */
    }
}

int ws_close(ws_t *w){
    if(!w) return -1;
    if(!w->connected){
        free(w->rbuf); w->rbuf=NULL; w->rcap=0; w->rlen=0; w->rpos=0;
        return 0;
    }
    ws_send_control(w, 0x8); /* best-effort masked CLOSE */
    tls_free((tls_ctx_t*)w->tls);
    sceNetSocketClose(w->fd);
    free(w->rbuf); w->rbuf=NULL; w->rcap=0;
    w->connected=0; w->sock=0; w->tls=NULL; w->fd=0; w->rlen=0; w->rpos=0;
    w->skip_left=0;
    return 0;
}
