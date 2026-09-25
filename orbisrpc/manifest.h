/* manifest.h - signed release manifest: parse, policy, signature verify.
 *
 * Release channel trust (v1.0.1+):
 *   manifest.json (version, channel, min_version, platform, asset sha256s)
 *   manifest.sig  (raw 64-byte ECDSA P-256 r||s over manifest.json bytes)
 * verified with the embedded release public key (release_pubkey.h).
 * Policy: manifest+valid sig REQUIRED for update; SHA256SUMS is a
 * compat fallback only when no manifest exists; ELF-only is refused.
 */
#ifndef ORBISRPC_MANIFEST_H
#define ORBISRPC_MANIFEST_H
#include <stddef.h>

#define MANIFEST_MAX_ASSETS 8

typedef struct {
    char name[64];
    char sha256[65];
} manifest_asset_t;

typedef struct {
    char version[32];
    char channel[16];
    char min_version[32];
    char platform[32];
    manifest_asset_t assets[MANIFEST_MAX_ASSETS];
    int nassets;
} manifest_t;

/* Parse manifest.json bytes. 0 ok, -1 invalid. */
int manifest_parse(const char *json, size_t len, manifest_t *out);
/* Find asset hash by filename. 0 ok + out_hex, -1 not listed. */
int manifest_find(const manifest_t *m, const char *name, char out_hex[65]);
/* Check data against the listed hash for name. 0 match, -1 mismatch/unlisted. */
int manifest_check(const manifest_t *m, const char *name,
                   const unsigned char *data, size_t n);
/* 1 when manifest version is newer than local, 0 otherwise. */
int manifest_is_newer(const manifest_t *m, const char *local_version);
/* Channel/platform gate: 1 accepted (stable/ps4-goldhen), 0 refused. */
int manifest_gate(const manifest_t *m);
/* Verify raw ECDSA P-256 signature (r||s, 64 bytes) over msg with raw
 * pubkey (X||Y, 64 bytes). 0 valid, -1 invalid/error. */
int manifest_verify_sig(const unsigned char *msg, size_t msglen,
                        const unsigned char sig[64],
                        const unsigned char pubkey[64]);
/* SHA256 of buffer as lowercase hex (64+NUL). 0 ok. */
int manifest_sha256_hex(const unsigned char *data, size_t n, char out[65]);

#endif
