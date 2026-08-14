/* discord.h - gateway session + presence. */
#ifndef DISCORD_H
#define DISCORD_H
#include "ws.h"
typedef struct {
    ws_t ws;
    int32_t seq;              /* last dispatch sequence (for RESUME) */
    char session_id[64];
    char token[256];
    int64_t last_heartbeat;   /* when we last sent op 1 */
    int64_t last_ack;         /* when the gateway last acked (op 11) */
    int64_t hb_interval_ms;
    int connected;
} discord_t;
/* resume=1 reuses the stored session_id/seq (RESUME op 6), else IDENTIFY. */
int discord_connect(discord_t *d, const char *token, int resume);
int discord_set_presence(discord_t *d, const char *state, const char *details,
                         int party_size, int party_max, const char *large_image,
                         const char *large_text);      /* presence update (op 3) */
int discord_clear_presence(discord_t *d);               /* clear activity */
int discord_tick(discord_t *d);                          /* heartbeat + keep-alive; returns <0 to reconnect */
int discord_reconnect(discord_t *d);                     /* RESUME if possible, else fresh IDENTIFY */
#endif