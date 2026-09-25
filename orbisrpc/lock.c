/* lock.c - pid lockfile with stale detection. No flock on PS4 libc,
 * so use atomic create (O_CREAT|O_EXCL) + process-table liveness
 * (sysctl, no signals needed in spawned context). */
#include "lock.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#ifdef ORBISRPC_SDK_PAYLOAD
#include <sys/types.h>
#include <sys/sysctl.h>
static int pid_live(int pid){
    if(pid <= 0 || pid == (int)getpid()) return 0;
    int mib[4] = { 1, 14, 8, 0 };
    size_t sz = 0;
    if(sysctl(mib, 4, NULL, &sz, NULL, 0) != 0) return 0;
    static unsigned char buf[256*1024];
    if(sz > sizeof buf) return 1; /* huge table: assume live, fail closed */
    if(sysctl(mib, 4, buf, &sz, NULL, 0) != 0) return 0;
    size_t off = 0;
    while(off + 4 <= sz){
        int recsz = *(int *)(buf + off);
        if(recsz <= 0 || off + (size_t)recsz > sz) break;
        /* A recycled PID owned by a system daemon must not block us:
         * only a live payload process counts as a peer. Spawned
         * payloads (elfldr/GoldHEN) show up as "Payload". */
        if(recsz >= 479 && *(int *)(buf + off + 72) == pid)
            return !memcmp(buf + off + 447, "Payload", 8) &&
                   buf[off + 455] == 0;
        off += (size_t)recsz;
    }
    return 0;
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

int lock_acquire(void){
    int fd = open(LOCK_PATH, O_CREAT|O_EXCL|O_WRONLY, 0644);
    if(fd >= 0){
        char b[32];
        int n = snprintf(b, sizeof b, "%d\n", (int)getpid());
        if(n > 0) (void)write(fd, b, (size_t)n);
        close(fd);
        s_held = 1;
        return 0;
    }
    /* Exists: check liveness of the holder. */
    FILE *f = fopen(LOCK_PATH, "rb");
    if(!f) return -1;
    int pid = 0;
    if(fscanf(f, "%d", &pid) != 1) pid = 0;
    fclose(f);
    if(pid_live(pid)) return 1; /* live peer: stand down */
    /* Stale (holder dead or unreadable pid): reclaim. */
    remove(LOCK_PATH);
    fd = open(LOCK_PATH, O_CREAT|O_EXCL|O_WRONLY, 0644);
    if(fd < 0){
        /* Lost the race: re-check once — a live peer may hold it now. */
        f = fopen(LOCK_PATH, "rb");
        if(f){
            pid = 0;
            if(fscanf(f, "%d", &pid) != 1) pid = 0;
            fclose(f);
            if(pid_live(pid)) return 1;
        }
        return -1;
    }
    {
        char b[32];
        int n = snprintf(b, sizeof b, "%d\n", (int)getpid());
        if(n > 0) (void)write(fd, b, (size_t)n);
        close(fd);
    }
    s_held = 1;
    return 0;
}

void lock_release(void){
    if(s_held){ remove(LOCK_PATH); s_held = 0; }
}
