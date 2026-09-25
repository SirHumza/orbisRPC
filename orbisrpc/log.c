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
static volatile int g_log_lock = 0;
static volatile int g_debug = 0;
void log_set_debug(int on){ g_debug = on ? 1 : 0; }
int log_is_debug(void){ return g_debug; }
static void log_lock(void){ while(__sync_lock_test_and_set(&g_log_lock, 1)) usleep(1000); }
static void log_unlock(void){ __sync_lock_release(&g_log_lock); }

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
    if(path) {
        g_log = fopen(path, "ab");
        if(g_log){
            /* cap check AFTER open to avoid stat->remove TOCTOU */
            struct stat st;
            if(fstat(fileno(g_log), &st)==0 && st.st_size > LOG_CAP){
                fclose(g_log); g_log = NULL;
                remove(path);
                g_log = fopen(path, "ab");
            }
        }
    }
    if (!g_log) {
        for (unsigned i = 0; i < sizeof(LOG_FALLBACKS)/sizeof(LOG_FALLBACKS[0]); i++) {
            const char *p = LOG_FALLBACKS[i];
            if (!p || !path || strcmp(p, path) == 0) continue;
            g_log = fopen(p, "ab");
            if (g_log) break;
        }
    }
    if (g_log) setvbuf(g_log, NULL, _IONBF, 0);
    /* klog mirror: survives even when every mount is invisible */
    g_klog = open("/dev/klog", O_WRONLY);
    if(g_klog < 0) g_klog = -1;
}

void log_msg(const char *fmt, ...) {
    if (!fmt) return;
    log_lock();
    if (!g_log) { g_log = stderr; }
    va_list ap, ap2;
    va_start(ap, fmt);
    va_copy(ap2, ap); /* copy BEFORE vfprintf consumes ap */
    time_t t = time(NULL);
    struct tm tmv; struct tm *tm = NULL;
    /* gmtime_r is our thread-safe shim; UTC is fine for logs */
    extern struct tm *gmtime_r(const time_t *, struct tm *);
    tm = gmtime_r(&t, &tmv);
    char ts[20];
    if(tm) strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", tm);
    else { strncpy(ts, "1970-01-01 00:00:00", sizeof ts); ts[sizeof ts-1]=0; }
    fprintf(g_log, "[%s] ", ts);
    vfprintf(g_log, fmt, ap);
    fprintf(g_log, "\n");
    fflush(g_log);
    va_end(ap);
    if (g_klog >= 0) {
        char line[512];
        int n = snprintf(line, sizeof line, "[orbisRPC %s] ", ts);
        if (n > 0 && n < (int)sizeof line)
            n += vsnprintf(line + n, sizeof line - n, fmt, ap2);
        if (n > 0) { if (n >= (int)sizeof line) n = (int)sizeof line - 1; line[n++] = '\n'; (void)write(g_klog, line, (size_t)n); }
    }
    va_end(ap2);
    log_unlock();
}

void log_close(void) {
    if (g_log && g_log != stderr) fclose(g_log);
    g_log = NULL;
    if (g_klog >= 0) { close(g_klog); g_klog = -1; }
}

void log_dbg(const char *fmt, ...) {
    if (!g_debug || !fmt) return;
    log_lock();
    if (!g_log) { g_log = stderr; }
    va_list ap;
    va_start(ap, fmt);
    time_t t = time(NULL);
    struct tm tmv; struct tm *tm = NULL;
    extern struct tm *gmtime_r(const time_t *, struct tm *);
    tm = gmtime_r(&t, &tmv);
    char ts[20];
    if(tm) strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", tm);
    else { strncpy(ts, "1970-01-01 00:00:00", sizeof ts); ts[sizeof ts-1]=0; }
    fprintf(g_log, "[%s] DBG ", ts);
    vfprintf(g_log, fmt, ap);
    fprintf(g_log, "\n");
    fflush(g_log);
    va_end(ap);
    log_unlock();
}
