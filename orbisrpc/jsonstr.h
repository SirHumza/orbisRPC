/* jsonstr.h - JSON string helpers shared by the streaming index resolvers.
 *
 * Extracted from retro.c, which had them as file-local statics. custom.c needs
 * exactly the same behaviour -- names and cover URLs carry quotes, backslashes
 * and \u escapes, so raw bytes cannot be copied out -- and duplicating the
 * parser would leave two copies to keep in step.
 *
 * Pure: no allocation, no I/O, no socket. Host-testable directly. */
#ifndef ORBISRPC_JSONSTR_H
#define ORBISRPC_JSONSTR_H

#include <stddef.h>

/* Copy len bytes of JSON string body into dst, decoding escapes. Returns the
 * number of bytes written, always NUL-terminating. */
size_t json_unescape(const char *src, size_t len, char *dst, size_t cap);

/* Copy the JSON string starting at p (which must point at '"'), unescaped.
 * Returns 1 on success, 0 if p is not a quote or the string is unterminated
 * within `end`. */
int json_grab_string(const char *p, const char *end, char *dst, size_t cap);

/* Copy the value of the key whose opening quote is at `key`. Skips to the
 * colon rather than using a fixed offset: the quote sits at +7 after "name":"
 * but +8 after "cover":", so a hardcoded skip silently drops one of the two. */
int json_grab_value(const char *key, const char *end, char *dst, size_t cap);

#endif /* ORBISRPC_JSONSTR_H */