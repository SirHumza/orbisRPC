/* tmdb.c - Sony TMDB runtime resolver: plain-HTTP GET + cache.
 * Transport mirrors ws.c patterns (resolver pool, SNDTIMEO connect cap,
 * recv deadline). One attempt, no retries: failure falls through. */
#include "tmdb.h"
#include "tmdb_crypto.h"
#include "log.h"
#include <orbis/Net.h>
#include <orbis/NetCtl.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <sys/time.h>

#define TMDB_HOST "tmdb.np.dl.playstation.net"
#define TMDB_PORT 80
#define TMDB_BODY_MAX 65536
#define TMDB_DEADLINE_S 10

static int32_t s_pool = -1;
static int net_up(void){
    int r = sceNetInit();
    if(r < 0 && r != 0x80410108) return -1;
    if(s_pool < 0){
        s_pool = sceNetPoolCreate("tmdb", 4*1024, 0);
        if(s_pool < 0) s_pool = -2;
    }
    return 0;
}

/* Minimal blocking HTTP GET with deadline. Returns body bytes, or -1. */
static int http_get(const char *host, const char *path,
                    char *body, size_t cap, int *out_status){
    if(out_status) *out_status = 0;
    if(net_up() < 0) return -1;
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
    int fd = sceNetSocket("tmdb", 2, 1, 0);
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
    char req[512];
    int rl = snprintf(req, sizeof req,
        "GET %s HTTP/1.0\r\nHost: %s\r\nUser-Agent: Mozilla/5.0\r\nConnection: close\r\n\r\n",
        path, host);
    if(rl <= 0 || rl >= (int)sizeof req){ sceNetSocketClose(fd); return -1; }
    int sent = 0;
    int64_t dl = time(NULL) + TMDB_DEADLINE_S;
    while(sent < rl){
        int r = sceNetSend(fd, req+sent, rl-sent, 0);
        if(r > 0){ sent += r; continue; }
        if(time(NULL) > dl || r == 0){ sceNetSocketClose(fd); return -1; }
    }
    /* read headers then body; cap total */
    char hb[2048];
    size_t hl = 0;
    int hdr_done = 0, status = 0;
    size_t bl = 0;
    for(;;){
        char tmp[1024];
        int r = sceNetRecv(fd, tmp, sizeof tmp, 0);
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
                sceNetSocketClose(fd); return -1; /* headers too big */
            } else continue;
        }
        while(off < (size_t)r && bl < cap - 1){
            body[bl++] = tmp[off++];
        }
        if(bl >= cap - 1) break;
        if(time(NULL) > dl) break;
    }
    sceNetSocketClose(fd);
    body[bl] = 0;
    if(out_status) *out_status = status;
    if(status != 200 || bl == 0) return -1;
    return (int)bl;
}
/* 8-entry cache: one lookup per title per process lifetime. */
#define TMDB_CACHE_N 8
static struct { char id[16]; char name[128]; char icon[256]; } s_cache[TMDB_CACHE_N];
static int s_cache_n = 0;

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
    char path[128];
    if(tmdb_path(titleId, path, sizeof path) != 0) return -1;
    static char body[TMDB_BODY_MAX];
    int status = 0;
    int n = http_get(TMDB_HOST, path, body, sizeof body, &status);
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
