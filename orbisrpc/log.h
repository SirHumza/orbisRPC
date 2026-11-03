/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 SirHumza and CyberMask367 - see NOTICE for attribution terms.
 */
/* log.h - timestamped file logger with stderr and optional kernel-log fallback. */
#ifndef LOG_H
#define LOG_H
#include <stdio.h>
#if defined(__GNUC__) || defined(__clang__)
#define ORBISRPC_PRINTF(f, a) __attribute__((format(printf, f, a)))
#else
#define ORBISRPC_PRINTF(f, a)
#endif
/* Selects the first writable log path, falling back to stderr if needed. */
void log_init(const char *path);
void log_msg(const char *fmt, ...) ORBISRPC_PRINTF(1, 2);
/* Debug logging: compiled in, emitted only when log_set_debug(1) is on.
 * Zero overhead otherwise. For per-poll detail — never for secrets. */
void log_set_debug(int on);
int log_is_debug(void);
void log_dbg(const char *fmt, ...) ORBISRPC_PRINTF(1, 2);
void log_close(void);
/* Path of the file currently receiving log lines, or "stderr" when no file
 * could be opened. Printed at startup so a tester can find the log without
 * guessing (D-16). */
const char *log_path(void);
/* Rotation cap in bytes (config log_max_kb); 0 restores the built-in default. */
void log_set_cap(size_t cap);
#endif
