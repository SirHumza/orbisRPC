/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 SirHumza and CyberMask367 - see NOTICE for attribution terms.
 */
/* lock.c - pid lockfile with stale detection. No flock on PS4 libc,
 * so use atomic create (O_CREAT|O_EXCL) + process-table liveness
 * (sysctl, no signals needed in spawned context). */
#include "lock.h"
#include "log.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/time.h>
#ifdef ORBISRPC_SDK_PAYLOAD
#include <sys/types.h>
#include <sys/sysctl.h>
#include "procwalk.h"
/* Is `pid` still a live spawned payload?
 *
 * Field offsets inside a kinfo_proc record are not self-describing and shift
 * between firmwares. The previous name-at-+447 / pid-at-+72 lookup therefore
 * silently failed on anything newer than 9.00: it reported live peers as dead,
 * so a second daemon would reclaim the lock while the first was still running
 * and both would fight over the gateway.
 *
 * Narrow to the record whose name is exactly "Payload" (unambiguous), then
 * look for the pid anywhere inside it. Spelled-out bias: a false positive only
 * costs an unnecessary stand-down, a false negative costs two daemons, so
 * err toward matching. */
static int pid_live(int pid){
    if(pid <= 0 || pid == (int)getpid()) return 0;
    int mib[4] = { 1, 14, 8, 0 };
    size_t sz = 0;
    if(sysctl(mib, 4, NULL, &sz, NULL, 0) != 0) return 0;
    static unsigned char buf[256*1024];
    if(sz > sizeof buf) return 1; /* huge table: assume live, fail closed */
    if(sysctl(mib, 4, buf, &sz, NULL, 0) != 0) return 0;
    const unsigned char *rec = NULL;
    size_t recsz = 0;
    /* rc -1 = unreadable table: fail closed, assume a peer holds the lock */
    int rc = procwalk_find_comm(buf, sz, "Payload", &rec, &recsz);
    if(rc == -1) return 1;
    if(rc != 0) return 0;         /* no Payload process at all */
    return procwalk_record_has_i32(rec, recsz, pid);
}
#else
#include <signal.h>
static int pid_live(int pid){
    if(pid == (int)getpid()) return 0; /* own stale lock: never "live" */
    if(kill(pid, 0) == 0) return 1;
    if(errno == EPERM) return 1;
    return 0;
}
#endif

static int s_held = 0;
/* Unique per instance, not per process: Payload Guest can run two loads
 * with the same pid, and both must still be distinguishable. */
static uint64_t s_token = 0;
static int64_t s_stale_s = LOCK_STALE_S;

void lock_set_stale(int seconds){
    if(seconds < 60) seconds = 60;
    s_stale_s = seconds;
}

static uint64_t make_token(void){
    static unsigned seq;                 /* shared by same-process reloads */
    seq++;
    uint64_t x = (uint64_t)time(NULL);
    x ^= ((uint64_t)(uintptr_t)&seq) << 32;
    x ^= (uint64_t)(uintptr_t)&x;
    x ^= ((uint64_t)getpid()) << 16;
    x ^= (uint64_t)seq * 0x9e3779b97f4a7c15ULL;
    return x ? x : 1;
}

static void write_lock(int fd){
    char b[48];
    int n = snprintf(b, sizeof b, "%d %016llx\n",
                     (int)getpid(), (unsigned long long)s_token);
    if(n > 0) (void)write(fd, b, (size_t)n);
    close(fd);
}

/* Wall-clock epoch of this boot. A lock older than it is from a previous
 * boot and must not stand down this one (D-04). Returns 0 when unknowable,
 * in which case lock_entry_stale() falls back to pid liveness only. */
static int64_t boot_wall(void){
#ifdef ORBISRPC_SDK_PAYLOAD
    int mib[2] = { 1, 21 }; /* CTL_KERN, KERN_BOOTTIME */
    struct timeval tv;
    size_t sz = sizeof tv;
    if(sysctl(mib, 2, &tv, &sz, NULL, 0) == 0 && tv.tv_sec > 0)
        return (int64_t)tv.tv_sec;
    return 0;
#else
    struct timespec ts;
    if(clock_gettime(CLOCK_MONOTONIC, &ts) != 0) return 0;
    return (int64_t)time(NULL) - (int64_t)ts.tv_sec;
#endif
}

/* Is the lock's current holder genuinely alive? A live pid that has stopped
 * touching the lock (LOCK_HEARTBEAT_S cadence, LOCK_STALE_S threshold) is a
 * wedged single-threaded payload, not a working daemon, so it must not block
 * recovery. Returns 1 when the lock may be reclaimed. */
static int holder_stale(void){
    FILE *f = fopen(LOCK_PATH, "rb");
    if(!f) return 1;                 /* vanished: nothing to preserve */
    int pid = 0;
    unsigned long long tok = 0;
    int parsed = fscanf(f, "%d %llx", &pid, &tok);
    fclose(f);
    if(parsed < 1) pid = 0;
    struct stat st;
    int64_t mt = (stat(LOCK_PATH, &st) == 0) ? (int64_t)st.st_mtime : 0;
    int64_t now = (int64_t)time(NULL);
    int live = pid_live(pid);
    int has_token = (parsed == 2 && tok != 0);
    if(!lock_entry_reclaim(mt, boot_wall(), now, s_stale_s, has_token,
                           (uint64_t)tok, s_token, live)){
        log_msg("lock: held by pid %d token %08llx (age %llds); standing down",
                pid, tok, mt > 0 ? (long long)(now - mt) : -1LL);
        return 0;
    }
    if(mt > 0 && lock_heartbeat_stale(mt, now, s_stale_s))
        log_msg("lock: holder pid %d token %08llx silent for %llds; reclaiming",
                pid, tok, (long long)(now - mt));
    return 1;
}

int lock_acquire(void){
    if(s_held) return 1;             /* already guarding this instance */
    if(s_token == 0) s_token = make_token();
    int fd = open(LOCK_PATH, O_CREAT|O_EXCL|O_WRONLY, 0644);
    if(fd >= 0){
        write_lock(fd);
        s_held = 1;
        log_msg("lock: acquired (pid %d token %08llx)",
                (int)getpid(), (unsigned long long)s_token);
        return 0;
    }
    if(!holder_stale()) return 1;    /* live, progressing peer: stand down */
    /* Stale (previous boot, dead pid, unreadable pid, or no heartbeat):
     * reclaim. */
    remove(LOCK_PATH);
    fd = open(LOCK_PATH, O_CREAT|O_EXCL|O_WRONLY, 0644);
    if(fd < 0){
        /* Lost the race: re-check once — a live peer may hold it now. */
        if(!holder_stale()) return 1;
        return -1;
    }
    write_lock(fd);
    s_held = 1;
    log_msg("lock: reclaimed and acquired (pid %d token %08llx)",
            (int)getpid(), (unsigned long long)s_token);
    return 0;
}

void lock_refresh(void){
    if(!s_held) return;
    /* utimes(NULL) stamps both times with the current clock; the mtime is
     * what lock_heartbeat_stale() reads. Failure is non-fatal: the mtime
     * simply stays old, and a holder that cannot touch its own lock for
     * LOCK_STALE_S is not making progress either way. */
    (void)utimes(LOCK_PATH, NULL);
}

int lock_owned(void){
    if(!s_held) return 1;            /* running unguarded: nothing to lose */
    FILE *f = fopen(LOCK_PATH, "rb");
    if(!f) return 1;                 /* unreadable is not proof of takeover */
    int pid = 0;
    unsigned long long tok = 0;
    int parsed = fscanf(f, "%d %llx", &pid, &tok);
    fclose(f);
    return lock_token_owned(parsed, pid, (uint64_t)tok,
                            (int)getpid(), s_token) ? 1 : 0;
}

void lock_release(void){
    if(!s_held) return;
    /* Only remove our own lock: if it was reclaimed (stale detection raced a
     * slow boot), deleting it would release the successor's guard. Same-pid
     * reloads make the token, not the pid, the identity check. */
    FILE *f = fopen(LOCK_PATH, "rb");
    if(f){
        int pid = 0;
        unsigned long long tok = 0;
        int parsed = fscanf(f, "%d %llx", &pid, &tok);
        fclose(f);
        if(lock_token_owned(parsed, pid, (uint64_t)tok,
                            (int)getpid(), s_token))
            remove(LOCK_PATH);
    }
    s_held = 0;
}
