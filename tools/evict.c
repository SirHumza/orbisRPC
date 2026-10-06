/* evict.c - stop a running orbisRPC daemon so a fresh payload can take over.
 * Reads /data/orbisRPC/daemon.lock, verifies the holder is a live "Payload"
 * process via the same offset-free sysctl walk as lock.c/procwalk.c, then
 * SIGTERM (clean stop: banks session, clears presence, releases lock) with a
 * SIGKILL fallback. Refuses to signal anything it cannot confirm is a live
 * Payload peer.
 * Build: PS4_PAYLOAD_SDK=... ./scripts/build_evict.sh -> build-sdk/evict.elf
 * Run: send via elfldr:9021 or GoldHEN BinLoader:9020, then send the fresh
 * orbisrpc_sdk.elf the same way.
 * Result: /data/orbisRPC/evict.txt (+ klog). Takes up to ~34 s: 24 x 1 s
 * waiting for a clean stop before the SIGKILL fallback. */
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/sysctl.h>
#include "../orbisrpc/procwalk.h"

#define LOCK_PATH "/data/orbisRPC/daemon.lock"
#define RESULT_PATH "/data/orbisRPC/evict.txt"

static void report(const char *msg){
    printf("evict: %s\n", msg);
    int fd = open(RESULT_PATH, O_CREAT|O_TRUNC|O_WRONLY, 0644);
    if(fd >= 0){ (void)write(fd, msg, strlen(msg)); (void)write(fd, "\n", 1); close(fd); }
}

/* 1 = pid is a live "Payload" process, 0 otherwise. Mirrors lock.c.
 *
 * This used to read the name at byte 447 and the pid at byte 72 of each
 * kinfo_proc record. Those are 9.00 offsets; on newer firmware they address
 * the wrong bytes, so every live payload looked dead and evict would delete
 * the lock and report STALE while the daemon kept running -- leaving exactly
 * the two-daemon pileup it exists to prevent.
 *
 * Narrow by exact process name first, then search the record for the pid.
 * Unlike lock.c the bias is the other way here: this gates a kill(), so an
 * unreadable table must report "not live" and let the caller do nothing. */
static int payload_live(int pid){
    if(pid <= 0 || pid == (int)getpid()) return 0;
    int mib[4] = { 1, 14, 8, 0 };
    size_t sz = 0;
    if(sysctl(mib, 4, NULL, &sz, NULL, 0) != 0) return 0;
    static unsigned char buf[256*1024];
    if(sz > sizeof buf) return 0; /* unreadable table: fail closed, do not kill */
    if(sysctl(mib, 4, buf, &sz, NULL, 0) != 0) return 0;
    const unsigned char *rec = NULL;
    size_t recsz = 0;
    int rc = procwalk_find_comm(buf, sz, "Payload", &rec, &recsz);
    if(rc != 0) return 0;          /* malformed, or no Payload process at all */
    return procwalk_record_has_i32(rec, recsz, pid);
}

int main(void){
    char msg[128];
    int fd = open(LOCK_PATH, O_RDONLY);
    if(fd < 0){ report("NO_LOCK nothing running"); return 0; }
    char b[32]; ssize_t n = read(fd, b, sizeof b - 1); close(fd);
    if(n <= 0){ report("LOCK_UNREADABLE"); return 1; }
    b[n] = 0;
    int pid = 0;
    if(sscanf(b, "%d", &pid) != 1 || pid <= 0){
        remove(LOCK_PATH);
        report("LOCK_GARBAGE removed");
        return 0;
    }
    if(!payload_live(pid)){
        remove(LOCK_PATH);
        snprintf(msg, sizeof msg, "STALE holder=%d not a live Payload; lock removed", pid);
        report(msg);
        return 0;
    }
    if(kill(pid, SIGTERM) != 0 && errno == ESRCH){
        remove(LOCK_PATH);
        report("HOLDER_GONE lock removed");
        return 0;
    }
    /* Grace period: clean stop banks session + releases lock. */
    for(int i = 0; i < 24; i++){
        sleep(1);
        if(!payload_live(pid)){
            remove(LOCK_PATH);
            snprintf(msg, sizeof msg, "EVICTED holder=%d clean stop", pid);
            report(msg);
            return 0;
        }
    }
    /* Wedged (e.g. stuck in blocking connect): SIGKILL fallback. */
    (void)kill(pid, SIGKILL);
    for(int i = 0; i < 10; i++){
        sleep(1);
        if(!payload_live(pid)){
            remove(LOCK_PATH);
            snprintf(msg, sizeof msg, "EVICTED holder=%d SIGKILL fallback", pid);
            report(msg);
            return 0;
        }
    }
    snprintf(msg, sizeof msg, "STUCK holder=%d still alive; reboot console", pid);
    report(msg);
    return 2;
}
