/* manifest.c - signed release manifest (host-testable + PS4).
 * Parsing via jsonlite; hashing + ECDSA via mbedTLS (already linked). */
#include "manifest.h"
#include "jsonlite.h"
#include "updater.h"
#include <string.h>
#include <mbedtls/sha256.h>
#include <mbedtls/ecdsa.h>
#include <mbedtls/ecp.h>
#include <mbedtls/bignum.h>

int manifest_sha256_hex(const unsigned char *data, size_t n, char out[65]){
    unsigned char dig[32];
    if(!data && n) return -1;
    mbedtls_sha256_context sc;
    mbedtls_sha256_init(&sc);
    int ok = mbedtls_sha256_starts(&sc, 0) == 0 &&
             mbedtls_sha256_update(&sc, data ? data : (const unsigned char *)"", n) == 0 &&
             mbedtls_sha256_finish(&sc, dig) == 0;
    mbedtls_sha256_free(&sc);
    if(!ok) return -1;
    for(int i = 0; i < 32; i++) snprintf(out + 2 * i, 3, "%02x", dig[i]);
    out[64] = 0;
    return 0;
}

static int copy_str(const jl_val_t *v, char *out, size_t cap){
    if(!v || v->type != JL_STRING || !v->str) return -1;
    size_t n = strlen(v->str);
    if(n >= cap) return -1;
    memcpy(out, v->str, n + 1);
    return 0;
}

int manifest_parse(const char *json, size_t len, manifest_t *out){
    if(!json || !out) return -1;
    memset(out, 0, sizeof *out);
    jl_val_t *r = jl_parse(json, len);
    if(!r || r->type != JL_OBJECT){ if(r) jl_free(r); return -1; }
    if(copy_str(jl_obj_get(r, "version"), out->version, sizeof out->version) != 0){
        jl_free(r); return -1;
    }
    /* optional with sane defaults */
    const jl_val_t *ch = jl_obj_get(r, "channel");
    if(ch && ch->type == JL_STRING){
        if(copy_str(ch, out->channel, sizeof out->channel) != 0){ jl_free(r); return -1; }
    } else {
        memcpy(out->channel, "stable", 7);
    }
    const jl_val_t *mv = jl_obj_get(r, "minimum_version");
    if(!mv) mv = jl_obj_get(r, "min_version");
    if(mv && mv->type == JL_STRING){
        if(copy_str(mv, out->min_version, sizeof out->min_version) != 0){ jl_free(r); return -1; }
    }
    const jl_val_t *pl = jl_obj_get(r, "platform");
    if(!pl) pl = jl_obj_get(r, "supported_platform");
    if(pl && pl->type == JL_STRING){
        if(copy_str(pl, out->platform, sizeof out->platform) != 0){ jl_free(r); return -1; }
    }
    const jl_val_t *assets = jl_obj_get(r, "assets");
    if(!assets) assets = jl_obj_get(r, "files");
    if(assets && assets->type == JL_OBJECT){
        /* object form: { "orbisrpc.bin": "<hex>", ... } */
        /* jsonlite has no object iterator in public API; fall back to
         * array form below. Object form unsupported -> invalid. */
        jl_free(r);
        return -1;
    }
    if(assets && assets->type == JL_ARRAY){
        int n = 0;
        for(size_t i = 0; ; i++){
            const jl_val_t *a = jl_arr_at(assets, i);
            if(!a) break;
            if(n >= MANIFEST_MAX_ASSETS) break;
            const jl_val_t *nm = jl_obj_get(a, "name");
            const jl_val_t *sh = jl_obj_get(a, "sha256");
            if(!sh) sh = jl_obj_get(a, "hash");
            if(!nm || nm->type != JL_STRING || !sh || sh->type != JL_STRING)
                continue;
            if(strlen(nm->str) >= sizeof out->assets[n].name) continue;
            if(strlen(sh->str) != 64) continue;
            int hexok = 1;
            for(int k = 0; k < 64; k++){
                char c = sh->str[k];
                if(!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
                     (c >= 'A' && c <= 'F'))){ hexok = 0; break; }
            }
            if(!hexok) continue;
            memcpy(out->assets[n].name, nm->str, strlen(nm->str) + 1);
            for(int k = 0; k < 64; k++){
                char c = sh->str[k];
                out->assets[n].sha256[k] = (c >= 'A' && c <= 'F') ? (char)(c - 'A' + 'a') : c;
            }
            out->assets[n].sha256[64] = 0;
            n++;
        }
        out->nassets = n;
    }
    jl_free(r);
    if(!out->version[0] || out->nassets <= 0) return -1;
    return 0;
}

int manifest_find(const manifest_t *m, const char *name, char out_hex[65]){
    if(!m || !name) return -1;
    for(int i = 0; i < m->nassets; i++){
        if(!strcmp(m->assets[i].name, name)){
            if(out_hex) memcpy(out_hex, m->assets[i].sha256, 65);
            return 0;
        }
    }
    return -1;
}

int manifest_check(const manifest_t *m, const char *name,
                   const unsigned char *data, size_t n){
    char want[65], got[65];
    if(manifest_find(m, name, want) != 0) return -1;
    if(manifest_sha256_hex(data, n, got) != 0) return -1;
    if(strcmp(got, want) != 0) return -1;
    return 0;
}

int manifest_is_newer(const manifest_t *m, const char *local_version){
    if(!m || !local_version) return 0;
    return updater_cmp(m->version, local_version) > 0;
}

int manifest_gate(const manifest_t *m){
    if(!m) return 0;
    if(m->channel[0] && strcmp(m->channel, "stable") != 0) return 0;
    if(m->platform[0] && strcmp(m->platform, "ps4-goldhen") != 0 &&
       strcmp(m->platform, "ps4") != 0)
        return 0;
    return 1;
}

int manifest_verify_sig(const unsigned char *msg, size_t msglen,
                        const unsigned char sig[64],
                        const unsigned char pubkey[64]){
    if(!msg || !sig || !pubkey) return -1;
    unsigned char hash[32];
    {
        mbedtls_sha256_context sc;
        mbedtls_sha256_init(&sc);
        int ok = mbedtls_sha256_starts(&sc, 0) == 0 &&
                 mbedtls_sha256_update(&sc, msg, msglen) == 0 &&
                 mbedtls_sha256_finish(&sc, hash) == 0;
        mbedtls_sha256_free(&sc);
        if(!ok) return -1;
    }
    mbedtls_ecp_group grp;
    mbedtls_ecp_point Q;
    mbedtls_mpi r, s;
    mbedtls_ecp_group_init(&grp);
    mbedtls_ecp_point_init(&Q);
    mbedtls_mpi_init(&r); mbedtls_mpi_init(&s);
    int rc = -1;
    unsigned char uncompressed[65];
    if(mbedtls_ecp_group_load(&grp, MBEDTLS_ECP_DP_SECP256R1) != 0) goto out;
    if(mbedtls_mpi_read_binary(&r, sig, 32) != 0) goto out;
    if(mbedtls_mpi_read_binary(&s, sig + 32, 32) != 0) goto out;
    /* Public API only (no struct internals): uncompressed point 0x04||X||Y. */
    uncompressed[0] = 0x04;
    memcpy(uncompressed + 1, pubkey, 64);
    if(mbedtls_ecp_point_read_binary(&grp, &Q, uncompressed,
                                       sizeof uncompressed) != 0) goto out;
    if(mbedtls_ecp_check_pubkey(&grp, &Q) != 0) goto out;
    if(mbedtls_ecdsa_verify(&grp, hash, sizeof hash, &Q, &r, &s) != 0) goto out;
    rc = 0;
out:
    mbedtls_ecp_group_free(&grp);
    mbedtls_ecp_point_free(&Q);
    mbedtls_mpi_free(&r); mbedtls_mpi_free(&s);
    return rc;
}
