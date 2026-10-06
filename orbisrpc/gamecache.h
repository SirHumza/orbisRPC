/* gamecache.h - persistent title id -> {name, cover URL} store.
 *
 * Replaces the two caches this used to have:
 *   - config.json's "titles" map (id -> name), written by cfg_learn()
 *   - artwork_cache.json (url -> mp: path), the art disk cache
 *
 * One file, one job: remember what a title id resolved to, so a boot with no
 * network still shows the right name and the right cover. Name AND art live
 * here because art was never persisted anywhere -- s_last_art is RAM-only and
 * is cleared on every title change, so cover art was re-fetched every session.
 *
 * Pure parsing/serialisation is separated from the file I/O so the host tests
 * can drive it without a console, the same split as procwalk.c and bigapp.c.
 */
#ifndef ORBISRPC_GAMECACHE_H
#define ORBISRPC_GAMECACHE_H

#include <stddef.h>
#include <stdint.h>

#define GAMECACHE_PATH "/data/orbisRPC/games_cache.json"
#define GAMECACHE_MAX_ENTRIES 512
#define GAMECACHE_ID_LEN   16
#define GAMECACHE_NAME_LEN 128
#define GAMECACHE_ART_LEN  512
/* Entries older than this are re-resolved rather than trusted forever: a
 * homebrew title can be renamed upstream and a stale name would stick. */
#define GAMECACHE_TTL_SECS (30LL*24*3600)

/* Where a cached entry came from, for diagnostics only. */
typedef struct {
    char id[GAMECACHE_ID_LEN];
    char name[GAMECACHE_NAME_LEN];
    char art[GAMECACHE_ART_LEN];
    int64_t at;
} gamecache_entry_t;

/* --- pure helpers (host-testable) ----------------------------------- */

/* Parse a games_cache.json document. Returns the number of entries loaded, or
 * -1 when the document is unusable. Entries that are malformed are skipped
 * rather than failing the whole load. `max` caps how many are stored.
 * `now` is compared against each entry's "at" to drop stale rows. */
int gamecache_parse(const char *json, size_t len, gamecache_entry_t *out,
                    int max, int64_t now);

/* Serialise entries to a fresh malloc'd NUL-terminated buffer. Caller frees.
 * Returns NULL on OOM. */
char *gamecache_serialize(const gamecache_entry_t *e, int n);

/* A title id is only worth storing if it looks like one and is not an echo of
 * its own name (which is what a failed resolve produces). */
int gamecache_id_valid(const char *id);

/* --- live store ----------------------------------------------------- */

/* Load once, lazily. Safe to call repeatedly. */
void gamecache_load(void);

/* 1 + filled name (and art when the entry has one) for a fresh entry.
 * 0 when absent or stale, so the caller falls through to the network. */
int gamecache_get(const char *id, char *name, size_t name_cap,
                  char *art, size_t art_cap);

/* Insert or update. `src` is informational and stored for diagnostics only.
 * Returns 1 if the store changed (caller may log), 0 if unchanged/full. */
int gamecache_put(const char *id, const char *name, const char *art,
                  const char *src);

/* Flush to disk. Called after puts; failures are logged, never fatal. */
void gamecache_save(void);

/* Forget everything (used by tests and by a manual reset). */
void gamecache_reset(void);

#endif /* ORBISRPC_GAMECACHE_H */