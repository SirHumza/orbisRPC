#include "../orbisrpc/jsonlite.h"
#include "../orbisrpc/b64.h"
#include "../orbisrpc/sfo.h"
#include "../orbisrpc/tmdb_crypto.h"
#include "../orbisrpc/updater.h"
#include "../orbisrpc/art.h"
#include "../orbisrpc/health.h"
#include "../orbisrpc/manifest.h"
#include "../orbisrpc/cfg.h"
#include "../orbisrpc/appdb.h"
#include "../orbisrpc/discord.h"
#include "../orbisrpc/detect.h"
#include "../installer/icfg.h"
#include "sqlite3.h"
#include <string.h>

/* discord.c host stubs: builder tests never touch the wire. */
int detect_media_type(const char *t){
    if(t && !strcmp(t, "CUSA00127")) return 3;
    return 0;
}
int ws_connect(ws_t *w, const char *h, int p, const char *r, const char *k){
    (void)w; (void)h; (void)p; (void)r; (void)k; return -1;
}
int ws_send_text(ws_t *w, const char *m, size_t n){
    (void)w; (void)m; (void)n; return -1;
}
int ws_recv_frame(ws_t *w, char *b, size_t c, int *o, int *f){
    (void)w; (void)b; (void)c; (void)o; (void)f; return -1;
}
int ws_pong(ws_t *w){ (void)w; return -1; }
int ws_close(ws_t *w){ (void)w; return -1; }
#include <mbedtls/ecdsa.h>
#include <mbedtls/ecp.h>
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/entropy.h>
#include <mbedtls/sha256.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/stat.h>

/* Portable temp dir (mkdtemp needs feature macros this toolchain lacks). */
static int make_tmpdir(char *out, size_t cap){
    static int seq = 0;
    snprintf(out, cap, "/tmp/orx_test_%d_%d", (int)getpid(), seq++);
    if(mkdir(out, 0700) != 0) return -1;
    return 0;
}

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

static void test_json_oom_safe(void) {    assert(jl_parse("true", 4) != NULL);
    assert(jl_parse("false", 5) != NULL);
    assert(jl_parse("null", 4) != NULL);
    jl_val_t *n = jl_parse("123.5", 5);
    assert(n && n->type == JL_NUMBER);
    jl_free(n);
    /* incomplete pair must fail cleanly, no leak/crash */
    assert(jl_parse("{\"a\":", 5) == NULL);
    assert(jl_parse("{\"a\":1", 6) == NULL);
    /* oversize input refused before any allocation */
    assert(jl_parse("[]", 5u*1024u*1024u) == NULL);
}
static void test_json_hostile(void) {
    /* 200-deep nesting must be rejected, not stack-smash */
    char deep[420];
    memset(deep, '[', 200);
    memset(deep+200, ']', 200);
    deep[400] = 0;
    assert(jl_parse(deep, 400) == NULL);
    /* 60-deep is fine */
    char okd[130];
    memset(okd, '[', 60);
    memset(okd+60, ']', 60);
    okd[120] = 0;
    jl_val_t *r = jl_parse(okd, 120);
    assert(r && r->type == JL_ARRAY);
    jl_free(r);
    /* number running exactly to buffer end (no NUL past it) */
    char num[16];
    memcpy(num, "{\"s\":41250}", 11);
    jl_val_t *r2 = jl_parse(num, 11);
    assert(r2);
    assert(jl_obj_get(r2, "s")->num == 41250);
    jl_free(r2);
    /* absurd number token fails cleanly */
    char big[80];
    memset(big, '9', 70);
    big[70] = 0;
    assert(jl_parse(big, 70) == NULL);
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
static void test_tmdb(void) {
    unsigned char dig[20];
    char hex[41];
    int i;
    /* SHA1("abc") = a9993e364706816aba3e25717850c26c9cd0d4d */
    tmdb_sha1((const unsigned char *)"abc", 3, dig);
    for(i=0;i<20;i++) snprintf(hex+2*i, 3, "%02x", dig[i]);
    assert(strcmp(hex, "a9993e364706816aba3e25717850c26c9cd0d89d") == 0);
    /* HMAC-SHA1 RFC 2202 case 1 */
    {
        unsigned char key[20];
        memset(key, 0x0b, 20);
        tmdb_hmac_sha1(key, 20, (const unsigned char *)"Hi There", 8, dig);
        for(i=0;i<20;i++) snprintf(hex+2*i, 3, "%02x", dig[i]);
        assert(strcmp(hex, "b617318655057264e28bc0b6fb378c8ef146be00") == 0);
    }
    /* URL path must match the hash Sony's live service accepts */
    {
        char path[128];
        assert(tmdb_path("CUSA00740", path, sizeof path) == 0);
        assert(strcmp(path, "/tmdb2/CUSA00740_00_95C83DE844D155477CB77C577A24D735F2E9AC08/CUSA00740_00.json") == 0);
        assert(tmdb_path("junk!", path, sizeof path) != 0);
        assert(tmdb_path("CUSA00740", path, 10) != 0);
    }
    /* response parse: name + icon URL */
    {
        const char body[] = "{\"names\":[{\"name\":\"Terraria\"}],\"icons\":[{\"icon\":\"http://x/y/icon0.png\",\"type\":\"512x512\"}]}";
        char name[64], icon[128];
        assert(tmdb_parse(body, sizeof(body)-1, name, sizeof name, icon, sizeof icon) == 0);
        assert(strcmp(name, "Terraria") == 0);
        assert(strcmp(icon, "http://x/y/icon0.png") == 0);
        /* non-URL icon rejected, name still wins */
        const char body2[] = "{\"names\":[{\"name\":\"X\"}],\"icons\":[{\"icon\":\"not a url\"}]}";
        assert(tmdb_parse(body2, sizeof(body2)-1, name, sizeof name, icon, sizeof icon) == 0);
        assert(icon[0] == 0);
        /* no names -> fail */
        assert(tmdb_parse("{\"icons\":[]}", 12, name, sizeof name, icon, sizeof icon) != 0);
    }
}
static void test_updater(void) {
    assert(updater_cmp("0.4.0", "0.4.0") == 0);
    assert(updater_cmp("v0.4.1", "0.4.0") > 0);
    assert(updater_cmp("0.4.0", "v0.4.1") < 0);
    assert(updater_cmp("0.10.0", "0.9.9") > 0);
    assert(updater_cmp("1.0", "1.0.0") == 0);
    assert(updater_cmp(NULL, "0.1") < 0);
    unsigned char elf[64];
    memset(elf, 0, sizeof elf);
    assert(updater_elf_ok(elf, sizeof elf) == 0);
    assert(updater_elf_ok(NULL, 100) == 0);
    assert(updater_elf_ok(elf, 10) == 0);
    elf[0]=0x7f; elf[1]='E'; elf[2]='L'; elf[3]='F';
    elf[4]=2; elf[5]=1; elf[18]=62; elf[19]=0;
    assert(updater_elf_ok(elf, sizeof elf) == 1);
    elf[18]=99;
    assert(updater_elf_ok(elf, sizeof elf) == 0);
}
static void test_art_parse(void) {
    /* Real external-assets response shape (verified against live API). */
    const char *body = "[{\"url\":\"https://example.com/a.png\","
        "\"external_asset_path\":\"external/ABC/https/example.com/a.png\"},"
        "{\"url\":\"https://example.com/b.png\","
        "\"external_asset_path\":\"external/DEF/https/example.com/b.png\"}]";
    char out[128] = {0};
    assert(art_parse_mp(body, strlen(body),
                        "https://example.com/b.png", out, sizeof out) == 1);
    assert(strcmp(out, "mp:external/DEF/https/example.com/b.png") == 0);
    assert(art_parse_mp(body, strlen(body),
                        "https://example.com/zzz.png", out, sizeof out) == 0);
    assert(art_parse_mp("[]", 2, "x", out, sizeof out) == 0);
    assert(art_parse_mp(NULL, 0, "x", out, sizeof out) == 0);
    assert(art_resolve_mp("1", "t", "https://example.com/a.png", out, sizeof out) == 0);
}

static void test_health_safe_mode(void) {
    /* Unclean-boot marker semantics: normal reboots never count. */
    char dir[64];
    assert(make_tmpdir(dir, sizeof dir) == 0);
    health_set_base(dir);
    health_mark_clean();
    /* clean boot x3: counter stays 0, never safe mode */
    assert(health_boot_note_crash() == 0);
    health_mark_healthy();
    assert(health_boot_note_crash() == 0);
    health_mark_healthy();
    assert(health_boot_note_crash() == 0);
    /* now crash repeatedly WITHOUT going healthy: 1, 2, then safe */
    assert(health_boot_note_crash() == 0); /* count=1 */
    assert(health_boot_note_crash() == 0); /* count=2 */
    assert(health_boot_note_crash() == 1); /* count=3 -> safe mode */
    /* recovery clears */
    health_mark_healthy();
    assert(health_boot_note_crash() == 0);
    health_mark_healthy();
}

static void test_health_stage_activate(void) {
    /* Atomic staging: bad .new never touches live; rollback restores. */
    char dir[64];
    assert(make_tmpdir(dir, sizeof dir) == 0);
    health_set_base(dir);
    char live[256], tmp[256], bak[256];
    snprintf(live, sizeof live, "%s/live.bin", dir);
    snprintf(tmp, sizeof tmp, "%s/live.bin.new", dir);
    snprintf(bak, sizeof bak, "%s/live.bin.bak", dir);
    /* live = valid ELF stand-in (>=64B, ELF magic via updater_image_ok?
     * use real check: write 64 zero bytes won't pass; stage path only
     * needs .new validation, so craft minimal ELF header). */
    unsigned char elf[128];
    memset(elf, 0, sizeof elf);
    elf[0] = 0x7f; elf[1] = 'E'; elf[2] = 'L'; elf[3] = 'F';
    elf[4] = 2; elf[5] = 1; elf[18] = 62;
    FILE *f = fopen(live, "wb");
    assert(f); assert(fwrite(elf, 1, sizeof elf, f) == sizeof elf); fclose(f);
    /* corrupt .new is refused, live untouched */
    f = fopen(tmp, "wb");
    assert(f); assert(fwrite("garbage-not-elf-at-all......................"
                            "..............................", 1, 64, f) == 64);
    fclose(f);
    assert(health_stage_activate(live) != 0);
    f = fopen(live, "rb");
    assert(f);
    unsigned char chk[4];
    assert(fread(chk, 1, 4, f) == 4);
    fclose(f);
    assert(chk[0] == 0x7f && chk[1] == 'E');
    /* valid .new activates, backup created, rollback restores */
    f = fopen(tmp, "wb");
    assert(f);
    elf[7] = 0x42;
    assert(fwrite(elf, 1, sizeof elf, f) == sizeof elf);
    fclose(f);
    assert(health_stage_activate(live) == 0);
    f = fopen(bak, "rb");
    assert(f); fclose(f);
    assert(health_verify_or_rollback(live) == 0);
    /* corrupt live + backup present -> rollback */
    f = fopen(live, "wb");
    assert(f); assert(fwrite("XX", 1, 2, f) == 2); fclose(f);
    assert(health_verify_or_rollback(live) == 1);
    f = fopen(live, "rb");
    assert(f);
    assert(fread(chk, 1, 4, f) == 4);
    fclose(f);
    assert(chk[0] == 0x7f);
}

static void test_manifest(void) {
    const char *json = "{\"version\":\"1.0.0\",\"channel\":\"stable\","
        "\"min_version\":\"0.9.0\",\"platform\":\"ps4-goldhen\","
        "\"assets\":[{\"name\":\"orbisrpc.bin\","
        "\"sha256\":\"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef\"}]}";
    manifest_t m;
    assert(manifest_parse(json, strlen(json), &m) == 0);
    assert(strcmp(m.version, "1.0.0") == 0);
    assert(strcmp(m.channel, "stable") == 0);
    char hex[65] = {0};
    assert(manifest_find(&m, "orbisrpc.bin", hex) == 0);
    assert(!strcmp(hex, "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"));
    assert(manifest_find(&m, "nope.bin", hex) != 0);
    assert(manifest_gate(&m) == 1);
    assert(manifest_is_newer(&m, "0.9.0") == 1);
    assert(manifest_is_newer(&m, "1.0.0") == 0);
    /* wrong channel / platform refused */
    const char *bad = "{\"version\":\"9.9.9\",\"channel\":\"beta\","
        "\"platform\":\"ps5\",\"assets\":[{\"name\":\"x.bin\","
        "\"sha256\":\"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef\"}]}";
    manifest_t m2;
    assert(manifest_parse(bad, strlen(bad), &m2) == 0);
    assert(manifest_gate(&m2) == 0);
    /* malformed rejected */
    assert(manifest_parse("{}", 2, &m) != 0);
    assert(manifest_parse("not json", 8, &m) != 0);
    /* hash check: real sha256 of "abc" must match */
    const char *jh = "{\"version\":\"1\",\"assets\":[{\"name\":\"a\","
        "\"sha256\":\"ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad\"}]}";
    manifest_t m3;
    assert(manifest_parse(jh, strlen(jh), &m3) == 0);
    assert(manifest_check(&m3, "a", (const unsigned char *)"abc", 3) == 0);
    assert(manifest_check(&m3, "a", (const unsigned char *)"abd", 3) != 0);
}

static void test_manifest_sig(void) {
    /* Full round trip with a fresh keypair: sign via mbedTLS, verify via
     * our public-API-only manifest_verify_sig. Tampered bytes must fail.
     * Uses only public 3.x APIs (raw group + MPIs, no context internals). */
    static const unsigned char msg[] = "{\"version\":\"9.9.9\"}";
    mbedtls_entropy_context ent;
    mbedtls_entropy_init(&ent);
    mbedtls_ctr_drbg_context rng;
    mbedtls_ctr_drbg_init(&rng);
    assert(mbedtls_ctr_drbg_seed(&rng, mbedtls_entropy_func, &ent,
                                  (const unsigned char *)"test", 4) == 0);
    mbedtls_ecp_group grp;
    mbedtls_ecp_group_init(&grp);
    assert(mbedtls_ecp_group_load(&grp, MBEDTLS_ECP_DP_SECP256R1) == 0);
    mbedtls_mpi d, r, s;
    mbedtls_mpi_init(&d); mbedtls_mpi_init(&r); mbedtls_mpi_init(&s);
    mbedtls_ecp_point Q;
    mbedtls_ecp_point_init(&Q);
    assert(mbedtls_ecp_gen_keypair(&grp, &d, &Q,
                                    mbedtls_ctr_drbg_random, &rng) == 0);
    unsigned char hash[32], sig[64], rawpub[64];
    {
        mbedtls_sha256_context sc;
        mbedtls_sha256_init(&sc);
        assert(mbedtls_sha256_starts(&sc, 0) == 0);
        assert(mbedtls_sha256_update(&sc, msg, sizeof msg - 1) == 0);
        assert(mbedtls_sha256_finish(&sc, hash) == 0);
        mbedtls_sha256_free(&sc);
    }
    assert(mbedtls_ecdsa_sign(&grp, &r, &s, &d, hash, sizeof hash,
                               mbedtls_ctr_drbg_random, &rng) == 0);
    assert(mbedtls_mpi_write_binary(&r, sig, 32) == 0);
    assert(mbedtls_mpi_write_binary(&s, sig + 32, 32) == 0);
    /* export X||Y via the public point-write API */
    {
        unsigned char uncomp[65];
        size_t olen = 0;
        assert(mbedtls_ecp_point_write_binary(&grp, &Q,
               MBEDTLS_ECP_PF_UNCOMPRESSED, &olen, uncomp, sizeof uncomp) == 0);
        assert(olen == 65 && uncomp[0] == 0x04);
        memcpy(rawpub, uncomp + 1, 64);
    }
    assert(manifest_verify_sig(msg, sizeof msg - 1, sig, rawpub) == 0);
    sig[10] ^= 0x01;
    assert(manifest_verify_sig(msg, sizeof msg - 1, sig, rawpub) != 0);
    sig[10] ^= 0x01;
    unsigned char bad[sizeof msg];
    memcpy(bad, msg, sizeof bad);
    bad[5] ^= 0x01;
    assert(manifest_verify_sig(bad, sizeof bad - 1, sig, rawpub) != 0);
    assert(manifest_verify_sig(NULL, 0, sig, rawpub) != 0);
    mbedtls_mpi_free(&d); mbedtls_mpi_free(&r); mbedtls_mpi_free(&s);
    mbedtls_ecp_point_free(&Q);
    mbedtls_ecp_group_free(&grp);
    mbedtls_ctr_drbg_free(&rng);
    mbedtls_entropy_free(&ent);
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

static void test_cfg_titles(void) {
    char dir[64], path[96], path2[96];
    assert(make_tmpdir(dir, sizeof dir) == 0);
    snprintf(path, sizeof path, "%s/cfg.json", dir);
    snprintf(path2, sizeof path2, "%s/cfg2.json", dir);
    FILE *f = fopen(path, "wb");
    assert(f);
    fputs("{\"token\":\"t\",\"titles\":{\"CUSA11995\":\"Marvel's Spider-Man\","
          "\"BAD KEY!\": \"junk\", \"CUSA00001\": \"x\", \"TOOLONGTITLEID12345\": \"y\","
          "\"CUSA00002\": 42}}", f);
    fclose(f);
    cfg_t c;
    assert(cfg_load(path, &c) == 0);
    assert(c.n_titles == 1); /* only the well-formed entry survives */
    char name[128];
    assert(cfg_title(&c, "CUSA11995", name, sizeof name) == 0);
    assert(!strcmp(name, "Marvel's Spider-Man"));
    assert(cfg_title(&c, "CUSA99999", name, sizeof name) != 0);
    assert(cfg_title(&c, "", name, sizeof name) != 0);
    assert(cfg_title(NULL, "CUSA11995", name, sizeof name) != 0);
    /* round-trip: save preserves overrides + home_art */
    strncpy(c.home_art, "pslogo", sizeof c.home_art - 1);
    assert(cfg_save(path2, &c) == 0);
    cfg_t c2;
    assert(cfg_load(path2, &c2) == 0);
    assert(c2.n_titles == 1);
    assert(cfg_title(&c2, "CUSA11995", name, sizeof name) == 0);
    assert(!strcmp(name, "Marvel's Spider-Man"));
    assert(!strcmp(c2.home_art, "pslogo"));
    /* missing titles object: zero overrides, lookup misses */
    f = fopen(path, "wb");
    assert(f);
    fputs("{\"token\":\"t\"}", f);
    fclose(f);
    assert(cfg_load(path, &c) == 0);
    assert(c.n_titles == 0);
    assert(!strcmp(c.home_art, "https://raw.githubusercontent.com/SirHumza/orbisRPC/main/config/icons/logo.png"));
}

static void test_appdb(void) {
    char dir[64], db[96], meta[96], pj[160];
    assert(make_tmpdir(dir, sizeof dir) == 0);
    snprintf(db, sizeof db, "%s/app.db", dir);
    snprintf(meta, sizeof meta, "%s/appmeta", dir);
    assert(mkdir(meta, 0700) == 0);
    sqlite3 *s = NULL;
    (void)sqlite3_initialize();
    assert(sqlite3_open_v2(db, &s, SQLITE_OPEN_READWRITE|SQLITE_OPEN_CREATE, NULL) == SQLITE_OK);
    assert(sqlite3_exec(s, "CREATE TABLE tbl_appbrowse(titleId TEXT, titleName TEXT);", 0, 0, 0) == SQLITE_OK);
    assert(sqlite3_exec(s, "INSERT INTO tbl_appbrowse VALUES('CUSA11995','Marvel''s Spider-Man');", 0, 0, 0) == SQLITE_OK);
    assert(sqlite3_exec(s, "CREATE TABLE tbl_appinfo(titleId TEXT, key TEXT, val TEXT);", 0, 0, 0) == SQLITE_OK);
    assert(sqlite3_exec(s, "INSERT INTO tbl_appinfo VALUES('CUSA00001','TITLE_01','Lang One');", 0, 0, 0) == SQLITE_OK);
    assert(sqlite3_exec(s, "INSERT INTO tbl_appinfo VALUES('CUSA00001','TITLE','Base Name');", 0, 0, 0) == SQLITE_OK);
    assert(sqlite3_exec(s, "INSERT INTO tbl_appinfo VALUES('CUSA00002','TITLE','');", 0, 0, 0) == SQLITE_OK);
    sqlite3_close(s);
    char name[128];
    /* tier 1: browse name wins */
    assert(appdb_title_from(db, meta, "CUSA11995", name, sizeof name) == 0);
    assert(!strcmp(name, "Marvel's Spider-Man"));
    /* tier 2: bare TITLE preferred over TITLE_01 */
    assert(appdb_title_from(db, meta, "CUSA00001", name, sizeof name) == 0);
    assert(!strcmp(name, "Base Name"));
    /* empty val rows never match; no param.json staged -> miss */
    assert(appdb_title_from(db, meta, "CUSA00002", name, sizeof name) != 0);
    assert(appdb_title_from(db, meta, "CUSA99999", name, sizeof name) != 0);
    /* tier 3: staged param.json */
    char gdir[128];
    snprintf(gdir, sizeof gdir, "%s/CUSA00002", meta);
    assert(mkdir(gdir, 0700) == 0);
    snprintf(pj, sizeof pj, "%s/param.json", gdir);
    FILE *f = fopen(pj, "wb");
    assert(f);
    fputs("{\"titleId\":\"CUSA00002\",\"titleName\":\"JSON Game\"}", f);
    fclose(f);
    assert(appdb_title_from(db, meta, "CUSA00002", name, sizeof name) == 0);
    assert(!strcmp(name, "JSON Game"));
    /* invalid inputs fail soft */
    assert(appdb_title_from(db, meta, NULL, name, sizeof name) != 0);
    assert(appdb_title_from(db, meta, "short", name, sizeof name) != 0);
    assert(appdb_title_from(db, meta, "cusa11995", name, sizeof name) != 0);
    assert(appdb_title_from(db, meta, "CUSA1199!", name, sizeof name) != 0);
    assert(appdb_title_from("/nonexistent/app.db", meta, "CUSA11995", name, sizeof name) != 0);
    assert(appdb_title_from(db, meta, "CUSA11995", NULL, 0) != 0);
}

static void test_discord_builder(void) {
    /* game presence: name shown, raw ID nowhere visible, hover has the ID */
    jl_val_t *a = discord_build_activity("On PS4", "Marvel's Spider-Man",
        "CUSA11995", "1536977374795538532", "https://x/icons/", NULL, NULL,
        1700000000LL, "");
    assert(a);
    const jl_val_t *v = jl_obj_get(a, "name");
    assert(v && v->type == JL_STRING && !strcmp(v->str, "Marvel's Spider-Man"));
    assert(jl_obj_get(a, "details") == NULL); /* never the raw ID */
    v = jl_obj_get(a, "state");
    assert(v && v->type == JL_STRING && !strcmp(v->str, "On PS4"));
    v = jl_obj_get(a, "type");
    assert(v && v->type == JL_NUMBER && (int)v->num == 0);
    const jl_val_t *ts = jl_obj_get(a, "timestamps");
    assert(ts && ts->type == JL_OBJECT);
    v = jl_obj_get(ts, "start");
    assert(v && v->type == JL_NUMBER && v->inum == 1700000000LL * 1000LL);
    const jl_val_t *as = jl_obj_get(a, "assets");
    assert(as && as->type == JL_OBJECT);
    v = jl_obj_get(as, "large_image");
    assert(v && v->type == JL_STRING && !strcmp(v->str, "cusa11995"));
    v = jl_obj_get(as, "large_text");
    assert(v && v->type == JL_STRING && !strcmp(v->str, "CUSA11995"));
    assert(jl_obj_get(as, "small_image") == NULL); /* no badge requested */
    jl_free(a);
    /* system badge: mp: URL used as-is with platform hover text */
    a = discord_build_activity("On PS4", "Marvel's Spider-Man",
        "CUSA11995", "1536977374795538532", NULL, NULL, "mp:1/2/logo",
        1700000000LL, "");
    assert(a);
    as = jl_obj_get(a, "assets");
    assert(as && as->type == JL_OBJECT);
    v = jl_obj_get(as, "small_image");
    assert(v && v->type == JL_STRING && !strcmp(v->str, "mp:1/2/logo"));
    v = jl_obj_get(as, "small_text");
    assert(v && v->type == JL_STRING && !strcmp(v->str, "PlayStation 4"));
    jl_free(a);
    /* media type mapping survives */
    a = discord_build_activity(NULL, "YouTube", "CUSA01015", "app",
        NULL, NULL, NULL, 0, "");
    assert(a);
    v = jl_obj_get(a, "type");
    assert(v && (int)v->num == 0); /* stub maps only CUSA00127 */
    jl_free(a);
    a = discord_build_activity(NULL, "Netflix", "CUSA00127", "app",
        NULL, NULL, NULL, 0, "");
    assert(a);
    v = jl_obj_get(a, "type");
    assert(v && (int)v->num == 3);
    jl_free(a);
    /* home + uploaded key: trusted key used as-is */
    a = discord_build_activity("On PS4", "PlayStation 4", "home", "app",
        NULL, "pslogo", NULL, 0, "");
    assert(a);
    as = jl_obj_get(a, "assets");
    assert(as && as->type == JL_OBJECT);
    v = jl_obj_get(as, "large_image");
    assert(v && !strcmp(v->str, "pslogo"));
    v = jl_obj_get(as, "large_text");
    assert(v && !strcmp(v->str, "PlayStation 4"));
    jl_free(a);
    /* home with nothing: no assets block at all (never dangling) */
    a = discord_build_activity("On PS4", "PlayStation 4", "home", "app",
        NULL, NULL, NULL, 0, "");
    assert(a);
    assert(jl_obj_get(a, "assets") == NULL);
    jl_free(a);
    /* NULL name rejected */
    assert(discord_build_activity(NULL, NULL, "home", "app", NULL, NULL, NULL, 0, "") == NULL);
}

static void test_cfg_learn(void) {
    cfg_t c;
    cfg_defaults(&c);
    assert(c.n_titles == 0);
    assert(cfg_learn(&c, "CUSA11995", "Marvel's Spider-Man") == 1);
    assert(cfg_learn(&c, "CUSA11995", "Marvel's Spider-Man") == 0); /* dup */
    assert(cfg_learn(&c, "CUSA11995", "CUSA11995") == 0); /* ID echo refused */
    assert(cfg_learn(&c, "CUSA11995", "x") == 0); /* too short */
    assert(cfg_learn(&c, "bad key!", "Name") == 0);
    assert(cfg_learn(&c, "CUSA11995", "Spider-Man 2") == 1); /* update */
    char name[128];
    assert(cfg_title(&c, "CUSA11995", name, sizeof name) == 0);
    assert(!strcmp(name, "Spider-Man 2"));
    assert(cfg_learn(NULL, "CUSA11995", "N") == 0);
    assert(cfg_learn(&c, NULL, "N") == 0);
    /* full map refuses */
    c.n_titles = CFG_MAX_TITLES;
    assert(cfg_learn(&c, "CUSA00001", "Full Game") == 0);
}

static void test_installer_cfg(void) {
    char dir[64], path[96];
    assert(make_tmpdir(dir, sizeof dir) == 0);
    snprintf(path, sizeof path, "%s/config.json", dir);
    /* token validation */
    assert(token_valid("MTIz.ABCdef_0123456789-xYz.ABCDEFGHijklmnopQRSTUVwxyz") == 1);
    assert(token_valid("short") == 0);
    assert(token_valid("SET_ME") == 0);
    assert(token_valid("has space.in.it.aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa") == 0);
    assert(token_valid("nodots") == 0);
    assert(token_valid(NULL) == 0);
    /* save creates, preserves, round-trips */
    assert(icfg_token_save(path, "MTIz.ABCdef_0123456789-xYz.ABCDEFGHijklmnopQRSTUVwxyz") == 0);
    assert(icfg_set_str(path, "presence_state", "On PS4") == 0);
    assert(icfg_set_int(path, "poll_interval_s", 12) == 0);
    /* invalid token refused, file untouched */
    assert(icfg_token_save(path, "junk") == -1);
    char tok[160], st[64];
    long n = 0;
    assert(icfg_token_load(path, tok, sizeof tok) == 0);
    assert(!strcmp(tok, "MTIz.ABCdef_0123456789-xYz.ABCDEFGHijklmnopQRSTUVwxyz"));
    assert(icfg_get_str(path, "presence_state", st, sizeof st) == 0);
    assert(!strcmp(st, "On PS4"));
    assert(icfg_get_int(path, "poll_interval_s", &n) == 0 && n == 12);
    /* missing file: loads fail soft, save still works */
    assert(icfg_get_str("/nonexistent/x.json", "k", st, sizeof st) != 0);
}

int main(void) {
    test_json();
    test_gateway_op_spoof();
    test_json_oom_safe();
    test_json_hostile();
    test_tmdb();
    test_updater();
    test_sfo();
    test_base64();
    test_art_parse();
    test_health_safe_mode();
    test_health_stage_activate();
    test_manifest();
    test_manifest_sig();
    test_cfg_titles();
    test_cfg_learn();
    test_installer_cfg();
    test_appdb();
    test_discord_builder();
    puts("utility tests passed");
    return 0;
}
