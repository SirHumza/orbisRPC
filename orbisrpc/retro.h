/* retro.h - homebrew/retro title names and covers from retro-games.cybermask.
 *
 * Three JSON indexes, one per platform, checked in order. This exists because
 * Sony's TMDB endpoint only knows retail CUSA titles, so a retro or homebrew
 * title id has no on-box source and no pkg-zone entry.
 *
 * Fetched, not parsed: each index is ~1-1.5 MB and jsonlite would allocate a
 * node per value, costing megabytes of heap for a single title lookup. The
 * entries are flat and uniform --
 *
 *     {"id":"CPCS00701","name":"DINO CRISIS 5TH ANNESSARY","cover":"https://..."}
 *
 * -- so the stream is scanned for the wanted id and the fields beside it are
 * copied out. The response is never held whole.
 */
#ifndef ORBISRPC_RETRO_H
#define ORBISRPC_RETRO_H

#include <stddef.h>

/* Longest name in the three indexes is 164 bytes; the extra room keeps the
 * widest title intact instead of slicing it. */
#define RETRO_MAX_NAME 176
#define RETRO_MAX_URL  320
/* Ids run to 20 characters -- far past the PS4's 9 -- so match on the 4-char
 * prefix and carry the whole id through. */
#define RETRO_MAX_ID   24

/* 1 when this id's 4-character prefix belongs to any retro platform. */
int retro_wants(const char *title_id);

/* Pure entry extractor: pull the entry whose "id" matches `pat` ("id":"XXXX")
 * out of a body region. Test seam — no sockets, so the host suite drives this
 * directly instead of the fetch. Returns 1 when a name was recovered. */
int retro_extract(const char *p, size_t len, const char *pat,
                  char *name, size_t name_cap,
                  char *url, size_t url_cap);

/* Which platform indexes this prefix could be in, in check order. Returns the
 * count and fills `out` with indices into the PS1/PS2/PSP list. */
int retro_candidates_for(const char *title_id, unsigned char *out);

/* Resolve a title id to its name and cover URL.
 * Returns 0 on a hit (name always filled; url empty when the entry has none),
 * -1 on a miss or any fetch/parse failure. Never partially resolves: a title
 * either gets a real name or none at all, so the caller never posts a name
 * with no cover behind a stale cached one. */
int retro_resolve(const char *title_id, char *name, size_t name_cap,
                  char *url, size_t url_cap);

#endif /* ORBISRPC_RETRO_H */