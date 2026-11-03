/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 SirHumza and CyberMask367 - see NOTICE for attribution terms.
 */
/* cfg.h - tiny JSON config via jsonlite */
#ifndef CFG_H
#define CFG_H
#include <stdint.h>
#include <stddef.h>
#include "jsonlite.h"
#define CFG_PATH "/data/orbisRPC/config.json"
#define LOG_PATH "/data/orbisRPC/log.txt"
#define DATA_DIR "/data/orbisRPC"
/* Config schema version. Stamp on load so re-saves converge; unknown
 * future versions load defensively (known fields only) and are not
 * rewritten by an older build. v2 only adds the explicit stamp plus the
 * pre-migration backup; the layout is unchanged from v1. */
#define CFG_SCHEMA_VERSION 2
/* User-local title overrides: {"CUSA11995": "Marvel's Spider-Man"}.
 * Lives in the user's own config.json (never shipped) for games no
 * on-box source can name (no pronunciation.xml, TMDB blocked, ...).
 * Fixed-size tables: no malloc, PS4-safe. */
#define CFG_MAX_TITLES 64
#define CFG_MAX_RULES 64
#define CFG_TITLEID_LEN 16
#define CFG_TITLENAME_LEN 128
/* Per-title behaviour, keyed by title id:
 *   "title_rules": {"CUSA11995": {"disabled": true,
 *                                 "details": "Swinging around",
 *                                 "art": "https://.../spidey.png"}}
 * All three keys are optional. `disabled` suppresses presence for that
 * title; the daemon still detects it, so nothing is hidden from the log. */
typedef struct {
    char title_id[CFG_TITLEID_LEN];
    char details[CFG_TITLENAME_LEN];
    char art[256];
    int disabled;
    int duration_min;
} cfg_title_rule_t;
typedef struct {
    int schema_version;
    char token[512];           /* Discord user session token */
    char application_id[64];   /* optional: app id for uploaded asset images */
    char art_base_url[256];    /* optional: icon pack base URL, e.g.
                                * https://raw.githubusercontent.com/.../icons/
                                * large_image becomes <base><lower titleId>.png */
    char home_art[256];        /* optional: idle/home tile art. http(s) URL
                                * (mp:-proxied) or uploaded Discord asset key.
                                * Default: project-hosted PlayStation logo.
                                * Empty falls back to <art_base_url>home.png. */
    int n_titles;
    char title_ids[CFG_MAX_TITLES][CFG_TITLEID_LEN];
    char title_names[CFG_MAX_TITLES][CFG_TITLENAME_LEN];
    int n_rules;
    cfg_title_rule_t title_rules[CFG_MAX_RULES];
    int enabled;
    int pkgzone_enabled;   /* ask pkg-zone.com for homebrew names (3rd party) */
    int retro_enabled;    /* ask the retro-games indexes for retro/homebrew */
    /* Visibility gates. Each suppresses one category of presence; the
     * daemon still detects it, it just does not tell Discord about it.
     * All default to 1, so a config that predates them behaves as before. */
    int show_firmware;   /* 0: omit the "Firmware X.YY" state line */
    int show_logo;       /* 0: omit the PlayStation badge and idle logo */
    int show_idle;       /* 0: no presence on home / Settings / browser */
    int show_media;      /* 0: media apps are not posted at all */
    int show_homebrew;   /* 0: pkg-zone homebrew not posted; retro is
                              * deliberately unaffected (see is_homebrew_id) */
    char status[16];     /* Discord status: online, idle, dnd or invisible */
    char timestamp_mode[16]; /* elapsed or none */
    int auto_update;       /* check GitHub releases once per boot, stage newer */
    int debug;             /* verbose debug logging (per-poll detail, no secrets) */
    int poll_interval_s;   /* game-check cadence */
    int presence_reconcile_s; /* periodic presence re-assert (60..3600) */
    int presence_hold_s;   /* keep the last tile this long after a game closes
                            * (0..1800, default 300); the timer still resets */
    /* Recovery / lifecycle timings. Defaults match the 1.1 built-ins, so a
     * config without these keys behaves exactly as before. */
    int lock_heartbeat_s;  /* touch the lock this often (default 60) */
    int lock_stale_s;      /* reclaim a lock silent this long (default 600,
                            * forced to at least 2x lock_heartbeat_s) */
    int reconnect_max_s;   /* slow-cadence cap after repeated failures
                            * (default 600, clamped 60..3600) */
    int timesync_interval_s; /* NTP refresh cadence (default 3600) */
    int log_max_kb;        /* log rotation size in KiB (default 512) */
    char presence_state[128];  /* legacy: "On PS4". If changed from the default it
                                * overrides presence_details_game, so existing
                                * customisations keep working. */
    char presence_details_game[128];      /* line 2 while a game is posted */
    char presence_details_home[128];      /* line 2 on the home screen */
    char presence_details_media[128];     /* line 2 for Watching/Listening */
    char presence_settings_text[128];     /* line 2 while Settings is in front */
    char asset_idle[64];    /* large image on home + settings */
    char asset_playing[64]; /* small badge while playing/watching */
    /* Large image (home + settings) and the game badge, as URLs.
     * Both go through Discord's mp: external-assets proxy: raw https
     * in large_image renders "?" or drops the whole activity.
     * Fall back: large_art -> home_art, small_art -> large_art. */
    char large_art[256];
    char small_art[256];
    char browser_art[256];   /* large image while the web browser is up */
    /* Client fingerprint reported to the gateway and to REST. These are the
     * fields a real desktop client sends and a hand-rolled client usually
     * omits, which is what makes it identifiable as automation. They go
     * stale when Discord ships a new stable build, so they are config, not
     * constants: update them without rebuilding the payload. */
    char fp_client_version[32];      /* "1.0.9059" */
    char fp_client_build[16];        /* web BUILD_NUMBER; must match stable */
    char fp_browser_version[32];     /* Chromium version, same as the UA */
    char fp_os[16];                  /* "Windows" */
    char fp_os_version[32];          /* "10.0.22631" */
    char fp_os_sdk_version[16];      /* "10" */
    char fp_arch[16];                /* "x64" */
    char fp_locale[16];              /* "en-US" */
    char fp_timezone[48];            /* "Europe/London" */
    char fp_user_agent[256];         /* full desktop UA for this build */
    char fp_installation_id[40];     /* stable per-console UUID */
} cfg_t;
extern cfg_t g_cfg;
int cfg_load(const char *path, cfg_t *c);
/* Bring a parsed config document up to CFG_SCHEMA_VERSION. `from` is the
 * on-disk schema (1 when the key is absent). Returns 0 when the document is
 * writable by this build, -1 when it is malformed or came from a newer
 * schema (the caller must not rewrite it). */
int cfg_migrate(jl_val_t *root, int from);
/* Atomic save (tmp + fsync + rename). 0 saved, -1 failed (caller must
 * surface, not assume). Before the write the current file is copied to
 * <path>.bak; a pre-versioned file also gets a one-time <path>.v1.bak. */
int cfg_save(const char *path, const cfg_t *c);
void cfg_defaults(cfg_t *c);
/* User override lookup: 0 + name copied when titleId has an entry. */
int cfg_title(const cfg_t *c, const char *titleId, char *out, size_t cap);
/* Per-title rule, or NULL when the title has none. */
const cfg_title_rule_t *cfg_title_rule(const cfg_t *c, const char *titleId);
/* Learn an authoritatively resolved name (1 = changed, save it). */
int cfg_learn(cfg_t *c, const char *titleId, const char *name);
/* Fill fp_installation_id if empty and persist it. An installation id that
 * changes every boot is itself a signal, so it is generated once and saved.
 * Returns 1 when it generated (and saved) a new id, 0 if one was present. */
int cfg_ensure_installation_id(cfg_t *c);
#endif
