/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 SirHumza and CyberMask367 - see NOTICE for attribution terms.
 */
/* updater_http.c - pure HTTP response parsing (host-testable + PS4). */
#include "updater_http.h"
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdio.h>

char *upd_parse_response(const char *raw, size_t rawlen, size_t cap,
                         int *out_status, size_t *out_len,
                         char *out_location, size_t loc_cap){
    if(out_status) *out_status = 0;
    if(out_len) *out_len = 0;
    if(out_location && loc_cap) out_location[0] = 0;
    if(!raw || cap < 2) return NULL;
    /* Split headers/body. */
    const char *e = strstr(raw, "\r\n\r\n");
    if(!e || (size_t)(e - raw) > UPD_HDR_MAX) return NULL;
    size_t hlen = (size_t)(e - raw);
    const char *body = e + 4;
    size_t bodylen = (hlen + 4 <= rawlen) ? rawlen - hlen - 4 : 0;
    int status = 0;
    /* strtol, not atoi: the status line is network input and atoi has
     * undefined behaviour on a value it cannot represent. */
    if(hlen >= 12 && !memcmp(raw, "HTTP/1.", 7)) status = (int)strtol(raw + 9, NULL, 10);
    if(out_status) *out_status = status;
    /* Header scan (case-insensitive names, single pass). */
    long content_len = -1;
    int chunked = 0;
    {
        const char *line = raw;
        while(line < e){
            const char *nl = strstr(line, "\r\n");
            if(!nl || nl > e) nl = e;
            const char *colon = memchr(line, ':', (size_t)(nl - line));
            if(colon){
                size_t nl2 = (size_t)(colon - line);
                while(nl2 > 0 && (line[nl2-1] == ' ' || line[nl2-1] == '\t')) nl2--;
                const char *val = colon + 1;
                while(val < nl && (*val == ' ' || *val == '\t')) val++;
                size_t vlen = (size_t)(nl - val);
                if(nl2 == 14 && !strncasecmp(line, "content-length", 14)){
                    char nb[32];
                    size_t cn = vlen < sizeof nb - 1 ? vlen : sizeof nb - 1;
                    memcpy(nb, val, cn); nb[cn] = 0;
                    /* strtol: atol is UB on overflow. A value beyond LONG_MAX
                     * clamps, which then fails the short-body check below. */
                    content_len = strtol(nb, NULL, 10);
                } else if(nl2 == 17 && !strncasecmp(line, "transfer-encoding", 17)){
                    if(vlen >= 7 && !strncasecmp(val + vlen - 7, "chunked", 7))
                        chunked = 1;
                } else if(nl2 == 8 && !strncasecmp(line, "location", 8)){
                    if(out_location && loc_cap > 1){
                        size_t cn = vlen < loc_cap - 1 ? vlen : loc_cap - 1;
                        memcpy(out_location, val, cn);
                        out_location[cn] = 0;
                    }
                }
            }
            line = (*nl == '\r') ? nl + 2 : nl;
            if(line >= e) break;
        }
    }
    char *out = NULL;
    size_t olen = 0;
    if(chunked){
        out = (char*)malloc(cap);
        if(!out) return NULL;
        size_t pos = 0;
        while(pos < bodylen){
            const char *le = strstr(body + pos, "\r\n");
            if(!le || (size_t)(le - (body + pos)) > 32){ free(out); return NULL; }
            char nb[33];
            size_t nlen = (size_t)(le - (body + pos));
            memcpy(nb, body + pos, nlen); nb[nlen] = 0;
            long cn = strtol(nb, NULL, 16);
            if(cn < 0 || cn > (long)(cap - 1)){ free(out); return NULL; }
            pos += nlen + 2;
            if(pos > bodylen){ free(out); return NULL; }
            if(cn == 0) break;
            if(pos + (size_t)cn + 2 > bodylen){ free(out); return NULL; }
            if(olen + (size_t)cn > cap - 1){ free(out); return NULL; }
            memcpy(out + olen, body + pos, (size_t)cn);
            olen += (size_t)cn;
            pos += (size_t)cn + 2; /* data + trailing CRLF */
        }
    } else {
        /* A declared Content-Length longer than the bytes received means the
         * connection died mid-body. Treating the short body as complete made
         * a truncated download look successful; the hash check caught the
         * binary case, but release JSON and small assets could slip through. */
        if(content_len >= 0 && (size_t)content_len > bodylen) return NULL;
        if(content_len >= 0 && (size_t)content_len < bodylen)
            bodylen = (size_t)content_len;
        if(bodylen > cap - 1) return NULL;
        out = (char*)malloc(cap);
        if(!out) return NULL;
        memcpy(out, body, bodylen);
        olen = bodylen;
    }
    out[olen] = 0;
    if(out_len) *out_len = olen;
    return out;
}
