/* appdb.h - display names from the system app.db (read-only SQLite). */
#ifndef APPDB_H
#define APPDB_H
#include <stddef.h>
/* Resolve a display name for a 9-char title ID. 0 on success. */
int appdb_title(const char *titleId, char *out, size_t cap);
/* Test seam: explicit DB + appmeta paths (NULL = on-console defaults). */
int appdb_title_from(const char *dbpath, const char *appmeta_base,
                     const char *titleId, char *out, size_t cap);
#endif
