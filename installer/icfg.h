/* icfg.h - installer config read/write for /data/orbisRPC/config.json.
 * Read-modify-write: preserves learned titles and every other field.
 * Pure logic (token_valid) is host-tested.
 *
 * Everything here is compiled as C. The extern "C" block is not decoration: the
 * Settings page includes this from main.cpp, and without it a call from C++ looks
 * for a C++-mangled name and fails at link time with an undefined symbol, naming
 * a function that plainly does exist. */
#ifndef INSTALLER_ICFG_H
#define INSTALLER_ICFG_H
#include <stddef.h>

/* ICFG_PATH lives in paths.h, with every other on-console path, so that install
 * and uninstall cannot end up disagreeing about where the config is. */
#include "paths.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 1 valid user token, 0 not. Tokens are dot-separated base64-ish,
 * 40..150 chars. Rejects placeholders and whitespace. */
int token_valid(const char *t);

/* Load current token ("" when missing). 0 ok, -1 unreadable. */
int icfg_token_load(const char *path, char *out, size_t cap);
/* Read a string/int key. 0 found, -1 missing/unreadable. */
int icfg_get_str(const char *path, const char *key, char *out, size_t cap);
int icfg_get_int(const char *path, const char *key, long *out);

/* ---- carry-over across a config overwrite --------------------------
 * The installer replaces config.json wholesale, which would discard every
 * user-set option. These two calls bracket the replacement: capture the
 * listed keys from the outgoing file, then write them back into the new
 * one. Keys absent from the outgoing file are simply not carried.
 *
 * Only scalars carry over (string / int / bool). An object or array value
 * is reported as absent rather than being flattened -- these are user
 * options, not nested state. */

/* Fixed capacity so the caller can pass a plain array with no allocation. */
#define ICFG_CARRY_MAX 16
#define ICFG_KEY_MAX  40
#define ICFG_VAL_MAX  192

/* Mirrors jl_type_t for the scalars that can carry. Values come from
 * jsonlite.h so the two cannot drift; the installer never includes that
 * header directly. */
enum icfg_val_type {
    ICFG_T_ABSENT = 0,
    ICFG_T_STRING,
    ICFG_T_INT,
    ICFG_T_DOUBLE,
    ICFG_T_BOOL
};

typedef struct {
    char key[ICFG_KEY_MAX];
    int  type;      /* enum icfg_val_type; ABSENT = not carried */
    long inum;
    double num;
    int  boolean;
    char str[ICFG_VAL_MAX];
} icfg_carry_t;

/* Reads up to `cap` keys (NULL-terminated array of names) from `path`.
 * Always fills the caller's slots -- absent keys get present = 0 -- so a
 * partially unreadable file cannot leave the array holding garbage.
 * Returns the number of keys actually found. Host-tested. */
int icfg_carry_capture(const char *path, const char *const *keys,
                       icfg_carry_t *out, int cap);

/* Writes captured values into `path`, preserving everything else in it.
 * Skips entries with present == 0. Returns the number written, or -1 if
 * the file could not be read or written at all. */
int icfg_carry_apply(const char *path, const icfg_carry_t *vals, int count);

#ifdef __cplusplus
}
#endif

#endif
