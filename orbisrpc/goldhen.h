/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 SirHumza and CyberMask367 - see NOTICE for attribution terms.
 */
/* goldhen.h - read the GoldHEN version out of the kernel log.
 *
 * D-18: on old GoldHEN (2.4b18.8) the payload has no "Utilities" entry and
 * the user gets no explanation. GoldHEN prints its banner to kern.msgbuf at
 * load, which the daemon already reads for scene detection, so the version is
 * available without a new API. Pure parsing lives here for host tests.
 *
 * Version shape: 2.4b18.10 -> (2, 4, 18, 10). Returns -1 when no banner is
 * present. */
#ifndef ORBISRPC_GOLDHEN_H
#define ORBISRPC_GOLDHEN_H
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

/* Minimum that exposes the Utilities entry in the payload menu. */
#define GOLDHEN_MIN "2.4b18.10"

static inline int goldhen_parse_ver(const char *s, int v[4]){
    if(!s || !v) return -1;
    char *end = NULL;
    for(int i = 0; i < 4; i++) v[i] = 0;
    v[0] = (int)strtol(s, &end, 10);
    if(!end || *end != '.') return -1;
    s = end + 1;
    v[1] = (int)strtol(s, &end, 10);
    if(!end || (*end != 'b' && *end != 'B')) return -1;
    s = end + 1;
    v[2] = (int)strtol(s, &end, 10);
    if(!end || *end != '.') return -1;
    s = end + 1;
    v[3] = (int)strtol(s, &end, 10);
    return 0;
}

static inline int goldhen_ver_cmp(const int a[4], const int b[4]){
    for(int i = 0; i < 4; i++){
        if(a[i] < b[i]) return -1;
        if(a[i] > b[i]) return 1;
    }
    return 0;
}

/* Find the newest "GoldHEN vX.YbZ.W" banner in `log`. Returns 0 and fills
 * `out` when found, -1 otherwise. */
static inline int goldhen_version_from_log(const char *log,
                                           char *out, size_t cap){
    if(!log || !out || cap == 0) return -1;
    const char *p = log, *last = NULL;
    while((p = strstr(p, "GoldHEN v")) != NULL){
        last = p + sizeof("GoldHEN v") - 1;
        p = last;
    }
    if(!last || !*last) return -1;
    size_t n = 0;
    while(last[n] && last[n] != ' ' && last[n] != '\n' &&
          last[n] != '\r' && last[n] != '\t' && n + 1 < cap)
        n++;
    if(n == 0) return -1;
    memcpy(out, last, n);
    out[n] = 0;
    return 0;
}

#endif /* ORBISRPC_GOLDHEN_H */
