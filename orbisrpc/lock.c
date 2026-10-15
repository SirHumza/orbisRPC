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
#include "procwalk.h"
/* Is `pid` still a live spawned payload?
 *
 * Field offsets inside a kinfo_proc record are not self-describing and shift
 * between firmwares, so a name-plus-offset lookup silently fails on newer
 * firmware: it reports live peers as dead, so a second daemon reclaims the
 * lock while the first is still running and both fight over the gateway
 * (observed on 13.52 as doubled + torn log lines and dueling sessions).
 *
 * Narrow to the record whose name is exactly "Payload" (unambiguous), then
 * look for the pid anywhere inside it. If no record carries that name the
 * daemon may run under another comm, so fall back to scanning every
 * well-formed record for the pid instead of declaring it dead.
 * Spelled-out bias: a false positive only costs an unnecessary stand-down,
 * a false negative costs two daemons, so err toward matching. */
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
    if(rc == 0) return procwalk_record_has_i32(rec, recsz, pid);
    /* No "Payload" record at all (-2): the holder may live under another
     * comm. A table-wide pid hit still reads as live; -1 (malformed)
     * also reads as live. Only a clean walk with no hit reads as dead. */
    return procwalk_any_record_has_i32(buf, sz, pid) != 0;
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
