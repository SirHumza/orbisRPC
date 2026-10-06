/* cfg.h - tiny JSON config via jsonlite */
#ifndef CFG_H
#define CFG_H
#include <stdint.h>
#include <stddef.h>
#define CFG_PATH "/data/orbisRPC/config.json"
#define LOG_PATH "/data/orbisRPC/log.txt"
#define DATA_DIR "/data/orbisRPC"
/* Config schema version. Stamp on load so re-saves converge; unknown
 * future versions load defensively (known fields only). */
#define CFG_SCHEMA_VERSION 1
/* User-local title overrides: {"CUSA11995": "Marvel's Spider-Man"}.
 * Lives in the user's own config.json (never shipped) for games no
 * on-box source can name (no pronunciation.xml, TMDB blocked, ...).
 * Fixed-size tables: no malloc, PS4-safe. */
#define CFG_MAX_TITLES 64
#define CFG_TITLEID_LEN 16
#define CFG_TITLENAME_LEN 128
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
    int enabled;
    int pkgzone_enabled;   /* ask pkg-zone.com for homebrew names (3rd party) */
    int retro_enabled;    /* ask the retro-games indexes for retro/homebrew */
    /* Visibility gates. Each suppresses one category of presence; the
     * daemon still detects it, it just does not tell Discord about it.
     * All default to 1, so a config that predates them behaves as before. */
    int show_firmware;   /* 0: omit the "Firmware X.YY" state line */
    int show_idle;       /* 0: no presence on home / Settings / browser */
    int show_media;      /* 0: media apps are not posted at all */
    int show_homebrew;   /* 0: pkg-zone homebrew not posted; retro is
                              * deliberately unaffected (see is_homebrew_id) */
    int auto_update;       /* check GitHub releases once per boot, stage newer */
    int debug;             /* verbose debug logging (per-poll detail, no secrets) */
    int poll_interval_s;   /* game-check cadence */
    char presence_state[128];  /* legacy: "On PS4". If changed from the default it
                                * overrides presence_details_game, so existing
                                * customisations keep working. */
    char presence_details_game[128];      /* line 2 while a game is posted */
    char presence_details_home[128];      /* line 2 on the home screen */
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
} cfg_t;
extern cfg_t g_cfg;
int cfg_load(const char *path, cfg_t *c);
/* Atomic save (tmp + fsync + rename). 0 saved, -1 failed (caller must
 * surface, not assume). */
int cfg_save(const char *path, const cfg_t *c);
void cfg_defaults(cfg_t *c);
/* User override lookup: 0 + name copied when titleId has an entry. */
int cfg_title(const cfg_t *c, const char *titleId, char *out, size_t cap);
/* Learn an authoritatively resolved name (1 = changed, save it). */
int cfg_learn(cfg_t *c, const char *titleId, const char *name);
#endif
