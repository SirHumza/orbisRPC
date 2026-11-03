/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 SirHumza and CyberMask367 - see NOTICE for attribution terms.
 */
/* icfg.h - installer config read/write for /data/orbisRPC/config.json.
 * Read-modify-write: preserves learned titles and every other field.
 * Pure logic (token_valid) is host-tested. */
#ifndef INSTALLER_ICFG_H
#define INSTALLER_ICFG_H
#include <stddef.h>

#define ICFG_PATH "/data/orbisRPC/config.json"

/* 1 valid user token, 0 not. Delegates to the daemon's shared token.h:
 * 40..200 chars, at least three dot-separated base64url segments.
 * Rejects placeholders, whitespace and empty segments. */
int token_valid(const char *t);

/* Load current token ("" when missing). 0 ok, -1 unreadable. */
int icfg_token_load(const char *path, char *out, size_t cap);
/* Set token (validated first). 0 saved, -1 invalid, -2 I/O error. */
int icfg_token_save(const char *path, const char *token);
/* Set a string/int key, preserving everything else. 0 saved, -1 I/O. */
int icfg_set_str(const char *path, const char *key, const char *val);
int icfg_set_int(const char *path, const char *key, long val);
/* Read a string/int key. 0 found, -1 missing/unreadable. */
int icfg_get_str(const char *path, const char *key, char *out, size_t cap);
int icfg_get_int(const char *path, const char *key, long *out);
/* Count entries in the "titles" map (learned + manual). Fail-soft 0. */
int icfg_titles_count(const char *path);

#endif
