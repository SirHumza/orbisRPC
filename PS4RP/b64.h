/* b64.h - small url-safe/base64 encoder. */
#ifndef B64_H
#define B64_H
#include <stddef.h>
size_t b64_encode(const unsigned char *in, size_t len, char *out);
/* returns bytes written to out (no null) */
#endif