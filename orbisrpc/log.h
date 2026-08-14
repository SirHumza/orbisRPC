/* log.h - simple file logger (falls back to stderr). */
#ifndef LOG_H
#define LOG_H
#include <stdio.h>
void log_init(const char *path);
void log_msg(const char *fmt, ...);
void log_close(void);
#endif