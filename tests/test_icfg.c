/* test_icfg.c - installer config tests against installer/jsonlite.
 *
 * icfg.c includes installer/jsonlite.h (quoted include resolves to its own
 * directory), whose jl_val layout and enum values differ from the payload's
 * orbis/jsonlite. Linking icfg.o against orbis jsonlite.o therefore
 * misreads every value (e.g. JL_OBJECT is 5 here, 4 there). The real
 * installer build links its own C sources together, so this binary mirrors
 * that: icfg.o + installer jsonlite.o, nothing from orbis/.
 */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
#include "../installer/icfg.h"

static int make_tmpdir(char *out, size_t cap){
    static int seq = 0;
    snprintf(out, cap, "/tmp/orx_icfg_%d_%d", (int)getpid(), seq++);
    if(mkdir(out, 0700) != 0) return -1;
    return 0;
}

static void test_installer_cfg(void) {
    char dir[64], path[96], path2[96];
    assert(make_tmpdir(dir, sizeof dir) == 0);
    snprintf(path, sizeof path, "%s/config.json", dir);
    /* token validation */
    assert(token_valid("MTIz.ABCdef_0123456789-xYz.ABCDEFGHijklmnopQRSTUVwxyz") == 1);
    assert(token_valid("short") == 0);
    assert(token_valid("SET_ME") == 0);
    assert(token_valid("has space.in.it.aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa") == 0);
    assert(token_valid("nodots") == 0);
    assert(token_valid(NULL) == 0);
    /* fixture via stdio: no C write API exists anymore (the C++ settings
     * page owns writes); these tests pin the read + carry API it relies on */
    FILE *f = fopen(path, "wb");
    assert(f);
    fputs("{\"token\":\"MTIz.ABCdef_0123456789-xYz.ABCDEFGHijklmnopQRSTUVwxyz\","
          "\"presence_state\":\"On PS4\",\"poll_interval_s\":12}", f);
    fclose(f);
    char tok[160], st[64];
    long n = 0;
    assert(icfg_token_load(path, tok, sizeof tok) == 0);
    assert(!strcmp(tok, "MTIz.ABCdef_0123456789-xYz.ABCDEFGHijklmnopQRSTUVwxyz"));
    assert(icfg_get_str(path, "presence_state", st, sizeof st) == 0);
    assert(!strcmp(st, "On PS4"));
    assert(icfg_get_int(path, "poll_interval_s", &n) == 0 && n == 12);
    /* missing file: loads fail soft */
    assert(icfg_get_str("/nonexistent/x.json", "k", st, sizeof st) != 0);
    /* carry round-trip across a wholesale overwrite */
    const char *keys[] = { "token", "presence_state", "poll_interval_s", "nope_missing", NULL };
    icfg_carry_t vals[4];
    assert(icfg_carry_capture(path, keys, vals, 4) == 3);
    snprintf(path2, sizeof path2, "%s/config2.json", dir);
    FILE *g = fopen(path2, "wb");
    assert(g);
    fputs("{\"schema_version\":1}", g);
    fclose(g);
    assert(icfg_carry_apply(path2, vals, 4) == 3);
    assert(icfg_token_load(path2, tok, sizeof tok) == 0);
    assert(!strcmp(tok, "MTIz.ABCdef_0123456789-xYz.ABCDEFGHijklmnopQRSTUVwxyz"));
    assert(icfg_get_int(path2, "poll_interval_s", &n) == 0 && n == 12);
    /* corrupt source: capture yields all-absent, never garbage */
    FILE *h = fopen(path, "wb");
    assert(h);
    fputs("{not json", h);
    fclose(h);
    assert(icfg_carry_capture(path, keys, vals, 4) == 0);
}

int main(void){
    test_installer_cfg();
    puts("icfg tests passed");
    return 0;
}
