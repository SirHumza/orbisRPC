/* updater.c - self-updater. Pure logic (host-tested) plus PS4 HTTPS.
 *
 * Flow, once per daemon boot when enabled:
 *   GET https://api.github.com/repos/<repo>/releases/latest
 *   -> tag_name + asset browser_download_urls
 *   if tag newer than ORBISRPC_VERSION:
 *     download each known asset (cap 4MB), validate ELF magic,
 *     write <target>.new, rename over target.
 * Plugin .prx takes effect on next game launch; payload .bin on next
 * injection. Never deletes, never writes unvalidated bytes.
 */
#include "updater.h"
#include "version.h"
#include "jsonlite.h"
#include "log.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

/* ---- PS4 HTTPS transport (mirrors ws.c/tmdb.c bring-up) ---- */
#include "tls.h"
#include <orbis/Net.h>
#include <orbis/Sysmodule.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#define UPD_HOST "api.github.com"
#define UPD_DEADLINE_S 15
#define UPD_BODY_MAX (4u*1024u*1024u)

#ifndef SO_NBIO
#define SO_NBIO 0x2000   /* same local define as ws.c; absent from SDK headers */
#endif

static int32_t s_upool = -1;
static int upd_net(void){
    static int ready = 0;
    if(ready) return 0;
    uint32_t ur = sceSysmoduleLoadModuleInternal(ORBIS_SYSMODULE_INTERNAL_NET);
    if((int)ur < 0) return -1;
    if(sceNetInit() < 0) return -1;
    s_upool = (int32_t)sceNetPoolCreate("upd", 128*1024, 0);
    if(s_upool < 0) return -1;
    ready = 1;
    return 0;
}

static int upd_connect(const char *host, int port){
    if(upd_net() < 0) return -1;
    OrbisNetInAddr in;
    memset(&in, 0, sizeof in);
    int32_t rid = sceNetResolverCreate("updR", (uint32_t)s_upool, 0);
    if(rid < 0) return -1;
    int32_t rr = sceNetResolverStartNtoa(rid, host, &in, 8000000, 3, 0);
    sceNetResolverDestroy(rid);
    if(rr < 0){ log_msg("updater: dns fail"); return -1; }
    int fd = sceNetSocket("upd", ORBIS_NET_AF_INET, ORBIS_NET_SOCK_STREAM, 0);
    if(fd < 0) return -1;
    struct timeval tv = { .tv_sec = UPD_DEADLINE_S, .tv_usec = 0 };
    sceNetSetsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
    OrbisNetSockaddr sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_family = 2;
    *(uint16_t*)sa.sa_data = sceNetHtons((uint16_t)port);
    memcpy(sa.sa_data+2, &in.s_addr, 4);
    if(sceNetConnect(fd, &sa, sizeof sa) < 0){
        sceNetSocketClose(fd); return -1;
    }
    int nb = 1;
    sceNetSetsockopt(fd, SOL_SOCKET, SO_NBIO, &nb, sizeof nb);
    return fd;
}

/* HTTPS GET, returns heap body (caller frees) with out_len/out_status. */
static char *https_get(const char *host, const char *path,
                       size_t cap, int *out_status, size_t *out_len){
    if(out_status) *out_status = 0;
    if(out_len) *out_len = 0;
    int fd = upd_connect(host, 443);
    if(fd < 0) return NULL;
    tls_ctx_t *t = tls_start(fd, host);
    if(!t){ sceNetSocketClose(fd); return NULL; }
    char req[512];
    int rl = snprintf(req, sizeof req,
        "GET %s HTTP/1.1\r\nHost: %s\r\nUser-Agent: orbisRPC/%s\r\nConnection: close\r\n\r\n",
        path, host, ORBISRPC_VERSION);
    if(rl <= 0 || rl >= (int)sizeof req){ tls_free(t); return NULL; }
    if(tls_write(t, req, (size_t)rl) < 0){ tls_free(t); return NULL; }
    char *body = (char*)malloc(cap);
    if(!body){ tls_free(t); return NULL; }
    size_t bl = 0, hl = 0;
    static char hb[2048];
    int hdr_done = 0, status = 0;
    int64_t dl = time(NULL) + UPD_DEADLINE_S + 20;
    for(;;){
        char tmp[2048];
        int r = tls_read(t, tmp, sizeof tmp);
        if(r < 0) break;
        if(r == 0){
            if(time(NULL) > dl) break;
            usleep(20000);
            continue;
        }
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
                if(off > (size_t)r) off = (size_t)r;
            } else if(hl >= sizeof hb - 1){
                free(body); tls_free(t); return NULL;
            } else continue;
        }
        while(off < (size_t)r && bl < cap - 1) body[bl++] = tmp[off++];
        if(bl >= cap - 1 || time(NULL) > dl) break;
    }
    tls_free(t);
    body[bl] = 0;
    if(out_status) *out_status = status;
    if(out_len) *out_len = bl;
    if(status != 200 || bl == 0){ free(body); return NULL; }
    return body;
}

static int stage_file(const char *target, const unsigned char *data, size_t n){
    char tmp[192];
    snprintf(tmp, sizeof tmp, "%s.new", target);
    FILE *f = fopen(tmp, "wb");
    if(!f){ log_msg("updater: cannot write %s", tmp); return -1; }
    int ok = (fwrite(data, 1, n, f) == n);
    if(fflush(f) != 0) ok = 0;
    if(ok){ int fd = fileno(f); if(fd >= 0 && fsync(fd) != 0) ok = 0; }
    if(fclose(f) != 0) ok = 0;
    if(!ok){ remove(tmp); return -1; }
    if(rename(tmp, target) != 0){ remove(tmp); return -1; }
    return 0;
}

int updater_check_and_stage(void){
    char path[160];
    snprintf(path, sizeof path, "/repos/%s/releases/latest", ORBISRPC_REPO);
    size_t rl = 0;
    int status = 0;
    char *js = https_get(UPD_HOST, path, 65536, &status, &rl);
    if(!js){ log_msg("updater: release check failed (status=%d)", status); return -1; }
    jl_val_t *r = jl_parse(js, rl);
    free(js);
    if(!r){ log_msg("updater: release parse failed"); return -1; }
    const jl_val_t *tag = jl_obj_get(r, "tag_name");
    const jl_val_t *assets = jl_obj_get(r, "assets");
    int updated = 0;
    if(tag && tag->type == JL_STRING && tag->str[0] &&
       updater_cmp(tag->str, ORBISRPC_VERSION) > 0){
        log_msg("updater: %s available (local %s)", tag->str, ORBISRPC_VERSION);
        if(assets && assets->type == JL_ARRAY){
            for(size_t i = 0; ; i++){
                const jl_val_t *a = jl_arr_at(assets, i);
                if(!a) break;
                const jl_val_t *nm = jl_obj_get(a, "name");
                const jl_val_t *dl = jl_obj_get(a, "browser_download_url");
                if(!nm || nm->type != JL_STRING || !dl || dl->type != JL_STRING) continue;
                const char *target = NULL;
                if(!strcmp(nm->str, "orbisrpc.bin")) target = "/data/GoldHEN/payloads/orbisrpc.bin";
                else if(!strcmp(nm->str, "orbisrpc_plugin.prx")) target = "/data/GoldHEN/plugins/orbisrpc_plugin.prx";
                else continue;
                /* download URLs are full https://... : split host/path */
                const char *url = dl->str;
                const char *h0 = strstr(url, "://");
                if(!h0) continue;
                h0 += 3;
                const char *p0 = strchr(h0, '/');
                if(!p0 || (size_t)(p0-h0) >= 128) continue;
                char host[128];
                memcpy(host, h0, (size_t)(p0-h0)); host[p0-h0] = 0;
                size_t al = 0;
                char *bin = https_get(host, p0, UPD_BODY_MAX, &status, &al);
                if(!bin || !updater_elf_ok((unsigned char*)bin, al)){
                    log_msg("updater: asset %s failed validation", nm->str);
                    free(bin);
                    continue;
                }
                if(stage_file(target, (unsigned char*)bin, al) == 0){
                    log_msg("updater: staged %s (%zu bytes)", target, al);
                    updated = 1;
                }
                free(bin);
            }
        }
    }
    jl_free(r);
    return updated ? 1 : 0;
}
