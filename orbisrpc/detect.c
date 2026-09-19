/* detect.c
 * Foreground-game detection on PS4.
 *
 * Primary signal: sceShellCoreUtilIsAppLaunched() — resolved at runtime via
 * dlopen/dlsym (libSceShellCoreUtil.sprx). Returns 1 when a user app (game)
 * is in the foreground vs the home screen. This is the reliable "playing"
 * indicator; it stays 0 on the home screen so presence clears.
 *
 * Fallback (if ShellCoreUtil can't be resolved in the payload context):
 * sceUserServiceGetForegroundUser >= 0 + most-recent app.xml heuristic.
 *
 * Title naming is best-effort:
 *   a) /data/app/<titleid>/app.xml <title> for the most-recently-modified dir
 *   b) app.db tbl_app_static scan (crude byte scan)
 *   c) titleId (CUSAxxxxx) as last resort
 *
 * NOTE: mapping the foreground app to its pid/titleId is not exposed by the
 * public SDK, so the NAME is heuristic. The core signal (a game is in the
 * foreground) is reliable via ShellCoreUtil.
 */
#include "detect.h"
#include "log.h"
#include "sfo.h"
#include "tmdb.h"
#include "nametable.h"
#include <orbis/UserService.h>
#include <orbis/libkernel.h>
#include <orbis/Sysmodule.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <dirent.h>
#include <sys/stat.h>
#include <errno.h>

static int s_user_inited = 0;
static int s_user_ok = 0;
static int user_init(void){
    if(s_user_inited) return s_user_ok ? 0 : -1;
    s_user_inited = 1;
    /* UserService is an external module -> load via internal id */
    uint32_t r = sceSysmoduleLoadModuleInternal(ORBIS_SYSMODULE_INTERNAL_USER_SERVICE);
    if(r != 0){ int32_t ir=(int32_t)r; log_msg("load UserService fail %d", ir); return -1; }
    int32_t rc = sceUserServiceInitialize(NULL);
    if(rc != 0){ log_msg("UserService init fail %d", rc); return -1; }
    s_user_ok = 1;
    return 0;
}

/* --- ShellCoreUtil runtime resolution ------------------------------- */
/* PS4 SDK has no <dlfcn.h>; libkernel exports dlopen/dlsym directly. */
extern void *dlopen(const char *filename, int flags);
extern void *dlsym(void *handle, const char *symbol);
typedef int (*shellcore_isapplaunched_fn)(void);
static shellcore_isapplaunched_fn s_is_app_launched = NULL;
static int s_scu_tried = 0;

static int scu_init(void){
    if(s_scu_tried) return s_is_app_launched != NULL;
    s_scu_tried = 1;
    void *h = dlopen("libSceShellCoreUtil.sprx", 0);
    if(!h){ log_msg("ShellCoreUtil unavailable (dlopen fail); using foreground-user fallback"); return 0; }
    s_is_app_launched = (shellcore_isapplaunched_fn)(uintptr_t)dlsym(h, "sceShellCoreUtilIsAppLaunched");
    log_msg("ShellCoreUtil IsAppLaunched %s", s_is_app_launched ? "resolved" : "unavailable (dlsym fail)");
    return s_is_app_launched != NULL;
}

/* --- foreground-active: the core "a game is running" signal ---------- */
int detect_foreground_active(void){
    if(scu_init() && s_is_app_launched){
        int on = s_is_app_launched();
        return (on != 0) ? 1 : 0;   /* 0 when sitting on the home screen */
    }
    /* fallback: foreground user exists. If UserService itself failed,
     * report inactive instead of guessing "playing". */
    if(user_init() != 0) return 0;
    int32_t fg = -1;
    int32_t rc = sceUserServiceGetForegroundUser(&fg);
    if(rc != 0){ log_msg("GetForegroundUser err %d", rc); return 0; }
    return (fg >= 0) ? 1 : 0;
}

/* --- title naming ---------------------------------------------------- */
static int is_title_prefix(const char *n){
    /* CUSA (PS4) + PPSA (PS5-backport) + EU/JP/indie variants */
    return strncmp(n,"CUSA",4)==0 || strncmp(n,"PPSA",4)==0 ||
           strncmp(n,"PCSE",4)==0 || strncmp(n,"PCSB",4)==0 ||
           strncmp(n,"PCSG",4)==0 || strncmp(n,"EPSA",4)==0;
}
/* last resolved titleId (for Discord asset key); valid after a successful
 * detect_current_game / detect_name_for_title. */
static char s_last_titleid[16] = "";
static char s_last_art[256] = "";
const char *detect_last_titleid(void){ return s_last_titleid[0] ? s_last_titleid : NULL; }
const char *detect_last_art(void){ return s_last_art[0] ? s_last_art : NULL; }
static void remember_titleid(const char *ti){
    if(!ti) return;
    strncpy(s_last_titleid, ti, sizeof s_last_titleid - 1);
    s_last_titleid[sizeof s_last_titleid - 1] = 0;
}
static long scan_one_appdir(const char *base, char *out, size_t cap, long best){
    DIR *d = opendir(base);
    if(!d) return best;
    struct dirent *e;
    char path[256]; size_t plen;
    while((e=readdir(d))){
        if(e->d_name[0]=='.') continue;
        size_t l=strlen(e->d_name);
        if(l!=9 || !is_title_prefix(e->d_name)) continue;
        plen=snprintf(path,sizeof path,"%s/%s/app.xml",base,e->d_name);
        if(plen>=sizeof path) continue;
        struct stat st2;
        if(stat(path,&st2)==0){
            const char *ti = e->d_name;
            if(st2.st_mtime > best || best<0){ best=st2.st_mtime; if(cap>1)strncpy(out,ti,cap-1); out[cap-1]=0; }
        }
    }
    closedir(d);
    return best;
}
static long scan_recent_titleid(char *out, size_t cap){
    out[0]=0;
    long best = scan_one_appdir("/user/app", out, cap, -1);
    best = scan_one_appdir("/data/app", out, cap, best);
    return (out[0])? 0 : -1;
}

/* Best name on the box: /user/appmeta/<id>/pronunciation.xml holds the
 * display title in its first <text> element (works for every game that
 * ships speech data, e.g. Terraria). Small file, single read. */
static int pronunc_title(const char *titleId, char *out, size_t cap){
    char path[256]; snprintf(path,sizeof path,"/user/appmeta/%s/pronunciation.xml",titleId);
    int fd=open(path,O_RDONLY);
    if(fd<0){ log_msg("appmeta open fail %s err=%d", path, errno); return -1; }
    char buf[2048]; ssize_t n=read(fd,buf,sizeof buf-1); close(fd);
    if(n<=0) return -1; buf[n]=0;
    const char *p=strstr(buf,"<text>");
    if(!p) return -1;
    p+=6;
    while(*p==' '||*p=='\t'||*p=='\r'||*p=='\n') p++;
    size_t i=0;
    while(*p && *p!='<' && *p!='\n' && *p!='\r' && i<cap-1){ out[i++]=*p++; }
    out[i]=0;
    while(i>0 && (out[i-1]==' '||out[i-1]=='\t')) out[--i]=0;
    return (i>1)?0:-1;
}

/* Game-process-safe TITLE read: the running game's own param.sfo via the
 * app0 mount plus on-disc sce_sys copies. Small single read, works on ANY
 * console with zero setup. Returns 0 on success. */
static int sfo_file_title(const char *titleId, char *out, size_t cap){
    char path[256];
    const char *fixed[] = { "app0/sce_sys/param.sfo", "/app0/sce_sys/param.sfo", NULL };
    for(int i = 0; fixed[i]; i++){
        int fd = open(fixed[i], O_RDONLY);
        if(fd < 0) continue;
        unsigned char buf[4096];
        ssize_t n = read(fd, buf, sizeof buf);
        close(fd);
        if(n > 0 && sfo_title(buf, (size_t)n, out, cap) == 0) return 0;
    }
    if(titleId && titleId[0]){
        const char *bases[] = { "/user/app", "/data/app", NULL };
        for(int b = 0; bases[b]; b++){
            snprintf(path, sizeof path, "%s/%s/sce_sys/param.sfo", bases[b], titleId);
            int fd = open(path, O_RDONLY);
            if(fd < 0) continue;
            unsigned char buf[4096];
            ssize_t n = read(fd, buf, sizeof buf);
            close(fd);
            if(n > 0 && sfo_title(buf, (size_t)n, out, cap) == 0) return 0;
        }
    }
    return -1;
}

static int appxml_title(const char *titleId, char *out, size_t cap){
    const char *bases[] = { "/user/app", "/data/app", NULL };
    for(int b=0; bases[b]; b++){
    char path[256]; snprintf(path,sizeof path,"%s/%s/app.xml",bases[b],titleId);
    int fd=open(path,O_RDONLY); if(fd<0) continue;
    char buf[640]; ssize_t n=read(fd,buf,sizeof buf-1); close(fd);
    if(n<=0) continue; buf[n]=0;
    char *p=strstr(buf,"<title>");
    if(!p) p=strstr(buf,"titleName");
    if(!p) continue;
    p = strchr(p, '>');
    if(!p) continue;
    p++;
    while(*p==' '||*p=='\t'||*p=='\r'||*p=='\n') p++; /* trim leading ws */
    size_t i=0;
    while(*p && *p!='<' && *p!='\n' && *p!='\r' && i<cap-1){ out[i++]=*p++; }
    out[i]=0;
    while(i>0 && (out[i-1]==' '||out[i-1]=='\t')) out[--i]=0; /* trim trailing */
    if(i>0) return 0;
    }
    return -1;
}

/* NOTE: an earlier revision byte-scanned app.db for titles. Removed:
 * the packed record layout makes the title/contentId boundary ambiguous
 * and the scanner returned wrong names (worse than raw IDs). Exact
 * sources above plus Sony TMDB cover every case instead. */

int detect_current_game(char *out_name, size_t cap, char *out_path, size_t p_cap){
    if(!out_name || cap==0) return -1;
    out_name[0] = 0;
    if(out_path && p_cap) out_path[0] = 0;
    if(!detect_foreground_active()) return -1;
    char titleId[16]=""; int named=0;
    if(scan_recent_titleid(titleId,sizeof titleId)==0){
        remember_titleid(titleId);
        s_last_art[0] = 0;
        /* cheap, game-process-safe sources first; Sony TMDB (network)
         * resolves anything local sources miss, on any console. */
        if(pronunc_title(titleId, out_name, cap)==0){ named=1; log_msg("name: %s via appmeta", out_name); }
        if(!named){ if(sfo_file_title(titleId, out_name, cap)==0){ named=1; log_msg("name: %s via sfo", out_name); } }
        if(!named){ if(appxml_title(titleId, out_name, cap)==0){ named=1; log_msg("name: %s via appxml", out_name); } }
        if(!named){ if(nametable_lookup(titleId, out_name, cap)==0){ named=1; log_msg("name: %s via table", out_name); } }
        if(!named){
            char art[256] = "";
            if(tmdb_resolve(titleId, out_name, cap, art, sizeof art)==0){
                named = 1;
                strncpy(s_last_art, art, sizeof s_last_art-1);
            } else s_last_art[0] = 0;
        }
        if(!named){ strncpy(out_name, titleId, cap-1); out_name[cap-1]=0; }
    }else{
        strncpy(out_name, "(unknown game)", cap-1); out_name[cap-1]=0;
    }
    if(out_path){ snprintf(out_path, p_cap, "/data/orbisRPC/.lastgame/%s", titleId[0]?titleId:"unknown"); }
    return 0;
}

/* Resolve a display name for a KNOWN title id (plugin mode: the plugin is
 * loaded into the game process and knows the titleid from the GoldHEN SDK,
 * so we skip the foreground-app heuristics entirely). */
int detect_name_for_title(const char *titleId, char *out_name, size_t cap){
    if(!titleId || !titleId[0] || !out_name || cap==0) return -1;
    remember_titleid(titleId);
    s_last_art[0] = 0;
    /* Game-process-safe only: small reads plus one bounded network
     * lookup; no multi-megabyte scans anywhere in this codebase. */
    if(pronunc_title(titleId, out_name, cap)==0){ log_msg("name: %s via appmeta", out_name); return 0; }
    else log_msg("name: appmeta miss for %s", titleId);
    if(sfo_file_title(titleId, out_name, cap)==0){ log_msg("name: %s via sfo", out_name); return 0; }
    if(appxml_title(titleId, out_name, cap)==0){ log_msg("name: %s via appxml", out_name); return 0; }
    if(nametable_lookup(titleId, out_name, cap)==0){ log_msg("name: %s via table", out_name); return 0; }
    {
        char art[256] = "";
        if(tmdb_resolve(titleId, out_name, cap, art, sizeof art)==0){
            strncpy(s_last_art, art, sizeof s_last_art-1);
            return 0; /* tmdb_resolve already logged */
        }
        s_last_art[0] = 0;
    }
    strncpy(out_name, titleId, cap-1); out_name[cap-1]=0;
    return 0;
}