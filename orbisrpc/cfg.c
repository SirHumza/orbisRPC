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
    c->schema_version = CFG_SCHEMA_VERSION;
    c->enabled = 1;
    c->auto_update = 1;
    c->poll_interval_s = 12;
    strncpy(c->token, "SET_ME", sizeof(c->token)-1);
    strncpy(c->presence_state, "On PS4", sizeof(c->presence_state)-1);
    /* Idle tile ships working: project-hosted orbisRPC logo, resolved
     * through the mp: proxy like every other art URL. Override with any
     * http(s) URL or uploaded asset key. */
    strncpy(c->home_art, "https://raw.githubusercontent.com/SirHumza/orbisRPC/main/config/icons/logo.png",
            sizeof(c->home_art)-1);
    c->n_titles = 0;
    /* Default art backend: our own Sony-CDN icon pack, resolved through
     * Discord's external-assets proxy (mp:) at post time. Works from the
     * start with zero setup; missing titles degrade to no art. */
    strncpy(c->art_base_url, "https://raw.githubusercontent.com/SirHumza/orbisrpc-host/main/icons/",
            sizeof(c->art_base_url)-1);
    strncpy(c->application_id, "1536977374795538532", sizeof(c->application_id)-1);
}

/* keep the daemon sane if the user puts junk in config */
static void clamp_cfg(cfg_t *c){
    if(c->poll_interval_s < 5){
        log_msg("config: poll_interval_s %d too small; using 5", c->poll_interval_s);
        c->poll_interval_s = 5;
    }
    if(c->poll_interval_s > 300){
        log_msg("config: poll_interval_s %d too large; using 300", c->poll_interval_s);
        c->poll_interval_s = 300;
    }
    if(c->token[0] && !strcmp(c->token, "SET_ME"))
        log_msg("config: token is still the SET_ME placeholder");
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
    STR("home_art", home_art);
    STR("presence_state", presence_state);
#undef STR
    /* User-local title overrides: {"CUSA11995": "Marvel's Spider-Man"}.
     * Defensive: wrong types, overlong keys/names, and overflow past
     * CFG_MAX_TITLES are ignored, never fatal. */
    c->n_titles = 0;
    o = jl_obj_get(root, "titles");
    if(o && o->type == JL_OBJECT){
        for(jl_val_t *p = o->child; p && c->n_titles < CFG_MAX_TITLES; p = p->next){
            const jl_val_t *v = p->child ? p->child : p;
            if(!p->str || !v || v->type != JL_STRING || !v->str) continue;
            size_t kl = strlen(p->str), vl = strlen(v->str);
            if(kl == 0 || kl >= (size_t)CFG_TITLEID_LEN) continue;
            if(vl < 2 || vl >= (size_t)CFG_TITLENAME_LEN) continue;
            int ok = 1;
            for(size_t i = 0; i < kl; i++){
                char ch = p->str[i];
                if(!((ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == '_')){ ok = 0; break; }
            }
            if(!ok) continue;
            strncpy(c->title_ids[c->n_titles], p->str, CFG_TITLEID_LEN - 1);
            c->title_ids[c->n_titles][CFG_TITLEID_LEN - 1] = 0;
            strncpy(c->title_names[c->n_titles], v->str, CFG_TITLENAME_LEN - 1);
            c->title_names[c->n_titles][CFG_TITLENAME_LEN - 1] = 0;
            c->n_titles++;
        }
    }
    o = jl_obj_get(root, "enabled");         if (o && o->type == JL_BOOL)   c->enabled = (int)o->num;
    o = jl_obj_get(root, "auto_update");     if (o && o->type == JL_BOOL)   c->auto_update = (int)o->num;
    o = jl_obj_get(root, "debug");           if (o && o->type == JL_BOOL)   c->debug = (int)o->num;
    o = jl_obj_get(root, "poll_interval_s"); if (o && o->type == JL_NUMBER) c->poll_interval_s = (int)o->num;
    o = jl_obj_get(root, "schema_version");  if (o && o->type == JL_NUMBER) c->schema_version = (int)o->num;
    jl_free(root);
    clamp_cfg(c);
    /* Migration: stamp current schema so re-saves converge.
     * v0 (no key): identical layout, adopt as-is. */
    c->schema_version = CFG_SCHEMA_VERSION;
    return 0;
}

int cfg_title(const cfg_t *c, const char *titleId, char *out, size_t cap){
    if(!c || !titleId || !titleId[0] || !out || cap == 0) return -1;
    for(int i = 0; i < c->n_titles; i++){
        if(!strcmp(c->title_ids[i], titleId)){
            strncpy(out, c->title_names[i], cap - 1);
            out[cap - 1] = 0;
            return out[0] ? 0 : -1;
        }
    }
    return -1;
}

/* Self-learning map (PC-tool "mapped" pattern): remember an authoritatively
 * resolved name so later boots resolve instantly even when every live
 * source is unreachable. Never learns raw-ID echoes or junk.
 * Returns 1 when the map changed (caller should cfg_save). */
int cfg_learn(cfg_t *c, const char *titleId, const char *name){
    if(!c || !titleId || !name) return 0;
    size_t kl = strlen(titleId), vl = strlen(name);
    if(kl == 0 || kl >= (size_t)CFG_TITLEID_LEN) return 0;
    if(vl < 2 || vl >= (size_t)CFG_TITLENAME_LEN) return 0;
    if(!strcmp(titleId, name)) return 0; /* ID echo, not a name */
    for(size_t i = 0; i < kl; i++){
        char ch = titleId[i];
        if(!((ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == '_')) return 0;
    }
    for(int i = 0; i < c->n_titles; i++){
        if(!strcmp(c->title_ids[i], titleId)){
            if(!strcmp(c->title_names[i], name)) return 0;
            strncpy(c->title_names[i], name, CFG_TITLENAME_LEN - 1);
            c->title_names[i][CFG_TITLENAME_LEN - 1] = 0;
            return 1;
        }
    }
    if(c->n_titles >= CFG_MAX_TITLES){
        log_msg("config: titles map full; not learning %s", titleId);
        return 0;
    }
    strncpy(c->title_ids[c->n_titles], titleId, CFG_TITLEID_LEN - 1);
    c->title_ids[c->n_titles][CFG_TITLEID_LEN - 1] = 0;
    strncpy(c->title_names[c->n_titles], name, CFG_TITLENAME_LEN - 1);
    c->title_names[c->n_titles][CFG_TITLENAME_LEN - 1] = 0;
    c->n_titles++;
    return 1;
}

int cfg_save(const char *path, const cfg_t *c) {
    if(!path || !c) return -1;
    jl_val_t *r = jl_new_object();
    if(!r) { log_msg("cfg_save: allocation failed"); return -1; }
    jl_obj_set(r, "schema_version",  jl_new_number((double)CFG_SCHEMA_VERSION));
    jl_obj_set(r, "token",           jl_new_string(c->token));
    jl_obj_set(r, "application_id",  jl_new_string(c->application_id));
    jl_obj_set(r, "art_base_url",    jl_new_string(c->art_base_url));
    jl_obj_set(r, "enabled",         jl_new_bool(c->enabled));
    jl_obj_set(r, "auto_update",     jl_new_bool(c->auto_update));
    jl_obj_set(r, "debug",           jl_new_bool(c->debug));
    jl_obj_set(r, "poll_interval_s", jl_new_number((double)c->poll_interval_s));
    jl_obj_set(r, "presence_state",  jl_new_string(c->presence_state));
    jl_obj_set(r, "home_art",        jl_new_string(c->home_art));
    if(c->n_titles > 0){
        jl_val_t *t = jl_new_object();
        if(t){
            for(int i = 0; i < c->n_titles; i++)
                jl_obj_set(t, c->title_ids[i], jl_new_string(c->title_names[i]));
            jl_obj_set(r, "titles", t);
        }
    }
    char *s = jl_stringify(r);
    if(!s){ log_msg("cfg_save: serialization failed"); jl_free(r); return -1; }
    /* write tmp + fsync + rename so a power loss can't corrupt the config */
    char tmp[160];
    snprintf(tmp, sizeof tmp, "%s.tmp", path);
    FILE *f = fopen(tmp, "wb");
    int ok = 0;
    if (f) {
        ok = (fputs(s, f) >= 0);
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
    return ok ? 0 : -1;
}
