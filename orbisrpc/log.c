/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 SirHumza and CyberMask367 - see NOTICE for attribution terms.
 */
/* log.c - file logger: rotating (512 KB cap), timestamped, falls back to
 * stderr. Tries several paths because mount visibility differs between app
 * and payload processes. Every line is mirrored to /dev/klog when
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
static char g_log_path[256];      /* file backing g_log; "" for stderr */
static size_t g_cap = LOG_CAP;
static int g_klog = -1;
static volatile int g_log_lock = 0;
static volatile int g_debug = 0;
void log_set_debug(int on){ g_debug = on ? 1 : 0; }
int log_is_debug(void){ return g_debug; }
/* Runtime rotation cap (config log_max_kb); host tests shrink it to
 * exercise rotation without writing 512 KB. */
void log_set_cap(size_t cap){ g_cap = cap ? cap : LOG_CAP; }
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

/* mkdir the directory part of `path`, ignoring EEXIST.
 *
 * Without this, fopen("/data/orbisRPC/log.txt") fails on a console where
 * nothing has created /data/orbisRPC yet -- which is exactly a fresh install
 * with no PKG installer. main() writes its first line before daemon_run() gets
 * around to creating that directory, so the payload could run perfectly and
 * write nothing anywhere: silent, and indistinguishable from not starting.
 *
 * mkdir() is not recursive, so every missing component is created in turn. */
static void ensure_parent_dir(const char *path){
    char dir[256];
    size_t n = path ? strlen(path) : 0;
    if(n == 0 || n >= sizeof dir) return;
    memcpy(dir, path, n + 1);
    char *slash = strrchr(dir, '/');
    if(!slash || slash == dir) return;   /* no directory component, or root */
    *slash = 0;
    for(char *p = dir + 1; *p; p++){
        if(*p != '/') continue;
        *p = 0;
        (void)mkdir(dir, 0777);   /* EEXIST is success, ignore */
        *p = '/';
    }
    (void)mkdir(dir, 0777);
}

/* Try `want` first, then the fallback list. Returns the open stream or NULL.
 * Does not touch g_log itself so callers can retry later. */
static FILE *open_log(const char *want){
    const char *cand[sizeof LOG_FALLBACKS/sizeof LOG_FALLBACKS[0] + 1];
    size_t n = 0;
    if(want && want[0]) cand[n++] = want;
    for(unsigned i = 0; i < sizeof LOG_FALLBACKS/sizeof LOG_FALLBACKS[0]; i++){
        const char *p = LOG_FALLBACKS[i];
        if(p && (!want || strcmp(p, want) != 0)) cand[n++] = p;
    }
    for(size_t i = 0; i < n; i++){
        ensure_parent_dir(cand[i]);
        FILE *f = fopen(cand[i], "ab");
        if(!f) continue;
        /* cap check AFTER open to avoid stat->remove TOCTOU */
        struct stat st;
        if(fstat(fileno(f), &st) == 0 && (size_t)st.st_size > g_cap){
            fclose(f);
            remove(cand[i]);
            f = fopen(cand[i], "ab");
            if(!f) continue;
        }
        setvbuf(f, NULL, _IONBF, 0);
        snprintf(g_log_path, sizeof g_log_path, "%s", cand[i]);
        return f;
    }
    return NULL;
}

/* Runtime cap. open_log() only checks at startup, so a daemon that runs for
 * weeks grew log.txt without bound. Roll the current file to <path>.1 (one
 * previous generation kept) and continue in a fresh file. Called with the
 * log lock held. If the roll fails, keep writing: a full disk must not turn
 * logging into a crash loop. */
static void log_rotate_if_needed(void){
    if(!g_log || !g_log_path[0]) return;
    struct stat st;
    if(fstat(fileno(g_log), &st) != 0) return;
    if(st.st_size >= 0 && (size_t)st.st_size <= g_cap) return;
    char old[300];
    snprintf(old, sizeof old, "%s.1", g_log_path);
    (void)remove(old);
    if(rename(g_log_path, old) != 0) return;   /* nothing moved: keep stream */
    FILE *nf = fopen(g_log_path, "ab");
    if(!nf){
        /* The stream now backs the renamed .1 inode, but the live path is
         * gone. Without putting it back, every later rotation hits ENOENT on
         * rename() and returns above, so the file grows without bound again —
         * exactly what this function exists to prevent. Restore the name; if
         * even that fails there is nothing safe left to do from here (the log
         * lock is held, so no log_msg). */
        (void)rename(old, g_log_path);
        return;
    }
    setvbuf(nf, NULL, _IONBF, 0);
    FILE *of = g_log;
    g_log = nf;
    fclose(of);
}

/* Lazy re-open cadence: mounts can appear after startup and the data directory
 * is only created later in the run, so keep retrying instead of giving up. */
static unsigned s_retry = 0;

void log_init(const char *path) {
    if(g_log) return;
    g_log = open_log(path);
    /* klog mirror: survives even when every mount is invisible */
    g_klog = open("/dev/klog", O_WRONLY);
    if(g_klog < 0) g_klog = -1;
}

/* Resolve an output stream. Deliberately does NOT latch stderr into g_log:
 * doing so pinned logging to an unreachable stream permanently, even after the
 * data directory became writable. */
static FILE *log_out(void){
    if(!g_log && (s_retry++ & 0x1fu) == 0) g_log = open_log(NULL);
    return g_log ? g_log : stderr;
}

void log_msg(const char *fmt, ...) {
    if (!fmt) return;
    log_lock();
    log_rotate_if_needed();
    FILE *out = log_out();
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
    fprintf(out, "[%s] ", ts);
    vfprintf(out, fmt, ap);
    fprintf(out, "\n");
    fflush(out);
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
    g_log_path[0] = 0;
    if (g_klog >= 0) { close(g_klog); g_klog = -1; }
}

const char *log_path(void){
    return g_log_path[0] ? g_log_path : "stderr";
}

void log_dbg(const char *fmt, ...) {
    if (!g_debug || !fmt) return;
    log_lock();
    log_rotate_if_needed();
    FILE *out = log_out();
    va_list ap;
    va_start(ap, fmt);
    time_t t = time(NULL);
    struct tm tmv; struct tm *tm = NULL;
    extern struct tm *gmtime_r(const time_t *, struct tm *);
    tm = gmtime_r(&t, &tmv);
    char ts[20];
    if(tm) strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", tm);
    else { strncpy(ts, "1970-01-01 00:00:00", sizeof ts); ts[sizeof ts-1]=0; }
    fprintf(out, "[%s] DBG ", ts);
    vfprintf(out, fmt, ap);
    fprintf(out, "\n");
    fflush(out);
    va_end(ap);
    log_unlock();
}