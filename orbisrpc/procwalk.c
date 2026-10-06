/* procwalk.c - see procwalk.h. */
#include "procwalk.h"
#include <string.h>

/* Read the leading int without unaligned-load or aliasing undefined
 * behaviour. Records are 4-byte aligned in practice, but memcpy costs
 * nothing here and stays correct if that ever stops being true. */
static int rd_int(const unsigned char *p){
    int v;
    memcpy(&v, p, sizeof v);
    return v;
}

/* Does this record carry a NUL-terminated `needle` anywhere inside it?
 *
 * Requiring the terminator to sit within the record matters: a bare memmem
 * would happily match a name that runs off the end of the record into the
 * next one. ki_comm is a fixed MAXCOMLEN+1 char array, so a genuine hit is
 * always NUL-terminated inside its own record. */
static int rec_has_comm(const unsigned char *rec, size_t recsz, const char *needle){
    if(!rec || !needle) return 0;
    size_t n = strlen(needle);
    if(n == 0 || recsz < n + 1) return 0;
    for(size_t i = 0; i + n < recsz; i++){
        if(rec[i + n] != '\0') continue;
        if(memcmp(rec + i, needle, n) == 0) return 1;
    }
    return 0;
}

int procwalk_count_comm(const unsigned char *buf, size_t sz, const char *needle){
    if(!buf || !needle || !needle[0]) return -1;
    size_t off = 0;
    int n = 0;
    while(off + sizeof(int) <= sz){
        int recsz = rd_int(buf + off);
        /* A non-positive size means the framing is broken. Report unknown
         * rather than 0: callers read 0 as "no game running", which would
         * blank an active presence instead of leaving it alone. */
        if(recsz <= 0) return -1;
        size_t rsz = (size_t)recsz;
        /* A record claiming more bytes than the buffer holds is corruption we
         * cannot tell apart from a truncated final record. Either way the
         * count would be a guess, and a wrong guess here costs presence, so
         * report unknown. */
        if(off + rsz > sz) return -1;
        if(rec_has_comm(buf + off, rsz, needle)) n++;
        off += rsz;
    }
    return n;
}

int procwalk_has_comm(const unsigned char *buf, size_t sz, const char *needle){
    int n = procwalk_count_comm(buf, sz, needle);
    if(n < 0) return -1;
    return n > 0 ? 1 : 0;
}

size_t procwalk_first_structsize(const unsigned char *buf, size_t sz){
    if(!buf || sz < sizeof(int)) return 0;
    int recsz = rd_int(buf);
    if(recsz <= 0 || (size_t)recsz > sz) return 0;
    return (size_t)recsz;
}

int procwalk_find_comm(const unsigned char *buf, size_t sz, const char *needle,
                       const unsigned char **rec, size_t *recsz){
    if(!rec || !recsz) return -1;
    *rec = NULL;
    *recsz = 0;
    if(!buf || !needle || !needle[0]) return -1;
    size_t off = 0;
    while(off + sizeof(int) <= sz){
        int rsz_i = rd_int(buf + off);
        if(rsz_i <= 0) return -1;
        size_t rsz = (size_t)rsz_i;
        if(off + rsz > sz) return -1;
        if(rec_has_comm(buf + off, rsz, needle)){
            *rec = buf + off;
            *recsz = rsz;
            return 0;
        }
        off += rsz;
    }
    return -2;
}

int procwalk_record_has_i32(const unsigned char *rec, size_t recsz, int want){
    if(!rec || recsz < sizeof(int32_t)) return 0;
    int32_t v = (int32_t)want;
    for(size_t i = 0; i + sizeof v <= recsz; i++){
        int32_t got;
        memcpy(&got, rec + i, sizeof got);
        if(got == v) return 1;
    }
    return 0;
}