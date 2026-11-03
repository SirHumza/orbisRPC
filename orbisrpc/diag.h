/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 SirHumza and CyberMask367 - see NOTICE for attribution terms.
 */
/* diag.h - redacted diagnostic bundle. */
#ifndef ORBISRPC_DIAG_H
#define ORBISRPC_DIAG_H
#include <stddef.h>
#include <stdint.h>
#include "cfg.h"

typedef struct {
    const char *version;
    const char *firmware;
    int64_t uptime_s;
    int64_t last_post_age_s;   /* -1 when nothing was posted yet */
    const char *close_ring;    /* "4004,1006" or NULL */
} diag_info_t;

/* Build the bundle into a malloc'd NUL-terminated string. The configured
 * token is never emitted, and is scrubbed from the log tail. */
char *diag_build(const diag_info_t *info, const cfg_t *cfg,
                 const char *log_tail, size_t log_len);

/* Read the tail of log_path, build, and atomically write `path` at 0600.
 * Returns 0 on success, -1 on any failure. */
int diag_write_file(const char *path, const diag_info_t *info,
                    const cfg_t *cfg, const char *log_path);

#endif
