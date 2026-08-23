/* cfg.h - tiny JSON config via jsonlite */
#ifndef CFG_H
#define CFG_H
#include <stdint.h>
#define CFG_PATH "/data/orbisRPC/config.json"
#define LOG_PATH "/data/orbisRPC/log.txt"
#define DATA_DIR "/data/orbisRPC"
typedef struct {
    char token[160];           /* Discord user session token */
    char application_id[32];   /* optional: app id for asset images */
    int enabled;
    int poll_interval_s;       /* game-check cadence */
    char presence_state[128];  /* activity "state" line, e.g. "On PS4" */
} cfg_t;
extern cfg_t g_cfg;
int cfg_load(const char *path, cfg_t *c);
void cfg_save(const char *path, const cfg_t *c);
void cfg_defaults(cfg_t *c);
#endif
