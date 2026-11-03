/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 SirHumza and CyberMask367 - see NOTICE for attribution terms.
 */
/* discord.h - gateway session + presence (user session token). */
#ifndef DISCORD_H
#define DISCORD_H
#include "ws.h"
#include "jsonlite.h"

/* Gateway capabilities. AUTH_TOKEN_REFRESH (1 << 8) is deliberately not
 * requested: when it is set, Discord may return a replacement user token in
 * READY and expects the client to discard the old one. orbisRPC cannot
 * update the desktop client that owns the token, so that rotation logs the
 * desktop out. A presence-only client has no use for the feature. */
#define DISCORD_GW_CAP_AUTH_TOKEN_REFRESH (1u << 8)
#define DISCORD_GW_CAPABILITIES 16125u /* 16381 minus AUTH_TOKEN_REFRESH */

typedef struct {
    ws_t ws;
    char token[512];
    int64_t last_heartbeat;   /* when we last sent op 1 */
    int64_t last_ack;         /* when the gateway last acked (op 11) */
    int64_t hb_interval_ms;
    int seq;                  /* last dispatch sequence (heartbeat payload) */
    int connected;
    int sent_hb;              /* at least one heartbeat sent */
    /* Session recovery. Captured from READY; empty means the next connect
     * must be a fresh IDENTIFY. */
    char session_id[64];
    char resume_url[192];
} discord_t;
/* Returns 0 on READY, -1 net/proto error, -2 auth-fatal (close 4004: don't
 * retry — the token is wrong and Discord bans IPs that hammer it). */
/* Connects and authenticates. The caller must ws_close() any previous
 * session first: this function zeroes the whole struct, so a live handle
 * passed in would leak its socket and TLS context. */
int discord_connect(discord_t *d, const char *token);
/* Reconnect to the session captured from READY (op 6). Returns 0 when the
 * gateway answered RESUMED, -1 on a transient/network error (session kept),
 * -2 when the token was rejected, -3 when the session is dead and the
 * caller must do a fresh discord_connect(). */
int discord_resume(discord_t *d, const char *token);
int discord_can_resume(const discord_t *d);
void discord_forget_session(discord_t *d);
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
/* Base64 JSON for X-Super-Properties, built from g_cfg so gateway and REST
 * agree. Returns encoded length, 0 on failure. */
int discord_super_properties(char *out, size_t cap);
/* Recent gateway close codes, oldest first ("4004,1006"). Used by the
 * diagnostic bundle; never contains account data. */
void discord_close_ring_text(char *out, size_t cap);
/* Lowercase titleId into the icon-pack/uploaded-asset key. Returns key
 * length, 0 on bad input. */
size_t discord_asset_key(const char *title_id, char *key, size_t cap);
int discord_tick(discord_t *d);             /* 0 ok; -1 drop/reconnect; -2 auth-fatal; -3 invalid session (retry promptly) */
#endif
