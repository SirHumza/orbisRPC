/* fw.c - see fw.h. */
#include "fw.h"
#include "log.h"
#include <string.h>
#include <stdio.h>
#include <stdint.h>

/* Firmware layout as reported by sceKernelGetSystemSwVersion(). The string is
 * authoritative; the packed value is only a fallback for builds where the
 * string comes back empty. */
typedef struct fw_info {
    uint64_t reserved;
    char version_string[0x1c];
    uint32_t version;
} fw_info_t;

int sceKernelGetSystemSwVersion(fw_info_t *);

/* Sony returns a three-part version, e.g. 13.520.001. Only the first two
 * parts are wanted, so trailing components are accepted and trimmed
 * rather than treated as garbage -- a strict MAJOR.MINOR check reported
 * unknown on a perfectly readable 13.52 console. */
static int digits_ok(const char *s){
    if(!s || !s[0]) return 0;
    int dots = 0, n = 0;
    for(const char *p = s; *p; p++){
        if(*p == '.'){ dots++; continue; }
        if(*p < '0' || *p > '9') return 0;
        n++;
    }
    return dots >= 1 && n >= 2;
}

/* Normalise Sony's packed version to "MAJOR.MINOR".
 *
 * The minor field is three digits with a trailing sub-patch zero, so
 * "13.520.001" means 13.52, not 13.520:
 *
 *   13.520.001 -> 13.52      9.000 -> 9.00      8.500 -> 8.50
 *
 * Cutting only at the second dot (an earlier revision) left the zero attached
 * and rendered "Firmware 13.520" on the presence card.
 */
void fw_normalize(char *v){
    if(!v) return;
    /* Sony pads the string with a leading space on some firmware -- 9.00
     * returns " 9.008.031" (verified on console 2026-10-05) while 13.52
     * returns "13.520" clean. Skipped here rather than in digits_ok() so
     * the caller hands out a trimmed string either way: without this the
     * space survived the dot logic and the presence read "Firmware  9.00".
     *
     * The bytes are shifted down, not just walked past: this edits the
     * caller's buffer in place, so a bare pointer bump would leave the
     * space sitting at v[0] for them to print. */
    char *start = v;
    while(*start == ' ' || *start == '\t') start++;
    if(start != v) memmove(v, start, strlen(start) + 1);
    /* trailing padding would survive the dot cut the same way */
    size_t n = strlen(v);
    while(n && (v[n-1] == ' ' || v[n-1] == '\t')) v[--n] = 0;

    char *first = strchr(v, '.');
    if(!first) return;
    char *second = strchr(first + 1, '.');
    if(second) *second = 0;
    /* minor is at most 2 significant digits; drop a third if present */
    if(first[1] && first[2] && first[3]) first[3] = 0;
}

int fw_version(char *out, size_t cap){
    if(!out || cap == 0) return -1;
    static const char unknown[] = "unknown";
    snprintf(out, cap, "%s", unknown);

    fw_info_t info;
    memset(&info, 0, sizeof info);
    if(sceKernelGetSystemSwVersion(&info) != 0){
        log_msg("fw: system sw version call failed");
        return -1;
    }
    info.version_string[sizeof(info.version_string) - 1] = 0;
    /* Normalise BEFORE validating: fw_normalize() strips the padding some
     * firmware adds, and digits_ok() rejects any byte that is not a digit or
     * a dot. Validating first would report a padded-but-perfectly-readable
     * " 9.008.031" as unrecognised. */
    char s[sizeof(info.version_string)];
    snprintf(s, sizeof(s), "%s", info.version_string);
    fw_normalize(s);
    if(digits_ok(s)){
        snprintf(out, cap, "%s", s);
        return 0;
    }
    /* Fall back to the packed BCD-ish value: major<<40 | minor<<32.
     * Shift on a 64-bit copy: shifting a uint32_t by 40 would be undefined. */
    uint64_t v = (uint64_t)info.version;
    uint8_t major = (uint8_t)(v >> 40);
    uint8_t minor = (uint8_t)(v >> 32);
    if((major & 0x0f) <= 9 && (major >> 4) <= 9 &&
       (minor & 0x0f) <= 9 && (minor >> 4) <= 9 && major){
        snprintf(out, cap, "%x.%02x", major, minor);
        return 0;
    }
    /* Bracketed so a stray leading/trailing byte is visible instead of
     * blending into the quotes as padding. */
    log_msg("fw: unrecognised version string [%s]", info.version_string);
    return -1;
}