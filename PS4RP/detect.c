/* detect.c
 * Foreground-game detection on PS4.
 *   - sceUserServiceGetForegroundUser -> user_id >= 0  => a title holds the foreground.
 *   - We then attempt to name that title:
 *       a) via app.db tbl_app_static (title_id/title_name) near the foreground row, and
 *       b) /data/app/<titleid>/app.xml <title> read for the most-recently-modified dir.
 *   If the title can't be named, we still report active (game is running) and let the
 *   caller post a generic "On PS4" presence.
 *
 * NOTE: mapping the foreground *user_id* to that title's pid/titleId is not exposed
 * by the public SDK, so the name is best-effort (heuristic). The core signal
 * (a game is in the foreground) is reliable.
 */
#include "detect.h"
#include "cfg.h"
#include "log.h"
#include <orbis/UserService.h>
#include <orbis/libkernel.h>
#include <orbis/Sysmodule.h>
#include <orbis/Net.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <dirent.h>

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

int detect_foreground_active(void){
    user_init();
    int32_t fg = -1;
    int32_t rc = sceUserServiceGetForegroundUser(&fg);
    if(rc != 0){ log_msg("GetForegroundUser err %d", rc); return 0; }
    return (fg >= 0) ? 1 : 0;
}

static long scan_recent_titleid(char *out, size_t cap){
    DIR *d = opendir("/data/app");
    if(!d) return -1;
    struct dirent *e; long best=-1; out[0]=0;
    struct stat st; /* we use d_type where avail */
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
    /* skip to the value */
    p = strchr(p, '>');
    if(!p) return -1;
    p++; /* first char of value */
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
        /* crude: copy bytes following the titleId as the name until a non-printable */
        const char *q=strstr(b,titleId);
        if(q){
            q+=strlen(titleId); /* past the id */
            /* skip until a plausible name starts */
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
