/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 SirHumza and CyberMask367 - see NOTICE for attribution terms.
 */
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
 * success, 0 when unusable (caller sends no art). Not thread-safe.
 * "mp:..." input returns itself without a network call. */
int art_resolve_mp(const char *app_id, const char *token, const char *url,
                   char *out_mp, size_t cap);
/* Cache-only lookup: never touches the network. Returns 1 for an "mp:" input
 * or a URL already resolved this boot. Use this on the presence path so an
 * op 3 send can never block on external-assets. */
int art_resolve_mp_cached(const char *url, char *out_mp, size_t cap);
/* Test seam + internal store: record url -> "mp:..." without a network call.
 * Only values beginning "mp:" are accepted. */
void art_cache_put(const char *url, const char *out_mp);
/* Resolve now (network) so a later art_resolve_mp_cached() hits. Safe to call
 * with empty/unresolvable values; failures are logged and ignored. */
void art_prefetch(const char *url);
/* Drop the mp: cache (call on new game sessions: mappings can expire and
 * each title deserves a fresh resolve anyway). */
void art_cache_clear(void);
/* Pure helper (host-tested): extract mp path for url from response. */
int art_parse_mp(const char *body, size_t len, const char *url,
                 char *out, size_t cap);
/* Pure helper (host-tested): a URL safe to interpolate into the JSON POST.
 * Rejects non-http(s), overlong values, quotes, backslashes and control bytes;
 * those either break the body or let a config value inject JSON fields. */
int art_url_ok(const char *url);
#endif
