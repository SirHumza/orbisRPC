/* art.h - Discord external-asset (mp:) resolution for artwork.
 *
 * Verified on hardware 2026-09-23: raw https URLs and dangling asset keys
 * in large_image either drop the whole activity or render "?". The working
 * path used by every functional tool: POST the image URL to
 * /applications/{app}/external-assets, use the returned mp: path.
 * Results cached per boot (one call per title). */
#ifndef ORBISRPC_ART_H
#define ORBISRPC_ART_H
#include <stddef.h>
/* Resolve url -> mp: path. Returns 1 and fills out_mp ("mp:..." NUL) on
 * success, 0 when unusable (caller sends no art). Not thread-safe. */
int art_resolve_mp(const char *app_id, const char *token, const char *url,
                   char *out_mp, size_t cap);
/* Drop the mp: cache (call on new game sessions: mappings can expire and
 * each title deserves a fresh resolve anyway). */
void art_cache_clear(void);
/* Pure helper (host-tested): extract mp path for url from response. */
int art_parse_mp(const char *body, size_t len, const char *url,
                 char *out, size_t cap);
#endif
