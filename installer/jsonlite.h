/* jsonlite.h - minimal JSON reader/writer for the installer config.
 *
 * Reconstructed to match the API that icfg.c expects. The original
 * orbisrpc/jsonlite.c was not present alongside the installer sources, so this
 * is an independent implementation of the same surface, not a copy.
 *
 * Object members and array elements are singly-linked through `next`, and an
 * object's member carries its `key`. Insertion order is preserved, so
 * stringify() round-trips a config file without reshuffling it.
 */
#ifndef INSTALLER_JSONLITE_H
#define INSTALLER_JSONLITE_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
	JL_NULL = 0,
	JL_BOOL,
	JL_NUMBER,
	JL_STRING,
	JL_ARRAY,
	JL_OBJECT
} jl_type_t;

typedef struct jl_val
{
	jl_type_t    type;

	int          num_is_int;
	long         inum;
	double       num;
	int          boolean;

	char        *str;   /* JL_STRING: NUL-terminated, owned */

	struct jl_val *child; /* first member/element, owned */
	struct jl_val *next;  /* next sibling, owned */

	char        *key;    /* member key when this value sits in an object */
} jl_val_t;

/* Parses `len` bytes of JSON. Returns NULL on malformed input. The buffer is
 * not retained. */
jl_val_t *jl_parse(const char *text, size_t len);

/* Serialises to a malloc'd, NUL-terminated string. Caller frees.
 * Returns NULL on allocation failure. */
char *jl_stringify(const jl_val_t *value);

/* Recursively frees a value and everything it owns. Safe on NULL. */
void jl_free(jl_val_t *value);

/* Constructors. All return NULL only on allocation failure. */
jl_val_t *jl_new_object(void);
jl_val_t *jl_new_array(void);
jl_val_t *jl_new_string(const char *text);
jl_val_t *jl_new_int(long value);
jl_val_t *jl_new_double(double value);
jl_val_t *jl_new_bool(int value);

/* Object access. jl_obj_get returns NULL when absent. jl_obj_set takes
 * ownership of `value` and frees any previous value for the same key. */
jl_val_t *jl_obj_get(const jl_val_t *object, const char *key);
void     jl_obj_set(jl_val_t *object, const char *key, jl_val_t *value);

/* Arrays are parsed and freed but never built by hand: nothing in the installer
 * constructs a JL_ARRAY, so there is no array-append entry point. */

#ifdef __cplusplus
}
#endif

#endif
