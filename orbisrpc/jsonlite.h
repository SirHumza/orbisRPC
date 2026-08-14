/* jsonlite.h - tiny JSON parser/serializer (self contained). */
#ifndef JSONLITE_H
#define JSONLITE_H
#include <stddef.h>
#include <stdint.h>
typedef enum { JL_NULL, JL_BOOL, JL_NUMBER, JL_STRING, JL_OBJECT, JL_ARRAY } jl_type_t;
typedef struct jl_val {
    jl_type_t type;
    char  *str;     /* string text OR object-entry key */
    size_t strlen;
    double num;
    struct jl_val *child;   /* object: first pair ; array: first elem */
    struct jl_val *next;    /* object pair chain OR array elem chain */
    int32_t count;
} jl_val_t;
typedef struct { const char *cur; const char *end; int err; } jl_parse_t;
jl_val_t *jl_parse(const char *s, size_t len);
void jl_free(jl_val_t *v);
/* NOTE: the tree is mutable; getters return non-const so results can be
 * passed to jl_obj_set / jl_arr_push without const-dropping warnings. */
jl_val_t *jl_obj_get(jl_val_t *obj, const char *key);
jl_val_t *jl_arr_at(jl_val_t *arr, size_t i);
char *jl_stringify(const jl_val_t *v);
jl_val_t *jl_new_string(const char *s);
jl_val_t *jl_new_number(double n);
jl_val_t *jl_new_bool(int b);
jl_val_t *jl_new_object(void);
jl_val_t *jl_new_array(void);
void jl_obj_set(jl_val_t *obj, const char *key, jl_val_t *val);
void jl_arr_push(jl_val_t *arr, jl_val_t *val);
#endif