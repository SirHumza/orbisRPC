/* http.h - OAuth token exchange via SceHttp */
#ifndef HTTP_H
#define HTTP_H
#include <stdint.h>
#include <stddef.h>
/* POST to token endpoint; fills access_token/refresh_token/expires_in. Returns 0 ok. */
int http_oauth_token(const char *client_id, const char *client_secret,
                     const char *code, const char *refresh_token,
                     char *access_token, size_t at_cap,
                     char *refresh_out, size_t rt_cap,
                     int64_t *expires_out_epoch);
/* simple GET into caller buffer */
int http_get(const char *url, char *out, size_t cap);
#endif