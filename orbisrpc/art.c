/* art.c - external-asset resolution (host-testable parsing + PS4 HTTPS).
 * Pure parsing lives in art_parse_mp() (unit-tested); the POST itself
 * mirrors updater.c's bounded HTTPS client. */
#include "art.h"
#include "jsonlite.h"
#include "log.h"
#include "updater_http.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>

/* Extract external_asset_path for url from an external-assets response.
 * Returns 1 + out ("mp:"+path), else 0. Pure function, host-tested. */
int art_parse_mp(const char *body, size_t len, const char *url,
                 char *out, size_t cap){
    if(!body || !url || !out || cap < 8) return 0;
    jl_val_t *r = jl_parse(body, len);
    if(!r || r->type != JL_ARRAY){ if(r) jl_free(r); return 0; }
    int rc = 0;
    for(size_t i = 0; ; i++){
        const jl_val_t *e = jl_arr_at(r, i);
        if(!e) break;
        if(e->type != JL_OBJECT) continue;
        const jl_val_t *u = jl_obj_get(e, "url");
        const jl_val_t *p = jl_obj_get(e, "external_asset_path");
        if(!u || u->type != JL_STRING || !u->str) continue;
        if(!p || p->type != JL_STRING || !p->str) continue;
        if(strcmp(u->str, url) != 0) continue;
        if(strlen(p->str) > cap - 5) continue;
        snprintf(out, cap, "mp:%s", p->str);
        rc = 1;
        break;
    }
    jl_free(r);
    return rc;
}

#ifdef ORBISRPC_SDK_PAYLOAD
#include "tls.h"
#include "clock.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#elif defined(__PS4__)
#include "tls.h"
#include "clock.h"
#include <orbis/Net.h>
#include <orbis/Sysmodule.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>
#endif

#define ART_HOST "discord.com"
#define ART_DEADLINE_S 12
#define ART_RESP_MAX (16u*1024u)

static char s_last_url[512] = "";
static char s_last_mp[512] = "";

void art_cache_clear(void){
    s_last_url[0] = 0;
    s_last_mp[0] = 0;
}

/* Disk cache: url -> mp path, 7-day TTL. Survives reboots so titles
 * resolved once never pay the external-assets round trip again.
 * Best-effort only: any I/O failure degrades to memory-only caching. */
#define ART_DISK_MAX 32
#define ART_DISK_TTL (7*24*3600)
static struct { char url[512]; char mp[512]; int64_t at; } s_disk[ART_DISK_MAX];
static int s_disk_n = -1; /* -1 = not loaded yet */

static void art_disk_load(void){
    if(s_disk_n >= 0) return;
    s_disk_n = 0;
    FILE *f = fopen("/data/orbisRPC/artwork_cache.json", "rb");
    if(!f) return;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    int64_t now = (int64_t)time(NULL);
    if(sz > 0 && sz < 65536){
        char *buf = (char*)malloc((size_t)sz + 1);
        if(buf && fread(buf, 1, (size_t)sz, f) == (size_t)sz){
            buf[sz] = 0;
            jl_val_t *r = jl_parse(buf, (size_t)sz);
            if(r && r->type == JL_ARRAY){
                for(size_t i = 0; ; i++){
                    const jl_val_t *e = jl_arr_at(r, i);
                    if(!e) break;
                    if(s_disk_n >= ART_DISK_MAX) break;
                    if(e->type != JL_OBJECT) continue;
                    const jl_val_t *u = jl_obj_get(e, "url");
                    const jl_val_t *m = jl_obj_get(e, "mp");
                    const jl_val_t *a = jl_obj_get(e, "at");
                    if(!u || u->type != JL_STRING || !u->str) continue;
                    if(!m || m->type != JL_STRING || !m->str) continue;
                    if(!a || a->type != JL_NUMBER) continue;
                    if(now - (int64_t)a->num > ART_DISK_TTL) continue; /* prune */
                    snprintf(s_disk[s_disk_n].url, sizeof s_disk[s_disk_n].url, "%s", u->str);
                    snprintf(s_disk[s_disk_n].mp, sizeof s_disk[s_disk_n].mp, "%s", m->str);
                    s_disk[s_disk_n].at = (int64_t)a->num;
                    s_disk_n++;
                }
            }
            if(r) jl_free(r);
        }
        free(buf);
    }
    fclose(f);
}

static void art_disk_save(void){
    char tmp[256];
    snprintf(tmp, sizeof tmp, "%s.new", "/data/orbisRPC/artwork_cache.json");
    FILE *f = fopen(tmp, "wb");
    if(!f) return;
    int ok = 1;
    if(fputs("[", f) < 0) ok = 0;
    for(int i = 0; ok && i < s_disk_n; i++){
        if(i > 0 && fputs(",", f) < 0){ ok = 0; break; }
        /* URLs/mp paths are response-derived; escape defensively. */
        char eu[1024], em[1024];
        size_t a = 0, b = 0;
        for(const char *p = s_disk[i].url; *p && a + 2 < sizeof eu; p++){
            if(*p == '"' || *p == '\\') eu[a++] = '\\';
            eu[a++] = *p;
        }
        eu[a] = 0;
        for(const char *p = s_disk[i].mp; *p && b + 2 < sizeof em; p++){
            if(*p == '"' || *p == '\\') em[b++] = '\\';
            em[b++] = *p;
        }
        em[b] = 0;
        if(fprintf(f, "{\"url\":\"%s\",\"mp\":\"%s\",\"at\":%lld}",
                   eu, em, (long long)s_disk[i].at) < 0) ok = 0;
    }
    if(ok && fputs("]", f) < 0) ok = 0;
    if(ok && fflush(f) != 0) ok = 0;
    if(ok){ int fd = fileno(f); if(fd >= 0 && fsync(fd) != 0) ok = 0; }
    if(fclose(f) != 0) ok = 0;
    if(ok) rename(tmp, "/data/orbisRPC/artwork_cache.json");
    else remove(tmp);
}

static int art_disk_get(const char *url, char *out_mp, size_t cap){
    art_disk_load();
    int64_t now = (int64_t)time(NULL);
    for(int i = 0; i < s_disk_n; i++){
        if(!strcmp(s_disk[i].url, url)){
            if(now - s_disk[i].at > ART_DISK_TTL) return 0;
            strncpy(out_mp, s_disk[i].mp, cap - 1);
            out_mp[cap - 1] = 0;
            return 1;
        }
    }
    return 0;
}

static void art_disk_put(const char *url, const char *mp){
    art_disk_load();
    for(int i = 0; i < s_disk_n; i++){
        if(!strcmp(s_disk[i].url, url)){
            snprintf(s_disk[i].mp, sizeof s_disk[i].mp, "%s", mp);
            s_disk[i].at = (int64_t)time(NULL);
            art_disk_save();
            return;
        }
    }
    if(s_disk_n < ART_DISK_MAX){
        snprintf(s_disk[s_disk_n].url, sizeof s_disk[s_disk_n].url, "%s", url);
        snprintf(s_disk[s_disk_n].mp, sizeof s_disk[s_disk_n].mp, "%s", mp);
        s_disk[s_disk_n].at = (int64_t)time(NULL);
        s_disk_n++;
        art_disk_save();
    }
}

static int art_post(const char *app_id, const char *token, const char *url,
                    char *out_body, size_t body_cap, size_t *out_len){
#ifdef ORBISRPC_SDK_PAYLOAD
    struct addrinfo hints, *res = NULL;
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    if(getaddrinfo(ART_HOST, "443", &hints, &res) != 0 || !res) return -1;
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if(fd < 0){ freeaddrinfo(res); return -1; }
    struct timeval tv = { ART_DEADLINE_S, 0 };
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    if(connect(fd, res->ai_addr, res->ai_addrlen) < 0){
        close(fd); freeaddrinfo(res); return -1;
    }
    freeaddrinfo(res);
    {
        int fl = fcntl(fd, F_GETFL, 0);
        if(fl >= 0) fcntl(fd, F_SETFL, fl | O_NONBLOCK);
    }
#else
    /* Host test build: no transport here (parse path still testable). */
    (void)app_id; (void)token; (void)url;
    (void)out_body; (void)body_cap; (void)out_len;
    return -1;
#endif
#if defined(ORBISRPC_SDK_PAYLOAD) || defined(__PS4__)
    tls_ctx_t *t = tls_start(fd, ART_HOST);
    if(!t){
#ifdef ORBISRPC_SDK_PAYLOAD
        close(fd);
#else
        sceNetSocketClose(fd);
#endif
        return -1;
    }
    char payload[768];
    int pl = snprintf(payload, sizeof payload, "{\"urls\":[\"%s\"]}", url);
    if(pl <= 0 || pl >= (int)sizeof payload){ tls_free(t); return -1; }
    char req[1024];
    int rl = snprintf(req, sizeof req,
        "POST /api/v10/applications/%s/external-assets HTTP/1.1\r\n"
        "Host: %s\r\nAuthorization: %s\r\nContent-Type: application/json\r\n"
        "Content-Length: %d\r\nConnection: close\r\n\r\n",
        app_id, ART_HOST, token, pl);
    if(rl <= 0 || rl >= (int)sizeof req){ tls_free(t); return -1; }
    if(tls_write(t, req, (size_t)rl) < 0){ tls_free(t); return -1; }
    if(tls_write(t, payload, (size_t)pl) < 0){ tls_free(t); return -1; }
    size_t bl = 0;
    int64_t dl = orbis_mono_s() + ART_DEADLINE_S + 10;
    for(;;){
        char tmp[1024];
        int r = tls_read(t, tmp, sizeof tmp - 1);
        if(r < 0) break;
        if(r == 0){
            if(orbis_mono_s() > dl) break;
            usleep(20000);
            continue;
        }
        if(bl + (size_t)r >= body_cap) break;
        memcpy(out_body + bl, tmp, (size_t)r);
        bl += (size_t)r;
        if(orbis_mono_s() > dl) break;
    }
    tls_free(t);
    if(bl == 0){ return -1; }
    out_body[bl] = 0;
    /* Parse with the shared chunked-aware HTTP parser (Discord serves
     * Transfer-Encoding: chunked here; naive header-splitting reads the
     * chunk framing as body and the mp: lookup silently misses). */
    {
        int st = 0;
        size_t olen = 0;
        char *body = upd_parse_response(out_body, bl, body_cap,
                                        &st, &olen, NULL, 0);
        if(!body || st != 200){ free(body); return -1; }
        if(out_len) *out_len = olen;
        memmove(out_body, body, olen + 1);
        free(body);
    }
    return 0;
#else
    /* Host test build: no transport; parse path still testable. */
    (void)app_id; (void)token; (void)url;
    (void)out_body; (void)body_cap; (void)out_len;
    return -1;
#endif
}

int art_resolve_mp(const char *app_id, const char *token, const char *url,
                   char *out_mp, size_t cap){
    if(!app_id || !app_id[0] || !token || !token[0] || !url || !url[0] ||
       !out_mp || cap < 8)
        return 0;
    if(!strcmp(url, s_last_url) && s_last_mp[0]){
        strncpy(out_mp, s_last_mp, cap - 1);
        out_mp[cap - 1] = 0;
        return 1;
    }
    /* Disk cache (7-day TTL): titles resolved on earlier boots skip the
     * round trip entirely. */
    if(art_disk_get(url, out_mp, cap)){
        snprintf(s_last_url, sizeof s_last_url, "%s", url);
        snprintf(s_last_mp, sizeof s_last_mp, "%s", out_mp);
        log_msg("art: mp from disk cache");
        return 1;
    }
    static char resp[ART_RESP_MAX];
    size_t rlen = 0;
    if(art_post(app_id, token, url, resp, sizeof resp, &rlen) != 0){
        log_msg("art: external-assets POST failed");
        return 0;
    }
    if(!art_parse_mp(resp, rlen, url, out_mp, cap)){
        log_msg("art: no mp path for url (resp %zuB: %.120s)", rlen, resp);
        return 0;
    }
    snprintf(s_last_url, sizeof s_last_url, "%s", url);
    snprintf(s_last_mp, sizeof s_last_mp, "%s", out_mp);
    art_disk_put(url, out_mp);
    log_msg("art: resolved mp (%zuB)", strlen(out_mp));
    return 1;
}
