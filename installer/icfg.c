/* icfg.c - atomic read-modify-write over the daemon config. */
#include "icfg.h"
#include "jsonlite.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

#define ICFG_MAX (64u*1024u)

int token_valid(const char *t){
    size_t n, i;
    int dots = 0;
    if(!t) return 0;
    n = strlen(t);
    if(n < 40 || n > 150) return 0;
    if(!strcmp(t, "SET_ME") || !strcmp(t, "PUT_TOKEN_HERE")) return 0;
    for(i = 0; i < n; i++){
        char c = t[i];
        if(c == '.'){ dots++; continue; }
        if(!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
             (c >= '0' && c <= '9') || c == '_' || c == '-'))
            return 0;
    }
    return dots >= 2;
}

static jl_val_t *icfg_read(const char *path){
    FILE *f = fopen(path, "rb");
    long sz;
    char *buf;
    jl_val_t *r;
    if(!f) return NULL;
    if(fseek(f, 0, SEEK_END) != 0){ fclose(f); return NULL; }
    sz = ftell(f);
    if(sz <= 0 || sz > (long)ICFG_MAX){ fclose(f); return NULL; }
    if(fseek(f, 0, SEEK_SET) != 0){ fclose(f); return NULL; }
    buf = (char *)malloc((size_t)sz + 1);
    if(!buf){ fclose(f); return NULL; }
    if(fread(buf, 1, (size_t)sz, f) != (size_t)sz){
        free(buf); fclose(f); return NULL;
    }
    fclose(f);
    buf[sz] = 0;
    r = jl_parse(buf, (size_t)sz);
    free(buf);
    if(!r || r->type != JL_OBJECT){ if(r) jl_free(r); return NULL; }
    return r;
}

static int icfg_write(const char *path, jl_val_t *r){
    char tmp[300];
    char *s;
    FILE *f;
    int ok = 1;
    if(snprintf(tmp, sizeof tmp, "%s.new", path) <= 0 ||
       (size_t)strlen(path) + 5 >= sizeof tmp)
        return -1;
    s = jl_stringify(r);
    if(!s) return -1;
    f = fopen(tmp, "wb");
    if(!f){ free(s); return -1; }
    if(fputs(s, f) < 0) ok = 0;
    if(ok){
        int fd = fileno(f);
        if(fd >= 0 && fsync(fd) != 0) ok = 0;
    }
    if(ok && fclose(f) != 0) ok = 0;
    free(s);
    if(!ok){ remove(tmp); return -1; }
    if(rename(tmp, path) != 0){ remove(tmp); return -1; }
    return 0;
}

int icfg_token_load(const char *path, char *out, size_t cap){
    jl_val_t *r = icfg_read(path);
    const jl_val_t *v;
    if(!out || cap == 0) return -1;
    out[0] = 0;
    if(!r) return -1;
    v = jl_obj_get(r, "token");
    if(v && v->type == JL_STRING && v->str){
        strncpy(out, v->str, cap - 1);
        out[cap - 1] = 0;
    }
    jl_free(r);
    return 0;
}

int icfg_get_str(const char *path, const char *key, char *out, size_t cap){
    jl_val_t *r = icfg_read(path);
    const jl_val_t *v;
    if(!out || cap == 0) return -1;
    out[0] = 0;
    if(!r || !key){ if(r) jl_free(r); return -1; }
    v = jl_obj_get(r, key);
    if(v && v->type == JL_STRING && v->str){
        strncpy(out, v->str, cap - 1);
        out[cap - 1] = 0;
    }
    jl_free(r);
    return 0;
}

int icfg_get_int(const char *path, const char *key, long *out){
    jl_val_t *r = icfg_read(path);
    const jl_val_t *v;
    if(!out) return -1;
    if(!r || !key){ if(r) jl_free(r); return -1; }
    v = jl_obj_get(r, key);
    if(v && v->type == JL_NUMBER){
        *out = v->num_is_int ? (long)v->inum : (long)v->num;
        jl_free(r);
        return 0;
    }
    jl_free(r);
    return -1;
}

/* ---- carry-over ---------------------------------------------------- */

int icfg_carry_capture(const char *path, const char *const *keys,
                       icfg_carry_t *out, int cap){
    jl_val_t *r;
    int n = 0, i;

    if(!out || cap <= 0) return 0;

    /* Clear the caller's array first. The whole point of capture is that
     * the file it reads may not exist (fresh install) or may not parse
     * (corrupt). Both must yield an all-absent result, never uninitialised
     * memory that apply() would then write into the new config. */
    memset(out, 0, sizeof(*out) * (size_t)cap);

    r = icfg_read(path);
    if(!r) return 0;

    for(i = 0; i < cap && keys && keys[i]; i++){
        const jl_val_t *v = jl_obj_get(r, keys[i]);
        icfg_carry_t *slot = &out[i];

        if(strlen(keys[i]) >= ICFG_KEY_MAX) continue;
        strncpy(slot->key, keys[i], ICFG_KEY_MAX - 1);
        slot->key[ICFG_KEY_MAX - 1] = 0;

        /* Objects and arrays are deliberately not carried: an option is a
         * scalar, and flattening one would silently change its shape. */
        if(!v) continue;

        switch(v->type){
        case JL_STRING:
            if(!v->str || strlen(v->str) >= ICFG_VAL_MAX) break;
            strncpy(slot->str, v->str, ICFG_VAL_MAX - 1);
            slot->str[ICFG_VAL_MAX - 1] = 0;
            slot->type = ICFG_T_STRING;
            n++;
            break;
        case JL_NUMBER:
            slot->inum = v->num_is_int ? v->inum : (long)v->num;
            slot->num = v->num;
            slot->type = v->num_is_int ? ICFG_T_INT : ICFG_T_DOUBLE;
            n++;
            break;
        case JL_BOOL:
            slot->boolean = v->boolean;
            slot->type = ICFG_T_BOOL;
            n++;
            break;
        default:
            break;
        }
    }

    jl_free(r);
    return n;
}

int icfg_carry_apply(const char *path, const icfg_carry_t *vals, int count){
    jl_val_t *r;
    int i, written = 0;

    if(!vals || count <= 0) return 0;

    r = icfg_read(path);
    if(!r){
        /* The new config should exist by now. Creating an empty object would
         * mask a failed download with a plausible-looking file, so treat an
         * unreadable target as an error and let the caller's report stand. */
        return -1;
    }

    for(i = 0; i < count; i++){
        jl_val_t *v = NULL;
        switch(vals[i].type){
        case ICFG_T_STRING: v = jl_new_string(vals[i].str); break;
        case ICFG_T_INT:    v = jl_new_int(vals[i].inum); break;
        case ICFG_T_DOUBLE: v = jl_new_double(vals[i].num); break;
        case ICFG_T_BOOL:   v = jl_new_bool(vals[i].boolean); break;
        default: continue;
        }
        if(!v) continue;   /* OOM: leave the incoming value in place */
        jl_obj_set(r, vals[i].key, v);
        written++;
    }

    if(written > 0 && icfg_write(path, r) != 0){
        jl_free(r);
        return -1;
    }

    jl_free(r);
    return written;
}
