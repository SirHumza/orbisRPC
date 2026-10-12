/* custom.h - community-contributed title names, for apps nothing else knows.
 *
 * See custom.h's companion comment in custom.c for why this exists alongside
 * pkg-zone and the retro indexes. */
#ifndef ORBISRPC_CUSTOM_H
#define ORBISRPC_CUSTOM_H

#include <stddef.h>

#define CUSTOM_MAX_NAME 176   /* matches retro: long titles are not truncated */
#define CUSTOM_MAX_URL  320   /* matches retro: cover URLs can be long */

/* Resolve a TITLEID against the community list.
 *
 * Writes the display name and, when the entry carries one, a cover URL.
 * Returns 0 on a hit, -1 on a miss or any fetch failure. Never fatal: every
 * failure path leaves the caller's buffers empty so the raw title id is used. */
int custom_resolve(const char *title_id, char *name, size_t name_cap,
                   char *url, size_t url_cap);

/* Pull the entry for `pat` ("id":"XXXX") out of a body region. Pure, so the
 * host tests can drive it without a socket. Returns 1 when a usable name was
 * recovered, 0 otherwise. Both buffers are cleared first: on a miss the
 * caller must not be left holding the previous title's name. */
int custom_extract(const char *p, size_t len, const char *pat,
                   char *name, size_t name_cap,
                   char *url, size_t url_cap);

#endif /* ORBISRPC_CUSTOM_H */