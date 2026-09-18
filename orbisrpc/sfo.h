/* sfo.h - PARAM.SFO TITLE extractor. */
#ifndef SFO_H
#define SFO_H
#include <stddef.h>
/* Returns 0 and writes the TITLE on success, -1 otherwise. Never reads
 * past buf+n. */
int sfo_title(const unsigned char *buf, size_t n, char *out, size_t cap);
#endif
