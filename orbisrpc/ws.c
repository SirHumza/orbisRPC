/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 SirHumza and CyberMask367 - see NOTICE for attribution terms.
 */
/* ws.c - WebSocket client over SceNet + mbedTLS TLS.
 * Non-blocking friendly: tls_read() returns 0 when no data is pending, and
 * the receive buffer grows for large server frames (user-account READY
 * payloads are big); frames beyond WS_RBUF_MAX are drained and skipped.
 * All client->server frames are masked per RFC 6455 5.3, control frames too.
 */
#include "ws.h"
#include "clock.h"
#include "tls.h"
#include "log.h"
#ifdef ORBISRPC_SDK_PAYLOAD
/* Payload-SDK build: plain BSD sockets, no Sony modules. getaddrinfo
 * replaces the kernel resolver; nothing needs initializing. */
#include "netconnect.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <fcntl.h>
#include <errno.h>
#include <stdio.h>
#else
#include <fcntl.h>
#include <orbis/Net.h>
#include <orbis/Sysmodule.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#endif
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <sys/time.h>

#ifndef SOL_SOCKET
#define SOL_SOCKET 0xffff
#endif
#ifndef SO_NBIO
#define SO_NBIO 0x2000
#endif

static int s_net_ready = 0;
#ifndef ORBISRPC_SDK_PAYLOAD
static int s_net_mem = 0;
#endif

static int net_ensure(void){
    if(s_net_ready) return 0;
#ifdef ORBISRPC_SDK_PAYLOAD
    /* BSD sockets need no init. */
    s_net_ready = 1;
    return 0;
#else
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
#endif
}

/* Public wrapper: the daemon's startup DNS probe needs the stack ready
 * before any ws_connect(). */
int ws_net_init(void){ return net_ensure(); }

/* Write exactly n bytes of plaintext through the TLS engine. */
static int ws_send_all(ws_t *w, const unsigned char *data, size_t n){
    return tls_write((tls_ctx_t*)w->tls, data, n) < 0 ? -1 : (int)n;
}

static void next_mask(unsigned char mk[4]){
    /* RFC 6455 masking key: fresh bytes from /dev/urandom (the same strong
     * source as TLS). The LCG below is a last-resort fallback for a
     * console with no urandom at all; masking is obfuscation, not secrecy,
     * but there is no reason to use a predictable key when OS randomness
     * exists. */
    int fd = open("/dev/urandom", O_RDONLY);
    if(fd >= 0){
        size_t got = 0;
        while(got < 4){
            long r = read(fd, (char *)mk + got, 4 - got);
            if(r <= 0) break;
            got += (size_t)r;
        }
        close(fd);
        if(got == 4) return;
    }
    static uint32_t mk_seed;
    if(!mk_seed) mk_seed = (uint32_t)orbis_mono_s() ^ 0x9e3779b9u ^ (uint32_t)(uintptr_t)&mk_seed;
    mk_seed = mk_seed*1664525u + 1013904223u;
    mk[0]=(unsigned char)(mk_seed&0xff);       mk[1]=(unsigned char)((mk_seed>>8)&0xff);
    mk[2]=(unsigned char)((mk_seed>>16)&0xff); mk[3]=(unsigned char)((mk_seed>>24)&0xff);
}

size_t ws_make_handshake(char *out, size_t cap, const char *host,
                         const char *resource, const char *key,
                         const char *user_agent){
    if(!out || cap == 0 || !host || !key) return 0;
    if(!user_agent || !user_agent[0]) user_agent = "orbisRPC";
    int n = snprintf(out, cap,
        "GET %s HTTP/1.1\r\nHost: %s\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
        "User-Agent: %s\r\n"
        "Sec-WebSocket-Key: %s\r\nSec-WebSocket-Version: 13\r\n\r\n",
        resource ? resource : "/", host, user_agent, key);
    if(n <= 0 || (size_t)n >= cap) return 0;
    return (size_t)n;
}

int ws_connect(ws_t *w, const char *host, int port, const char *resource,
               const char *key, const char *user_agent){
    memset(w,0,sizeof(*w));
    w->rcap = WS_RBUF_MIN;
    w->rbuf = (unsigned char*)malloc(w->rcap);
    if(!w->rbuf){ log_msg("ws: rbuf alloc fail"); return -1; }
    if(net_ensure()<0) goto fail;
#ifdef ORBISRPC_SDK_PAYLOAD
    /* Bounded non-blocking connect: SO_SNDTIMEO does not bound connect(),
     * and a blackholed route used to freeze the only thread for the
     * kernel's SYN budget. The helper leaves the fd non-blocking. */
    int fd = net_tcp_connect(host, port, 10);
    if(fd < 0){ log_msg("connect fail to %s:%d", host, port); goto fail; }
    w->fd = fd; w->sock = fd;
    {
        /* Both timeouts stay armed for the socket's whole life (plus
         * NBIO below): a stuck peer can never wedge the daemon silently —
         * the failure observed on hardware was a frozen process with zero
         * output and no timeout to break it. */
        struct timeval tv0 = { .tv_sec = 15, .tv_usec = 0 };
        setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv0, sizeof tv0);
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv0, sizeof tv0);
    }
    w->nb = 1;
    log_msg("tcp established");
#else
    /* memid = our net pool: passing 0 here fails with EBADF (0x80410109).
     * If pool creation failed, skip DNS and only allow IP literals. */
    int32_t rid = -1;
    OrbisNetInAddr in; memset(&in,0,sizeof in);
    int resolved = 0;
    if(s_net_mem < 0){
        log_msg("net pool invalid; skipping resolver");
    }else{
    rid = sceNetResolverCreate("orbisrpcR", s_net_mem, 0);
    if(rid < 0){
        log_msg("resolver create fail %d", rid);
    }else{
        int32_t rr = sceNetResolverStartNtoa(rid, host, &in, 5000000, 3, 0);
        if(rr < 0) log_msg("resolver %s err %d", host, rr);
        sceNetResolverDestroy(rid);
        resolved = (rr >= 0);
    }
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
    /* Bound the blocking connect: 10s send timeout so a dead route can't
     * hang the daemon thread forever. */
    {
        struct timeval tv = { .tv_sec = 10, .tv_usec = 0 };
        sceNetSetsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
    }
    if(sceNetConnect(fd, &sa, sizeof sa) < 0){
        log_msg("connect fail (syscall) to %s:%d", host, port);
        goto fail;
    }
    {
        struct timeval tv0 = { .tv_sec = 0, .tv_usec = 0 };
        sceNetSetsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv0, sizeof tv0);
    }
    int on = 1;
    sceNetSetsockopt(fd, SOL_SOCKET, SO_NBIO, &on, sizeof on);
    w->nb = 1;
    log_msg("tcp established");
#endif
    /* TLS handshake over the established connection (mbedTLS, no external
     * module needed). Socket is NBIO; tls_start pumps it with a deadline. */
    w->tls = tls_start(fd, host);
    if(!w->tls){ goto fail; }
    /* HTTP Upgrade handshake. The UA comes from the same identity tuple as
     * IDENTIFY and REST; NO Origin header (native clients don't send Origin
     * to the gateway). Host has no :port — browsers never send the
     * default-port suffix and the front side ignores requests it can't
     * route. */
    char req[1024];
    size_t n = ws_make_handshake(req, sizeof req, host,
                                 resource ? resource : "/", key, user_agent);
    if(n == 0){ log_msg("hs request too long"); goto fail; }
    if(ws_send_all(w, (const unsigned char*)req, n) < 0){ log_msg("hs write fail"); goto fail; }
    log_msg("hs request sent (%zuB)", n);
    /* Read response headers; bytes past "\r\n\r\n" are the first websocket
     * frame (usually HELLO arriving early) and MUST be kept, not dropped. */
    char hdr[2048]; int hlen=0, rd; int64_t t0=orbis_mono_s();
    int header_end = -1;
    extern int daemon_stop_requested(void) __attribute__((weak));
    while(hlen<(int)sizeof hdr-1){
        if(daemon_stop_requested && daemon_stop_requested()){ log_msg("ws: hs aborted (stop)"); goto fail; }
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
            if(orbis_mono_s()-t0 > 10){ log_msg("hs timeout (got %dB)", hlen); goto fail; }
            usleep(20000); continue;
        }
        log_msg("hs read fail (got %dB)", hlen);
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
#ifdef ORBISRPC_SDK_PAYLOAD
    if(w->fd > 0) close(w->fd);
#else
    if(w->fd > 0)  sceNetSocketClose(w->fd);
#endif
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
    /* Small frames (heartbeats, presence) avoid malloc churn in long sessions */
    unsigned char stack[1024];
    unsigned char *buf = stack;
    int use_heap = (f + len > sizeof stack);
    if(use_heap){
        buf = (unsigned char*)malloc(f+len);
        if(!buf) return -1;
    }
    memcpy(buf,hdr,f);
    for(size_t i=0;i<len;i++) buf[f+i]=(unsigned char)msg[i]^mk[i%4];
    int r = ws_send_all(w, buf, f+len);
    if(use_heap) free(buf);
    return r<0?-1:r;
}

/* RFC 6455: client control frames (close/ping/pong) are masked too. */
static int ws_send_control(ws_t *w, unsigned char op){
    if(!w->connected) return -1;
    unsigned char mk[4]; next_mask(mk);
    unsigned char f[6] = { (unsigned char)(0x80|op), 0x80, mk[0], mk[1], mk[2], mk[3] };
    return ws_send_all(w, f, 6);
}

/* Answer a server PING with an (empty) PONG so the gateway keeps us. */
int ws_pong(ws_t *w){
    return ws_send_control(w, 0xA);
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

static int valid_frame_header(const unsigned char *b, uint64_t plen){
    int fin = (b[0] & 0x80) != 0;
    int rsv = b[0] & 0x70;
    int op = b[0] & 0x0f;
    int masked = (b[1] & 0x80) != 0;
    if (rsv || masked) return 0;
    if (op >= 3 && op <= 7) return 0; /* reserved data opcodes */
    if (op >= 0xB) return 0;          /* reserved control opcodes */
    if (op >= 8 && (!fin || plen > 125)) return 0; /* control: final, <=125 */
    if ((b[1] & 0x7f) == 127 && (b[2] & 0x80)) return 0; /* absurd 64-bit length */
    return 1;
}

/* A message split across frames is bounded by the same 8 MB the single-frame
 * path already allows; anything larger is dropped and the caller told so. */
#define WS_MSG_MAX WS_RBUF_MAX

static void frag_reset(ws_t *w){
    free(w->frag);
    w->frag = NULL;
    w->frag_len = 0;
    w->frag_cap = 0;
    w->frag_op = 0;
    w->in_frag = 0;
    w->frag_drop = 0;
}

/* Append one fragment to the reassembly buffer. -1 when the message would
 * exceed WS_MSG_MAX or the allocation fails; the caller then discards the
 * remaining fragments so the stream stays in sync. */
static int frag_append(ws_t *w, const unsigned char *p, size_t n){
    if(n == 0) return 0;
    if(w->frag_len > WS_MSG_MAX || n > WS_MSG_MAX - w->frag_len) return -1;
    size_t need = w->frag_len + n;
    if(need > w->frag_cap){
        size_t ncap = w->frag_cap ? w->frag_cap : 4096;
        while(ncap < need) ncap *= 2;
        if(ncap > WS_MSG_MAX) ncap = WS_MSG_MAX;
        unsigned char *nb = (unsigned char*)realloc(w->frag, ncap);
        if(!nb) return -1;
        w->frag = nb;
        w->frag_cap = ncap;
    }
    memcpy(w->frag + w->frag_len, p, n);
    w->frag_len = need;
    return 0;
}

/* Deliver one complete message. Buffer mode copies at most cap-1 bytes and
 * returns that count (the old behaviour); heap mode returns the whole
 * payload, which the READY handler needs because session_id and
 * resume_gateway_url can sit past the first couple of KB. */
static int frame_deliver(char *buf, size_t cap, char **heap_out,
                         size_t *heap_len, const unsigned char *src,
                         size_t n, int op, int fin,
                         int *opcode_out, int *fin_out){
    if(heap_out){
        char *p = (char*)malloc(n + 1);
        if(!p) return -1;
        if(n) memcpy(p, src, n);
        p[n] = 0;
        *heap_out = p;
        if(heap_len) *heap_len = n;
        if(opcode_out) *opcode_out = op;
        if(fin_out) *fin_out = fin;
        return (int)n;
    }
    size_t copy = n;
    if(copy > cap - 1) copy = cap - 1;
    if(copy) memcpy(buf, src, copy);
    buf[copy] = 0;
    if(heap_len) *heap_len = copy;
    if(opcode_out) *opcode_out = op;
    if(fin_out) *fin_out = fin;
    return (int)copy;
}

static int recv_frame_core(ws_t *w, char *buf, size_t cap, char **heap_out,
                           size_t *heap_len, int *opcode_out, int *fin_out){
    if(!w || !w->connected) return -1;
    if(!heap_out && (!buf || cap == 0)) return -1;
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
                if(w->in_frag) frag_reset(w);  /* message can't be reassembled */
                if(opcode_out)*opcode_out=w->skip_op;
                return -3; /* whole oversized frame skipped */
            }
        } else {
            size_t hdr; uint64_t plen;
            if(peek_frame(w,&hdr,&plen)){
                const unsigned char *b = w->rbuf + w->rpos;
                int fin=(b[0]&0x80)!=0, wire_op=b[0]&0x0f;
                if(!valid_frame_header(b, plen)){
                    log_msg("ws: protocol violation op=%d fin=%d plen=%llu b1=0x%02x b2=0x%02x",
                        b[0]&0x0f, (b[0]&0x80)!=0, (unsigned long long)plen, b[0], b[1]);
                    return -2;
                }
                /* absurd length: skip BEFORE arithmetic (hdr+plen could
                 * overflow uint64 and smuggle a tiny "total" past the cap) */
                if(plen > WS_RBUF_MAX){
                    log_msg("ws: oversized frame op=%d fin=%d plen=%llu (cap %u)",
                        b[0]&0x0f, (b[0]&0x80)!=0, (unsigned long long)plen, WS_RBUF_MAX);
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
                    const unsigned char *payload = b + hdr;
                    size_t plen_sz = (size_t)plen;

                    /* Control frames may be injected inside a fragmented
                     * message (RFC 6455 5.4). Deliver them immediately and
                     * keep the reassembly buffer untouched. */
                    if(wire_op >= 8){
                        int r = frame_deliver(buf, cap, heap_out, heap_len,
                                              payload, plen_sz, wire_op, fin,
                                              opcode_out, fin_out);
                        w->rpos += (size_t)total;
                        if(w->rpos >= w->rlen){ w->rpos=0; w->rlen=0; }
                        return r;
                    }

                    if(wire_op == 0){
                        /* Continuation: only valid after a FIN=0 start. */
                        if(!w->in_frag){
                            log_msg("ws: continuation frame with no start");
                            w->rpos += (size_t)total;
                            if(w->rpos >= w->rlen){ w->rpos=0; w->rlen=0; }
                            return -2;
                        }
                        if(!w->frag_drop && frag_append(w, payload, plen_sz) < 0){
                            log_msg("ws: fragmented message over %uB; dropping",
                                    (unsigned)WS_MSG_MAX);
                            w->frag_drop = 1;
                        }
                        w->rpos += (size_t)total;
                        if(w->rpos >= w->rlen){ w->rpos=0; w->rlen=0; }
                        if(!fin) continue;
                        if(w->frag_drop){ frag_reset(w); return -3; }
                        {
                            int msg_op = w->frag_op;
                            int r = frame_deliver(buf, cap, heap_out, heap_len,
                                                  w->frag, w->frag_len,
                                                  msg_op, 1, opcode_out, fin_out);
                            frag_reset(w);
                            return r;
                        }
                    }

                    /* A new data message while another is still open is a
                     * protocol violation, not something to splice together. */
                    if(w->in_frag){
                        log_msg("ws: new data frame inside fragmented message");
                        w->rpos += (size_t)total;
                        if(w->rpos >= w->rlen){ w->rpos=0; w->rlen=0; }
                        frag_reset(w);
                        return -2;
                    }

                    if(!fin){
                        /* First fragment: buffer it and wait for the rest. */
                        w->in_frag = 1;
                        w->frag_op = wire_op;
                        if(frag_append(w, payload, plen_sz) < 0){
                            log_msg("ws: fragmented message over %uB; dropping",
                                    (unsigned)WS_MSG_MAX);
                            w->frag_drop = 1;
                        }
                        w->rpos += (size_t)total;
                        if(w->rpos >= w->rlen){ w->rpos=0; w->rlen=0; }
                        continue;
                    }

                    /* Complete single-frame message. Buffer callers get the
                     * JSON prefix they need (op/t/s) and the rest is
                     * drained; heap callers get every byte. */
                    {
                        int r = frame_deliver(buf, cap, heap_out, heap_len,
                                              payload, plen_sz, wire_op, fin,
                                              opcode_out, fin_out);
                        w->rpos += (size_t)total;
                        if(w->rpos >= w->rlen){ w->rpos=0; w->rlen=0; }
                        return r;
                    }
                }
            }
            if(w->rlen == w->rcap){ /* full with no parseable frame: give up cleanly */
                log_msg("ws: buffer full with no frame; dropping %zuB and reconnecting", w->rlen);
                w->rpos=0; w->rlen=0; return -2;
            }
        }
        int rd = tls_read((tls_ctx_t*)w->tls, (char*)w->rbuf+w->rlen, w->rcap-w->rlen);
        if(rd > 0){ w->rlen += (size_t)rd; continue; }
        if(rd == 0) return 0; /* no data yet */
        return -1; /* closed / error */
    }
}

int ws_recv_frame(ws_t *w, char *buf, size_t cap, int *opcode_out, int *fin_out){
    return recv_frame_core(w, buf, cap, NULL, NULL, opcode_out, fin_out);
}

int ws_recv_alloc(ws_t *w, char **out, size_t *out_len, int *opcode_out, int *fin_out){
    if(!out) return -1;
    *out = NULL;
    if(out_len) *out_len = 0;
    return recv_frame_core(w, NULL, 0, out, out_len, opcode_out, fin_out);
}

int ws_close(ws_t *w){
    if(!w) return -1;
    /* Tear down transport resources even when a peer drop already cleared
     * `connected`. The old early return leaked the TLS context and socket on
     * every heartbeat timeout / send failure, because the daemon only calls
     * ws_close() after discord_tick() has set connected=0. */
    if(w->connected) ws_send_control(w, 0x8); /* best-effort masked CLOSE */
    if(w->tls) tls_free((tls_ctx_t*)w->tls);
#ifdef ORBISRPC_SDK_PAYLOAD
    if(w->fd > 0) close(w->fd);
#else
    if(w->fd > 0) sceNetSocketClose(w->fd);
#endif
    free(w->rbuf); w->rbuf=NULL; w->rcap=0;
    frag_reset(w);
    w->connected=0; w->sock=0; w->tls=NULL; w->fd=0; w->rlen=0; w->rpos=0;
    w->skip_left=0;
    return 0;
}
