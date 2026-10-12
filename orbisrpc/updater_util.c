/* updater_util.c - host-testable updater logic: version compare.
 *
 * updater_cmp is what the GitHub update check actually uses. The ELF
 * validators that used to live here went when the installed-payload sanity
 * check was dropped: with no staged install and no .bin on disk under the
 * BinLoader workflow, nothing ever called them. */
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
