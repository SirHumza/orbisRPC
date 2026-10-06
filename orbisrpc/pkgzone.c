/* pkgzone.c - see pkgzone.h. */
#include "pkgzone.h"
#include "log.h"
#include "retro.h"
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#ifdef ORBISRPC_SDK_PAYLOAD
#include "tls.h"
#include "updater_http.h"
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#endif

#define PKGZONE_BOILERPLATE "Install HB-Store on your Playstation"

int pkgzone_wants(const char *title_id){
    if(!title_id) return 0;
    size_t n = strlen(title_id);
    if(n != 9) return 0;
    /* Shape first: this only makes sense for a real TITLEID-shaped id. */
    for(size_t i = 0; i < n; i++){
        char c = title_id[i];
        if(!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))) return 0;
    }
    /* Retail titles are served by Sony TMDB; never ask a third party about
     * them, and never pay the latency. */
    if(!strncmp(title_id, "CUSA", 4)) return 0;
    /* Retro ids belong to retro-games, which indexes them. Ask the function
     * that owns the prefix tables so this cannot drift from them: the
     * hand-written list that used to live here was missing SCUS, so
     * SCUS97399 was scraped here forever (302/500 every poll) while
     * retro-games -- which has it -- was never consulted. */
    if(retro_wants(title_id)) return 0;
    /* Homebrew that targets a retro platform (self-hosted, custom-built).
     * retro.c does not list these and neither index carries them -- checked
     * all three: HARC/ELUA/ELUS/ELPM are 0 entries everywhere. So they stay
     * out of retro-games but still belong here rather than falling through
     * to a guaranteed miss. */
    if(!strncmp(title_id, "HARC", 4) || !strncmp(title_id, "ELUA", 4) ||
       !strncmp(title_id, "ELUS", 4) || !strncmp(title_id, "ELPM", 4))
        return 0;
    return 1;
}

/* --- pure scrape ----------------------------------------------------- */

/* strcasestr is glibc-only and the PS4 libc does not have it. A short
 * case-insensitive find over a bounded haystack is all this needs. */
static const char *find_ci(const char *hay, size_t hlen, const char *needle){
    size_t n = strlen(needle);
    if(n == 0 || hlen < n) return NULL;
    for(size_t i = 0; i + n <= hlen; i++){
        size_t k = 0;
        while(k < n && tolower((unsigned char)hay[i + k]) == tolower((unsigned char)needle[k])) k++;
        if(k == n) return hay + i;
    }
    return NULL;
}

/* Collapse HTML text to a single trimmed line: tags out, entities resolved
 * for the handful that actually show up in a title. */
static void squeeze(char *dst, size_t cap, const char *src, size_t len){
    size_t o = 0;
    int in_tag = 0;
    for(size_t i = 0; i < len && o + 1 < cap; i++){
        char c = src[i];
        if(in_tag){ if(c == '>') in_tag = 0; continue; }
        if(c == '<'){ in_tag = 1; continue; }
        if(c == '&'){
            /* &amp; &quot; &#39; &nbsp; &amp; — enough for a heading */
            if(i + 4 < len && !strncmp(src + i, "&amp;", 5)){ dst[o++] = '&'; i += 4; continue; }
            if(i + 5 < len && !strncmp(src + i, "&quot;", 6)){ dst[o++] = '"'; i += 5; continue; }
            if(i + 4 < len && !strncmp(src + i, "&nbsp;", 6)){ dst[o++] = ' '; i += 5; continue; }
            if(i + 4 < len && !strncmp(src + i, "&#39;", 5)){ dst[o++] = '\''; i += 4; continue; }
        }
        /* headings are single-line; collapse any whitespace run */
        if(isspace((unsigned char)c)){
            if(o && dst[o - 1] != ' ') dst[o++] = ' ';
            continue;
        }
        dst[o++] = c;
    }
    while(o && dst[o - 1] == ' ') dst[--o] = 0;
    dst[o] = 0;
}

int pkgzone_parse(const char *html, size_t len, const char *title_id,
                  char *out, size_t cap, char *url, size_t url_cap){
    if(!html || !out || cap == 0) return -1;
    out[0] = 0;
    if(url && url_cap) url[0] = 0;
    if(!title_id || strlen(title_id) != 9) return -1;

    /* First <h1> that is not the site boilerplate is the title. */
    char raw[256];
    const char *cur = html;
    const char *end = html + len;
    while(cur && cur < end){
        const char *open = find_ci(cur, (size_t)(end - cur), "<h1");
        if(!open || open >= end) break;
        /* Require a tag boundary so "<h1foo" cannot match. */
        if(open + 3 < end && !isspace((unsigned char)open[3]) && open[3] != '>'){
            cur = open + 3;
            continue;
        }
        const char *gt = memchr(open, '>', (size_t)(end - open));
        if(!gt) break;
        const char *close = find_ci(gt, (size_t)(end - gt), "</h1>");
        if(!close) break;
        size_t n = (size_t)(close - (gt + 1));
        if(n >= sizeof raw) n = sizeof raw - 1;
        memcpy(raw, gt + 1, n);
        raw[n] = 0;

        char line[256];
        squeeze(line, sizeof line, raw, n);
        if(line[0] && strcmp(line, PKGZONE_BOILERPLATE) != 0){
            snprintf(out, cap, "%s", line);
            /* The cover path is deterministic; parsing the page for it would
             * just be one more thing to break when the layout changes. */
            if(url && url_cap)
                snprintf(url, url_cap,
                         "https://" PKGZONE_HOST "/images/%s/cover.png", title_id);
            return 0;
        }
        cur = close + 5;
    }
    return -1;
}

/* --- network half ---------------------------------------------------- */
#ifdef ORBISRPC_SDK_PAYLOAD

int pkgzone_resolve(const char *title_id, char *name, size_t name_cap,
                    char *url, size_t url_cap){
    if(name && name_cap) name[0] = 0;
    if(url && url_cap) url[0] = 0;
    if(!pkgzone_wants(title_id)){
        log_msg("pkgzone: skipping %s (retail, retro or malformed)", title_id);
        return -1;
    }
    log_msg("pkgzone: resolving %s", title_id);

    char host[128], path[64];
    snprintf(host, sizeof host, "%s", PKGZONE_HOST);
    snprintf(path, sizeof path, "/details/%s", title_id);

    struct addrinfo hints, *res = NULL;
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    if(getaddrinfo(host, "443", &hints, &res) != 0 || !res){
        log_msg("pkgzone: dns fail for %s", host);
        if(res) freeaddrinfo(res);
        return -1;
    }
    int fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if(fd < 0){ freeaddrinfo(res); log_msg("pkgzone: socket fail errno=%d", errno); return -1; }
    if(connect(fd, res->ai_addr, (unsigned)res->ai_addrlen) != 0){
        int e = errno;
        freeaddrinfo(res);
        close(fd);
        log_msg("pkgzone: connect %s:443 fail errno=%d", host, e);
        return -1;
    }
    freeaddrinfo(res);

    tls_ctx_t *t = tls_start(fd, host);
    if(!t){
        close(fd);
        log_msg("pkgzone: tls handshake failed (see tls: lines above)");
        return -1;
    }

    char req[256];
    int rl = snprintf(req, sizeof req,
        "GET %s HTTP/1.1\r\nHost: %s\r\nUser-Agent: Mozilla/5.0\r\n"
        "Accept: text/html\r\nConnection: close\r\n\r\n", path, host);
    int rc = -1;
    if(rl <= 0 || tls_write(t, req, (size_t)rl) != rl){
        log_msg("pkgzone: request write fail");
        goto done;
    }

    /* Read to EOF. Bounded: a hostile or broken page must not eat the heap. */
    static char raw[PKGZONE_MAX_HTML + 4096];
    size_t total = 0;
    for(;;){
        int n = tls_read(t, raw + total, sizeof raw - total - 1);
        if(n < 0) break;
        if(n == 0) break;
        total += (size_t)n;
        if(total >= sizeof raw - 1) break;
    }
    raw[total] = 0;

    int status = 0, blen = 0;
    char loc[256];
    char *body = upd_parse_response(raw, total, PKGZONE_MAX_HTML, &status,
                                    (size_t *)&blen, loc, sizeof loc);
    if(!body){ log_msg("pkgzone: unparseable HTTP response (%zu bytes)", total); goto done; }
    if(status != 200){
        log_msg("pkgzone: status %d", status);
        free(body);
        rc = -1;
        goto done;
    }
    if(pkgzone_parse(body, (size_t)blen, title_id, name, name_cap,
                     url, url_cap) == 0){
        log_msg("pkgzone: %s -> %s", title_id, name);
        rc = 0;
    } else {
        log_msg("pkgzone: no heading for %s", title_id);
    }
    free(body);

done:
    tls_free(t);
    close(fd);
    return rc;
}
#else
int pkgzone_resolve(const char *title_id, char *name, size_t name_cap,
                    char *url, size_t url_cap){
    (void)title_id; (void)name; (void)name_cap; (void)url; (void)url_cap;
    return -1;
}
#endif