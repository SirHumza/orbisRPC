/* discord.h - gateway session + presence (user session token). */
#ifndef DISCORD_H
#define DISCORD_H
#include "ws.h"
#include "jsonlite.h"
typedef struct {
    ws_t ws;
    char token[512];
    char qos_token[512];
    int64_t last_heartbeat;   /* when we last sent op 1 */
    int64_t last_ack;         /* when the gateway last acked (op 11) */
    int64_t hb_interval_ms;
    int seq;                  /* last dispatch sequence (heartbeat payload) */
    int connected;
    int sent_hb;              /* at least one heartbeat sent */
    int64_t presence_sent_at[5];
    unsigned presence_next;
    size_t pending_presence_len;
    char pending_presence[16385];
} discord_t;
/* Returns 0 on READY, -1 network/protocol error, -2 explicit auth rejection
 * (close 4004). */
int discord_connect(discord_t *d, const char *token, const char *qos_token);
/* Pure Identify payload builder, exposed for host regression tests. */
jl_val_t *discord_build_identify(const char *token, const char *qos_token);
jl_val_t *discord_build_heartbeat(int seq);
int discord_set_presence(discord_t *d, const char *state, const char *name,
                         const char *application_id, int64_t started_epoch); /* op 3 */
/* Same + titleId asset key: when application_id is set, sends
 * assets { large_image: "<lower titleId>", large_text: "<name>" } so the
 * game icon shows once uploaded under that name in the Discord app.
 * When art_base_url is set instead, large_image becomes
 * "<base><lower titleId>.png" (external URL assets, no uploads needed).
 * `details` is the second visible line; NULL or "" omits it. */
int discord_set_presence_ex(discord_t *d, const char *state, const char *name,
                          const char *details,
                          const char *asset_idle, const char *asset_playing,
                         const char *title_id, const char *application_id,
                         const char *art_base_url, const char *art_url,
                         const char *small_art_url,
                         int64_t started_epoch); /* op 3 */
/* Test seam: pure activity-JSON builder (no sockets). token "" skips mp:. */
jl_val_t *discord_build_activity(const char *state, const char *name,
                          const char *details,
                          const char *asset_idle, const char *asset_playing,
                         const char *title_id, const char *application_id,
                         const char *art_base_url, const char *art_url,
                         const char *small_art_url,
                         int64_t started_epoch, const char *token);
int discord_clear_presence(discord_t *d);   /* clear activity, stay online */
int discord_tick(discord_t *d);             /* 0 ok; -1 drop/reconnect; -2 auth-fatal; -3 invalid session (retry promptly) */
/* Copy and consume a gateway-provided token rotation, if one was observed.
 * Returns 1 when copied, 0 when none is pending, -1 if the output is too small. */
int discord_take_rotated_token(discord_t *d, char *out, size_t cap);
int discord_take_qos_token(discord_t *d, char *out, size_t cap);
/* Clear credential copies after a session ends. */
void discord_clear_credentials(discord_t *d);
#endif
