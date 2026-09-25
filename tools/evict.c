/* evict.c - stop a running orbisRPC daemon so a fresh payload can take over.
 * Reads /data/orbisRPC/daemon.lock, verifies the holder is a live "Payload"
 * process via the same sysctl walk as lock.c, then SIGTERM (clean stop:
 * banks session, clears presence, releases lock) with a SIGKILL fallback.
 * Refuses to signal anything that is not a live Payload peer.
 * Build: ./scripts/build_evict.sh  ->  build-sdk/evict.elf
 * Run: send via elfldr:9021, then send the fresh orbisrpc_sdk.elf.
 * Result: /data/orbisRPC/evict.txt + klog printf. */
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/sysctl.h>

#define LOCK_PATH "/data/orbisRPC/daemon.lock"
#define RESULT_PATH "/data/orbisRPC/evict.txt"

static void report(const char *msg){
    printf("evict: %s\n", msg);
    int fd = open(RESULT_PATH, O_CREAT|O_TRUNC|O_WRONLY, 0644);
    if(fd >= 0){ (void)write(fd, msg, strlen(msg)); (void)write(fd, "\n", 1); close(fd); }
}

/* 1 = pid is a live "Payload" process, 0 otherwise. Mirrors lock.c. */
static int payload_live(int pid){
    if(pid <= 0 || pid == (int)getpid()) return 0;
    int mib[4] = { 1, 14, 8, 0 };
    size_t sz = 0;
    if(sysctl(mib, 4, NULL, &sz, NULL, 0) != 0) return 0;
    static unsigned char buf[256*1024];
    if(sz > sizeof buf) return 0; /* unreadable table: fail closed */
    if(sysctl(mib, 4, buf, &sz, NULL, 0) != 0) return 0;
    size_t off = 0;
    while(off + 4 <= sz){
        int recsz = *(int *)(buf + off);
        if(recsz <= 0 || off + (size_t)recsz > sz) break;
        if(recsz >= 479 && *(int *)(buf + off + 72) == pid)
            /* Exact "Payload" + NUL: a prefix match would bless
             * PayloadHelper-style names for the kill list. */
            return !memcmp(buf + off + 447, "Payload", 8) &&
                   buf[off + 455] == 0;
        off += (size_t)recsz;
    }
    return 0;
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
