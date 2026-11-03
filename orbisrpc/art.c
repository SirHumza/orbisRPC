/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 SirHumza and CyberMask367 - see NOTICE for attribution terms.
 */
/* art.c - external-asset resolution (host-testable parsing + PS4 HTTPS).
 * Pure parsing lives in art_parse_mp() (unit-tested); the POST itself
 * mirrors updater.c's bounded HTTPS client.
 *
 * mp: mappings are cached per boot only. Persisting them was artwork_cache.json,
 * which is gone: the cover URL a title resolves to now lives in
 * games_cache.json alongside its name, so there is one cache, one TTL, and no
 * chance of replaying an mp: path whose source URL has since changed. */
#include "art.h"
#include "cfg.h"
#include "discord.h"
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

int art_url_ok(const char *url){
    if(!url) return 0;
    size_t n = strlen(url);
    /* 511 is the cache slot size; the POST body has more headroom, but a
     * value that cannot be cached is not worth a network round trip. */
    if(n < 9 || n >= 512) return 0;
    if(strncmp(url, "http://", 7) != 0 && strncmp(url, "https://", 8) != 0)
        return 0;
    for(size_t i = 0; i < n; i++){
        unsigned char c = (unsigned char)url[i];
        if(c < 0x20 || c == 0x7f || c == '"' || c == '\\') return 0;
    }
    return 1;
}

#ifdef ORBISRPC_SDK_PAYLOAD
#include "tls.h"
#include "clock.h"
#include "netconnect.h"
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

/* Resolved mp: mappings for this boot. A handful of URLs covers everything
 * the daemon sends (three static images plus icon-pack fallbacks; a title's
 * own mapping travels as an "mp:" string from games_cache). One slot was not
 * enough: the boot prefetch of large/small/browser art left only the last
 * resolve, so the idle tile and the small badge silently lost their art. */
#define ART_CACHE_N 8
static struct { char url[512]; char mp[512]; } s_map[ART_CACHE_N];
static int s_map_n, s_map_next;

void art_cache_clear(void){
    memset(s_map, 0, sizeof s_map);
    s_map_n = 0;
    s_map_next = 0;
}

void art_cache_put(const char *url, const char *mp){
    if(!url || !url[0] || !mp || strncmp(mp, "mp:", 3) != 0) return;
    if(strlen(url) >= sizeof s_map[0].url || strlen(mp) >= sizeof s_map[0].mp)
        return;
    for(int i = 0; i < s_map_n; i++){
        if(!strcmp(s_map[i].url, url)){
            snprintf(s_map[i].mp, sizeof s_map[i].mp, "%s", mp);
            return;
        }
    }
    int slot;
    if(s_map_n < ART_CACHE_N) slot = s_map_n++;
    else { slot = s_map_next; s_map_next = (s_map_next + 1) % ART_CACHE_N; }
    snprintf(s_map[slot].url, sizeof s_map[slot].url, "%s", url);
    snprintf(s_map[slot].mp, sizeof s_map[slot].mp, "%s", mp);
}

#if defined(ORBISRPC_SDK_PAYLOAD) || defined(__PS4__)
/* tls_free() tears down the TLS state but does not close the socket. Every
 * exit after tls_start() must close it or one fd leaks per resolve. */
static void art_close(int fd){
#ifdef ORBISRPC_SDK_PAYLOAD
    close(fd);
#else
    sceNetSocketClose(fd);
#endif
}
#endif

static int art_post(const char *app_id, const char *token, const char *url,
                    char *out_body, size_t body_cap, size_t *out_len){
#ifdef ORBISRPC_SDK_PAYLOAD
    /* Bounded non-blocking connect; the fd stays non-blocking for the TLS
     * pump and the deadline read loop. */
    int fd = net_tcp_connect(ART_HOST, 443, ART_DEADLINE_S);
    if(fd < 0) return -1;
#else
    /* Host test build: no transport here (parse path still testable). */
    (void)app_id; (void)token; (void)url;
    (void)out_body; (void)body_cap; (void)out_len;
    return -1;
#endif
#if defined(ORBISRPC_SDK_PAYLOAD) || defined(__PS4__)
    tls_ctx_t *t = tls_start(fd, ART_HOST);
    if(!t){
        art_close(fd);
        return -1;
    }
    char payload[768];
    int pl = snprintf(payload, sizeof payload, "{\"urls\":[\"%s\"]}", url);
    if(pl <= 0 || pl >= (int)sizeof payload){ tls_free(t); art_close(fd); return -1; }
    /* Every real client sends these on every REST call. Authorization alone
     * is what a script sends, and it is what the heuristics key on. */
    char sp[2048];
    int spl = discord_super_properties(sp, sizeof sp);
    char req[4096];
    int rl = snprintf(req, sizeof req,
        "POST /api/v10/applications/%s/external-assets HTTP/1.1\r\n"
        "Host: %s\r\n"
        "Authorization: %s\r\n"
        "Content-Type: application/json\r\n"
        "User-Agent: %s\r\n"
        "X-Super-Properties: %s\r\n"
        "X-Discord-Locale: %s\r\n"
        "X-Discord-Timezone: %s\r\n"
        "X-Debug-Options: bugReporterEnabled\r\n"
        "X-Installation-ID: %s\r\n"
        "Accept: */*\r\n"
        "Accept-Encoding: identity\r\n"
        "Content-Length: %d\r\nConnection: close\r\n\r\n",
        app_id, ART_HOST, token,
        g_cfg.fp_user_agent,
        spl > 0 ? sp : "",
        g_cfg.fp_locale,
        g_cfg.fp_timezone,
        g_cfg.fp_installation_id,
        pl);
    if(rl <= 0 || rl >= (int)sizeof req){ tls_free(t); art_close(fd); return -1; }
    if(tls_write(t, req, (size_t)rl) < 0){ tls_free(t); art_close(fd); return -1; }
    if(tls_write(t, payload, (size_t)pl) < 0){ tls_free(t); art_close(fd); return -1; }
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
    art_close(fd);
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

int art_resolve_mp_cached(const char *url, char *out_mp, size_t cap){
    if(!url || !url[0] || !out_mp || cap < 8) return 0;
    if(!strncmp(url, "mp:", 3)){
        strncpy(out_mp, url, cap - 1);
        out_mp[cap - 1] = 0;
        return 1;
    }
    for(int i = 0; i < s_map_n; i++){
        if(strcmp(url, s_map[i].url) != 0) continue;
        strncpy(out_mp, s_map[i].mp, cap - 1);
        out_mp[cap - 1] = 0;
        return 1;
    }
    return 0;
}

void art_prefetch(const char *url){
    if(!url || !url[0]) return;
    char tmp[512];
    if(art_resolve_mp_cached(url, tmp, sizeof tmp)) return;
    /* An uploaded asset key is not a URL and needs no external-assets call. */
    if(strncmp(url, "http", 4) != 0) return;
    (void)art_resolve_mp(g_cfg.application_id, g_cfg.token, url, tmp, sizeof tmp);
}

int art_resolve_mp(const char *app_id, const char *token, const char *url,
                   char *out_mp, size_t cap){
    if(!app_id || !app_id[0] || !token || !token[0] || !url || !url[0] ||
       !out_mp || cap < 8)
        return 0;
    if(art_resolve_mp_cached(url, out_mp, cap)) return 1;
    if(!art_url_ok(url)){
        log_msg("art: refusing malformed URL (must be http(s), <512 bytes, no quotes/controls)");
        return 0;
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
    art_cache_put(url, out_mp);
    log_msg("art: resolved mp (%zuB)", strlen(out_mp));
    return 1;
}
