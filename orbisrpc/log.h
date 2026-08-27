/* log.h - timestamped file logger with stderr and optional kernel-log fallback. */
#ifndef LOG_H
#define LOG_H
#include <stdio.h>
/* Selects the first writable log path, falling back to stderr if needed. */
void log_init(const char *path);
void log_msg(const char *fmt, ...);
void log_close(void);
#endif