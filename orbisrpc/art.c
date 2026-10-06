/* art.c - external-asset resolution (host-testable parsing + PS4 HTTPS).
 * Pure parsing lives in art_parse_mp() (unit-tested); the POST itself
 * mirrors updater.c's bounded HTTPS client.
 *
 * mp: mappings are cached per boot only. Persisting them was artwork_cache.json,
 * which is gone: the cover URL a title resolves to now lives in
 * games_cache.json alongside its name, so there is one cache, one TTL, and no
 * chance of replaying an mp: path whose source URL has since changed. */
#include "art.h"
#include "jsonlite.h"
#include "log.h"
#include "updater_http.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
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
    log_msg("art: resolved mp (%zuB)", strlen(out_mp));
    return 1;
}
