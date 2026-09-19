#include "../orbisrpc/jsonlite.h"
#include "../orbisrpc/b64.h"
#include "../orbisrpc/sfo.h"
#include "../orbisrpc/nametable.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>

static void test_json(void) {
    const char input[] = "{\"name\":\"A\\u00e9\",\"items\":[true,2,null]}";
    jl_val_t *root = jl_parse(input, sizeof(input) - 1);
    assert(root && root->type == JL_OBJECT);
    assert(strcmp(jl_obj_get(root, "name")->str, "A\xc3\xa9") == 0);
    assert(jl_arr_at(jl_obj_get(root, "items"), 0)->num == 1);
    assert(jl_arr_at(jl_obj_get(root, "items"), 1)->num == 2);
    assert(jl_arr_at(jl_obj_get(root, "items"), 2)->type == JL_NULL);
    jl_free(root);

    assert(jl_parse("{} trailing", 11) == NULL);
    assert(jl_parse("[1,]", 4) == NULL);
    assert(jl_parse("\"unterminated", 13) == NULL);
}

static void test_gateway_op_spoof(void) {
    /* string value containing `"op":` must not spoof the real op */
    const char input[] = "{\"note\":\"x \\\"op\\\":99 y\",\"op\":10,\"s\":42}";
    jl_val_t *r = jl_parse(input, sizeof(input)-1);
    assert(r && r->type == JL_OBJECT);
    const jl_val_t *op = jl_obj_get(r, "op");
    const jl_val_t *s = jl_obj_get(r, "s");
    assert(op && op->type == JL_NUMBER && (int)op->num == 10);
    assert(s && s->type == JL_NUMBER && (int)s->num == 42);
    jl_free(r);
    /* missing op -> NULL, not crash */
    const char no_op[] = "{\"t\":\"READY\"}";
    jl_val_t *r2 = jl_parse(no_op, sizeof(no_op)-1);
    assert(r2 && jl_obj_get(r2, "op") == NULL);
    jl_free(r2);
}

static void test_json_oom_safe(void) {
    assert(jl_parse("true", 4) != NULL);
    assert(jl_parse("false", 5) != NULL);
    assert(jl_parse("null", 4) != NULL);
    jl_val_t *n = jl_parse("123.5", 5);
    assert(n && n->type == JL_NUMBER);
    jl_free(n);
    /* incomplete pair must fail cleanly, no leak/crash */
    assert(jl_parse("{\"a\":", 5) == NULL);
    assert(jl_parse("{\"a\":1", 6) == NULL);
}
static void test_sfo(void) {
    /* minimal synthetic param.sfo: header + 1 entry (TITLE="Terraria") */
    unsigned char sfo[128];
    char out[64];
    memset(sfo, 0, sizeof sfo);
    sfo[0]=0x00; sfo[1]='P'; sfo[2]='S'; sfo[3]='F';
    sfo[4]=0x01; sfo[5]=0x02;
    sfo[8]=36; sfo[12]=48; sfo[16]=1;   /* keys@36 data@48 */
    sfo[20]=0; sfo[21]=0; sfo[22]=0x04; sfo[23]=0; /* key_off=0 fmt=0x0004 */
    sfo[24]=9; sfo[28]=16; sfo[32]=0;   /* len=9 max=16 data_off=0 */
    memcpy(sfo+36, "TITLE", 6);
    memcpy(sfo+48, "Terraria", 9);
    assert(sfo_title(sfo, 64, out, sizeof out) == 0);
    assert(strcmp(out, "Terraria") == 0);
    /* malformed: bad magic, truncated, insane count, OOB offsets */
    unsigned char bad[64];
    memset(bad, 0, sizeof bad);
    assert(sfo_title(bad, sizeof bad, out, sizeof out) != 0);
    assert(sfo_title(sfo, 10, out, sizeof out) != 0);
    sfo[16]=200; /* count overflow */
    assert(sfo_title(sfo, 64, out, sizeof out) != 0);
    sfo[16]=1; sfo[8]=200; /* key_off OOB */
    assert(sfo_title(sfo, 64, out, sizeof out) != 0);
    sfo[8]=36;
    /* tiny output buffer still safe */
    assert(sfo_title(sfo, 64, out, 4) == 0);
    assert(out[3] == 0);
    /* unterminated key region: must fail, not read OOB */
    memset(sfo, 0x41, sizeof sfo);
    sfo[0]=0x00; sfo[1]='P'; sfo[2]='S'; sfo[3]='F';
    sfo[8]=36; sfo[12]=48; sfo[16]=1;
    sfo[20]=0; sfo[22]=0x04; sfo[24]=9; sfo[28]=16; sfo[32]=0;
    assert(sfo_title(sfo, 64, out, sizeof out) != 0);
}
static void test_nametable(void) {
    char out[64];
    assert(nametable_lookup("CUSA00740", out, sizeof out) == 0);
    assert(strcmp(out, "Terraria") == 0);
    assert(nametable_lookup("CUSA00411", out, sizeof out) == 0);
    assert(strcmp(out, "Grand Theft Auto V") == 0);
    assert(nametable_lookup("XXXX99999", out, sizeof out) != 0);
    assert(nametable_lookup(NULL, out, sizeof out) != 0);
    /* tiny buffer: truncated but terminated */
    assert(nametable_lookup("CUSA00740", out, 4) == 0);
    assert(out[3] == 0);
}
static void test_base64(void) {
    char out[32];
    assert(b64_encode((const unsigned char *)"", 0, out) == 0);
    assert(strcmp(out, "") == 0);
    assert(b64_encode((const unsigned char *)"f", 1, out) == 4);
    assert(strcmp(out, "Zg==") == 0);
    assert(b64_encode((const unsigned char *)"fo", 2, out) == 4);
    assert(strcmp(out, "Zm8=") == 0);
    assert(b64_encode((const unsigned char *)"foo", 3, out) == 4);
    assert(strcmp(out, "Zm9v") == 0);
}

int main(void) {
    test_json();
    test_gateway_op_spoof();
    test_json_oom_safe();
    test_sfo();
    test_nametable();
    test_base64();
    puts("utility tests passed");
    return 0;
}
