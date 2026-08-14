/* log.c */
#include "log.h"
#include <stdarg.h>
#include <time.h>
#include <string.h>

static FILE *g_log = NULL;

void log_init(const char *path) {
    if (g_log) return;
    g_log = fopen(path, "ab");
    if (g_log) setvbuf(g_log, NULL, _IONBF, 0);
}

void log_msg(const char *fmt, ...) {
    if (!g_log) { g_log = stderr; }
    va_list ap;
    va_start(ap, fmt);
    time_t t = time(NULL);
    struct tm *tm = localtime(&t);
    char ts[20];
    strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M", tm);
    fprintf(g_log, "[%s] ", ts);
    vfprintf(g_log, fmt, ap);
    fprintf(g_log, "\n");
    va_end(ap);
    fflush(g_log);
}

void log_close(void) {
    if (g_log && g_log != stderr) fclose(g_log);
    g_log = NULL;
}
