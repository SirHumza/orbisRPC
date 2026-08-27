/* b64.h - standard RFC 4648 Base64 encoder. */
#ifndef B64_H
#define B64_H
#include <stddef.h>
size_t b64_encode(const unsigned char *in, size_t len, char *out);
/* Writes a NUL-terminated string to out and returns its encoded length.
 * The caller must provide at least 4 * ((len + 2) / 3) + 1 bytes. */
#endif