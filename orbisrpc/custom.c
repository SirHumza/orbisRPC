/* custom.c - see custom.h.
 *
 * A community-maintained TITLEID -> name map for apps that pkg-zone does not
 * carry and that no retro prefix covers. Lives in the repo as
 * config/custom.json so contributions are ordinary pull requests.
 *
 * Deliberately mirrors retro.c rather than inventing a second shape:
 *
 *   [ {"id":"ORPC00042","name":"Some App","cover":"https://.../a.png"}, ... ]
 *
 * Two differences from the retro indexes, both intentional:
 *
 *   - No prefix gate. retro_resolve() only runs for known retro prefixes;
 *     this is the final fallback and runs for any unresolved TITLEID, which is
 *     the whole point -- it covers the long tail a prefix list cannot
 *     enumerate.
 *   - Unbounded. retro only ever holds one platform index; this may grow, so
 *     the body is streamed rather than parsed, and CUSTOM_MAX_BYTES caps how
 *     much will ever be pulled so a huge or hostile file cannot stall a poll.
 *
 * Cost note: because there is no prefix gate, every title nothing else can
 * resolve re-downloads the file on each re-resolve cycle. Misses are not
 * cached on purpose (a network miss must not poison games_cache.json), so the
 * only damper is resolve_reuse()'s 300s grace window. Accepted for now; if
 * this shows up as traffic, a disk cache is the fix.
 */
#include "custom.h"
#include "jsonstr.h"
#include "jsonlite.h"
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

/* Host and base path are separate for the same reason as retro.c: DNS and the
 * TLS certificate both need a bare host, never a URL path. */
#define CUSTOM_HOST "raw.githubusercontent.com"
#define CUSTOM_BASE "/SirHumza/orbisRPC/refs/heads/main/config"
#define CUSTOM_PATH CUSTOM_BASE "/custom.json"
#define CUSTOM_DEADLINE_S 25
#define CUSTOM_HDR_MAX 8192
/* Streaming window: a chunk plus a tail big enough to hold any single entry,
 * so a match spanning a chunk boundary survives. */
#define CUSTOM_CHUNK  4096
#define CUSTOM_TAIL   768
/* Ceiling on total body bytes read. The list is expected to stay well under
 * this; the cap exists so a malformed or unexpectedly huge file cannot turn a
 * name lookup into an unbounded download. */
#define CUSTOM_MAX_BYTES (4u*1024u*1024u)

int custom_extract(const char *p, size_t len, const char *pat,
                   char *name, size_t name_cap,
                   char *url, size_t url_cap){
    if(name && name_cap) name[0] = 0;
    if(url && url_cap) url[0] = 0;
    if(!p || !pat) return 0;
    size_t plen = strlen(pat);
    if(plen < 8 || strncmp(pat, "\"id\":\"", 6) || pat[plen - 1] != '"') return 0;
    size_t id_len = plen - 7;
    /* A streaming window need not end in NUL, or contain the surrounding
     * array. Parse complete bounded objects, never borrow the next entry's
     * name/cover when the requested entry omits one. */
    for(size_t start = 0; start < len; start++){
        if(p[start] != '{') continue;
        size_t end = start;
        unsigned depth = 0;
        int quoted = 0, escaped = 0;
        for(; end < len; end++){
            char ch = p[end];
            if(quoted){
                if(escaped) escaped = 0;
                else if(ch == '\\') escaped = 1;
                else if(ch == '"') quoted = 0;
            } else if(ch == '"') quoted = 1;
            else if(ch == '{') depth++;
            else if(ch == '}' && --depth == 0){ end++; break; }
        }
        if(depth || quoted) continue;
        jl_val_t *entry = jl_parse(p + start, end - start);
        if(!entry) continue;
        const jl_val_t *id = jl_obj_get(entry, "id");
        const jl_val_t *nm = jl_obj_get(entry, "name");
        const jl_val_t *cv = jl_obj_get(entry, "cover");
        int found = id && id->type == JL_STRING && strlen(id->str) == id_len &&
                    !memcmp(id->str, pat + 6, id_len) && nm && nm->type == JL_STRING &&
                    nm->str[0] && name && name_cap;
        if(found){
            snprintf(name, name_cap, "%s", nm->str);
            if(cv && cv->type == JL_STRING && url && url_cap)
                snprintf(url, url_cap, "%s", cv->str);
        }
        jl_free(entry);
        if(found) return 1;
        start = end - 1;
    }
    return 0;
}

#ifdef ORBISRPC_SDK_PAYLOAD
static int custom_fetch_scan(const char *pat,
                             char *name, size_t name_cap,
                             char *url, size_t url_cap){
    struct addrinfo hints, *res = NULL;
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    if(getaddrinfo(CUSTOM_HOST, "443", &hints, &res) != 0 || !res){
        if(res) freeaddrinfo(res);
        return -1;
    }
    int fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if(fd < 0){ freeaddrinfo(res); return -1; }
    struct timeval tv = { .tv_sec = CUSTOM_DEADLINE_S, .tv_usec = 0 };
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
    if(connect(fd, res->ai_addr, res->ai_addrlen) != 0){
        freeaddrinfo(res); close(fd); return -1;
    }
    freeaddrinfo(res);

    tls_ctx_t *t = tls_start(fd, CUSTOM_HOST);
    if(!t){ close(fd); return -1; }

    char req[512];
    int rl = snprintf(req, sizeof req,
        "GET %s HTTP/1.1\r\nHost: %s\r\nUser-Agent: Mozilla/5.0\r\n"
        "Accept: application/json\r\nConnection: close\r\n\r\n",
        CUSTOM_PATH, CUSTOM_HOST);
    if(rl <= 0 || rl >= (int)sizeof req || tls_write(t, req, (size_t)rl) < 0){
        tls_free(t); close(fd); return -1;
    }

    static char win[CUSTOM_HDR_MAX + CUSTOM_CHUNK + CUSTOM_TAIL];
    size_t have = 0;
    size_t body_at = 0;
    size_t total = 0;
    int rc = 0;

    while(have < CUSTOM_HDR_MAX){
        int r = tls_read(t, win + have, sizeof win - have);
        if(r <= 0) break;
        have += (size_t)r;
        char *e = NULL;
        for(size_t i = 0; i + 4 <= have; i++){
            if(!memcmp(win + i, "\r\n\r\n", 4)){ e = win + i; break; }
        }
        if(e){
            size_t body = (size_t)(e - win) + 4;
            if(body > CUSTOM_HDR_MAX || have < 12){ rc = -1; goto done; }
            if(strncmp(win, "HTTP/1.1 200", 12) != 0 &&
               strncmp(win, "HTTP/2 200", 11) != 0){
                /* Not an error worth logging per poll: a missing list just
                 * means the community has not added this title yet. */
                rc = -1;
                goto done;
            }
            memmove(win, win + body, have - body);
            have -= body;
            body_at = body;
            break;
        }
    }
    if(!body_at){ rc = -1; goto done; }

    for(;;){
        if(custom_extract(win, have, pat, name, name_cap, url, url_cap)){
            rc = 1;
            goto done;
        }
        if(have > CUSTOM_TAIL)
            memmove(win, win + have - CUSTOM_TAIL, CUSTOM_TAIL), have = CUSTOM_TAIL;
        int r = tls_read(t, win + have, sizeof win - have - 1);
        if(r <= 0) break;                 /* EOF or deadline: a miss, not an error */
        have += (size_t)r;
        total += (size_t)r;
        if(total >= CUSTOM_MAX_BYTES) break;   /* ceiling reached: treat as a miss */
    }
    rc = 0;

done:
    tls_free(t);
    close(fd);
    return rc;
}
#else /* host test build: no sockets, so the lookup always misses. The pure
       * extraction above stays compiled and is unit-tested. */
static int custom_fetch_scan(const char *pat,
                             char *name, size_t name_cap,
                             char *url, size_t url_cap){
    (void)pat; (void)name_cap; (void)url_cap;
    if(name && name_cap) name[0] = 0;
    if(url && url_cap) url[0] = 0;
    return -1;
}
#endif

int custom_resolve(const char *title_id, char *name, size_t name_cap,
                   char *url, size_t url_cap){
    if(name && name_cap) name[0] = 0;
    if(url && url_cap) url[0] = 0;
    if(!title_id || !title_id[0] || !name || !name_cap) return -1;

    char pat[64];
    int pl = snprintf(pat, sizeof pat, "\"id\":\"%s\"", title_id);
    if(pl <= 0 || pl >= (int)sizeof pat) return -1;

    if(custom_fetch_scan(pat, name, name_cap, url, url_cap) == 1){
        log_msg("custom: %s -> %s", title_id, name);
        return 0;
    }
    return -1;
}
