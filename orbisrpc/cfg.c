/* cfg.c - tiny JSON config via jsonlite */
#include "cfg.h"
#include "jsonlite.h"
#include "log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

cfg_t g_cfg;

void cfg_defaults(cfg_t *c) {
    memset(c, 0, sizeof(*c));
    c->enabled = 1;
    c->poll_interval_s = 12;
    strncpy(c->client_id, "SET_ME", sizeof(c->client_id)-1);
    strncpy(c->presence_state, "On PS4", sizeof(c->presence_state)-1);
}

/* keep the daemon sane if the user puts junk in config */
static void clamp_cfg(cfg_t *c){
    if(c->poll_interval_s < 5)  c->poll_interval_s = 5;
    if(c->poll_interval_s > 300) c->poll_interval_s = 300;
}

int cfg_load(const char *path, cfg_t *c) {
    cfg_defaults(c);
    FILE *f = fopen(path, "rb");
    if (!f) { log_msg("config not found at %s; defaults applied", path); return -1; }
    fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
    if (sz <= 0 || sz > 1<<20) { fclose(f); return -1; }
    char *buf = (char *)malloc((size_t)sz + 1);
    if (!buf) { fclose(f); return -1; }
    size_t n = fread(buf, 1, (size_t)sz, f); fclose(f);
    buf[n] = 0;
    jl_val_t *root = jl_parse(buf, n);
    free(buf);
    if (!root) { log_msg("config parse failed; using defaults"); return -1; }
    const jl_val_t *o;
#define STR(k,f) do { o=jl_obj_get(root,k); if(o&&o->type==JL_STRING) strncpy(c->f,o->str,sizeof(c->f)-1); } while(0)
    STR("client_id", client_id);
    STR("client_secret", client_secret);
    STR("access_token", access_token);
    STR("refresh_token", refresh_token);
    STR("auth_code", auth_code);
    STR("presence_state", presence_state);
#undef STR
    o = jl_obj_get(root, "token_expires_at"); if (o && o->type == JL_NUMBER) c->token_expires_at = (int64_t)o->num;
    o = jl_obj_get(root, "enabled");         if (o && o->type == JL_BOOL)   c->enabled = (int)o->num;
    o = jl_obj_get(root, "poll_interval_s"); if (o && o->type == JL_NUMBER) c->poll_interval_s = (int)o->num;
    jl_free(root);
    clamp_cfg(c);
    return 0;
}

void cfg_save(const char *path, const cfg_t *c) {
    jl_val_t *r = jl_new_object();
    jl_obj_set(r, "client_id",        jl_new_string(c->client_id));
    jl_obj_set(r, "client_secret",    jl_new_string(c->client_secret));
    jl_obj_set(r, "access_token",     jl_new_string(c->access_token));
    jl_obj_set(r, "refresh_token",    jl_new_string(c->refresh_token));
    jl_obj_set(r, "token_expires_at", jl_new_number((double)c->token_expires_at));
    jl_obj_set(r, "auth_code",        jl_new_string(c->auth_code));
    jl_obj_set(r, "enabled",          jl_new_bool(c->enabled));
    jl_obj_set(r, "poll_interval_s",  jl_new_number((double)c->poll_interval_s));
    jl_obj_set(r, "presence_state",   jl_new_string(c->presence_state));
    char *s = jl_stringify(r);
    /* write tmp + rename so a power loss can't corrupt the config */
    char tmp[160];
    snprintf(tmp, sizeof tmp, "%s.tmp", path);
    FILE *f = fopen(tmp, "wb");
    if (f) { fputs(s, f); fclose(f); rename(tmp, path); }
    else { log_msg("cfg_save: cannot write %s", tmp); }
    free(s); jl_free(r);
}