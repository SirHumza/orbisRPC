/* updater_util.c - host-testable updater logic: version compare, ELF check. */
#include "updater.h"
#include <string.h>
#include <stdlib.h>
#include <stddef.h>

int updater_cmp(const char *a, const char *b){
    unsigned av[4] = {0,0,0,0}, bv[4] = {0,0,0,0};
    if(a && (*a=='v'||*a=='V')) a++;
    if(b && (*b=='v'||*b=='V')) b++;
    for(int i = 0; i < 4; i++){
        char *ea = NULL, *eb = NULL;
        if(a && *a){ av[i] = (unsigned)strtoul(a, &ea, 10); a = ea; if(*a=='.') a++; }
        if(b && *b){ bv[i] = (unsigned)strtoul(b, &eb, 10); b = eb; if(*b=='.') b++; }
    }
    for(int i = 0; i < 4; i++){
        if(av[i] != bv[i]) return av[i] > bv[i] ? 1 : -1;
    }
    return 0;
}

int updater_elf_ok(const unsigned char *buf, size_t n){
    if(!buf || n < 64) return 0;
    if(buf[0] != 0x7f || buf[1] != 'E' || buf[2] != 'L' || buf[3] != 'F') return 0;
    if(buf[4] != 2) return 0;              /* 64-bit */
    if(buf[5] != 1) return 0;              /* little-endian */
    if(buf[18] != 62 || buf[19] != 0) return 0; /* x86-64 */
    if(n > 8u*1024u*1024u) return 0;
    return 1;
}
