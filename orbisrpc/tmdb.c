/* tmdb.c - Sony TMDB runtime resolver: plain-HTTP GET + cache.
 * Transport mirrors ws.c patterns (resolver pool, SNDTIMEO connect cap,
 * recv deadline). One attempt, no retries: failure falls through. */
#include "tmdb.h"
#include "clock.h"
#include "tmdb_crypto.h"
#include "log.h"
#include "tls.h"
#include "updater_http.h"
#ifdef ORBISRPC_SDK_PAYLOAD
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#else
#include <orbis/Net.h>
#include <orbis/Sysmodule.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#endif
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <sys/time.h>

#define TMDB_HOST "tmdb.np.dl.playstation.net"
#define TMDB_PORT 80
#define TMDB_BODY_MAX 65536
#define TMDB_DEADLINE_S 10
#define TMDB_HTTPS_DEADLINE_S 12

static int32_t s_pool = -1;
static int s_ready = 0;
/* Mirror ws.c net_ensure exactly: sysmodule first, static-once guard,
 * 128KB pool (4KB starves the resolver), and deliberately NO NetCtl
 * init inside game processes. */
static int net_up(void){
    if(s_ready) return 0;
#ifdef ORBISRPC_SDK_PAYLOAD
    /* BSD sockets need no init. */
    s_ready = 1;
    return 0;
#else
    uint32_t ur = sceSysmoduleLoadModuleInternal(ORBIS_SYSMODULE_INTERNAL_NET);
    if((int)ur < 0){ log_msg("tmdb: load NET fail %d", (int)ur); return -1; }
    if(sceNetInit() < 0){ log_msg("tmdb: sceNetInit fail"); return -1; }
    s_pool = (int32_t)sceNetPoolCreate("tmdb", 128*1024, 0);
    if(s_pool < 0){ log_msg("tmdb: net pool fail %d", (int)s_pool); return -1; }
    s_ready = 1;
    return 0;
#endif
}

/* Minimal blocking HTTP GET with deadline. Returns body bytes, or -1. */
static int http_get(const char *host, const char *path,
                    char *body, size_t cap, int *out_status){
    if(out_status) *out_status = 0;
    if(net_up() < 0) return -1;
#ifdef ORBISRPC_SDK_PAYLOAD
    struct addrinfo hints, *res = NULL;
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    {
        char portbuf[16];
        snprintf(portbuf, sizeof portbuf, "%d", TMDB_PORT);
        if(getaddrinfo(host, portbuf, &hints, &res) != 0 || !res){
            log_msg("tmdb: dns fail");
            return -1;
        }
    }
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if(fd < 0){ log_msg("tmdb: socket fail"); freeaddrinfo(res); return -1; }
    struct timeval tv = { .tv_sec = TMDB_DEADLINE_S, .tv_usec = 0 };
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    if(connect(fd, res->ai_addr, res->ai_addrlen) < 0){
        log_msg("tmdb: connect fail"); freeaddrinfo(res); close(fd); return -1;
    }
    freeaddrinfo(res);
#else
    OrbisNetInAddr in;
    memset(&in, 0, sizeof in);
    int ok = 0;
    if(s_pool >= 0){
        int32_t rid = sceNetResolverCreate("tmdbR", (uint32_t)s_pool, 0);
        if(rid >= 0){
            if(sceNetResolverStartNtoa(rid, host, &in, 5000000, 3, 0) >= 0) ok = 1;
            sceNetResolverDestroy(rid);
        }
    }
    if(!ok){
        /* IP literal fallback */
        unsigned a, b, c, d;
        if(sscanf(host, "%u.%u.%u.%u", &a, &b, &c, &d) != 4) return -1;
        if(a > 255 || b > 255 || c > 255 || d > 255) return -1;
        uint32_t ip = (uint32_t)((a<<24)|(b<<16)|(c<<8)|d);
        in.s_addr = sceNetHtonl(ip);
    }
    int fd = sceNetSocket("tmdb", ORBIS_NET_AF_INET, ORBIS_NET_SOCK_STREAM, 0);
    if(fd < 0){ log_msg("tmdb: socket fail"); return -1; }
    struct timeval tv = { .tv_sec = TMDB_DEADLINE_S, .tv_usec = 0 };
    sceNetSetsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
    sceNetSetsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    OrbisNetSockaddr sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_family = 2;
    *(uint16_t*)sa.sa_data = sceNetHtons((uint16_t)TMDB_PORT);
    memcpy(sa.sa_data+2, &in.s_addr, 4);
    if(sceNetConnect(fd, &sa, sizeof sa) < 0){
        log_msg("tmdb: connect fail"); sceNetSocketClose(fd); return -1;
    }
#endif
    char req[512];
    int rl = snprintf(req, sizeof req,
        "GET %s HTTP/1.0\r\nHost: %s\r\nUser-Agent: Mozilla/5.0\r\nConnection: close\r\n\r\n",
        path, host);
    if(rl <= 0 || rl >= (int)sizeof req){
#ifdef ORBISRPC_SDK_PAYLOAD
        close(fd);
#else
        sceNetSocketClose(fd);
#endif
        return -1;
    }
    int sent = 0;
    int64_t dl = orbis_mono_s() + TMDB_DEADLINE_S;
    while(sent < rl){
#ifdef ORBISRPC_SDK_PAYLOAD
        int r = (int)send(fd, req+sent, (size_t)(rl-sent), 0);
        if(r > 0){ sent += r; continue; }
        if(orbis_mono_s() > dl || r == 0){ close(fd); return -1; }
#else
        int r = sceNetSend(fd, req+sent, rl-sent, 0);
        if(r > 0){ sent += r; continue; }
        if(orbis_mono_s() > dl || r == 0){ sceNetSocketClose(fd); return -1; }
#endif
    }
    /* read headers then body; cap total */
    char hb[2048];
    size_t hl = 0;
    int hdr_done = 0, status = 0;
    size_t bl = 0;
    for(;;){
        char tmp[1024];
#ifdef ORBISRPC_SDK_PAYLOAD
        int r = (int)recv(fd, tmp, sizeof tmp, 0);
#else
        int r = sceNetRecv(fd, tmp, sizeof tmp, 0);
#endif
        if(r <= 0) break;
        size_t off = 0;
        if(!hdr_done){
            size_t room = sizeof hb - 1 - hl;
            size_t cp = (size_t)r < room ? (size_t)r : room;
            memcpy(hb+hl, tmp, cp); hl += cp; hb[hl] = 0;
            char *e = strstr(hb, "\r\n\r\n");
            if(e){
                hdr_done = 1;
                if(!strncmp(hb, "HTTP/1.", 7)) status = atoi(hb+9);
                off = (size_t)(e + 4 - hb) - (hl - cp);
                /* off = body bytes already in this chunk */
                if(off > (size_t)r) off = (size_t)r;
            } else if(hl >= sizeof hb - 1){
#ifdef ORBISRPC_SDK_PAYLOAD
                close(fd); return -1; /* headers too big */
#else
                sceNetSocketClose(fd); return -1; /* headers too big */
#endif
            } else continue;
        }
        while(off < (size_t)r && bl < cap - 1){
            body[bl++] = tmp[off++];
        }
        if(bl >= cap - 1) break;
        if(orbis_mono_s() > dl) break;
    }
#ifdef ORBISRPC_SDK_PAYLOAD
    close(fd);
#else
    sceNetSocketClose(fd);
#endif
    body[bl] = 0;
    if(out_status) *out_status = status;
    if(status != 200 || bl == 0) return -1;
    return (int)bl;
}
/* 8-entry cache: one lookup per title per process lifetime. */
#define TMDB_CACHE_N 8
static struct { char id[16]; char name[128]; char icon[256]; } s_cache[TMDB_CACHE_N];
static int s_cache_n = 0;


/* HTTPS GET over TLS (port 443) for the same TMDB paths. Uses the shared
 * chunked-aware HTTP parser. Returns body bytes or <=0. */
static int https_get_tmdb(const char *path, char *out, size_t cap, int *out_status){
    if(out_status) *out_status = 0;
    if(!path || !out || cap < 2) return -1;
    if(net_up() < 0) return -1;
#ifdef ORBISRPC_SDK_PAYLOAD
    struct addrinfo hints, *res = NULL;
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    if(getaddrinfo(TMDB_HOST, "443", &hints, &res) != 0 || !res){
        log_msg("tmdb: https dns fail");
        return -1;
    }
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if(fd < 0){ freeaddrinfo(res); return -1; }
    struct timeval tv = { .tv_sec = TMDB_HTTPS_DEADLINE_S, .tv_usec = 0 };
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
    if(connect(fd, res->ai_addr, res->ai_addrlen) < 0){
        log_msg("tmdb: https connect fail");
        freeaddrinfo(res); close(fd); return -1;
    }
    freeaddrinfo(res);
    {
        int fl = fcntl(fd, F_GETFL, 0);
        if(fl >= 0) fcntl(fd, F_SETFL, fl | O_NONBLOCK);
    }
    tls_ctx_t *t = tls_start(fd, TMDB_HOST);
    if(!t){
        close(fd);
        return -1;
    }
    char req[512];
    int rl = snprintf(req, sizeof req,
        "GET %s HTTP/1.1\r\nHost: %s\r\nUser-Agent: Mozilla/5.0\r\nConnection: close\r\n\r\n",
        path, TMDB_HOST);
    if(rl <= 0 || rl >= (int)sizeof req){ tls_free(t); close(fd); return -1; }
    if(tls_write(t, req, (size_t)rl) < 0){ tls_free(t); close(fd); return -1; }
    static char raw[TMDB_BODY_MAX];
    size_t bl = 0;
    int64_t dl = orbis_mono_s() + TMDB_HTTPS_DEADLINE_S + 10;
    for(;;){
        char tmp[1024];
        int r = tls_read(t, tmp, sizeof tmp - 1);
        if(r < 0) break;
        if(r == 0){
            if(orbis_mono_s() > dl) break;
            usleep(20000);
            continue;
        }
        if(bl + (size_t)r >= sizeof raw) break;
        memcpy(raw + bl, tmp, (size_t)r);
        bl += (size_t)r;
        if(orbis_mono_s() > dl) break;
    }
    tls_free(t);
    close(fd);
    if(bl == 0) return -1;
    int st = 0;
    size_t olen = 0;
    char *body = upd_parse_response(raw, bl, cap, &st, &olen, NULL, 0);
    if(out_status) *out_status = st;
    if(!body || st != 200){ free(body); return -1; }
    if(olen >= cap){ free(body); return -1; }
    memcpy(out, body, olen);
    out[olen] = 0;
    free(body);
    return (int)olen;
#else
    return -1; /* OpenOrbis app builds keep the plain-HTTP path only. */
#endif
}

int tmdb_resolve(const char *titleId, char *name, size_t name_cap,
                 char *icon, size_t icon_cap){
    if(!titleId || !name || name_cap == 0) return -1;
    name[0] = 0; if(icon && icon_cap) icon[0] = 0;
    for(int i = 0; i < s_cache_n; i++){
        if(!strcmp(s_cache[i].id, titleId)){
            strncpy(name, s_cache[i].name, name_cap-1); name[name_cap-1] = 0;
            if(icon && icon_cap){ strncpy(icon, s_cache[i].icon, icon_cap-1); icon[icon_cap-1] = 0; }
            return name[0] ? 0 : -1;
        }
    }
    /* Live Sony CDN over TLS first (plain HTTP is blocked on jailbroken
     * consoles — http_get below is last-resort only). No baked tables:
     * CUSA code + Sony CDN is the source of truth. */
    char path[128];
    if(tmdb_path(titleId, path, sizeof path) != 0) return -1;
    static char body[TMDB_BODY_MAX];
    int status = 0;
    int n = https_get_tmdb(path, body, sizeof body, &status);
    if(n > 0) log_msg("tmdb: live via https for %s", titleId);
    if(n <= 0){
        /* Plain HTTP (port 80): blocked from jailbroken consoles, so this
         * burns the connect timeout before giving up — keep it last. */
        n = http_get(TMDB_HOST, path, body, sizeof body, &status);
    }
    if(n <= 0){ log_msg("tmdb: fetch fail status=%d", status); return -1; }
    char iname[128] = "", iicon[256] = "";
    if(tmdb_parse(body, (size_t)n, iname, sizeof iname, iicon, sizeof iicon) != 0){
        log_msg("tmdb: parse fail for %s", titleId);
        return -1;
    }
    strncpy(name, iname, name_cap-1); name[name_cap-1] = 0;
    if(icon && icon_cap){ strncpy(icon, iicon, icon_cap-1); icon[icon_cap-1] = 0; }
    if(s_cache_n < TMDB_CACHE_N){
        strncpy(s_cache[s_cache_n].id, titleId, 15);
        strncpy(s_cache[s_cache_n].name, iname, 127);
        strncpy(s_cache[s_cache_n].icon, iicon, 255);
        s_cache_n++;
    }
    log_msg("name: %s via tmdb", name);
    return 0;
}
