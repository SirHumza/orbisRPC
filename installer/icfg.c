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

int icfg_token_save(const char *path, const char *token){
    jl_val_t *r;
    int rc;
    char back[160];
    if(!token_valid(token)) return -1;
    r = icfg_read(path);
    if(!r){
        r = jl_new_object();
        if(!r) return -2;
    }
    jl_obj_set(r, "token", jl_new_string(token));
    rc = icfg_write(path, r);
    jl_free(r);
    if(rc != 0) return -2;
    /* Read-back proof: an OOM inside the JSON build saves silently
     * WITHOUT the key. Never report success on faith. */
    back[0] = 0;
    if(icfg_token_load(path, back, sizeof back) != 0 ||
       strcmp(back, token) != 0)
        return -2;
    return 0;
}

int icfg_set_str(const char *path, const char *key, const char *val){
    jl_val_t *r;
    int rc;
    char back[256];
    if(!key || !key[0] || !val) return -1;
    r = icfg_read(path);
    if(!r){
        r = jl_new_object();
        if(!r) return -1;
    }
    jl_obj_set(r, key, jl_new_string(val));
    rc = icfg_write(path, r);
    jl_free(r);
    if(rc != 0) return -1;
    back[0] = 0;
    if(icfg_get_str(path, key, back, sizeof back) != 0 ||
       strcmp(back, val) != 0)
        return -1;
    return 0;
}

int icfg_set_int(const char *path, const char *key, long val){
    jl_val_t *r;
    int rc;
    long back = 0;
    if(!key || !key[0]) return -1;
    r = icfg_read(path);
    if(!r){
        r = jl_new_object();
        if(!r) return -1;
    }
    jl_obj_set(r, key, jl_new_int(val));
    rc = icfg_write(path, r);
    jl_free(r);
    if(rc != 0) return -1;
    if(icfg_get_int(path, key, &back) != 0 || back != val) return -1;
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

int icfg_titles_count(const char *path){
    jl_val_t *r = icfg_read(path);
    const jl_val_t *t;
    int n = 0;
    if(!r) return 0;
    t = jl_obj_get(r, "titles");
    if(t && t->type == JL_OBJECT){
        for(jl_val_t *p = t->child; p; p = p->next) n++;
    }
    jl_free(r);
    return n;
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
