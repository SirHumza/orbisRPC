/* cfg.c - tiny JSON config via jsonlite */
#include "cfg.h"
#include "jsonlite.h"
#include "log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <math.h>
#include <unistd.h>
#include <sys/stat.h>
#include "config_lock.h"

cfg_t g_cfg;

int cfg_lock(const char *path){
    return config_lock_acquire(path);
}

void cfg_unlock(int fd){
    config_lock_release(fd);
}

void cfg_defaults(cfg_t *c) {
    if(!c) return;
    memset(c, 0, sizeof(*c));
    c->schema_version = CFG_SCHEMA_VERSION;
    c->enabled = 1;
    /* On by default, but it sends the console's title id to a third-party
     * site, so it is one boolean away from being off for good. */
    c->pkgzone_enabled = 1;
    c->retro_enabled = 1;
    c->custom_enabled = 1;
    c->show_firmware = 1;
    c->show_idle = 1;
    c->show_media = 1;
    c->show_homebrew = 1;
    c->auto_update = 1;
    c->poll_interval_s = 12;
    strncpy(c->token, "SET_ME", sizeof(c->token)-1);
    strncpy(c->presence_state, "On PS4", sizeof(c->presence_state)-1);
    /* Presence shows three lines: name, details, state (+ timer). The details
     * line says what you are doing; the state line carries the firmware. */
    strncpy(c->presence_details_game, "Playing on PlayStation 4",
            sizeof(c->presence_details_game)-1);
    strncpy(c->presence_details_home, "Idling on Home Menu",
            sizeof(c->presence_details_home)-1);
    strncpy(c->presence_settings_text, "In Settings",
            sizeof(c->presence_settings_text)-1);
    /* Uploaded Discord asset keys. Referencing a key needs no
     * external-assets POST, so these work with no network at all.
     * Empty by default: the URLs below are what actually get used, and
     * raw https in large_image only renders because discord.c proxies it
     * through mp:. Set either to a key to prefer that instead. */
    c->asset_idle[0] = 0;
    c->asset_playing[0] = 0;
    /* URLs for the two images, resolved through Discord's mp: external-assets
     * proxy at post time (a raw https in large_image renders "?" or drops the
     * activity). large_art falls back to home_art, small_art to large_art. */
    strncpy(c->large_art, "https://raw.githubusercontent.com/SirHumza/orbisRPC/refs/heads/main/config/images/icons/ps-logo-full.png",
            sizeof(c->large_art)-1);
    strncpy(c->small_art, "https://raw.githubusercontent.com/SirHumza/orbisRPC/refs/heads/main/config/images/icons/ps-logo-small.png",
            sizeof(c->small_art)-1);
    strncpy(c->browser_art, "https://raw.githubusercontent.com/SirHumza/orbisRPC/refs/heads/main/config/images/icons/web_browser.png",
            sizeof(c->browser_art)-1);
    /* Idle tile: same logo as large_art, kept as the legacy fallback for
     * configs written before large_art existed. */
    strncpy(c->home_art, "https://raw.githubusercontent.com/SirHumza/orbisRPC/refs/heads/main/config/images/icons/ps-logo-full.png",
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

static int duplicate_credential_key(const jl_val_t *root){
    static const char *const keys[] = { "token", "qos_token" };
    for(size_t i = 0; i < sizeof keys / sizeof keys[0]; i++){
        unsigned count = 0;
        for(const jl_val_t *p = root->child; p; p = p->next)
            if(p->str && !strcmp(p->str, keys[i]) && ++count > 1) return 1;
    }
    return 0;
}

int cfg_load(const char *path, cfg_t *c) {
    if(!path || !c) return -1;
    FILE *f = fopen(path, "rb");
    if (!f) { log_msg("config unavailable at %s; keeping current config", path); return -1; }
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return -1; }
    long sz = ftell(f);
    if (sz < 0 || fseek(f, 0, SEEK_SET) != 0) { fclose(f); return -1; }
    if (sz <= 0 || sz > (1<<20)) { fclose(f); return -1; }
    char *buf = (char *)malloc((size_t)sz + 1);
    if (!buf) { fclose(f); return -1; }
    size_t n = fread(buf, 1, (size_t)sz, f); fclose(f);
    if(n != (size_t)sz){ free(buf); log_msg("config read failed at %s", path); return -1; }
    buf[n] = 0;
    const char *json = buf;
    size_t json_len = n;
    if(json_len >= 3 && (unsigned char)json[0] == 0xef &&
       (unsigned char)json[1] == 0xbb &&
       (unsigned char)json[2] == 0xbf){
        json += 3;
        json_len -= 3;
    }
    jl_val_t *root = jl_parse(json, json_len);
    free(buf);
    if (!root) { log_msg("config parse failed; keeping current config"); return -1; }
    if (root->type != JL_OBJECT) {
        jl_free(root);
        log_msg("config root is not an object; keeping current config");
        return -1;
    }
    if(duplicate_credential_key(root)){
        jl_free(root);
        log_msg("config contains duplicate credential fields; keeping current config");
        return -1;
    }
    /* Parse transactionally: any rejected known string leaves the caller's
     * last-good configuration intact. */
    cfg_t next;
    cfg_defaults(&next);
    const jl_val_t *o;
#define STR(k,f) do { \
        o=jl_obj_get(root,k); \
        if(o&&o->type==JL_STRING){ \
            if (!o->str || o->strlen >= sizeof(next.f) || memchr(o->str, 0, o->strlen)) { \
                jl_free(root); log_msg("config field %s is invalid or too long", k); return -1; \
            } \
            memcpy(next.f,o->str,o->strlen); next.f[o->strlen]=0; \
        } \
    } while(0)
    STR("token", token);
    STR("qos_token", qos_token);
    STR("application_id", application_id);
    STR("art_base_url", art_base_url);
    STR("home_art", home_art);
    STR("presence_state", presence_state);
    STR("presence_details_game", presence_details_game);
    STR("presence_details_home", presence_details_home);
    STR("presence_settings_text", presence_settings_text);
    STR("asset_idle", asset_idle);
    STR("asset_playing", asset_playing);
    STR("large_art", large_art);
    STR("small_art", small_art);
    STR("browser_art", browser_art);
#undef STR
    /* User-local title overrides: {"CUSA11995": "Marvel's Spider-Man"}.
     * Defensive: wrong types, overlong keys/names, and overflow past
     * CFG_MAX_TITLES are ignored, never fatal. */
    next.n_titles = 0;
    o = jl_obj_get(root, "titles");
    if(o && o->type == JL_OBJECT){
        for(jl_val_t *p = o->child; p && next.n_titles < CFG_MAX_TITLES; p = p->next){
            const jl_val_t *v = p->child ? p->child : p;
            if(!p->str || !v || v->type != JL_STRING || !v->str) continue;
            size_t kl = p->strlen, vl = v->strlen;
            if (memchr(p->str, 0, kl) || memchr(v->str, 0, vl) ||
                kl >= (size_t)CFG_TITLEID_LEN || vl >= (size_t)CFG_TITLENAME_LEN)
                continue;
            if(kl == 0 || kl >= (size_t)CFG_TITLEID_LEN) continue;
            if(vl < 2 || vl >= (size_t)CFG_TITLENAME_LEN) continue;
            int ok = 1;
            for(size_t i = 0; i < kl; i++){
                char ch = p->str[i];
                if(!((ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == '_')){ ok = 0; break; }
            }
            if(!ok) continue;
            memcpy(next.title_ids[next.n_titles], p->str, kl + 1);
            memcpy(next.title_names[next.n_titles], v->str, vl + 1);
            next.n_titles++;
        }
    }
    o = jl_obj_get(root, "enabled");         if (o && o->type == JL_BOOL)   next.enabled = (int)o->num;
    o = jl_obj_get(root, "pkgzone_enabled"); if (o && o->type == JL_BOOL)   next.pkgzone_enabled = (int)o->num;
    o = jl_obj_get(root, "retro_enabled");  if (o && o->type == JL_BOOL)   next.retro_enabled = (int)o->num;
    o = jl_obj_get(root, "custom_enabled"); if (o && o->type == JL_BOOL)   next.custom_enabled = (int)o->num;
    o = jl_obj_get(root, "show_firmware"); if (o && o->type == JL_BOOL)   next.show_firmware = (int)o->num;
    o = jl_obj_get(root, "show_idle");     if (o && o->type == JL_BOOL)   next.show_idle = (int)o->num;
    o = jl_obj_get(root, "show_media");    if (o && o->type == JL_BOOL)   next.show_media = (int)o->num;
    o = jl_obj_get(root, "show_homebrew"); if (o && o->type == JL_BOOL)   next.show_homebrew = (int)o->num;
    o = jl_obj_get(root, "auto_update");     if (o && o->type == JL_BOOL)   next.auto_update = (int)o->num;
    o = jl_obj_get(root, "debug");           if (o && o->type == JL_BOOL)   next.debug = (int)o->num;
    o = jl_obj_get(root, "poll_interval_s");
    if (o && o->type == JL_NUMBER) {
        if (!isfinite(o->num) || o->num < INT_MIN || o->num > INT_MAX) {
            jl_free(root); log_msg("config poll_interval_s is out of range"); return -1;
        }
        next.poll_interval_s = (int)o->num;
    }
    o = jl_obj_get(root, "schema_version");
    if (o && o->type == JL_NUMBER) {
        if (!isfinite(o->num) || o->num < INT_MIN || o->num > INT_MAX) {
            jl_free(root); log_msg("config schema_version is out of range"); return -1;
        }
        next.schema_version = (int)o->num;
    }
    jl_free(root);
    clamp_cfg(&next);
    /* Migration: stamp current schema so re-saves converge.
     * v0 (no key): identical layout, adopt as-is. */
    next.schema_version = CFG_SCHEMA_VERSION;
    *c = next;
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
    jl_obj_set(r, "qos_token",       jl_new_string(c->qos_token));
    jl_obj_set(r, "application_id",  jl_new_string(c->application_id));
    jl_obj_set(r, "art_base_url",    jl_new_string(c->art_base_url));
    jl_obj_set(r, "enabled",         jl_new_bool(c->enabled));
    jl_obj_set(r, "pkgzone_enabled", jl_new_bool(c->pkgzone_enabled));
    jl_obj_set(r, "custom_enabled", jl_new_bool(c->custom_enabled));
    jl_obj_set(r, "retro_enabled", jl_new_bool(c->retro_enabled));
    jl_obj_set(r, "show_firmware", jl_new_bool(c->show_firmware));
    jl_obj_set(r, "show_idle",     jl_new_bool(c->show_idle));
    jl_obj_set(r, "show_media",    jl_new_bool(c->show_media));
    jl_obj_set(r, "show_homebrew", jl_new_bool(c->show_homebrew));
    jl_obj_set(r, "auto_update",     jl_new_bool(c->auto_update));
    jl_obj_set(r, "debug",           jl_new_bool(c->debug));
    jl_obj_set(r, "poll_interval_s", jl_new_number((double)c->poll_interval_s));
    jl_obj_set(r, "presence_state",  jl_new_string(c->presence_state));
    jl_obj_set(r, "presence_details_game", jl_new_string(c->presence_details_game));
    jl_obj_set(r, "presence_details_home", jl_new_string(c->presence_details_home));
    jl_obj_set(r, "presence_settings_text", jl_new_string(c->presence_settings_text));
    jl_obj_set(r, "asset_idle", jl_new_string(c->asset_idle));
    jl_obj_set(r, "asset_playing", jl_new_string(c->asset_playing));
    jl_obj_set(r, "large_art", jl_new_string(c->large_art));
    jl_obj_set(r, "small_art", jl_new_string(c->small_art));
    jl_obj_set(r, "browser_art", jl_new_string(c->browser_art));
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
    int tmp_len = snprintf(tmp, sizeof tmp, "%s.tmp", path);
    if (tmp_len < 0 || (size_t)tmp_len >= sizeof tmp) {
        free(s); jl_free(r);
        log_msg("cfg_save: config path too long");
        return -1;
    }
    FILE *f = fopen(tmp, "wb");
    int ok = 0;
    if (f) {
        int fd = fileno(f);
        ok = fd >= 0 && fchmod(fd, S_IRUSR | S_IWUSR) == 0;
        if(ok) ok = (fputs(s, f) >= 0);
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
