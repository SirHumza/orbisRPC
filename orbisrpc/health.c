/* health.c - unclean-boot marker + consecutive-crash counter + rollback.
 *
 * Correct semantics: a normal reboot must NEVER count as a crash.
 *   boot N:   write boot.dirty marker
 *   healthy:  (stable runtime) remove marker + reset crash.count to 0
 *   boot N+1: marker present? previous boot died before healthy -> count++.
 *             marker absent?  previous boot was clean -> keep count (0).
 *   3 consecutive unclean boots -> safe mode.
 */
#include "health.h"
#include "updater.h"
#include "log.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <errno.h>

#ifdef HEALTH_TESTABLE
static char g_base[256] = "/data/orbisRPC";
void health_set_base(const char *dir){
    if(dir && dir[0]){
        snprintf(g_base, sizeof g_base, "%s", dir);
        g_base[sizeof g_base - 1] = 0;
    }
}
static void crash_path(char *out, size_t cap){
    snprintf(out, cap, "%s/crash.count", g_base);
}
static void dirty_path(char *out, size_t cap){
    snprintf(out, cap, "%s/boot.dirty", g_base);
}
#else
static void crash_path(char *out, size_t cap){
    snprintf(out, cap, "%s", CRASH_PATH);
}
static void dirty_path(char *out, size_t cap){
    snprintf(out, cap, "%s", DIRTY_PATH);
}
#endif

static int path_exists(const char *p){
    struct stat st;
    return stat(p, &st) == 0;
}

static int read_count(void){
    char cp[320];
    crash_path(cp, sizeof cp);
    FILE *f = fopen(cp, "rb");
    if(!f) return 0;
    int n = 0;
    if(fscanf(f, "%d", &n) != 1) n = 0;
    fclose(f);
    if(n < 0) n = 0;
    if(n > 1000) n = 1000;
    return n;
}

static void write_count(int n){
    char cp[320], tmp[352];
    crash_path(cp, sizeof cp);
    snprintf(tmp, sizeof tmp, "%s.new", cp);
    FILE *f = fopen(tmp, "wb");
    if(!f) return;
    fprintf(f, "%d\n", n);
    if(fflush(f) != 0){ fclose(f); remove(tmp); return; }
    int fd = fileno(f);
    if(fd >= 0) fsync(fd);
    fclose(f);
    rename(tmp, cp);
}

static void write_dirty(void){
    char dp[320], tmp[352];
    dirty_path(dp, sizeof dp);
    snprintf(tmp, sizeof tmp, "%s.new", dp);
    FILE *f = fopen(tmp, "wb");
    if(!f){
        /* best-effort: try direct write (dir may not allow tmp+rename) */
        f = fopen(dp, "wb");
        if(!f) return;
        fprintf(f, "dirty\n");
        fclose(f);
        return;
    }
    fprintf(f, "dirty\n");
    if(fflush(f) != 0){ fclose(f); remove(tmp); return; }
    int fd = fileno(f);
    if(fd >= 0) fsync(fd);
    fclose(f);
    rename(tmp, dp);
}

static void clear_path(const char *p){
    remove(p);
}

int health_boot_note_crash(void){
    char dp[320];
    dirty_path(dp, sizeof dp);
    int unclean = path_exists(dp);
    int n = read_count();
    /* Marker first, count second: a crash between the two still leaves
     * the marker behind, so the death is counted next boot instead of
     * being lost. */
    write_dirty();
    if(unclean){
        n += 1;
        write_count(n);
    }
    return n >= SAFE_THRESHOLD ? 1 : 0;
}

void health_mark_healthy(void){
    char cp[320], dp[320];
    crash_path(cp, sizeof cp);
    dirty_path(dp, sizeof dp);
    clear_path(dp);
    clear_path(cp);
}

void health_mark_clean(void){
    health_mark_healthy();
}

int health_check_binary(const char *path){
    FILE *f = fopen(path, "rb");
    if(!f) return 0;
    unsigned char h[64];
    size_t n = fread(h, 1, sizeof h, f);
    long sz = -1;
    if(fseek(f, 0, SEEK_END) == 0) sz = ftell(f);
    fclose(f);
    if(n < 64 || sz <= 0) return 0;
    return updater_image_ok(h, n) && (size_t)sz <= 8u*1024u*1024u;
}
