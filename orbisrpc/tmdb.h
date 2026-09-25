/* tmdb.h - Sony TMDB runtime title/icon resolver (PS4 transport + cache). */
#ifndef TMDB_H
#define TMDB_H
#include <stddef.h>
/* Resolve titleId (e.g. "CUSA00740") to official display name and icon
 * URL via Sony's TMDB service. Results cached in memory. Returns 0 with
 * name set on success (icon may stay empty), -1 on any failure.
 * Bounded: single attempt, ~10s worst case, no retries (caller falls
 * through to the next name source). */
int tmdb_resolve(const char *titleId, char *name, size_t name_cap,
                 char *icon, size_t icon_cap);
#endif
