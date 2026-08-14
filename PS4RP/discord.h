/* discord.h - gateway session + presence. */
#ifndef DISCORD_H
#define DISCORD_H
#include "ws.h"
typedef struct {
    ws_t ws;
    int32_t seq;
    char session_id[64];
    char token[256];
    int64_t last_heartbeat;
    int64_t hb_interval_ms;
    int connected;
} discord_t;
int discord_connect(discord_t *d, const char *token, const char *intent); /* intent = "activities" or "rpc.activities" */
int discord_set_presence(discord_t *d, const char *state, const char *details,
                         int party_size, int party_max, const char *large_image,
                         const char *large_text);      /* presence on the bot/user */
int discord_clear_presence(discord_t *d);               /* clear activity */
int discord_tick(discord_t *d);                          /* heartbeat + keep-alive; returns <0 to reconnect */
int discord_identify(discord_t *d, const char *token);  /* sends IDENTIFY using stored token */
int discord_reconnect(discord_t *d);
#endif