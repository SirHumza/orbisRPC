/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 SirHumza and CyberMask367 - see NOTICE for attribution terms.
 */
/* diag.c - redacted diagnostic bundle (host-testable + PS4).
 *
 * One plain-text file, no archive library, no network: version, uptime,
 * close-code ring, presence age, a curated subset of config, and the log
 * tail. The token is never printed; its value is scrubbed from the log
 * text as a second line of defence. */
#include "diag.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define DIAG_MAX (128u * 1024u)

static int append(char **buf, size_t *len, size_t *cap, const char *s){
    size_t n = strlen(s);
    if(*len + n + 1 > DIAG_MAX) return -1;
    if(*len + n + 1 > *cap){
        size_t nc = *cap ? *cap : 512;
        while(nc < *len + n + 1) nc *= 2;
        if(nc > DIAG_MAX) nc = DIAG_MAX;
        char *nb = (char *)realloc(*buf, nc);
        if(!nb) return -1;
        *buf = nb; *cap = nc;
    }
    memcpy(*buf + *len, s, n + 1);
    *len += n;
    return 0;
}

/* Replace every occurrence of `secret` in src with "[redacted]". */
static char *scrub_exact(const char *src, const char *secret){
    if(!src) return NULL;
    if(!secret || !secret[0]) return strdup(src);
    size_t sl = strlen(secret), n = 0, hits = 0;
    for(const char *p = src; (p = strstr(p, secret)) != NULL; p += sl) hits++;
    if(!hits) return strdup(src);
    size_t outlen = strlen(src) + hits * (strlen("[redacted]") - sl) + 1;
    char *out = (char *)malloc(outlen);
    if(!out) return NULL;
    const char *p = src, *q;
    while((q = strstr(p, secret)) != NULL){
        size_t chunk = (size_t)(q - p);
        memcpy(out + n, p, chunk); n += chunk;
        memcpy(out + n, "[redacted]", 10); n += 10;
        p = q + sl;
    }
    strcpy(out + n, p);
    return out;
}

static unsigned fnv1a(const char *s){
    unsigned h = 2166136261u;
    for(; s && *s; s++){ h ^= (unsigned char)*s; h *= 16777619u; }
    return h;
}

char *diag_build(const diag_info_t *info, const cfg_t *cfg,
                 const char *log_tail, size_t log_len){
    if(!info || !cfg) return NULL;
    char *buf = NULL;
    size_t len = 0, cap = 0;
    char line[1024];
    int rc = 0;
    rc |= append(&buf, &len, &cap, "orbisRPC diagnostic bundle\n");
    rc |= append(&buf, &len, &cap, "=========================\n");
    snprintf(line, sizeof line, "version: %s\nfirmware: %s\nuptime_s: %lld\n",
             info->version ? info->version : "?",
             info->firmware ? info->firmware : "?",
             (long long)info->uptime_s);
    rc |= append(&buf, &len, &cap, line);
    if(info->last_post_age_s >= 0){
        snprintf(line, sizeof line, "last_presence_age_s: %lld\n",
                 (long long)info->last_post_age_s);
        rc |= append(&buf, &len, &cap, line);
    } else {
        rc |= append(&buf, &len, &cap, "last_presence_age_s: never\n");
    }
    snprintf(line, sizeof line, "close_codes: %s\n",
             (info->close_ring && info->close_ring[0]) ? info->close_ring : "none");
    rc |= append(&buf, &len, &cap, line);

    snprintf(line, sizeof line,
        "config:\n"
        "  enabled: %d\n"
        "  poll_interval_s: %d\n"
        "  presence_reconcile_s: %d\n"
        "  auto_update: %d\n"
        "  pkgzone_enabled: %d\n"
        "  retro_enabled: %d\n"
        "  show_firmware: %d show_idle: %d show_media: %d show_homebrew: %d\n"
        "  application_id: %s\n"
        "  fp_client_version: %s\n"
        "  fp_client_build: %s\n"
        "  learned_titles: %d\n",
        cfg->enabled, cfg->poll_interval_s, cfg->presence_reconcile_s,
        cfg->auto_update, cfg->pkgzone_enabled, cfg->retro_enabled,
        cfg->show_firmware, cfg->show_idle, cfg->show_media, cfg->show_homebrew,
        cfg->application_id[0] ? cfg->application_id : "(none)",
        cfg->fp_client_version, cfg->fp_client_build, cfg->n_titles);
    rc |= append(&buf, &len, &cap, line);

    /* The token never enters the bundle. Report only shape, never bytes. */
    snprintf(line, sizeof line, "  token: [redacted] len=%zu fp=%06x\n",
             cfg->token[0] ? strlen(cfg->token) : 0,
             fnv1a(cfg->token) & 0xffffffu);
    rc |= append(&buf, &len, &cap, line);

    rc |= append(&buf, &len, &cap, "\nlog tail:\n");
    char *safe = scrub_exact(log_tail ? log_tail : "", cfg->token);
    if(!safe){ free(buf); return NULL; }
    rc |= append(&buf, &len, &cap, safe);
    free(safe);
    if(rc != 0){ free(buf); return NULL; }
    (void)log_len; /* NUL-terminated by the caller; len kept for the API */
    return buf;
}

int diag_write_file(const char *path, const diag_info_t *info,
                    const cfg_t *cfg, const char *log_path){
    if(!path || !info || !cfg) return -1;
    char tail[8193];
    size_t got = 0;
    FILE *lf = log_path ? fopen(log_path, "rb") : NULL;
    if(lf){
        if(fseek(lf, 0, SEEK_END) == 0){
            long sz = ftell(lf);
            long want = sz > (long)sizeof tail - 1 ? (long)sizeof tail - 1 : sz;
            if(want > 0 && fseek(lf, sz - want, SEEK_SET) == 0){
                got = fread(tail, 1, (size_t)want, lf);
            }
        }
        fclose(lf);
    }
    tail[got] = 0;
    char *bundle = diag_build(info, cfg, tail, got);
    if(!bundle) return -1;

    char tmp[320];
    snprintf(tmp, sizeof tmp, "%s.new", path);
    FILE *f = fopen(tmp, "wb");
    if(!f){ free(bundle); return -1; }
    size_t n = strlen(bundle);
    int ok = (fwrite(bundle, 1, n, f) == n);
    if(ok && fflush(f) != 0) ok = 0;
    if(ok){ int fd = fileno(f); if(fd >= 0 && fsync(fd) != 0) ok = 0; }
    if(fclose(f) != 0) ok = 0;
    free(bundle);
    if(!ok){ remove(tmp); return -1; }
    chmod(tmp, 0600);
    if(rename(tmp, path) != 0){ remove(tmp); return -1; }
    return 0;
}
