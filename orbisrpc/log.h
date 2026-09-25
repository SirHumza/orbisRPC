/* log.h - timestamped file logger with stderr and optional kernel-log fallback. */
#ifndef LOG_H
#define LOG_H
#include <stdio.h>
/* Selects the first writable log path, falling back to stderr if needed. */
void log_init(const char *path);
void log_msg(const char *fmt, ...);
/* Debug logging: compiled in, emitted only when log_set_debug(1) is on.
 * Zero overhead otherwise. For per-poll detail — never for secrets. */
void log_set_debug(int on);
int log_is_debug(void);
void log_dbg(const char *fmt, ...);
void log_close(void);
#endif
