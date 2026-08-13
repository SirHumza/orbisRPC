/* jsonlite.h - tiny JSON parser/serializer for PS4RP.
 * Self-contained: parse gateway frames + config + API responses.
 * Public domain, no dependencies beyond <stdlib.h>/<string.h>. */
#ifndef JSONLITE_H
#define JSONLITE_H

#include <stddef.h>
#include <stdint.h>

typedef enum { JL_NULL, JL_BOOL, JL_NUMBER, JL_STRING, JL_OBJECT, JL_ARRAY } jl_type_t;

typedef struct jl_val {
    jl_type_t type;
    /* string / object key */
    char *str;         /* for JL_STRING: own data. for OBJECT entries: key */
    size_t strlen;
    /* number */
    double num;
    /* bool (1/0) reuses num */
    /* object / array contents */
    struct jl_val *child;   /* first pair (object) or first element (array) */
    struct jl_val *next;    /* next sibling: for object pairs and array elems */
    int32_t count;          /* pair/element count (optional for iteration) */
} jl_val_t;

typedef struct { const char *cur; const char *end; int err; } jl_parse_t;

/* Parse a NUL-terminated (or len-bounded) JSON string. Returns root or NULL on error. */
jl_val_t *jl_parse(const char *s, size_t len);

/* Free a value tree. */
void jl_free(jl_val_t *v);

/* Object/array helpers (returns NULL if missing). */
const jl_val_t *jl_obj_get(const jl_val_t *obj, const char *key);
const jl_val_t *jl_arr_at(const jl_val_t *arr, size_t i);

/* Serialize a value to a malloc'd compact JSON string (caller frees). */
char *jl_stringify(const jl_val_t *v);

/* Build helpers. */
jl_val_t *jl_new_string(const char *s);
jl_val_t *jl_new_number(double n);
jl_val_t *jl_new_bool(int b);
jl_val_t *jl_new_object(void);
jl_val_t *jl_new_array(void);
void jl_obj_set(jl_val_t *obj, const char *key, jl_val_t *val); /* moves val */
void jl_arr_push(jl_val_t *arr, jl_val_t *val);                /* moves val */
void jl_set_type_string(jl_val_t *v, const char *s);
void jl_set_type_number(jl_val_t *v, double n);

#endif
