/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 SirHumza and CyberMask367 - see NOTICE for attribution terms.
 */
/* cfg.c - tiny JSON config via jsonlite */
#include "cfg.h"
#include "fingerprint_gen.h"
#include "jsonlite.h"
#include "log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>

cfg_t g_cfg;

static int cfg_status_valid(const char *s){
    return s && (!strcmp(s, "online") || !strcmp(s, "idle") ||
                 !strcmp(s, "dnd") || !strcmp(s, "invisible"));
}

/* Title ids are CUSA12345 / NPXS20001 / SLPM55146: uppercase A-Z, digits,
 * underscore, 4..15 chars. Everything else in the two tables is ignored. */
static int cfg_valid_title_id(const char *s){
    if(!s) return 0;
    size_t n = strlen(s);
    if(n < 4 || n >= (size_t)CFG_TITLEID_LEN) return 0;
    for(size_t i = 0; i < n; i++){
        char ch = s[i];
        if(!((ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == '_'))
            return 0;
    }
    return 1;
}

/* Tokens are pasted from a browser and arrive wrapped in quotes or padding
 * often enough to be a recurring support issue (D-14). JSON unescapes one
 * layer; strip any further matching quote pair plus surrounding whitespace. */
static void cfg_trim_token(char *s){
    if(!s) return;
    char *p = s;
    while(*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
    if(p != s) memmove(s, p, strlen(p) + 1);
    size_t n = strlen(s);
    while(n > 0 && (s[n-1] == ' ' || s[n-1] == '\t' ||
                    s[n-1] == '\r' || s[n-1] == '\n')) s[--n] = 0;
    if(n >= 2 && ((s[0] == '"' && s[n-1] == '"') ||
                  (s[0] == '\'' && s[n-1] == '\''))){
        memmove(s, s + 1, n - 2);
        s[n - 2] = 0;
        cfg_trim_token(s);
    }
}

/* Accept the hand-edit mistake JSON cannot: an unquoted token value.
 * Returns a malloc'd copy of `buf` with that one value quoted, or NULL when
 * the token is missing or already quoted. Everything else stays byte-for-byte
 * so one bad field does not reset every other setting to defaults. */
static char *cfg_quote_bare_token(const char *buf, size_t n, size_t *out_n){
    if(!buf || !out_n) return NULL;
    const char *key = strstr(buf, "\"token\"");
    if(!key) return NULL;
    const char *p = key + sizeof("\"token\"") - 1;
    const char *limit = buf + n;
    while(p < limit && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')) p++;
    if(p >= limit || *p != ':') return NULL;
    p++;
    while(p < limit && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')) p++;
    if(p >= limit || *p == '"') return NULL;  /* already valid JSON */
    const char *end = p;
    while(end < limit && *end != ',' && *end != '}' &&
          *end != '\r' && *end != '\n') end++;
    const char *ve = end;
    while(ve > p && (ve[-1] == ' ' || ve[-1] == '\t')) ve--;
    if(ve == p) return NULL;
    size_t total = n + 2;  /* two quote bytes */
    char *out = (char *)malloc(total + 1);
    if(!out) return NULL;
    size_t pre = (size_t)(p - buf);
    memcpy(out, buf, pre);
    out[pre] = '"';
    memcpy(out + pre + 1, p, (size_t)(ve - p));
    out[pre + 1 + (size_t)(ve - p)] = '"';
    memcpy(out + pre + 1 + (size_t)(ve - p) + 1, ve, (size_t)(limit - ve));
    *out_n = total;
    out[total] = 0;
    return out;
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
    c->show_firmware = 1;
    c->show_logo = 1;
    c->show_idle = 1;
    c->show_media = 1;
    c->show_homebrew = 1;
    snprintf(c->status, sizeof c->status, "online");
    snprintf(c->timestamp_mode, sizeof c->timestamp_mode, "elapsed");
    c->auto_update = 1;
    c->poll_interval_s = 12;
    c->presence_reconcile_s = 900;
    c->presence_hold_s = 300;
    c->lock_heartbeat_s = 60;
    c->lock_stale_s = 600;
    c->reconnect_max_s = 600;
    c->timesync_interval_s = 3600;
    c->log_max_kb = 512;
    strncpy(c->token, "SET_ME", sizeof(c->token)-1);
    strncpy(c->presence_state, "On PS4", sizeof(c->presence_state)-1);
    /* Presence shows three lines: name, details, state (+ timer). The details
     * line says what you are doing; the state line carries the firmware. */
    strncpy(c->presence_details_game, "Playing on PlayStation 4",
            sizeof(c->presence_details_game)-1);
    strncpy(c->presence_details_home, "Idling on Home Menu",
            sizeof(c->presence_details_home)-1);
    strncpy(c->presence_details_media, "Watching on PlayStation 4",
            sizeof(c->presence_details_media)-1);
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
    strncpy(c->large_art, "https://retro-games.cybermask.dpdns.org/images/ps-logo-full.png",
            sizeof(c->large_art)-1);
    strncpy(c->small_art, "https://retro-games.cybermask.dpdns.org/images/ps-logo-blue.png",
            sizeof(c->small_art)-1);
    strncpy(c->browser_art, "https://retro-games.cybermask.dpdns.org/images/web_browser.png",
            sizeof(c->browser_art)-1);
    /* Idle tile: same logo as large_art, kept as the legacy fallback for
     * configs written before large_art existed. */
    strncpy(c->home_art, "https://retro-games.cybermask.dpdns.org/images/ps-logo-full.png",
            sizeof(c->home_art)-1);
    c->n_titles = 0;
    /* Default art backend: our own Sony-CDN icon pack, resolved through
     * Discord's external-assets proxy (mp:) at post time. Works from the
     * start with zero setup; missing titles degrade to no art. */
    strncpy(c->art_base_url, "https://raw.githubusercontent.com/SirHumza/orbisrpc-host/main/icons/",
            sizeof(c->art_base_url)-1);
    strncpy(c->application_id, "1536977374795538532", sizeof(c->application_id)-1);
    /* Fingerprint defaults track the Discord desktop stable build. The tuple
     * is generated from the reviewed scripts/fingerprint.json, so refreshing
     * it never needs a code edit. Config beats these. */
    strncpy(c->fp_client_version, ORBISRPC_FP_VERSION,
            sizeof(c->fp_client_version)-1);
    snprintf(c->fp_client_build, sizeof(c->fp_client_build), "%d",
             ORBISRPC_FP_BUILD);
    strncpy(c->fp_browser_version, ORBISRPC_FP_BROWSER_VERSION,
            sizeof(c->fp_browser_version)-1);
    strncpy(c->fp_os, ORBISRPC_FP_OS, sizeof(c->fp_os)-1);
    strncpy(c->fp_os_version, ORBISRPC_FP_OS_VERSION,
            sizeof(c->fp_os_version)-1);
    strncpy(c->fp_os_sdk_version, ORBISRPC_FP_OS_SDK_VERSION,
            sizeof(c->fp_os_sdk_version)-1);
    strncpy(c->fp_arch, ORBISRPC_FP_ARCH, sizeof(c->fp_arch)-1);
    strncpy(c->fp_locale, ORBISRPC_FP_LOCALE, sizeof(c->fp_locale)-1);
    strncpy(c->fp_timezone, ORBISRPC_FP_TIMEZONE, sizeof(c->fp_timezone)-1);
    strncpy(c->fp_user_agent, ORBISRPC_FP_USER_AGENT,
            sizeof(c->fp_user_agent)-1);
    /* Empty = generated once and persisted by cfg_ensure_installation_id(). */
    c->fp_installation_id[0] = 0;
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
    if(c->presence_reconcile_s < 60){
        log_msg("config: presence_reconcile_s %d too small; using 60", c->presence_reconcile_s);
        c->presence_reconcile_s = 60;
    }
    if(c->presence_reconcile_s > 3600){
        log_msg("config: presence_reconcile_s %d too large; using 3600", c->presence_reconcile_s);
        c->presence_reconcile_s = 3600;
    }
    if(c->presence_hold_s < 0){
        log_msg("config: presence_hold_s %d too small; using 0", c->presence_hold_s);
        c->presence_hold_s = 0;
    }
    if(c->presence_hold_s > 1800){
        log_msg("config: presence_hold_s %d too large; using 1800", c->presence_hold_s);
        c->presence_hold_s = 1800;
    }
    if(c->lock_heartbeat_s < 30){
        log_msg("config: lock_heartbeat_s %d too small; using 30", c->lock_heartbeat_s);
        c->lock_heartbeat_s = 30;
    }
    if(c->lock_heartbeat_s > 600){
        log_msg("config: lock_heartbeat_s %d too large; using 600", c->lock_heartbeat_s);
        c->lock_heartbeat_s = 600;
    }
    if(c->lock_stale_s < 120){
        log_msg("config: lock_stale_s %d too small; using 120", c->lock_stale_s);
        c->lock_stale_s = 120;
    }
    if(c->lock_stale_s > 86400){
        log_msg("config: lock_stale_s %d too large; using 86400", c->lock_stale_s);
        c->lock_stale_s = 86400;
    }
    if(c->lock_stale_s < c->lock_heartbeat_s * 2){
        log_msg("config: lock_stale_s %d is under 2x heartbeat; using %d",
                c->lock_stale_s, c->lock_heartbeat_s * 2);
        c->lock_stale_s = c->lock_heartbeat_s * 2;
    }
    if(c->reconnect_max_s < 60){
        log_msg("config: reconnect_max_s %d too small; using 60", c->reconnect_max_s);
        c->reconnect_max_s = 60;
    }
    if(c->reconnect_max_s > 3600){
        log_msg("config: reconnect_max_s %d too large; using 3600", c->reconnect_max_s);
        c->reconnect_max_s = 3600;
    }
    if(c->timesync_interval_s < 300){
        log_msg("config: timesync_interval_s %d too small; using 300", c->timesync_interval_s);
        c->timesync_interval_s = 300;
    }
    if(c->timesync_interval_s > 86400){
        log_msg("config: timesync_interval_s %d too large; using 86400", c->timesync_interval_s);
        c->timesync_interval_s = 86400;
    }
    if(c->log_max_kb < 64){
        log_msg("config: log_max_kb %d too small; using 64", c->log_max_kb);
        c->log_max_kb = 64;
    }
    if(c->log_max_kb > 8192){
        log_msg("config: log_max_kb %d too large; using 8192", c->log_max_kb);
        c->log_max_kb = 8192;
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
    if(!root){
        /* D-14: one unquoted token must not throw away the whole file. */
        size_t rn = 0;
        char *fixed = cfg_quote_bare_token(buf, n, &rn);
        if(fixed){
            root = jl_parse(fixed, rn);
            if(root) log_msg("config: recovered an unquoted \"token\" value");
            else log_msg("config: unquoted token found, but the file is still invalid JSON");
            free(fixed);
        }
    }
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
    cfg_trim_token(c->token);
    STR("application_id", application_id);
    STR("art_base_url", art_base_url);
    STR("home_art", home_art);
    STR("presence_state", presence_state);
    STR("presence_details_game", presence_details_game);
    STR("presence_details_home", presence_details_home);
    STR("presence_details_media", presence_details_media);
    STR("presence_settings_text", presence_settings_text);
    STR("status", status);
    STR("timestamp_mode", timestamp_mode);
    STR("asset_idle", asset_idle);
    STR("asset_playing", asset_playing);
    STR("large_art", large_art);
    STR("small_art", small_art);
    STR("browser_art", browser_art);
    STR("fp_client_version", fp_client_version);
    STR("fp_client_build", fp_client_build);
    STR("fp_browser_version", fp_browser_version);
    STR("fp_os", fp_os);
    STR("fp_os_version", fp_os_version);
    STR("fp_os_sdk_version", fp_os_sdk_version);
    STR("fp_arch", fp_arch);
    STR("fp_locale", fp_locale);
    STR("fp_timezone", fp_timezone);
    STR("fp_user_agent", fp_user_agent);
    STR("fp_installation_id", fp_installation_id);
#undef STR
    if(!cfg_status_valid(c->status))
        snprintf(c->status, sizeof c->status, "online");
    if(strcmp(c->timestamp_mode, "elapsed") && strcmp(c->timestamp_mode, "none"))
        snprintf(c->timestamp_mode, sizeof c->timestamp_mode, "elapsed");
    /* User-local title overrides: {"CUSA11995": "Marvel's Spider-Man"}.
     * Defensive: wrong types, overlong keys/names, and overflow past
     * CFG_MAX_TITLES are ignored, never fatal. */
    c->n_titles = 0;
    o = jl_obj_get(root, "titles");
    if(o && o->type == JL_OBJECT){
        for(jl_val_t *p = o->child; p && c->n_titles < CFG_MAX_TITLES; p = p->next){
            const jl_val_t *v = p->child ? p->child : p;
            if(!p->str || !v || v->type != JL_STRING || !v->str) continue;
            size_t vl = strlen(v->str);
            if(!cfg_valid_title_id(p->str)) continue;
            if(vl < 2 || vl >= (size_t)CFG_TITLENAME_LEN) continue;
            strncpy(c->title_ids[c->n_titles], p->str, CFG_TITLEID_LEN - 1);
            c->title_ids[c->n_titles][CFG_TITLEID_LEN - 1] = 0;
            strncpy(c->title_names[c->n_titles], v->str, CFG_TITLENAME_LEN - 1);
            c->title_names[c->n_titles][CFG_TITLENAME_LEN - 1] = 0;
            c->n_titles++;
        }
    }
    /* Per-title rules: optional details/art/disabled. A rule with no field
     * set is dropped, so an empty object cannot occupy a slot. */
    c->n_rules = 0;
    o = jl_obj_get(root, "title_rules");
    if(o && o->type == JL_OBJECT){
        for(jl_val_t *p = o->child; p && c->n_rules < CFG_MAX_RULES; p = p->next){
            const jl_val_t *v = p->child;
            if(!p->str || !v || v->type != JL_OBJECT) continue;
            if(!cfg_valid_title_id(p->str)) continue;
            cfg_title_rule_t rule;
            memset(&rule, 0, sizeof rule);
            const jl_val_t *dv = jl_obj_get(v, "disabled");
            if(dv && dv->type == JL_BOOL && dv->num) rule.disabled = 1;
            const jl_val_t *dt = jl_obj_get(v, "details");
            if(dt && dt->type == JL_STRING && dt->str){
                strncpy(rule.details, dt->str, sizeof rule.details - 1);
            }
            const jl_val_t *ar = jl_obj_get(v, "art");
            if(ar && ar->type == JL_STRING && ar->str){
                strncpy(rule.art, ar->str, sizeof rule.art - 1);
            }
            const jl_val_t *du = jl_obj_get(v, "duration_min");
            if(du && du->type == JL_NUMBER && du->num >= 1 && du->num <= 1440)
                rule.duration_min = (int)du->num;
            if(!rule.disabled && !rule.details[0] && !rule.art[0] &&
               rule.duration_min == 0) continue;
            snprintf(rule.title_id, sizeof rule.title_id, "%s", p->str);
            c->title_rules[c->n_rules++] = rule;
        }
    }
    o = jl_obj_get(root, "enabled");         if (o && o->type == JL_BOOL)   c->enabled = (int)o->num;
    o = jl_obj_get(root, "pkgzone_enabled"); if (o && o->type == JL_BOOL)   c->pkgzone_enabled = (int)o->num;
    o = jl_obj_get(root, "retro_enabled"); if (o && o->type == JL_BOOL)   c->retro_enabled = (int)o->num;
    o = jl_obj_get(root, "show_firmware"); if (o && o->type == JL_BOOL)   c->show_firmware = (int)o->num;
    o = jl_obj_get(root, "show_logo");     if (o && o->type == JL_BOOL)   c->show_logo = (int)o->num;
    o = jl_obj_get(root, "show_idle");     if (o && o->type == JL_BOOL)   c->show_idle = (int)o->num;
    o = jl_obj_get(root, "show_media");    if (o && o->type == JL_BOOL)   c->show_media = (int)o->num;
    o = jl_obj_get(root, "show_homebrew"); if (o && o->type == JL_BOOL)   c->show_homebrew = (int)o->num;
    o = jl_obj_get(root, "auto_update");     if (o && o->type == JL_BOOL)   c->auto_update = (int)o->num;
    o = jl_obj_get(root, "debug");           if (o && o->type == JL_BOOL)   c->debug = (int)o->num;
    o = jl_obj_get(root, "poll_interval_s"); if (o && o->type == JL_NUMBER) c->poll_interval_s = (int)o->num;
    o = jl_obj_get(root, "presence_reconcile_s"); if (o && o->type == JL_NUMBER) c->presence_reconcile_s = (int)o->num;
    o = jl_obj_get(root, "presence_hold_s"); if (o && o->type == JL_NUMBER) c->presence_hold_s = (int)o->num;
    o = jl_obj_get(root, "lock_heartbeat_s"); if (o && o->type == JL_NUMBER) c->lock_heartbeat_s = (int)o->num;
    o = jl_obj_get(root, "lock_stale_s"); if (o && o->type == JL_NUMBER) c->lock_stale_s = (int)o->num;
    o = jl_obj_get(root, "reconnect_max_s"); if (o && o->type == JL_NUMBER) c->reconnect_max_s = (int)o->num;
    o = jl_obj_get(root, "timesync_interval_s"); if (o && o->type == JL_NUMBER) c->timesync_interval_s = (int)o->num;
    o = jl_obj_get(root, "log_max_kb"); if (o && o->type == JL_NUMBER) c->log_max_kb = (int)o->num;
    o = jl_obj_get(root, "schema_version");  if (o && o->type == JL_NUMBER) c->schema_version = (int)o->num;
    jl_free(root);
    clamp_cfg(c);
    /* Older builds wrote config.json 0644. The file holds a user token, so
     * tighten it on every successful load; a read-only mount is logged once
     * and is not fatal. */
    if(chmod(path, 0600) != 0){
        static int warned = 0;
        if(!warned){ warned = 1;
            log_msg("config: cannot set 0600 on %s; token may be readable", path); }
    }
    /* Migration: stamp current schema so re-saves converge.
     * v0 (no key): identical layout, adopt as-is. */
    c->schema_version = CFG_SCHEMA_VERSION;
    return 0;
}

const cfg_title_rule_t *cfg_title_rule(const cfg_t *c, const char *titleId){
    if(!c || !titleId || !titleId[0]) return NULL;
    for(int i = 0; i < c->n_rules; i++)
        if(!strcmp(c->title_rules[i].title_id, titleId))
            return &c->title_rules[i];
    return NULL;
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

/* Read the config file that is already on disk. Unknown top-level keys
 * belong to the operator (or a newer build), so a rewrite must start from
 * what is there instead of a blank object. Returns NULL when the file is
 * absent or unusable; the caller then writes a fresh object. */
static jl_val_t *cfg_read_root(const char *path){
    FILE *f = fopen(path, "rb");
    if(!f) return NULL;
    if(fseek(f, 0, SEEK_END) != 0){ fclose(f); return NULL; }
    long sz = ftell(f);
    if(sz <= 0 || sz > 1<<20 || fseek(f, 0, SEEK_SET) != 0){ fclose(f); return NULL; }
    char *buf = (char *)malloc((size_t)sz + 1);
    if(!buf){ fclose(f); return NULL; }
    size_t n = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    jl_val_t *root = NULL;
    if(n == (size_t)sz){ buf[n] = 0; root = jl_parse(buf, n); }
    free(buf);
    if(root && root->type != JL_OBJECT){ jl_free(root); root = NULL; }
    return root;
}

/* Set key, replacing an existing pair. jl_obj_set() always appends, which
 * would leave a duplicate key and the on-disk order would decide which one
 * readers see. */
static void cfg_set(jl_val_t *obj, const char *key, jl_val_t *val){
    if(!obj || obj->type != JL_OBJECT || !key || !val){ jl_free(val); return; }
    for(jl_val_t *p = obj->child; p; p = p->next){
        if(p->str && !strcmp(p->str, key)){
            if(p->child) jl_free(p->child);
            p->child = val;
            return;
        }
    }
    jl_obj_set(obj, key, val);
}

int cfg_migrate(jl_val_t *root, int from) {
    if(!root || root->type != JL_OBJECT || from < 0) return -1;
    if(from > CFG_SCHEMA_VERSION) {
        log_msg("config: schema v%d is newer than this build (v%d); not rewriting",
                from, CFG_SCHEMA_VERSION);
        return -1;
    }
    /* v0 (no key) and v1 share the layout: the stamp itself is the migration,
     * and unknown top-level keys are already preserved by cfg_save(). */
    cfg_set(root, "schema_version", jl_new_number((double)CFG_SCHEMA_VERSION));
    return 0;
}

/* Byte-for-byte copy at 0600. Backups hold the token, so the destination is
 * created with the final mode instead of being opened world-readable and
 * fixed up afterwards. Best effort: a failed backup must not block a save. */
static int cfg_backup(const char *path, const char *dst) {
    int in = open(path, O_RDONLY);
    if(in < 0) return -1;
    int out = open(dst, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if(out < 0) { close(in); return -1; }
    char buf[4096];
    int ok = 1;
    for(;;) {
        ssize_t n = read(in, buf, sizeof buf);
        if(n == 0) break;
        if(n < 0) { ok = 0; break; }
        ssize_t off = 0;
        while(off < n) {
            ssize_t w = write(out, buf + off, (size_t)(n - off));
            if(w <= 0) { ok = 0; break; }
            off += w;
        }
        if(!ok) break;
    }
    if(ok && fsync(out) != 0) ok = 0;
    if(close(out) != 0) ok = 0;
    close(in);
    if(!ok) { remove(dst); return -1; }
    return 0;
}

int cfg_save(const char *path, const cfg_t *c) {
    if(!path || !c) return -1;
    jl_val_t *r = cfg_read_root(path);
    int from = CFG_SCHEMA_VERSION;
    struct stat exist_st;
    int existed = (stat(path, &exist_st) == 0);
    if(r) {
        const jl_val_t *sv = jl_obj_get(r, "schema_version");
        from = (sv && sv->type == JL_NUMBER) ? (int)sv->num : 1;
        if(cfg_migrate(r, from) != 0) { jl_free(r); return -1; }
    } else {
        r = jl_new_object();
    }
    if(!r) { log_msg("cfg_save: allocation failed"); return -1; }
    /* Keep the pre-save bytes. A 1.0 file also gets a one-time pristine copy
     * under its own version, so a later bad write cannot consume it. */
    if(existed && from <= CFG_SCHEMA_VERSION) {
        char bak[180];
        snprintf(bak, sizeof bak, "%s.bak", path);
        if(cfg_backup(path, bak) != 0)
            log_msg("cfg_save: cannot write backup %s", bak);
        if(from < CFG_SCHEMA_VERSION) {
            int v = from < 1 ? 1 : from;
            char vb[180];
            struct stat st;
            snprintf(vb, sizeof vb, "%s.v%d.bak", path, v);
            if(stat(vb, &st) != 0 && cfg_backup(path, vb) == 0)
                log_msg("cfg_save: pre-migration backup %s", vb);
        }
    }
    cfg_set(r, "schema_version",  jl_new_number((double)CFG_SCHEMA_VERSION));
    cfg_set(r, "token",           jl_new_string(c->token));
    cfg_set(r, "application_id",  jl_new_string(c->application_id));
    cfg_set(r, "art_base_url",    jl_new_string(c->art_base_url));
    cfg_set(r, "enabled",         jl_new_bool(c->enabled));
    cfg_set(r, "pkgzone_enabled", jl_new_bool(c->pkgzone_enabled));
    cfg_set(r, "retro_enabled", jl_new_bool(c->retro_enabled));
    cfg_set(r, "show_firmware", jl_new_bool(c->show_firmware));
    cfg_set(r, "show_logo",     jl_new_bool(c->show_logo));
    cfg_set(r, "show_idle",     jl_new_bool(c->show_idle));
    cfg_set(r, "show_media",    jl_new_bool(c->show_media));
    cfg_set(r, "show_homebrew", jl_new_bool(c->show_homebrew));
    cfg_set(r, "auto_update",     jl_new_bool(c->auto_update));
    cfg_set(r, "debug",           jl_new_bool(c->debug));
    cfg_set(r, "poll_interval_s", jl_new_number((double)c->poll_interval_s));
    cfg_set(r, "presence_reconcile_s", jl_new_number((double)c->presence_reconcile_s));
    cfg_set(r, "presence_hold_s", jl_new_number((double)c->presence_hold_s));
    cfg_set(r, "presence_state",  jl_new_string(c->presence_state));
    cfg_set(r, "status", jl_new_string(cfg_status_valid(c->status) ? c->status : "online"));
    cfg_set(r, "timestamp_mode", jl_new_string(
        !strcmp(c->timestamp_mode, "none") ? "none" : "elapsed"));
    cfg_set(r, "presence_details_game", jl_new_string(c->presence_details_game));
    cfg_set(r, "presence_details_home", jl_new_string(c->presence_details_home));
    cfg_set(r, "presence_details_media", jl_new_string(c->presence_details_media));
    cfg_set(r, "presence_settings_text", jl_new_string(c->presence_settings_text));
    cfg_set(r, "asset_idle", jl_new_string(c->asset_idle));
    cfg_set(r, "asset_playing", jl_new_string(c->asset_playing));
    cfg_set(r, "large_art", jl_new_string(c->large_art));
    cfg_set(r, "small_art", jl_new_string(c->small_art));
    cfg_set(r, "browser_art", jl_new_string(c->browser_art));
    cfg_set(r, "fp_client_version", jl_new_string(c->fp_client_version));
    cfg_set(r, "fp_client_build", jl_new_string(c->fp_client_build));
    cfg_set(r, "fp_browser_version", jl_new_string(c->fp_browser_version));
    cfg_set(r, "fp_os", jl_new_string(c->fp_os));
    cfg_set(r, "fp_os_version", jl_new_string(c->fp_os_version));
    cfg_set(r, "fp_os_sdk_version", jl_new_string(c->fp_os_sdk_version));
    cfg_set(r, "fp_arch", jl_new_string(c->fp_arch));
    cfg_set(r, "fp_locale", jl_new_string(c->fp_locale));
    cfg_set(r, "fp_timezone", jl_new_string(c->fp_timezone));
    cfg_set(r, "fp_user_agent", jl_new_string(c->fp_user_agent));
    cfg_set(r, "fp_installation_id", jl_new_string(c->fp_installation_id));
    cfg_set(r, "home_art",        jl_new_string(c->home_art));
    if(c->n_titles > 0){
        jl_val_t *t = jl_new_object();
        if(t){
            for(int i = 0; i < c->n_titles; i++)
                jl_obj_set(t, c->title_ids[i], jl_new_string(c->title_names[i]));
            cfg_set(r, "titles", t);
        }
    }
    if(c->n_rules > 0){
        jl_val_t *rr = jl_new_object();
        if(rr){
            for(int i = 0; i < c->n_rules; i++){
                const cfg_title_rule_t *rule = &c->title_rules[i];
                jl_val_t *e = jl_new_object();
                if(!e) continue;
                if(rule->disabled) jl_obj_set(e, "disabled", jl_new_bool(1));
                if(rule->details[0]) jl_obj_set(e, "details", jl_new_string(rule->details));
                if(rule->art[0]) jl_obj_set(e, "art", jl_new_string(rule->art));
                if(rule->duration_min > 0 && rule->duration_min <= 1440)
                    jl_obj_set(e, "duration_min", jl_new_int(rule->duration_min));
                jl_obj_set(rr, rule->title_id, e);
            }
            cfg_set(r, "title_rules", rr);
        }
    }
    char *s = jl_stringify(r);
    if(!s){ log_msg("cfg_save: serialization failed"); jl_free(r); return -1; }
    /* write tmp + fsync + rename so a power loss can't corrupt the config.
     * Create the temp file with 0600 already applied: fopen()+fchmod leaves
     * a window where the token is world-readable under a permissive umask. */
    char tmp[160];
    snprintf(tmp, sizeof tmp, "%s.tmp", path);
    int tfd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if(tfd >= 0 && fchmod(tfd, 0600) != 0){ close(tfd); tfd = -1; }
    FILE *f = tfd >= 0 ? fdopen(tfd, "wb") : NULL;
    int ok = 0;
    if(!f && tfd >= 0) close(tfd);
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

int cfg_ensure_installation_id(cfg_t *c){
    if(!c) return 0;
    if(c->fp_installation_id[0]) return 0;
    unsigned char b[16];
    int got = 0;
    FILE *u = fopen("/dev/urandom", "rb");
    if(u){
        got = (int)fread(b, 1, sizeof b, u);
        fclose(u);
    }
    if(got != (int)sizeof b){
        log_msg("cfg: no urandom for installation id; leaving it empty");
        return 0;
    }
    /* RFC 4122 version 4 shape: Discord shows this as a UUID, and a wrong
     * shape is one more thing that does not look like a real client. */
    b[6] = (unsigned char)((b[6] & 0x0f) | 0x40);
    b[8] = (unsigned char)((b[8] & 0x3f) | 0x80);
    snprintf(c->fp_installation_id, sizeof c->fp_installation_id,
             "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
             b[0],b[1],b[2],b[3], b[4],b[5], b[6],b[7], b[8],b[9],
             b[10],b[11],b[12],b[13],b[14],b[15]);
    if(cfg_save(CFG_PATH, c) != 0){
        log_msg("cfg: installation id generated but not persisted");
        return 0;
    }
    return 1;
}
