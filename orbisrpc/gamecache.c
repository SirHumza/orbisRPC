/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 SirHumza and CyberMask367 - see NOTICE for attribution terms.
 */
/* gamecache.c - see gamecache.h. */
#include "gamecache.h"
#include "jsonlite.h"
#include "log.h"
#include "clock.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>

static gamecache_entry_t s_ent[GAMECACHE_MAX_ENTRIES];
static int s_n = -1;          /* -1 = not loaded yet */

int gamecache_id_valid(const char *id){
    if(!id) return 0;
    size_t n = strlen(id);
    if(n == 0 || n >= GAMECACHE_ID_LEN) return 0;
    for(size_t i = 0; i < n; i++){
        char c = id[i];
        if(!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_'))
            return 0;
    }
    return 1;
}

/* Copy with a hard cap and guaranteed NUL. Returns 0 when src is unusable. */
static int copy_field(char *dst, size_t cap, const char *src){
    if(!dst || cap == 0) return 0;
    dst[0] = 0;
    if(!src || !src[0]) return 0;
    size_t n = strlen(src);
    if(n >= cap) n = cap - 1;
    memcpy(dst, src, n);
    dst[n] = 0;
    return 1;
}

int gamecache_parse(const char *json, size_t len, gamecache_entry_t *out,
                    int max, int64_t now){
    (void)now;   /* age is enforced by gamecache_get(), not at parse time */
    if(!json || !out || max <= 0) return -1;
    if(max > GAMECACHE_MAX_ENTRIES) max = GAMECACHE_MAX_ENTRIES;
    memset(out, 0, sizeof(gamecache_entry_t) * (size_t)max);

    jl_val_t *r = jl_parse(json, len);
    if(!r || r->type != JL_OBJECT){ if(r) jl_free(r); return -1; }

    const jl_val_t *games = jl_obj_get(r, "games");
    if(!games || games->type != JL_ARRAY){
        /* No usable games array: an empty store, not a failure. A hand-edited
         * or truncated file must not stop the daemon resolving anything. */
        jl_free(r);
        return 0;
    }

    int n = 0;
    for(size_t i = 0; i < (size_t)max; i++){
        const jl_val_t *e = jl_arr_at(games, i);
        if(!e || e->type != JL_OBJECT) continue;
        const jl_val_t *id  = jl_obj_get(e, "id");
        const jl_val_t *nm  = jl_obj_get(e, "name");
        const jl_val_t *art = jl_obj_get(e, "art");
        const jl_val_t *at  = jl_obj_get(e, "at");
        if(!id  || id->type != JL_STRING || !id->str) continue;
        if(!gamecache_id_valid(id->str)) continue;
        if(!nm  || nm->type != JL_STRING || !nm->str || !nm->str[0]) continue;
        /* at is optional: a missing timestamp is treated as stale so a
         * hand-written row is re-resolved rather than trusted blindly.
         * The row is still kept: gamecache_get() ignores stale entries,
         * while gamecache_get_any() can show the last known card offline. */
        int64_t when = (at && at->type == JL_NUMBER) ? (int64_t)at->num : 0;

        memcpy(out[n].id, id->str, strlen(id->str) + 1);
        copy_field(out[n].name, sizeof out[n].name, nm->str);
        copy_field(out[n].art, sizeof out[n].art,
                   (art && art->type == JL_STRING) ? art->str : NULL);
        const jl_val_t *mp = jl_obj_get(e, "mp");
        copy_field(out[n].mp, sizeof out[n].mp,
                   (mp && mp->type == JL_STRING) ? mp->str : NULL);
        out[n].at = when;
        n++;
    }
    jl_free(r);
    return n;
}

char *gamecache_serialize(const gamecache_entry_t *e, int n){
    if(!e || n <= 0) return NULL;
    size_t cap = ((size_t)n * (2 * (GAMECACHE_ID_LEN + GAMECACHE_NAME_LEN +
                              GAMECACHE_ART_LEN + GAMECACHE_MP_LEN) + 80)) + 64;
    char *out = (char *)malloc(cap);
    if(!out) return NULL;
    size_t o = 0;
    int wrote = snprintf(out + o, cap - o, "{\"schema\":1,\"games\":[");
    if(wrote < 0 || (size_t)wrote >= cap - o){ free(out); return NULL; }
    o += (size_t)wrote;
    for(int i = 0; i < n; i++){
        if(i > 0 && o + 2 < cap){ out[o++] = ','; }
        char id[GAMECACHE_ID_LEN*2], nm[GAMECACHE_NAME_LEN*2],
             ar[GAMECACHE_ART_LEN*2], mp[GAMECACHE_MP_LEN*2];
        /* Escape defensively: names and URLs are network-derived. */
        size_t a = 0;
        for(const char *p = e[i].id; *p && a + 2 < sizeof id; p++){
            if(*p == '"' || *p == '\\') id[a++] = '\\';
            else if((unsigned char)*p < 0x20){ id[a++] = ' '; continue; }
            id[a++] = *p;
        }
        id[a] = 0;
        a = 0;
        for(const char *p = e[i].name; *p && a + 2 < sizeof nm; p++){
            if(*p == '"' || *p == '\\') nm[a++] = '\\';
            else if((unsigned char)*p < 0x20){ nm[a++] = ' '; continue; }
            nm[a++] = *p;
        }
        nm[a] = 0;
        a = 0;
        for(const char *p = e[i].art; *p && a + 2 < sizeof ar; p++){
            if(*p == '"' || *p == '\\') ar[a++] = '\\';
            else if((unsigned char)*p < 0x20){ ar[a++] = ' '; continue; }
            ar[a++] = *p;
        }
        ar[a] = 0;
        a = 0;
        for(const char *p = e[i].mp; *p && a + 2 < sizeof mp; p++){
            if(*p == '"' || *p == '\\') mp[a++] = '\\';
            else if((unsigned char)*p < 0x20){ mp[a++] = ' '; continue; }
            mp[a++] = *p;
        }
        mp[a] = 0;
        int w = snprintf(out + o, cap - o,
            "{\"id\":\"%s\",\"name\":\"%s\",\"art\":\"%s\",\"mp\":\"%s\",\"at\":%lld}",
            id, nm, ar, mp, (long long)e[i].at);
        if(w < 0 || (size_t)w >= cap - o){ free(out); return NULL; }
        o += (size_t)w;
    }
    if(o + 3 >= cap){ free(out); return NULL; }
    out[o++] = ']';
    out[o++] = '}';
    out[o] = 0;
    if(o > GAMECACHE_MAX_BYTES){ free(out); return NULL; }
    return out;
}

void gamecache_load(void){
    if(s_n >= 0) return;
    s_n = 0;
    FILE *f = fopen(GAMECACHE_PATH, "rb");
    if(!f) return;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    /* Bounded: a corrupt or hand-edited file must not be able to allocate
     * unbounded memory on a payload. */
    if(sz > 0 && sz < 1024*1024){
        char *buf = (char *)malloc((size_t)sz + 1);
        if(buf && fread(buf, 1, (size_t)sz, f) == (size_t)sz){
            buf[sz] = 0;
            int n = gamecache_parse(buf, (size_t)sz, s_ent, GAMECACHE_MAX_ENTRIES,
                                    (int64_t)time(NULL));
            if(n >= 0) s_n = n;
            else log_msg("games_cache: unreadable, starting empty");
        }
        free(buf);
    } else if(sz >= 1024*1024){
        log_msg("games_cache: %ld bytes is over the cap; ignoring", sz);
    }
    fclose(f);
    log_msg("games_cache: %d entr%s from disk", s_n, s_n == 1 ? "y" : "ies");
}

static int gamecache_get_impl(const char *id, char *name, size_t name_cap,
                              char *art, size_t art_cap, int allow_stale){
    if(name && name_cap) name[0] = 0;
    if(art && art_cap) art[0] = 0;
    gamecache_load();
    if(!gamecache_id_valid(id)) return 0;
    int64_t now = (int64_t)time(NULL);
    for(int i = 0; i < s_n; i++){
        if(strcmp(s_ent[i].id, id) != 0) continue;
        if(!allow_stale && now - s_ent[i].at > GAMECACHE_TTL_SECS) return 0;
        copy_field(name, name_cap, s_ent[i].name);
        copy_field(art, art_cap, s_ent[i].mp[0] ? s_ent[i].mp : s_ent[i].art);
        return name && name[0] ? 1 : 0;
    }
    return 0;
}

int gamecache_get(const char *id, char *name, size_t name_cap,
                  char *art, size_t art_cap){
    return gamecache_get_impl(id, name, name_cap, art, art_cap, 0);
}

int gamecache_get_any(const char *id, char *name, size_t name_cap,
                      char *art, size_t art_cap){
    return gamecache_get_impl(id, name, name_cap, art, art_cap, 1);
}

int gamecache_set_mp(const char *id, const char *mp){
    if(!gamecache_id_valid(id) || !mp || strncmp(mp, "mp:", 3) != 0) return 0;
    gamecache_load();
    for(int i = 0; i < s_n; i++){
        if(strcmp(s_ent[i].id, id) != 0) continue;
        if(!strcmp(s_ent[i].mp, mp)) return 0;
        copy_field(s_ent[i].mp, sizeof s_ent[i].mp, mp);
        gamecache_save();
        log_msg("games_cache: %s mp resolved", id);
        return 1;
    }
    return 0;
}

void gamecache_save(void){
    gamecache_load();
    if(s_n <= 0) return;
    char *json = gamecache_serialize(s_ent, s_n);
    if(!json){ log_msg("games_cache: serialise failed"); return; }

    char tmp[128];
    snprintf(tmp, sizeof tmp, "%s.new", GAMECACHE_PATH);
    FILE *f = fopen(tmp, "wb");
    if(!f){ log_msg("games_cache: cannot write %s", tmp); free(json); return; }
    int ok = 1;
    size_t n = strlen(json);
    if(fwrite(json, 1, n, f) != n) ok = 0;
    if(ok && fflush(f) != 0) ok = 0;
    if(ok){ int fd = fileno(f); if(fd >= 0 && fsync(fd) != 0) ok = 0; }
    if(fclose(f) != 0) ok = 0;
    free(json);
    /* Atomic: a half-written cache is worse than a missing one. */
    if(ok) rename(tmp, GAMECACHE_PATH);
    else { remove(tmp); log_msg("games_cache: save failed, kept the old file"); }
}

int gamecache_put(const char *id, const char *name, const char *art,
                  const char *src){
    if(!gamecache_id_valid(id) || !name || !name[0]) return 0;
    if(strcmp(id, name) == 0) return 0;   /* an unresolved echo, not a name */
    gamecache_load();
    int64_t now = (int64_t)time(NULL);
    for(int i = 0; i < s_n; i++){
        if(strcmp(s_ent[i].id, id) != 0) continue;
        int same = !strcmp(s_ent[i].name, name) &&
                   !strcmp(s_ent[i].art, art ? art : "");
        /* Identical content is not a no-op. `at` is the freshness stamp
         * checked when the file is loaded, so returning early here let a
         * title played every day expire after 30 days and re-resolve over
         * the network (or lose its name offline). Refresh at most once a
         * day: a write per boot is fine, a write per poll is not. */
        if(same && s_ent[i].at > 0 && now - s_ent[i].at < 86400) return 0;
        copy_field(s_ent[i].name, sizeof s_ent[i].name, name);
        if(strcmp(s_ent[i].art, art ? art : "") != 0){
            /* A new cover URL invalidates the old mp: mapping. */
            copy_field(s_ent[i].art, sizeof s_ent[i].art, art);
            s_ent[i].mp[0] = 0;
        }
        s_ent[i].at = now;
        gamecache_save();
        log_msg("games_cache: %s %s (%s)", same ? "refreshed" : "updated",
                id, src ? src : "?");
        return 1;
    }
    int slot = s_n;
    if(s_n >= GAMECACHE_MAX_ENTRIES){
        /* Bounded store: evict the least recently refreshed row instead of
         * refusing every new title once the table fills. */
        slot = 0;
        for(int i = 1; i < s_n; i++)
            if(s_ent[i].at < s_ent[slot].at) slot = i;
        log_msg("games_cache: full (%d); evicting %s", s_n, s_ent[slot].id);
        memset(&s_ent[slot], 0, sizeof s_ent[slot]);
    } else {
        s_n++;
    }
    memcpy(s_ent[slot].id, id, strlen(id) + 1);
    copy_field(s_ent[slot].name, sizeof s_ent[slot].name, name);
    copy_field(s_ent[slot].art, sizeof s_ent[slot].art, art);
    s_ent[slot].at = now;
    gamecache_save();
    log_msg("games_cache: stored %s (%s)", id, src ? src : "?");
    return 1;
}

void gamecache_reset(void){
    memset(s_ent, 0, sizeof s_ent);
    s_n = 0;
}