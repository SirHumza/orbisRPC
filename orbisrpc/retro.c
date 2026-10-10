/* retro.c - see retro.h. */
#include "retro.h"
#include "log.h"
#include <errno.h>
#ifdef ORBISRPC_SDK_PAYLOAD
#include "tls.h"
#include <sys/socket.h>
#include <netdb.h>
#include <sys/time.h>
#include <unistd.h>
#endif
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* Host and base path are separate because a raw.githubusercontent.com URL is
 * a path, not a hostname: getaddrinfo() and tls_start() below both need a bare
 * host, and the certificate is issued for raw.githubusercontent.com. Putting the
 * whole URL in RETRO_HOST made DNS fail outright. */
#define RETRO_HOST "raw.githubusercontent.com"
#define RETRO_BASE "/SirHumza/orbisRPC/refs/heads/main/config"
#define RETRO_DEADLINE_S 25
#define RETRO_HDR_MAX 8192
/* Working window. Independent of the 1.5 MB file: a chunk plus enough tail to
 * hold any single entry, so a match across a chunk boundary survives. */
#define RETRO_CHUNK  4096
#define RETRO_TAIL   768

/* Verified on console 2026-10-05: ps1.json 1.31 MB / 9,930 entries,
 * ps2.json 1.50 MB / 11,207, psp.json 0.94 MB / 6,959. Compact, BOM-free,
 * bare array of flat entries. Some prefixes belong to more than one platform
 * (SLPM, PBPX), so candidates are tried in this order and the first hit wins. */
#define RETRO_PLAT_N 3

static const char *const s_plat_file[RETRO_PLAT_N] = {
    "ps1.json", "ps2.json", "psp.json",
};

static const char *const retro_prefixes_ps1[] = {
    "SCUS","SLUS","SPUS","SCES","SLES","SCED","SLED","SCPS","SLPS","SLPM",
    "SIPS","SCZS","SCKA","SLKA","ESPM","PAPX","PBPX","PCPD","PCPX","PDPX",
    "PEPX","PUPX","LSD","SND",
};
static const char *const retro_prefixes_ps2[] = {
    "SCUS","SLUS","SCES","SLES","SCED","SLED","TCES","TLES","SCPS","SLPS",
    "SLPM","KOEI","SCAJ","SLAJ","SCKA","SLKA","SCCH","SLCH","PBPX","PTPX",
    "PICS","LDTL",
};
static const char *const retro_prefixes_psp[] = {
    "UCUS","ULUS","UCES","ULES","UCJS","ULJS","ULJM","UCAS","ULAS","UCKS",
    "ULKS","ULUX","UMDT","NPUG","NPUH","NPUJ","NPUM","NPEG","NPEH","NPEZ",
    "NPJG","NPJH","NPJJ","NPHG","NPHH","NPHZ",
};

#define NELEM(a) (sizeof(a)/sizeof *(a))

/* Prefix lengths vary: most are 4 characters, but the list carries 3-char
 * ones (LSD, SND), so compare each entry at its own length rather than
 * assuming four. */
static int in_list(const char *const *list, size_t n, const char *pfx){
    for(size_t i = 0; i < n; i++){
        size_t len = strlen(list[i]);
        if(len == 0 || len > 4) continue;
        if(!strncmp(list[i], pfx, len)) return 1;
    }
    return 0;
}

static int retro_candidates(const char *pfx, unsigned char *out){
    int n = 0;
    if(in_list(retro_prefixes_ps1, NELEM(retro_prefixes_ps1), pfx)) out[n++] = 0;
    if(in_list(retro_prefixes_ps2, NELEM(retro_prefixes_ps2), pfx)) out[n++] = 1;
    if(in_list(retro_prefixes_psp, NELEM(retro_prefixes_psp), pfx)) out[n++] = 2;
    return n;
}

int retro_candidates_for(const char *title_id, unsigned char *out){
    if(!title_id || !out) return 0;
    size_t n = strlen(title_id);
    if(n < 4 || n >= RETRO_MAX_ID) return 0;
    return retro_candidates(title_id, out);
}

int retro_wants(const char *title_id){
    if(!title_id) return 0;
    size_t n = strlen(title_id);
    if(n < 4 || n >= RETRO_MAX_ID) return 0;
    unsigned char c[RETRO_PLAT_N];
    return retro_candidates(title_id, c) > 0;
}

/* --- JSON strings -------------------------------------------------------
 * Names carry quotes and backslashes (verified), so raw bytes cannot be
 * copied straight out. */

static size_t unescape(const char *src, size_t len, char *dst, size_t cap){
    size_t o = 0;
    for(size_t i = 0; i < len && o + 1 < cap; i++){
        if(src[i] != '\\'){ dst[o++] = src[i]; continue; }
        if(i + 1 >= len) break;
        char c = src[++i];
        switch(c){
        case 'n': dst[o++] = '\n'; break;
        case 't': dst[o++] = '\t'; break;
        case 'r': dst[o++] = '\r'; break;
        case 'b': dst[o++] = '\b'; break;
        case 'f': dst[o++] = '\f'; break;
        case 'u': {
            if(i + 4 >= len){ i = len; break; }
            unsigned cp = 0;
            for(int k = 1; k <= 4; k++){
                char h = src[i + k];
                cp <<= 4;
                if(h >= '0' && h <= '9') cp |= (unsigned)(h - '0');
                else if(h >= 'a' && h <= 'f') cp |= (unsigned)(h - 'a' + 10);
                else if(h >= 'A' && h <= 'F') cp |= (unsigned)(h - 'A' + 10);
            }
            i += 4;
            if(cp < 0x80) dst[o++] = (char)cp;
            else if(cp < 0x800 && o + 2 < cap){
                dst[o++] = (char)(0xC0 | (cp >> 6));
                dst[o++] = (char)(0x80 | (cp & 0x3F));
            } else if(cp < 0x10000 && o + 3 < cap){
                dst[o++] = (char)(0xE0 | (cp >> 12));
                dst[o++] = (char)(0x80 | ((cp >> 6) & 0x3F));
                dst[o++] = (char)(0x80 | (cp & 0x3F));
            }
            break;
        }
        default: dst[o++] = c; break;   /* covers \" \\ \/ */
        }
    }
    dst[o] = 0;
    return o;
}

/* Copy the JSON string starting at p (which must point at '"'), unescaped. */
static int grab_string(const char *p, const char *end, char *dst, size_t cap){
    if(!p || p >= end || *p != '"') return 0;
    p++;
    const char *q = p;
    while(q < end){
        if(*q == '\\'){ if(q + 1 >= end) return 0; q += 2; continue; }
        if(*q == '"') break;
        q++;
    }
    if(q >= end) return 0;
    unescape(p, (size_t)(q - p), dst, cap);
    return 1;
}

/* Value of the key whose opening quote is at `key`. Skips to the colon rather
 * than a fixed offset: the quote sits at +7 after "name":" but +8 after
 * "cover":", so a hardcoded skip silently drops one of the two. */
static int grab_value(const char *key, const char *end, char *dst, size_t cap){
    if(!key || key >= end) return 0;
    const char *c = key;
    while(c < end && *c != ':') c++;
    if(c >= end) return 0;
    return grab_string(c + 1, end, dst, cap);
}

/* Pull the entry for `pat` ("id":"XXXX") out of a body region. */
int retro_extract(const char *p, size_t len, const char *pat,
                  char *name, size_t name_cap,
                  char *url, size_t url_cap){
    /* Clear first: on a miss the caller's buffers must not keep whatever the
     * previous title put there, or a stale name could be posted. */
    if(name && name_cap) name[0] = 0;
    if(url && url_cap) url[0] = 0;
    if(!p || !pat || len < strlen(pat)) return 0;
    const char *hit = NULL;
    for(const char *q = p; q + strlen(pat) <= p + len; q++)
        if(!memcmp(q, pat, strlen(pat))){ hit = q; break; }
    if(!hit) return 0;
    const char *end = p + len;
    const char *nm = strstr(hit, "\"name\"");
    const char *cv = strstr(hit, "\"cover\"");
    if(nm && nm < end) grab_value(nm, end, name, name_cap);
    if(cv && cv < end) grab_value(cv, end, url, url_cap);
    return name[0] ? 1 : 0;
}

/* --- streaming fetch ----------------------------------------------------
 * The index is 1.3-1.5 MB and one entry is wanted, so the body is never held
 * whole. A fixed window is scanned after each chunk and the front is dropped;
 * a tail of RETRO_TAIL bytes is kept so a match spanning a chunk boundary is
 * still found. */

#ifdef ORBISRPC_SDK_PAYLOAD
static int retro_fetch_scan(const char *path, const char *pat,
                            char *name, size_t name_cap,
                            char *url, size_t url_cap){
    struct addrinfo hints, *res = NULL;
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    if(getaddrinfo(RETRO_HOST, "443", &hints, &res) != 0 || !res){
        if(res) freeaddrinfo(res);
        return -1;
    }
    int fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if(fd < 0){ freeaddrinfo(res); return -1; }
    struct timeval tv = { .tv_sec = RETRO_DEADLINE_S, .tv_usec = 0 };
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
    if(connect(fd, res->ai_addr, res->ai_addrlen) != 0){
        freeaddrinfo(res); close(fd); return -1;
    }
    freeaddrinfo(res);

    tls_ctx_t *t = tls_start(fd, RETRO_HOST);
    if(!t){ close(fd); return -1; }

    char req[512];
    int rl = snprintf(req, sizeof req,
        "GET %s HTTP/1.1\r\nHost: %s\r\nUser-Agent: Mozilla/5.0\r\n"
        "Accept: application/json\r\nConnection: close\r\n\r\n",
        path, RETRO_HOST);
    if(rl <= 0 || rl >= (int)sizeof req || tls_write(t, req, (size_t)rl) < 0){
        tls_free(t); close(fd); return -1;
    }

    static char win[RETRO_HDR_MAX + RETRO_CHUNK + RETRO_TAIL];
    size_t have = 0;
    size_t body_at = 0;
    int rc = 0;

    /* --- headers --- */
    while(have < RETRO_HDR_MAX){
        int r = tls_read(t, win + have, sizeof win - have);
        if(r <= 0) break;
        have += (size_t)r;
        char *e = strstr(win, "\r\n\r\n");
        if(e){
            size_t body = (size_t)(e - win) + 4;
            if(strncmp(win, "HTTP/1.1 200", 12) != 0 &&
               strncmp(win, "HTTP/2 200", 11) != 0){
                log_msg("retro: %s -> HTTP %d", path,
                        atoi(win + 9) ? atoi(win + 9) : 0);
                rc = -1;
                goto done;
            }
            /* keep the body start plus room to read into */
            memmove(win, win + body, have - body);
            have -= body;
            body_at = body;
            break;
        }
    }
    if(!body_at){ rc = -1; goto done; }

    /* --- body, streamed --- */
    for(;;){
        if(retro_extract(win, have, pat, name, name_cap, url, url_cap)){
            rc = 1;
            goto done;
        }
        /* drop everything except a tail that can still contain a whole entry */
        if(have > RETRO_TAIL) memmove(win, win + have - RETRO_TAIL, RETRO_TAIL), have = RETRO_TAIL;
        int r = tls_read(t, win + have, sizeof win - have - 1);
        if(r <= 0) break;                 /* EOF or deadline: a miss, not an error */
        have += (size_t)r;
    }
    rc = 0;                               /* scanned to the end, no match */

done:
    tls_free(t);
    close(fd);
    return rc;
}
#else /* host test build: no sockets, so the lookup always misses. The pure
       * parts above (prefix matching, unescaping, entry extraction) stay
       * compiled and are unit-tested. */
static int retro_fetch_scan(const char *path, const char *pat,
                            char *name, size_t name_cap,
                            char *url, size_t url_cap){
    (void)path; (void)pat; (void)name_cap; (void)url_cap;
    if(name && name_cap) name[0] = 0;
    if(url && url_cap) url[0] = 0;
    return -1;
}
#endif

int retro_resolve(const char *title_id, char *name, size_t name_cap,
                  char *url, size_t url_cap){
    if(!title_id || !name || !name_cap) return -1;
    name[0] = 0;
    if(url && url_cap) url[0] = 0;

    char pat[RETRO_MAX_ID + 16];
    int pl = snprintf(pat, sizeof pat, "\"id\":\"%s\"", title_id);
    if(pl <= 0 || pl >= (int)sizeof pat) return -1;

    unsigned char cands[RETRO_PLAT_N];
    int n = retro_candidates(title_id, cands);
    if(!n) return -1;

    for(int k = 0; k < n; k++){
        /* Sized for RETRO_BASE + "/" + the longest index filename, with room
         * to spare: a longer org/repo name must not truncate the request
         * path into a silent 404. */
        char path[192];
        snprintf(path, sizeof path, RETRO_BASE "/%s", s_plat_file[cands[k]]);
        name[0] = 0;
        if(url && url_cap) url[0] = 0;
        int rc = retro_fetch_scan(path, pat, name, name_cap, url, url_cap);
        if(rc == 1){
            log_msg("retro: %s -> %s (%s)", title_id, name, path);
            return 0;
        }
        if(rc < 0)
            log_msg("retro: %s fetch failed (%s)", path, strerror(errno));
    }
    return -1;
}