/* tmdb_crypto.h - Sony TMDB URL builder + response parser (host-testable). */
#ifndef TMDB_CRYPTO_H
#define TMDB_CRYPTO_H
#include <stddef.h>
void tmdb_sha1(const unsigned char *p, size_t n, unsigned char out[20]);
void tmdb_hmac_sha1(const unsigned char *key, size_t klen,
                    const unsigned char *msg, size_t mlen,
                    unsigned char out[20]);
/* Build "/tmdb2/<TID>_00_<HEX>/<TID>_00.json". 0 ok, -1 invalid input. */
int tmdb_path(const char *titleId, char *out, size_t cap);
/* Parse TMDB JSON body into display name + icon URL. 0 ok, -1 no name. */
int tmdb_parse(const char *body, size_t len,
               char *name, size_t name_cap, char *icon, size_t icon_cap);
#endif
