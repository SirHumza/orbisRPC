/* appdb.c - display names from /system_data/priv/mms/app.db (read-only).
 * Method follows the proven on-console pattern (SonicLoader et al.): SQLite
 * query, never raw byte-scanning (an earlier revision byte-scanned app.db
 * and returned wrong names: the file reads fine, the parse was garbage).
 * Tiers, first hit wins:
 *   1. tbl_appbrowse.titleName (when the firmware carries it)
 *   2. tbl_appinfo val for key TITLE / TITLE_xx (localized display titles)
 *   3. /user/appmeta/<id>/param.json title fields (ShadowMountPlus pattern)
 * Every tier is schema-guarded and fail-soft: missing tables, missing keys,
 * unreadable files, and locked DBs all fall through to the next source.
 * Host-testable via appdb_title_from() with a synthetic DB path. */
#include "appdb.h"
#include "jsonlite.h"
#include "log.h"
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include "sqlite3.h"

#define APPDB_PATH "/system_data/priv/mms/app.db"
#define APPMETA_BASE "/user/appmeta"

static int valid_tid(const char *t){
    if(!t) return 0;
    size_t n = strlen(t);
    if(n != 9) return 0;
    for(size_t i = 0; i < n; i++){
        char c = t[i];
        if(!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))) return 0;
    }
    return 1;
}

static int table_exists(sqlite3 *db, const char *name){
    sqlite3_stmt *st = NULL;
    int found = 0;
    if(sqlite3_prepare_v2(db, "SELECT 1 FROM sqlite_master WHERE type='table' AND name=?1 LIMIT 1;",
                           -1, &st, NULL) != SQLITE_OK)
        return 0;
    sqlite3_bind_text(st, 1, name, -1, SQLITE_TRANSIENT);
    if(sqlite3_step(st) == SQLITE_ROW) found = 1;
    sqlite3_finalize(st);
    return found;
}

/* Copy first non-empty TEXT result of a one-row query. 0 on hit. */
static int query_name(sqlite3 *db, const char *sql, const char *titleId,
                      char *out, size_t cap){
    sqlite3_stmt *st = NULL;
    int rc = -1;
    if(sqlite3_prepare_v2(db, sql, -1, &st, NULL) != SQLITE_OK) return -1;
    sqlite3_bind_text(st, 1, titleId, -1, SQLITE_TRANSIENT);
    if(sqlite3_step(st) == SQLITE_ROW){
        const unsigned char *v = sqlite3_column_text(st, 0);
        if(v && v[0]){
            strncpy(out, (const char *)v, cap - 1);
            out[cap - 1] = 0;
            if(out[0] && strlen(out) > 1) rc = 0;
        }
    }
    sqlite3_finalize(st);
    return rc;
}

/* param.json title fields: small single read, jsonlite parse. Best-effort. */
static int paramjson_title(const char *appmeta_base, const char *titleId,
                           char *out, size_t cap){
    char path[256];
    int n = snprintf(path, sizeof path, "%s/%s/param.json",
                     appmeta_base ? appmeta_base : APPMETA_BASE, titleId);
    if(n <= 0 || (size_t)n >= sizeof path) return -1;
    int fd = open(path, O_RDONLY);
    if(fd < 0) return -1;
    char buf[4096];
    ssize_t r = read(fd, buf, sizeof buf - 1);
    close(fd);
    if(r <= 0) return -1;
    buf[r] = 0;
    jl_val_t *root = jl_parse(buf, (size_t)r);
    if(!root) return -1;
    static const char *keys[] = { "titleName", "title", "name", NULL };
    int rc = -1;
    for(int i = 0; keys[i]; i++){
        const jl_val_t *v = jl_obj_get(root, keys[i]);
        if(v && v->type == JL_STRING && v->str && strlen(v->str) > 1){
            strncpy(out, v->str, cap - 1);
            out[cap - 1] = 0;
            rc = 0;
            break;
        }
    }
    jl_free(root);
    return rc;
}

int appdb_title_from(const char *dbpath, const char *appmeta_base,
                     const char *titleId, char *out, size_t cap){
    if(!out || cap == 0) return -1;
    out[0] = 0;
    if(!valid_tid(titleId)) return -1;
    if(!dbpath) dbpath = APPDB_PATH;

    sqlite3 *db = NULL;
    int orc = sqlite3_open_v2(dbpath, &db,
                              SQLITE_OPEN_READONLY | SQLITE_OPEN_NOMUTEX, NULL);
    if(orc != SQLITE_OK){
        if(db) sqlite3_close(db);
        return -1;
    }
    sqlite3_busy_timeout(db, 2000);

    /* Tier 1: friendly browse name. */
    if(table_exists(db, "tbl_appbrowse")){
        if(query_name(db, "SELECT titleName FROM tbl_appbrowse WHERE titleId=?1 "
                          "AND titleName IS NOT NULL AND titleName<>'' LIMIT 1;",
                      titleId, out, cap) == 0){
            log_msg("name: %s via appdb-browse", out);
            sqlite3_close(db);
            return 0;
        }
    }
    /* Tier 2: localized TITLE keys; ORDER BY key prefers bare TITLE
     * (system language) over TITLE_00..NN variants. */
    if(table_exists(db, "tbl_appinfo")){
        if(query_name(db, "SELECT val FROM tbl_appinfo WHERE titleId=?1 "
                          "AND (key='TITLE' OR key LIKE 'TITLE\\_%' ESCAPE '\\') "
                          "AND val IS NOT NULL AND val<>'' ORDER BY key LIMIT 1;",
                      titleId, out, cap) == 0){
            log_msg("name: %s via appdb-info", out);
            sqlite3_close(db);
            return 0;
        }
    }
    sqlite3_close(db);

    /* Tier 3: staged appmeta descriptor (no DB needed). */
    if(paramjson_title(appmeta_base, titleId, out, cap) == 0){
        log_msg("name: %s via paramjson", out);
        return 0;
    }
    return -1;
}

int appdb_title(const char *titleId, char *out, size_t cap){
    return appdb_title_from(NULL, NULL, titleId, out, cap);
}
