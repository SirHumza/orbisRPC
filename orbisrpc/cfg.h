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
    int auto_update;       /* check GitHub releases once per boot, stage newer */
    int debug;             /* verbose debug logging (per-poll detail, no secrets) */
    int poll_interval_s;   /* game-check cadence */
    char presence_state[128];  /* activity "state" line, e.g. "On PS4" */
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
