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
#include "updater_http.h"
#include "version.h"
#include "clock.h"
#include "jsonlite.h"
#include "health.h"
#include "manifest.h"
#include "release_pubkey.h"
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
    char *raw = (char*)malloc(rawcap);
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

static int stage_file(const char *target, const unsigned char *data, size_t n){
    char tmp[192];
    snprintf(tmp, sizeof tmp, "%s.new", target);
    if(!updater_image_ok(data, n)){ log_msg("updater: staged bytes failed validation"); return -1; }
    FILE *f = fopen(tmp, "wb");
    if(!f){ log_msg("updater: cannot write %s", tmp); return -1; }
    int ok = (fwrite(data, 1, n, f) == n);
    if(fflush(f) != 0) ok = 0;
    if(ok){ int fd = fileno(f); if(fd >= 0 && fsync(fd) != 0) ok = 0; }
    if(fclose(f) != 0) ok = 0;
    if(!ok){ remove(tmp); return -1; }
    /* Atomic verified activation: validates <target>.new, backs up live
     * to .bak, activates, re-verifies, auto-restores .bak on failure.
     * A refused stage leaves the live target untouched (ORX-UPDATE-003). */
    if(health_stage_activate(target) != 0){
        log_msg("updater: stage activate failed for %s; live kept", target);
        return -1;
    }
    return 0;
}

/* SHA256 of a buffer, hex-encoded (64 chars + NUL). Returns 0 on success. */
static int sha256_hex(const unsigned char *data, size_t n, char out[65]){
    unsigned char dig[32];
    mbedtls_sha256_context sc;
    mbedtls_sha256_init(&sc);
    int ok = mbedtls_sha256_starts(&sc, 0) == 0 &&
             mbedtls_sha256_update(&sc, data ? data : (const unsigned char *)"", n) == 0 &&
             mbedtls_sha256_finish(&sc, dig) == 0;
    mbedtls_sha256_free(&sc);
    if(!ok) return -1;
    for(int i=0;i<32;i++) snprintf(out+2*i, 3, "%02x", dig[i]);
    out[64]=0;
    return 0;
}

/* Look up "<filename>" in a SHA256SUMS body ("<hash>  <name>\n" lines).
 * Returns 0 and fills want_hex when found. */
static int sums_lookup(const char *sums, const char *filename, char want_hex[65]){
    size_t fnlen = strlen(filename);
    const char *p = sums;
    while(*p){
        while(*p==' '||*p=='\t'||*p=='\r'||*p=='\n') p++;
        if(!*p) break;
        if(strlen(p) < 64) break;
        char hex[65];
        memcpy(hex, p, 64); hex[64]=0;
        int ishex=1;
        for(int i=0;i<64;i++){
            char c=hex[i];
            if(!((c>='0'&&c<='9')||(c>='a'&&c<='f')||(c>='A'&&c<='F'))){ ishex=0; break; }
        }
        const char *e = strchr(p, '\n');
        size_t linelen = e ? (size_t)(e-p) : strlen(p);
        if(ishex && linelen > 65){
            const char *nl = p+64;
            while(nl<p+linelen && (*nl==' '||*nl=='\t'||*nl=='*')) nl++;
            if((size_t)(p+linelen-(nl)) >= fnlen && !memcmp(nl, filename, fnlen) &&
               (nl[fnlen]=='\n'||nl[fnlen]=='\r'||nl[fnlen]==' '||nl[fnlen]=='\t'||nl[fnlen]==0||nl[fnlen]=='*')){
                for(int i=0;i<64;i++){
                    char c=hex[i];
                    want_hex[i]=(c>='A'&&c<='F')?(char)(c-'A'+'a'):c;
                }
                want_hex[64]=0;
                return 0;
            }
        }
        p = e ? e+1 : p+linelen;
    }
    return -1;
}

/* Download a release asset by exact name. Returns heap body or NULL. */
static char *fetch_asset(const jl_val_t *assets, const char *want_name,
                         size_t cap, int *status, size_t *out_len){
    for(size_t i = 0; ; i++){
        const jl_val_t *a = jl_arr_at(assets, i);
        if(!a) break;
        const jl_val_t *nm = jl_obj_get(a, "name");
        const jl_val_t *dl = jl_obj_get(a, "browser_download_url");
        if(!nm || nm->type != JL_STRING || !dl || dl->type != JL_STRING) continue;
        if(strcmp(nm->str, want_name) != 0) continue;
        const char *url = dl->str;
        if(strncmp(url, "https://", 8) != 0) return NULL;
        const char *h0 = url + 8;
        const char *p0 = strchr(h0, '/');
        if(!p0 || (size_t)(p0 - h0) >= 128) return NULL;
        char host[128];
        memcpy(host, h0, (size_t)(p0 - h0)); host[p0 - h0] = 0;
        return https_get(host, p0, cap, status, out_len);
    }
    return NULL;
}

/* Decode manifest.sig: raw 64 bytes, or 128 hex chars. 0 ok. */
static int decode_sig(const char *body, size_t len, unsigned char out[64]){
    if(len == 64){ memcpy(out, body, 64); return 0; }
    /* strip whitespace for hex form; exactly 128 hex chars required —
     * trailing garbage after the 128 fails closed */
    char hex[129];
    size_t hn = 0, total = 0;
    for(size_t i = 0; i < len; i++){
        char c = body[i];
        if(c == ' ' || c == '\t' || c == '\r' || c == '\n') continue;
        total++;
        if(hn < 128) hex[hn++] = c;
    }
    if(hn != 128 || total != 128) return -1;
    for(int i = 0; i < 64; i++){
        unsigned v = 0;
        for(int k = 0; k < 2; k++){
            char c = hex[2 * i + k];
            v <<= 4;
            if(c >= '0' && c <= '9') v |= (unsigned)(c - '0');
            else if(c >= 'a' && c <= 'f') v |= (unsigned)(c - 'a' + 10);
            else if(c >= 'A' && c <= 'F') v |= (unsigned)(c - 'A' + 10);
            else return -1;
        }
        out[i] = (unsigned char)v;
    }
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
            /* Preferred: signed manifest (manifest.json + manifest.sig).
             * Verified with the embedded release pubkey; every binary must
             * match its listed SHA256. A bad signature refuses the whole
             * update (ORX-UPDATE-002). */
            manifest_t mf;
            int have_manifest = 0;
            size_t ml = 0;
            char *mbody = fetch_asset(assets, "manifest.json", 65536, &status, &ml);
            if(mbody){
                size_t sl = 0;
                char *sbody = fetch_asset(assets, "manifest.sig", 4096, &status, &sl);
                if(sbody && manifest_parse(mbody, ml, &mf) == 0 &&
                   manifest_gate(&mf) && manifest_is_newer(&mf, ORBISRPC_VERSION)){
                    unsigned char sig[64];
                    if(decode_sig(sbody, sl, sig) == 0 &&
                       manifest_verify_sig((unsigned char*)mbody, ml, sig,
                                           ORBISRPC_RELEASE_PUBKEY) == 0){
                        have_manifest = 1;
                        log_msg("updater: manifest %s verified (key %s)",
                                mf.version, ORBISRPC_RELEASE_KEY_ID);
                    } else {
                        log_msg("updater: WARN ORX-UPDATE-002: manifest signature invalid; refusing update");
                    }
                } else if(sbody){
                    log_msg("updater: WARN ORX-UPDATE-002: manifest invalid/gated; refusing update");
                }
                free(sbody);
                if(!have_manifest){ free(mbody); mbody = NULL; }
            }
            /* Compat fallback: SHA256SUMS (unsigned but hash-pinned). */
            char *sums = NULL;
            if(!have_manifest){
                sums = fetch_asset(assets, "SHA256SUMS", 65536, &status, NULL);
                if(!sums)
                    log_msg("updater: WARN ORX-UPDATE-002: no manifest and no SHA256SUMS; refusing unsigned update");
            }
            /* Two-phase commit: download + verify EVERY asset first, then
             * activate all at once. A failure anywhere stages nothing, so
             * the runtime can never mix a new payload with an old plugin
             * (or vice versa) — rollback restores the complete runtime. */
            struct { const char *target; char *bin; size_t len; } pend[2];
            memset(pend, 0, sizeof pend);
            int pend_n = 0, pend_fail = 0;
            for(size_t i = 0; ; i++){
                const jl_val_t *a = jl_arr_at(assets, i);
                if(!a) break;
                const jl_val_t *nm = jl_obj_get(a, "name");
                const jl_val_t *dl = jl_obj_get(a, "browser_download_url");
                if(!nm || nm->type != JL_STRING || !dl || dl->type != JL_STRING) continue;
                const char *target = NULL;
                if(!strcmp(nm->str, "orbisrpc.bin")) target = "/data/payloads/orbisrpc.bin";
                else if(!strcmp(nm->str, "orbisrpc_plugin.prx")) target = "/data/GoldHEN/plugins/orbisrpc_plugin.prx";
                else continue;
                /* download URLs must be https (fail closed on http/other). */
                const char *url = dl->str;
                if(strncmp(url, "https://", 8) != 0){ pend_fail = 1; break; }
                const char *h0 = url + 8;
                const char *p0 = strchr(h0, '/');
                if(!p0 || (size_t)(p0-h0) >= 128){ pend_fail = 1; break; }
                char host[128];
                memcpy(host, h0, (size_t)(p0-h0)); host[p0-h0] = 0;
                size_t al = 0;
                char *bin = https_get(host, p0, UPD_BODY_MAX, &status, &al);
                if(!bin || !updater_image_ok((unsigned char*)bin, al)){
                    log_msg("updater: asset %s failed validation", nm->str);
                    free(bin);
                    pend_fail = 1;
                    break;
                }
                if(have_manifest){
                    if(manifest_check(&mf, nm->str, (unsigned char*)bin, al) != 0){
                        log_msg("updater: asset %s not in manifest or hash mismatch; refusing (ORX-UPDATE-002)", nm->str);
                        free(bin);
                        pend_fail = 1;
                        break;
                    }
                } else if(sums){
                    char want[65];
                    if(sums_lookup(sums, nm->str, want) != 0){
                        log_msg("updater: asset %s not in SHA256SUMS; refusing", nm->str);
                        free(bin);
                        pend_fail = 1;
                        break;
                    }
                    char got[65];
                    if(sha256_hex((unsigned char*)bin, al, got) != 0 || strcmp(got, want) != 0){
                        log_msg("updater: asset %s hash mismatch; refusing", nm->str);
                        free(bin);
                        pend_fail = 1;
                        break;
                    }
                } else {
                    /* No manifest and no SHA256SUMS: refuse unsigned bytes. */
                    log_msg("updater: asset %s refused: no trust anchor (ORX-UPDATE-002)", nm->str);
                    free(bin);
                    pend_fail = 1;
                    break;
                }
                if(pend_n < 2){
                    pend[pend_n].target = target;
                    pend[pend_n].bin = bin;
                    pend[pend_n].len = al;
                    pend_n++;
                } else {
                    free(bin); /* more binaries than we stage; ignore extras */
                }
            }
            if(!pend_fail && pend_n > 0){
                for(int pi = 0; pi < pend_n; pi++){
                    if(stage_file(pend[pi].target,
                                 (unsigned char*)pend[pi].bin, pend[pi].len) == 0){
                        log_msg("updater: staged %s (%zu bytes)",
                                pend[pi].target, pend[pi].len);
                        updated = 1;
                    }
                }
            } else if(pend_fail){
                log_msg("updater: incomplete set; staged nothing (versions stay matched)");
            }
            for(int pi = 0; pi < pend_n; pi++) free(pend[pi].bin);
            free(sums);
            free(mbody);
        }
    }
    jl_free(r);
    return updated ? 1 : 0;
}
