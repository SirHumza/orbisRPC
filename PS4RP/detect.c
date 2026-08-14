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

static int s_user_inited = 0;
static int user_init(void){
    if(s_user_inited) return 0;
    /* UserService is an external module -> load via internal id */
    uint32_t r = sceSysmoduleLoadModuleInternal(ORBIS_SYSMODULE_INTERNAL_USER_SERVICE);
    if(r != 0){ int32_t ir=(int32_t)r; log_msg("load UserService fail %d", ir); }
    int32_t rc = sceUserServiceInitialize(NULL);
    (void)rc;
    s_user_inited = 1;
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
    if(h){ s_is_app_launched = (shellcore_isapplaunched_fn)(uintptr_t)dlsym(h, "sceShellCoreUtilIsAppLaunched"); }
    log_msg("ShellCoreUtil IsAppLaunched %s", s_is_app_launched ? "resolved" : "unavailable");
    return s_is_app_launched != NULL;
}

/* --- foreground-active: the core "a game is running" signal ---------- */
int detect_foreground_active(void){
    if(scu_init() && s_is_app_launched){
        int on = s_is_app_launched();
        return (on != 0) ? 1 : 0;   /* 0 when sitting on the home screen */
    }
    /* fallback: foreground user exists */
    user_init();
    int32_t fg = -1;
    int32_t rc = sceUserServiceGetForegroundUser(&fg);
    if(rc != 0){ log_msg("GetForegroundUser err %d", rc); return 0; }
    return (fg >= 0) ? 1 : 0;
}

/* --- title naming ---------------------------------------------------- */
static long scan_recent_titleid(char *out, size_t cap){
    DIR *d = opendir("/data/app");
    if(!d) return -1;
    struct dirent *e; long best=-1; out[0]=0;
    char path[256]; size_t plen;
    while((e=readdir(d))){
        if(e->d_name[0]=='.') continue;
        size_t l=strlen(e->d_name);
        if(l!=9 || strncmp(e->d_name,"CUSA",4)!=0) continue;
        plen=snprintf(path,sizeof path,"/data/app/%s/app.xml",e->d_name);
        if(plen>=sizeof path) continue;
        struct stat st2;
        if(stat(path,&st2)==0){
            const char *ti = e->d_name;
            if(st2.st_mtime > best || best<0){ best=st2.st_mtime; if(cap>1)strncpy(out,ti,cap-1); out[cap-1]=0; }
        }
    }
    closedir(d);
    return (out[0])? 0 : -1;
}

static int appxml_title(const char *titleId, char *out, size_t cap){
    char path[256]; snprintf(path,sizeof path,"/data/app/%s/app.xml",titleId);
    int fd=open(path,O_RDONLY); if(fd<0) return -1;
    char buf[640]; ssize_t n=read(fd,buf,sizeof buf-1); close(fd);
    if(n<=0) return -1; buf[n]=0;
    char *p=strstr(buf,"<title>");
    if(!p) p=strstr(buf,"titleName");
    if(!p) return -1;
    p = strchr(p, '>');
    if(!p) return -1;
    p++;
    size_t i=0;
    while(*p && *p!='<' && *p!='\n' && *p!='\r' && i<cap-1){ out[i++]=*p++; }
    out[i]=0;
    return (i>0)?0:-1;
}

static void appdb_title(const char *titleId, char *out, size_t cap){
    out[0]=0;
    const char *dbs[]={ "/system_data/priv/app.db", "/system_data/etc/app.db", NULL };
    for(int k=0;dbs[k];k++){
        FILE *f=fopen(dbs[k],"rb");
        if(!f) continue;
        fseek(f,0,SEEK_END); long sz=ftell(f); fseek(f,0,SEEK_SET);
        if(sz<=0||sz>1L<<21){ fclose(f); continue; }
        char *b=(char*)malloc((size_t)sz+1);
        if(!b){fclose(f);return;}
        size_t rd=fread(b,1,(size_t)sz,f); fclose(f); b[rd]=0;
        const char *q=strstr(b,titleId);
        if(q){
            q+=strlen(titleId);
            while(*q && *q!='<'){
                if(*q>='A'&&*q<='z'){ break; }
                q++;
            }
            size_t i=0;
            while(*q && *q!='<' && *q!='\n' && *q!='\r' && i<cap-1){ out[i++]=*q++; }
            out[i]=0;
            free(b);
            if(i>0) return;
        }
        free(b);
    }
}

int detect_current_game(char *out_name, size_t cap, char *out_path, size_t p_cap){
    if(!detect_foreground_active()) return -1;
    char titleId[16]=""; int named=0;
    if(scan_recent_titleid(titleId,sizeof titleId)==0){
        if(appxml_title(titleId, out_name, cap)==0){ named=1; }
        if(!named){ appdb_title(titleId, out_name, cap); named=(out_name[0]!=0); }
        if(!named){ strncpy(out_name, titleId, cap-1); out_name[cap-1]=0; }
    }else{
        strncpy(out_name, "(unknown game)", cap-1); out_name[cap-1]=0;
    }
    if(out_path){ snprintf(out_path, p_cap, "/data/PS4RP/.lastgame/%s", titleId[0]?titleId:"unknown"); }
    log_msg("detect: active name=%s tid=%s", out_name, titleId);
    return 0;
}