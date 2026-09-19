/* cfg.c - tiny JSON config via jsonlite */
#include "cfg.h"
#include "jsonlite.h"
#include "log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

cfg_t g_cfg;

void cfg_defaults(cfg_t *c) {
    if(!c) return;
    memset(c, 0, sizeof(*c));
    c->enabled = 1;
    c->poll_interval_s = 12;
    strncpy(c->token, "SET_ME", sizeof(c->token)-1);
    strncpy(c->presence_state, "On PS4", sizeof(c->presence_state)-1);
}

/* keep the daemon sane if the user puts junk in config */
static void clamp_cfg(cfg_t *c){
    if(c->poll_interval_s < 5)  c->poll_interval_s = 5;
    if(c->poll_interval_s > 300) c->poll_interval_s = 300;
}

int cfg_load(const char *path, cfg_t *c) {
    if(!path || !c) return -1;
    cfg_defaults(c);
    FILE *f = fopen(path, "rb");
    if (!f) { log_msg("config not found at %s; defaults applied", path); return -1; }
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return -1; }
    long sz = ftell(f);
    if (sz < 0 || fseek(f, 0, SEEK_SET) != 0) { fclose(f); return -1; }
    if (sz <= 0 || sz > 1<<20) { fclose(f); return -1; }
    char *buf = (char *)malloc((size_t)sz + 1);
    if (!buf) { fclose(f); return -1; }
    size_t n = fread(buf, 1, (size_t)sz, f); fclose(f);
    if(n != (size_t)sz){ free(buf); log_msg("config read failed at %s", path); return -1; }
    buf[n] = 0;
    jl_val_t *root = jl_parse(buf, n);
    free(buf);
    if (!root) { log_msg("config parse failed; using defaults"); return -1; }
    const jl_val_t *o;
#define STR(k,f) do { \
        o=jl_obj_get(root,k); \
        if(o&&o->type==JL_STRING){ \
            strncpy(c->f,o->str,sizeof(c->f)-1); \
            c->f[sizeof(c->f)-1]=0; \
        } \
    } while(0)
    STR("token", token);
    STR("application_id", application_id);
    STR("art_base_url", art_base_url);
    STR("presence_state", presence_state);
#undef STR
    o = jl_obj_get(root, "enabled");         if (o && o->type == JL_BOOL)   c->enabled = (int)o->num;
    o = jl_obj_get(root, "poll_interval_s"); if (o && o->type == JL_NUMBER) c->poll_interval_s = (int)o->num;
    jl_free(root);
    clamp_cfg(c);
    return 0;
}

void cfg_save(const char *path, const cfg_t *c) {
    if(!path || !c) return;
    jl_val_t *r = jl_new_object();
    if(!r) { log_msg("cfg_save: allocation failed"); return; }
    jl_obj_set(r, "token",           jl_new_string(c->token));
    jl_obj_set(r, "application_id",  jl_new_string(c->application_id));
    jl_obj_set(r, "art_base_url",    jl_new_string(c->art_base_url));
    jl_obj_set(r, "enabled",         jl_new_bool(c->enabled));
    jl_obj_set(r, "poll_interval_s", jl_new_number((double)c->poll_interval_s));
    jl_obj_set(r, "presence_state",  jl_new_string(c->presence_state));
    char *s = jl_stringify(r);
    if(!s){ log_msg("cfg_save: serialization failed"); jl_free(r); return; }
    /* write tmp + fsync + rename so a power loss can't corrupt the config */
    char tmp[160];
    snprintf(tmp, sizeof tmp, "%s.tmp", path);
    FILE *f = fopen(tmp, "wb");
    if (f) {
        int ok = (fputs(s, f) >= 0);
        if(fflush(f) != 0) ok = 0;
        /* force bytes to disk before rename */
        if(ok) { int fd = fileno(f); if(fd >= 0 && fsync(fd) != 0) ok = 0; }
        if(fclose(f) != 0) ok = 0;
        if(ok && rename(tmp, path) != 0) ok = 0;
        if(!ok){
            remove(tmp);
            log_msg("cfg_save: write failed for %s", path);
        }
    } else { log_msg("cfg_save: cannot write %s", tmp); }
    free(s); jl_free(r);
}
