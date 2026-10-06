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
static int s_loaded_from_file = 0;

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
         * hand-written row is re-resolved rather than trusted blindly. */
        int64_t when = (at && at->type == JL_NUMBER) ? (int64_t)at->num : 0;
        if(now - when > GAMECACHE_TTL_SECS) continue;

        memcpy(out[n].id, id->str, strlen(id->str) + 1);
        copy_field(out[n].name, sizeof out[n].name, nm->str);
        copy_field(out[n].art, sizeof out[n].art,
                   (art && art->type == JL_STRING) ? art->str : NULL);
        out[n].at = when;
        n++;
    }
    jl_free(r);
    return n;
}

char *gamecache_serialize(const gamecache_entry_t *e, int n){
    if(!e || n <= 0) return NULL;
    size_t cap = (size_t)n * (GAMECACHE_ID_LEN + GAMECACHE_NAME_LEN +
                              GAMECACHE_ART_LEN + 64) + 64;
    char *out = (char *)malloc(cap);
    if(!out) return NULL;
    size_t o = 0;
    int wrote = snprintf(out + o, cap - o, "{\"schema\":1,\"games\":[");
    if(wrote < 0 || (size_t)wrote >= cap - o){ free(out); return NULL; }
    o += (size_t)wrote;
    for(int i = 0; i < n; i++){
        if(i > 0 && o + 2 < cap){ out[o++] = ','; }
        char id[GAMECACHE_ID_LEN*2], nm[GAMECACHE_NAME_LEN*2],
             ar[GAMECACHE_ART_LEN*2];
        /* Escape defensively: names and URLs are network-derived. */
        size_t a = 0;
        for(const char *p = e[i].id; *p && a + 2 < sizeof id; p++){
            if(*p == '"' || *p == '\\') id[a++] = '\\';
            id[a++] = *p;
        }
        id[a] = 0;
        a = 0;
        for(const char *p = e[i].name; *p && a + 2 < sizeof nm; p++){
            if(*p == '"' || *p == '\\') nm[a++] = '\\';
            nm[a++] = *p;
        }
        nm[a] = 0;
        a = 0;
        for(const char *p = e[i].art; *p && a + 2 < sizeof ar; p++){
            if(*p == '"' || *p == '\\') ar[a++] = '\\';
            ar[a++] = *p;
        }
        ar[a] = 0;
        int w = snprintf(out + o, cap - o,
            "{\"id\":\"%s\",\"name\":\"%s\",\"art\":\"%s\",\"at\":%lld}",
            id, nm, ar, (long long)e[i].at);
        if(w < 0 || (size_t)w >= cap - o){ free(out); return NULL; }
        o += (size_t)w;
    }
    if(o + 3 >= cap){ free(out); return NULL; }
    out[o++] = ']';
    out[o++] = '}';
    out[o] = 0;
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
            if(n >= 0){ s_n = n; s_loaded_from_file = 1; }
            else log_msg("games_cache: unreadable, starting empty");
        }
        free(buf);
    } else if(sz >= 1024*1024){
        log_msg("games_cache: %ld bytes is over the cap; ignoring", sz);
    }
    fclose(f);
    log_msg("games_cache: %d entr%s from disk", s_n, s_n == 1 ? "y" : "ies");
}

int gamecache_get(const char *id, char *name, size_t name_cap,
                  char *art, size_t art_cap){
    if(name && name_cap) name[0] = 0;
    if(art && art_cap) art[0] = 0;
    gamecache_load();
    if(!gamecache_id_valid(id)) return 0;
    for(int i = 0; i < s_n; i++){
        if(strcmp(s_ent[i].id, id) != 0) continue;
        copy_field(name, name_cap, s_ent[i].name);
        copy_field(art, art_cap, s_ent[i].art);
        return name && name[0] ? 1 : 0;
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
        if(same) return 0;
        copy_field(s_ent[i].name, sizeof s_ent[i].name, name);
        copy_field(s_ent[i].art, sizeof s_ent[i].art, art);
        s_ent[i].at = now;
        gamecache_save();
        log_msg("games_cache: updated %s (%s)", id, src ? src : "?");
        return 1;
    }
    if(s_n >= GAMECACHE_MAX_ENTRIES){
        log_msg("games_cache: full (%d); not storing %s", s_n, id);
        return 0;
    }
    memcpy(s_ent[s_n].id, id, strlen(id) + 1);
    copy_field(s_ent[s_n].name, sizeof s_ent[s_n].name, name);
    copy_field(s_ent[s_n].art, sizeof s_ent[s_n].art, art);
    s_ent[s_n].at = now;
    s_n++;
    gamecache_save();
    log_msg("games_cache: stored %s (%s)", id, src ? src : "?");
    return 1;
}

void gamecache_reset(void){
    memset(s_ent, 0, sizeof s_ent);
    s_n = 0;
    s_loaded_from_file = 0;
}