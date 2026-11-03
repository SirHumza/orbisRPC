/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 SirHumza and CyberMask367 - see NOTICE for attribution terms.
 */
/* token.h - Discord user-token shape check shared by the daemon and tests.
 *
 * Discord does not publish the session-token format, so this validates only
 * the shape every real token has: three dot-separated base64url segments
 * inside a plausible length range. It must never be the reason a working
 * token is refused, so the rules are deliberately loose. The value of the
 * check is the reason string: a rejected token always says exactly which
 * rule failed, which is what D-12 asked for.
 *
 * Header-only and dependency-free so host tests exercise the same code the
 * payload runs. */
#ifndef ORBISRPC_TOKEN_H
#define ORBISRPC_TOKEN_H
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#define TOKEN_MIN_LEN 40
#define TOKEN_MAX_LEN 200

static inline int token_reason(const char *t, char *why, size_t cap){
    if(why && cap) why[0] = 0;
    if(!t || !t[0]){
        if(why && cap) snprintf(why, cap, "token is empty");
        return -1;
    }
    if(!strcmp(t, "SET_ME") || !strcmp(t, "PUT_TOKEN_HERE") ||
       !strcmp(t, "YOUR_TOKEN_HERE")){
        if(why && cap) snprintf(why, cap, "token is still the placeholder");
        return -1;
    }
    size_t n = strlen(t);
    if(n < TOKEN_MIN_LEN){
        if(why && cap)
            snprintf(why, cap, "token is too short (%zu chars; expected at least %d)",
                     n, TOKEN_MIN_LEN);
        return -1;
    }
    if(n > TOKEN_MAX_LEN){
        if(why && cap)
            snprintf(why, cap, "token is too long (%zu chars; expected at most %d)",
                     n, TOKEN_MAX_LEN);
        return -1;
    }
    int dots = 0, seg = 0;
    for(size_t i = 0; i < n; i++){
        char c = t[i];
        if(c == '.'){
            if(seg == 0){
                if(why && cap)
                    snprintf(why, cap, "empty token segment %d (leading/trailing or double dot)",
                             dots + 1);
                return -1;
            }
            dots++;
            seg = 0;
            continue;
        }
        if(!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
             (c >= '0' && c <= '9') || c == '_' || c == '-')){
            if(why && cap)
                snprintf(why, cap,
                         "illegal character 0x%02x at position %zu "
                         "(allowed: A-Z a-z 0-9 _ -)",
                         (unsigned char)c, i);
            return -1;
        }
        seg++;
    }
    if(dots < 2){
        if(why && cap)
            snprintf(why, cap,
                     "expected at least 3 dot-separated segments, found %d", dots + 1);
        return -1;
    }
    if(seg == 0){
        if(why && cap) snprintf(why, cap, "last token segment is empty");
        return -1;
    }
    return 0;
}

#endif /* ORBISRPC_TOKEN_H */
