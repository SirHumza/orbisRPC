/* log.c - file logger: rotating (512 KB cap), timestamped, falls back to
 * stderr. Tries several paths because mount visibility differs between app,
 * plugin, and payload processes. Every line is mirrored to /dev/klog when
 * possible so payloads remain debuggable even with no writable mount. */
#include "log.h"
#include <stdarg.h>
#include <time.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>

#define LOG_CAP (512 * 1024)

static FILE *g_log = NULL;
static int g_klog = -1;

/* tried in order at log_init(); first openable file wins */
static const char *const LOG_FALLBACKS[] = {
    NULL, /* filled in with the caller's requested path */
    "/data/orbisRPC/log.txt",
    "/data/orbisrpc.log",
    "/user/temp/orbisrpc.log",
    "/mnt/sandbox/orbisrpc.log",
    "/orbisrpc.log",
};

void log_init(const char *path) {
    if (g_log) return;
    struct stat st;
    if (stat(path, &st) == 0 && st.st_size > LOG_CAP) remove(path);
    g_log = fopen(path, "ab");
    if (!g_log) {
        for (unsigned i = 0; i < sizeof(LOG_FALLBACKS)/sizeof(LOG_FALLBACKS[0]); i++) {
            const char *p = LOG_FALLBACKS[i];
            if (!p || strcmp(p, path) == 0) continue;
            g_log = fopen(p, "ab");
            if (g_log) break;
        }
    }
    if (g_log) setvbuf(g_log, NULL, _IONBF, 0);
    /* klog mirror: survives even when every mount is invisible */
    g_klog = open("/dev/klog", O_WRONLY);
}

void log_msg(const char *fmt, ...) {
    if (!g_log) { g_log = stderr; }
    va_list ap;
    va_start(ap, fmt);
    time_t t = time(NULL);
    struct tm *tm = localtime(&t);
    char ts[20];
    strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", tm);
    fprintf(g_log, "[%s] ", ts);
    vfprintf(g_log, fmt, ap);
    fprintf(g_log, "\n");
    fflush(g_log);
    if (g_klog >= 0) {
        va_list ap2;
        va_copy(ap2, ap); /* ap is consumed by vfprintf above */
        char line[512];
        int n = snprintf(line, sizeof line, "[orbisRPC %s] ", ts);
        if (n > 0 && n < (int)sizeof line)
            n += vsnprintf(line + n, sizeof line - n, fmt, ap2);
        va_end(ap2);
        if (n > 0) { if (n >= (int)sizeof line) n = (int)sizeof line - 1; line[n++] = '\n'; write(g_klog, line, n); }
    }
    va_end(ap);
}

void log_close(void) {
    if (g_log && g_log != stderr) fclose(g_log);
    g_log = NULL;
    if (g_klog >= 0) { close(g_klog); g_klog = -1; }
}
