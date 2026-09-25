/* updater_http.h - pure HTTP response parsing for the updater.
 *
 * Host-testable (no PS4 includes): status line, bounded headers,
 * Content-Length, Transfer-Encoding: chunked decoding, Location capture.
 * The socket/TLS/redirect-policy half stays in updater.c (PS4-only). */
#ifndef ORBISRPC_UPDATER_HTTP_H
#define ORBISRPC_UPDATER_HTTP_H
#include <stddef.h>

#define UPD_HDR_MAX 8192

/* Parse a complete raw HTTP response (rawlen bytes, NUL-terminated at
 * raw[rawlen]). Returns a heap body (caller frees, NUL-terminated) with
 * out_len, or NULL on any malformed/truncated/over-cap input.
 * out_status always set (0 when no valid status line); out_location gets
 * the redirect target ("" when none). Only the decoded body counts against
 * cap; headers are separately bounded by UPD_HDR_MAX. */
char *upd_parse_response(const char *raw, size_t rawlen, size_t cap,
                         int *out_status, size_t *out_len,
                         char *out_location, size_t loc_cap);
#endif
