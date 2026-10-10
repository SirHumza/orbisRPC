/* updater.c - update CHECK. Pure logic (host-tested) plus PS4 HTTPS.
 *
 * Once per daemon boot, when enabled:
 *   GET https://api.github.com/repos/<repo>/releases/latest
 *   -> tag_name
 *   if the tag is newer than ORBISRPC_VERSION, notify. Nothing else.
 *
 * This deliberately does NOT download or install anything. It used to fetch
 * orbisrpc.bin from the release and activate it over the live payload, which
 * made every boot a remote-code-execution path. Updates are applied by the
 * setup app now, and this file only tells the user to go do that.
 *
 * There is deliberately no signature check here. It was there to authenticate
 * a binary that was then downloaded and executed; with the download gone, the
 * only thing a signature could protect is the truth of a notification, and
 * that cost a second release asset and a signing key nobody outside the
 * original publisher holds. The tag arrives over TLS to api.github.com, so
 * the realistic worst case is a wrong version number in a notification --
 * not code execution.
 */
#include "updater.h"
#include "updater_http.h"
#include "version.h"
#include "clock.h"
#include "jsonlite.h"
#include "log.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

/* ---- PS4 HTTPS transport (mirrors ws.c/tmdb.c bring-up) ---- */
#include "tls.h"
#include <mbedtls/sha256.h>
#ifdef ORBISRPC_SDK_PAYLOAD
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <fcntl.h>
#else
#include <orbis/Net.h>
#include <orbis/Sysmodule.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#endif
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#define UPD_HOST "api.github.com"
#define UPD_DEADLINE_S 15
#define UPD_BODY_MAX (4u*1024u*1024u)
#define UPD_MAX_REDIRECTS 3

/* Release-channel host allowlist: only GitHub API + release infra.
 * Redirects anywhere else are refused (fail closed). */
static int upd_host_allowed(const char *host){
    static const char *ok[] = {
        "api.github.com",
        "github.com",
        "uploads.github.com",
        "objects.githubusercontent.com",
        "codeload.github.com",
        "raw.githubusercontent.com",
        NULL,
    };
    if(!host || !host[0]) return 0;
    for(int i = 0; ok[i]; i++) if(!strcmp(host, ok[i])) return 1;
    return 0;
}

#ifndef SO_NBIO
#define SO_NBIO 0x2000   /* same local define as ws.c; absent from SDK headers */
#endif

static int32_t s_upool = -1;
static int upd_net(void){
    static int ready = 0;
    if(ready) return 0;
#ifdef ORBISRPC_SDK_PAYLOAD
    ready = 1;
    return 0;
#else
    uint32_t ur = sceSysmoduleLoadModuleInternal(ORBIS_SYSMODULE_INTERNAL_NET);
    if((int)ur < 0) return -1;
    if(sceNetInit() < 0) return -1;
    s_upool = (int32_t)sceNetPoolCreate("upd", 128*1024, 0);
    if(s_upool < 0) return -1;
    ready = 1;
    return 0;
#endif
}

static int upd_connect(const char *host, int port){
    if(upd_net() < 0) return -1;
#ifdef ORBISRPC_SDK_PAYLOAD
    struct addrinfo hints, *res = NULL;
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    {
        char portbuf[16];
        snprintf(portbuf, sizeof portbuf, "%d", port);
        if(getaddrinfo(host, portbuf, &hints, &res) != 0 || !res){
            log_msg("updater: dns fail");
            return -1;
        }
    }
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if(fd < 0){ freeaddrinfo(res); return -1; }
    struct timeval tv = { .tv_sec = UPD_DEADLINE_S, .tv_usec = 0 };
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
    if(connect(fd, res->ai_addr, res->ai_addrlen) < 0){
        close(fd); freeaddrinfo(res); return -1;
    }
    freeaddrinfo(res);
    {
        int fl = fcntl(fd, F_GETFL, 0);
        if(fl >= 0) fcntl(fd, F_SETFL, fl | O_NONBLOCK);
    }
    return fd;
#else
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
#endif
}

/* HTTPS GET with real HTTP semantics (bounded):
 *   status line + headers (cap UPD_HDR_MAX) + Content-Length or
 *   Transfer-Encoding: chunked decoding + redirect capture.
 * Returns heap body (caller frees) with out_len; out_status always set;
 * out_location gets the redirect target ("" when none). Only the body
 * counts against cap; headers are separately bounded. */
static char *https_get_once(const char *host, const char *path,
                            size_t cap, int *out_status, size_t *out_len,
                            char *out_location, size_t loc_cap){
    if(out_status) *out_status = 0;
    if(out_len) *out_len = 0;
    if(out_location && loc_cap) out_location[0] = 0;
    if(!upd_host_allowed(host)){ log_msg("updater: host refused (%s)", host); return NULL; }
    int fd = upd_connect(host, 443);
    if(fd < 0) return NULL;
    tls_ctx_t *t = tls_start(fd, host);
    if(!t){
#ifdef ORBISRPC_SDK_PAYLOAD
        close(fd);
#else
        sceNetSocketClose(fd);
#endif
        return NULL;
    }
    char req[512];
    int rl = snprintf(req, sizeof req,
        "GET %s HTTP/1.1\r\nHost: %s\r\nUser-Agent: orbisRPC/%s\r\nConnection: close\r\n\r\n",
        path, host, ORBISRPC_VERSION);
    if(rl <= 0 || rl >= (int)sizeof req){ tls_free(t); return NULL; }
    if(tls_write(t, req, (size_t)rl) < 0){ tls_free(t); return NULL; }
    /* Read raw response (headers + body) up to header cap + body cap. */
    size_t rawcap = UPD_HDR_MAX + cap;
    char *raw = (char*)malloc(rawcap + 1); /* +1: NUL pad when a read fills cap exactly */
    if(!raw){ tls_free(t); return NULL; }
    size_t rl2 = 0;
    int64_t dl = orbis_mono_s() + UPD_DEADLINE_S + 20;
    for(;;){
        char tmp[2048];
        int r = tls_read(t, tmp, sizeof tmp);
        if(r < 0) break;
        if(r == 0){
            if(orbis_mono_s() > dl) break;
            usleep(20000);
            continue;
        }
        size_t room = rawcap > rl2 ? rawcap - rl2 : 0;
        size_t cp = (size_t)r < room ? (size_t)r : room;
        if(cp) memcpy(raw + rl2, tmp, cp);
        rl2 += cp;
        if(rl2 >= rawcap - 1 || orbis_mono_s() > dl) break;
    }
    tls_free(t);
    if(rl2 == 0){ free(raw); return NULL; }
    raw[rl2] = 0;
    char *out = upd_parse_response(raw, rl2, cap, out_status, out_len,
                                   out_location, loc_cap);
    free(raw);
    return out;
}

/* HTTPS GET following absolute-URL https redirects within the allowlist
 * (GitHub release assets redirect to object storage). Relative Location
 * values stay on the same host. Refuses non-https, off-allowlist, and
 * redirect loops. */
static char *https_get(const char *host, const char *path,
                       size_t cap, int *out_status, size_t *out_len){
    char hbuf[128], pbuf[512], loc[512];
    snprintf(hbuf, sizeof hbuf, "%s", host);
    snprintf(pbuf, sizeof pbuf, "%s", path);
    for(int i = 0; i <= UPD_MAX_REDIRECTS; i++){
        int status = 0;
        size_t len = 0;
        char *body = https_get_once(hbuf, pbuf, cap, &status, &len,
                                    loc, sizeof loc);
        if(!body){
            if(out_status) *out_status = status;
            if(out_len) *out_len = 0;
            return NULL;
        }
        if((status == 301 || status == 302 || status == 303 ||
            status == 307 || status == 308) && loc[0]){
            free(body);
            if(!strncmp(loc, "https://", 8)){
                const char *h0 = loc + 8;
                const char *p0 = strchr(h0, '/');
                if(!p0 || (size_t)(p0 - h0) >= sizeof hbuf){ 
                    if(out_status) *out_status = status;
                    if(out_len) *out_len = 0;
                    return NULL;
                }
                memcpy(hbuf, h0, (size_t)(p0 - h0));
                hbuf[p0 - h0] = 0;
                snprintf(pbuf, sizeof pbuf, "%s", p0);
            } else if(loc[0] == '/'){
                snprintf(pbuf, sizeof pbuf, "%s", loc);
            } else {
                if(out_status) *out_status = status;
                if(out_len) *out_len = 0;
                return NULL;
            }
            if(!upd_host_allowed(hbuf)){
                log_msg("updater: redirect refused (off-allowlist %s)", hbuf);
                if(out_status) *out_status = status;
                if(out_len) *out_len = 0;
                return NULL;
            }
            log_msg("updater: redirect -> %s%s", hbuf, pbuf);
            continue;
        }
        if(out_status) *out_status = status;
        if(out_len) *out_len = len;
        if(status != 200 || len == 0){ free(body); return NULL; }
        return body;
    }
    if(out_status) *out_status = 0;
    if(out_len) *out_len = 0;
    return NULL;
}

/* No stage_file() here any more: nothing is written to the payload path.
 * See the header comment -- the download-and-activate path was removed so
 * the payload cannot execute code fetched from the internet at boot. */

/* Check whether a newer signed release exists, and say so on screen.
 *
 * This used to download release assets and activate them over the live
 * payload (/data/payloads/orbisrpc.bin). That path is gone: the payload no
 * longer fetches or executes code from the internet, so a compromised
 * release, CDN, or redirect can at worst lie about a version number.
 * Updates are applied by the setup app instead.
 *
 * The only thing fetched is the release JSON. There is no signature check:
 * it authenticated a binary that used to be downloaded and executed, and
 * with the download gone it could only guard the truth of a notification.
 *
 * Returns 1 when a newer release tag was found (notification fired),
 * 0 when already current, -1 when the check itself failed.
 * Never fatal to the daemon. */
int updater_check_notify(char *newer_out, size_t newer_cap){
    if(newer_out && newer_cap) newer_out[0] = 0;

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
    int found = 0;

    if(tag && tag->type == JL_STRING && tag->str[0] &&
       updater_cmp(tag->str, ORBISRPC_VERSION) > 0){
        log_msg("updater: %s available (local %s)", tag->str, ORBISRPC_VERSION);
        if(newer_out && newer_cap){
            snprintf(newer_out, newer_cap, "%s", tag->str);
            found = 1;
        }
    } else {
        log_msg("updater: already current (local %s)", ORBISRPC_VERSION);
    }
    jl_free(r);
    return found;
}
