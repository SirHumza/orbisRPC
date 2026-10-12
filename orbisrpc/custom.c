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
    if(len < plen) return 0;
    const char *hit = NULL;
    for(const char *q = p; q + plen <= p + len; q++)
        if(!memcmp(q, pat, plen)){ hit = q; break; }
    if(!hit) return 0;
    const char *end = p + len;
    const char *nm = strstr(hit, "\"name\"");
    const char *cv = strstr(hit, "\"cover\"");
    if(nm && nm < end) json_grab_value(nm, end, name, name_cap);
    if(cv && cv < end) json_grab_value(cv, end, url, url_cap);
    return (name && name[0]) ? 1 : 0;
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
        char *e = strstr(win, "\r\n\r\n");
        if(e){
            size_t body = (size_t)(e - win) + 4;
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