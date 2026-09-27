/* focus.c - kern.msgbuf AppFocusChanged focus tracking.
 * Probe-first: tries each candidate msgbuf path, remembers the one that
 * works (sticky), parses only the LAST event (current focus), classifies by
 * title-id prefix. Any failure (no device, no events, short read) returns
 * FOCUS_UNKNOWN so callers keep previous state — never a transition. */
#include "focus.h"
#include "log.h"
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

static const char kEv[] = "AppFocusChanged [";

static const void *mem_find(const void *h, size_t hl,
                            const void *n, size_t nl){
    if(!h || !n || nl == 0 || hl < nl) return NULL;
    const unsigned char *p = h;
    for(size_t i = 0; i + nl <= hl; i++){
        if(!memcmp(p + i, n, nl)) return p + i;
    }
    return NULL;
}

int focus_parse_appfocus(const char *buf, size_t len,
                         char *out_tid, size_t cap){
    if(!buf || !out_tid || cap == 0) return -1;
    out_tid[0] = 0;
    /* Last event wins: earlier entries are stale focus history. */
    const char *last = NULL;
    const char *p = buf;
    size_t rem = len;
    while(rem >= sizeof kEv - 1){
        const char *f = mem_find(p, rem, kEv, sizeof kEv - 1);
        if(!f) break;
        last = f;
        size_t adv = (size_t)(f - p) + 1;
        p = f + 1;
        rem -= adv;
    }
    if(!last) return -1;
    /* Collect bracketed tokens after the marker; the focus target is the
     * last one ("AppFocusChanged [FROM] -> [TO]"). The first token's
     * opening bracket is already consumed by the marker itself. */
    const char *q = last + sizeof kEv - 1;
    const char *end = buf + len;
    char best[16] = "";
    const char *qs = q;
    while(q < end && *q != ']' && *q != '[' &&
          (size_t)(q - qs) < sizeof best - 1) q++;
    if(q < end && *q == ']' && q > qs){
        size_t n = (size_t)(q - qs);
        memcpy(best, qs, n);
        best[n] = 0;
        q++;
    }
    while(q < end){
        if(*q != '['){ q++; continue; }
        q++;
        qs = q;
        while(q < end && *q != ']' && (size_t)(q - qs) < sizeof best - 1) q++;
        if(q < end && *q == ']' && q > qs){
            size_t n = (size_t)(q - qs);
            if(n < sizeof best){ memcpy(best, qs, n); best[n] = 0; }
        }
        if(q < end) q++;
    }
    if(!best[0]) return -1;
    strncpy(out_tid, best, cap - 1);
    out_tid[cap - 1] = 0;
    return 0;
}

int focus_classify(const char *tid){
    if(!tid || !tid[0]) return FOCUS_UNKNOWN;
    if(!strncmp(tid, "NPXS", 4)) return FOCUS_SYSTEM;
    if(!strncmp(tid, "CUSA", 4) || !strncmp(tid, "PPSA", 4) ||
       !strncmp(tid, "PCSE", 4) || !strncmp(tid, "PCSB", 4) ||
       !strncmp(tid, "PCSG", 4) || !strncmp(tid, "EPSA", 4))
        return FOCUS_GAME;
    return FOCUS_UNKNOWN;
}

/* Candidate kernel message-buffer paths, in probe order. The exact device
 * name varies, so we try and remember — never assume. */
static const char *kPaths[] = {
    "/dev/kern.msgbuf",
    "/dev/msgbuf",
    "kern.msgbuf",
    NULL,
};

int detect_system_screen(char *out_tid, size_t cap){
    if(out_tid && cap) out_tid[0] = 0;
    static int known = -1;      /* sticky index into kPaths */
    static int logged_no_dev = 0;
    static unsigned char buf[256 * 1024];

    for(int pass = 0; pass < 2 && known != -2; pass++){
        int start = (known >= 0) ? known : 0;
        for(int i = start; kPaths[i]; i++){
            if(known >= 0 && i != known) continue;
            int fd = open(kPaths[i], O_RDONLY);
            if(fd < 0){
                if(known == i) known = -1; /* device vanished, re-probe */
                continue;
            }
            ssize_t n = read(fd, buf, sizeof buf - 1);
            close(fd);
            if(n <= 0){
                if(known == i) known = -1;
                continue;
            }
            known = i;
            buf[n] = 0;
            char tid[16] = "";
            if(focus_parse_appfocus((const char *)buf, (size_t)n,
                                    tid, sizeof tid) != 0)
                return FOCUS_UNKNOWN; /* buffer readable, no events yet */
            if(out_tid && cap){
                strncpy(out_tid, tid, cap - 1);
                out_tid[cap - 1] = 0;
            }
            return focus_classify(tid);
        }
        if(known >= 0) break;
        known = -1;
        if(pass == 0) continue;
    }
    if(!logged_no_dev){
        logged_no_dev = 1;
        log_msg("msgbuf unreadable from payload; focus via scu/sysctl");
    }
    return FOCUS_UNKNOWN;
}

int focus_msgbuf_ok(void){
    for(int i = 0; kPaths[i]; i++){
        int fd = open(kPaths[i], O_RDONLY);
        if(fd >= 0){ close(fd); return 1; }
    }
    return 0;
}
