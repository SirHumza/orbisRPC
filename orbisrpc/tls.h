/* tls.h - self-contained TLS client (mbedTLS, statically linked).
 * No PS4 TLS-module dependency: works in payload and plugin processes.
 * Certificate verification is REQUIRED against the curated bundle;
 * see ca_bundle_pem.h. */
#ifndef TLS_H
#define TLS_H
#include <stddef.h>

/* Runs the full TLS handshake over an already-connected socket.
 * Returns heap context, or NULL on failure (logged). */
typedef struct tls_ctx tls_ctx_t;
tls_ctx_t *tls_start(int fd, const char *host);

/* Returns decrypted bytes (>0), 0 if nothing available right now,
 * <0 on close/error. Non-blocking: never waits for the peer. */
int tls_read(tls_ctx_t *t, void *buf, size_t cap);

/* Writes exactly len bytes; returns len or <0. */
int tls_write(tls_ctx_t *t, const void *buf, size_t len);

void tls_free(tls_ctx_t *t);

#endif
