/* discord.h - gateway session + presence (user session token). */
#ifndef DISCORD_H
#define DISCORD_H
#include "ws.h"
typedef struct {
    ws_t ws;
    char token[160];
    int64_t last_heartbeat;   /* when we last sent op 1 */
    int64_t last_ack;         /* when the gateway last acked (op 11) */
    int64_t hb_interval_ms;
    int seq;                  /* last dispatch sequence (heartbeat payload) */
    int connected;
    int sent_hb;              /* at least one heartbeat sent */
} discord_t;
/* Returns 0 on READY, -1 net/proto error, -2 auth-fatal (close 4004: don't
 * retry — the token is wrong and Discord bans IPs that hammer it). */
int discord_connect(discord_t *d, const char *token);
int discord_set_presence(discord_t *d, const char *state, const char *name,
                         const char *application_id, int64_t started_epoch); /* op 3 */
int discord_clear_presence(discord_t *d);   /* clear activity, stay online */
int discord_tick(discord_t *d);             /* 0 ok; -1 drop/reconnect; -2 auth-fatal */
#endif
