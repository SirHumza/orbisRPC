/* discord.h - gateway session + presence (user session token). */
#ifndef DISCORD_H
#define DISCORD_H
#include "ws.h"
#include "jsonlite.h"
typedef struct {
    ws_t ws;
    char token[512];
    int64_t last_heartbeat;   /* when we last sent op 1 */
    int64_t last_ack;         /* when the gateway last acked (op 11) */
    int64_t hb_interval_ms;
    int seq;                  /* last dispatch sequence (heartbeat payload) */
    int connected;            /* 1 iff state == GW_READY (derived mirror) */
    int sent_hb;              /* at least one heartbeat sent */
    int state;                /* gw_state_t: explicit session state */
} discord_t;
/* Explicit gateway state machine. GW_DOWN is 0 so a zeroed struct starts
 * DOWN; connected==1 exactly when state==GW_READY. Deliberately no
 * RESUME state: we always fresh-IDENTIFY (simpler, stateless recovery). */
typedef enum {
    GW_DOWN = 0,     /* no usable session */
    GW_CONNECTING,   /* TCP+TLS+WebSocket handshake in flight */
    GW_HELLO_WAIT,   /* waiting for op 10 HELLO */
    GW_IDENTIFYING,  /* IDENTIFY sent, waiting for READY */
    GW_READY         /* live session (connected == 1) */
} gw_state_t;
const char *discord_state_name(const discord_t *d);
/* Returns 0 on READY, -1 net/proto error, -2 auth-fatal (close 4004: don't
 * retry — the token is wrong and Discord bans IPs that hammer it). */
int discord_connect(discord_t *d, const char *token);
int discord_set_presence(discord_t *d, const char *state, const char *name,
                         const char *application_id, int64_t started_epoch); /* op 3 */
/* Same + titleId asset key: when application_id is set, sends
 * assets { large_image: "<lower titleId>", large_text: "<name>" } so the
 * game icon shows once uploaded under that name in the Discord app.
 * When art_base_url is set instead, large_image becomes
 * "<base><lower titleId>.png" (external URL assets, no uploads needed). */
int discord_set_presence_ex(discord_t *d, const char *state, const char *name,
                         const char *title_id, const char *application_id,
                         const char *art_base_url, const char *art_url,
                         const char *small_art_url,
                         int64_t started_epoch); /* op 3 */
/* Test seam: pure activity-JSON builder (no sockets). token "" skips mp:. */
jl_val_t *discord_build_activity(const char *state, const char *name,
                         const char *title_id, const char *application_id,
                         const char *art_base_url, const char *art_url,
                         const char *small_art_url,
                         int64_t started_epoch, const char *token);
int discord_clear_presence(discord_t *d);   /* clear activity, stay online */
int discord_tick(discord_t *d);             /* 0 ok; -1 drop/reconnect; -2 auth-fatal; -3 invalid session (retry promptly) */
#endif
