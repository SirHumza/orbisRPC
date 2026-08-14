/* cfg.h - /data/orbisRPC/config.json load/save */
#ifndef CFG_H
#define CFG_H
#include <stdint.h>
#define CFG_PATH "/data/orbisRPC/config.json"
#define LOG_PATH "/data/orbisRPC/log.txt"
#define DATA_DIR "/data/orbisRPC"
typedef struct {
    char client_id[64];        /* Discord application client id */
    char client_secret[128];   /* only needed to exchange code / refresh */
    char access_token[256];    /* current bearer */
    char refresh_token[256];   /* long-lived refresh */
    int64_t token_expires_at;  /* epoch seconds */
    char auth_code[256];       /* pasted authorization code (one-time) */
    int enabled;
    int poll_interval_s;       /* game-check / presence refresh cadence */
    char presence_state[128];   /* e.g. "In game" or custom */
} cfg_t;
extern cfg_t g_cfg;
int cfg_load(const char *path, cfg_t *c);
void cfg_save(const char *path, const cfg_t *c);
void cfg_defaults(cfg_t *c);
#endif