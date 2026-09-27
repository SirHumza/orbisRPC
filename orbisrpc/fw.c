/* fw.c - firmware detection + per-FW kinfo_proc layout table. See fw.h.
 * Version source is uname(2) (libc, no libs, no syscalls): PS4 reports the
 * system version in the release field. The payload SDK libc has no uname,
 * so SDK builds report unknown and use the bounded scan (still correct,
 * just never the exact-offset fast path). Only FWs verified live on
 * hardware go in the table; everything else uses the bounded scan. */
#include "fw.h"
#include <string.h>
#include <stdio.h>
#ifndef ORBISRPC_SDK_PAYLOAD
#include <sys/utsname.h>
#endif

void fw_version(char *out, size_t cap){
    if(!out || cap == 0) return;
    /* Default before any parsing so every exit path is defined. */
    snprintf(out, cap, "?.??");
#ifdef ORBISRPC_SDK_PAYLOAD
    /* No uname in the payload SDK libc: unknown FW. Callers fall back
     * to the bounded scan, which needs no version. */
    return;
#else
    struct utsname u;
    if(uname(&u) != 0) return;
    /* Accept "9.00", "9.0", "13.52", or FreeBSD-style "12.0-RELEASE":
     * take leading digits.digits and normalize to MM.mm. */
    int maj = -1, min = -1;
    if(sscanf(u.release, "%d.%d", &maj, &min) != 2) return;
    if(maj < 0 || maj > 99 || min < 0 || min > 99) return;
    snprintf(out, cap, "%d.%02d", maj, min);
#endif
}

/* Verified live on hardware. DO NOT extend from theory: an entry here is
 * a promise that offset 447 holds the process name on that FW. */
static const struct { const char *ver; int name_off; int min_rec; } kKnown[] = {
    { "9.00", 447, 479 },
};

int fw_kinfo_for(const char *ver, int *name_off, int *min_rec){
    if(!ver) return -1;
    for(unsigned i = 0; i < sizeof kKnown / sizeof kKnown[0]; i++){
        if(!strcmp(ver, kKnown[i].ver)){
            if(name_off) *name_off = kKnown[i].name_off;
            if(min_rec) *min_rec = kKnown[i].min_rec;
            return 0;
        }
    }
    return -1;
}

int fw_kinfo(int *name_off, int *min_rec){
    char ver[16];
    fw_version(ver, sizeof ver);
    return fw_kinfo_for(ver, name_off, min_rec);
}

/* Bounded needle search inside one record. recsz caps the search so a
 * corrupt/short record can never over-read. */
static int rec_find(const unsigned char *rec, int recsz,
                    const char *needle, int *at){
    int nlen = (int)strlen(needle);
    if(!rec || recsz <= 0 || nlen <= 0 || nlen > recsz) return 0;
    for(int i = 0; i + nlen <= recsz; i++){
        if(!memcmp(rec + i, needle, (size_t)nlen)){
            if(at) *at = i;
            return 1;
        }
    }
    return 0;
}

int fw_match_eboot(const unsigned char *rec, int recsz){
    static const char want[] = "eboot.bin";
    int off = 0, minrec = 0;
    if(fw_kinfo(&off, &minrec) == 0){
        /* Known FW: exact offset, exact cost. */
        if(recsz >= minrec && off + 10 <= recsz &&
           !memcmp(rec + off, want, 10))
            return 1;
        return 0;
    }
    /* Unknown FW: bounded scan for the name anywhere in the record.
     * Require the trailing NUL so "eboot.binX" can't false-positive. */
    int at = -1;
    if(!rec_find(rec, recsz, want, &at)) return 0;
    if(at + 9 >= recsz || rec[at + 9] != 0) return 0;
    return 1;
}

int fw_match_payload_pid(const unsigned char *rec, int recsz, int pid){
    if(pid <= 0) return 0;
    int off = 0, minrec = 0;
    if(fw_kinfo(&off, &minrec) == 0){
        if(recsz < minrec || off + 8 > recsz) return 0;
        if(*(const int *)(rec + 72) != pid) return 0;
        return !memcmp(rec + off, "Payload", 8) && rec[off + 8] == 0;
    }
    /* Unknown FW: pid field offset is unverified, so only the name part
     * is probed; pid match is skipped rather than guessed. A missed
     * reclaim is a minor stall, a wrong kill would be data loss. */
    int at = -1;
    if(!rec_find(rec, recsz, "Payload", &at)) return 0;
    if(at + 7 >= recsz || rec[at + 7] != 0) return 0;
    (void)pid;
    return 1;
}
