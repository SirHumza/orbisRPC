/* http.c - OAuth2 token endpoint via SceHttp. */
#include "http.h"
#include "cfg.h"
#include "log.h"
#include <stdbool.h>
#include <orbis/Sysmodule.h>
#include <orbis/Net.h>
#include <orbis/Http.h>
#include <orbis/Ssl.h>
#include <time.h>
#include <string.h>
#include <stdlib.h>

/* Plausible desktop-client UA rather than an app name (fewer obvious
 * selfbot fingerprints when talking to Discord's API). */
#define UA "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) discord/1.0.9175 Chrome/122.0.6261.112 Electron/30.0.8 Safari/537.36"

#define NET_POOL  (64 * 1024)
#define SSL_PL     (128 * 1024)
#define HTTP_PL    (128 * 1024)

static int s_http_mem = 0, s_ssl_mem = 0, s_http_ctx = 0; /* ids */

/* NOTE: SceHttp/SSL headers declare funcs as void() stubs; we call them
 * with the real argument layout (no local redeclaration to avoid conflicts). */
static int http_init(void){
    if(s_http_ctx) return 0;
    int r;
    uint32_t ur = sceSysmoduleLoadModuleInternal(ORBIS_SYSMODULE_INTERNAL_NET);
    r=(int)ur;
    if(r<0){log_msg("load NET fail %d",r);return -1;}
    if((r=sceSysmoduleLoadModuleInternal(ORBIS_SYSMODULE_INTERNAL_HTTP))<0){log_msg("load HTTP fail %d",r);return -2;}
    if((r=sceSysmoduleLoadModuleInternal(ORBIS_SYSMODULE_INTERNAL_SSL))<0){log_msg("load SSL fail %d",r);return -3;}
    if((r=sceNetInit())<0){log_msg("sceNetInit %d",r);return -4;}
    s_http_mem=(int)sceNetPoolCreate("orbisrpcN",NET_POOL,0); if(s_http_mem<0){log_msg("netpool %d",s_http_mem);return -5;}
s_ssl_mem=(int)sceSslInit(SSL_PL); if(s_ssl_mem<0){log_msg("sslinit %d",s_ssl_mem);return -6;}
    if((r=sceHttpInit(s_http_mem,s_ssl_mem,HTTP_PL))<0){log_msg("httpinit %d",r);return -7;}
    s_http_ctx=r;
    return 0;
}

/* --- json helpers (avoid coupling to cfg's parser) --- */
static void jstr(const char *js,const char *key,char *out,size_t cap){
    char k[64]; size_t kl=strlen(key); if(kl>=sizeof k){if(out)*out=0;return;}
    k[0]='\"'; memcpy(k+1,key,kl); k[1+kl]='\"'; k[2+kl]=0;
    const char *p=strstr(js,k); if(!p||!out){if(out)*out=0;return;}
    p+=2+kl; while(*p==' '||*p==':')p++;
    size_t i=0;
    if(*p=='"'){ p++; while(*p&&*p!='"'&&i<cap-1){out[i++]=*p++;} }
    else { while(*p&&*p>='0'&&*p<='9'&&i<cap-1){out[i++]=*p++;} }
    out[i]=0;
}
static long jnum(const char *js,const char *key){
    char k[64]; size_t kl=strlen(key); if(kl>=sizeof k)return 0;
    k[0]='\"'; memcpy(k+1,key,kl); k[1+kl]='\"'; k[2+kl]=0;
    const char *p=strstr(js,k); if(!p)return 0;
    p+=2+kl; while(*p==' '||*p==':')p++; return strtol(p,NULL,10);
}

int http_oauth_token(const char *client_id,const char *client_secret,
                     const char *code,const char *refresh_token,
                     char *access_token,size_t at_cap,
                     char *refresh_out,size_t rt_cap,
                     int64_t *expires_out_epoch){
    if(http_init()) return -10;
    int32_t tpl=0,conn=0,req=0,rc=0,status=0;
    if(!(client_id&&*client_id)){log_msg("no client_id");return -11;}
    char body[1024]; int blen=0;
    blen+=snprintf(body+blen,sizeof body-blen,"client_id=%s",client_id?client_id:"");
    if(client_secret&&*client_secret) blen+=snprintf(body+blen,sizeof body-blen,"&client_secret=%s",client_secret);
    if(code&&*code) blen+=snprintf(body+blen,sizeof body-blen,
        "&grant_type=authorization_code&code=%s&redirect_uri=%s", code, "http://localhost:6770/callback");
    else if(refresh_token&&*refresh_token) blen+=snprintf(body+blen,sizeof body-blen,
        "&grant_type=refresh_token&refresh_token=%s", refresh_token);
    else blen+=snprintf(body+blen,sizeof body-blen,"&grant_type=client_credentials&scope=applications.commands");
    const char *URL="https://discord.com/api/oauth2/token";
    tpl=sceHttpCreateTemplate(s_http_ctx, UA, ORBIS_HTTP_VERSION_1_1, 0); if(tpl<0){log_msg("tmpl %d",tpl);rc=-1;goto done;}
    conn=sceHttpCreateConnectionWithURL(tpl, URL, 0); if(conn<0){log_msg("conn %d",conn);rc=-2;goto done;}
    req=sceHttpCreateRequestWithURL(conn, ORBIS_METHOD_POST, URL, (uint64_t)blen); if(req<0){log_msg("req %d",req);rc=-3;goto done;}
    sceHttpAddRequestHeader(req,"Content-Type","application/x-www-form-urlencoded",0);
    sceHttpAddRequestHeader(req,"User-Agent",UA,0);
    if((rc=sceHttpSendRequest(req,body,blen))<0){log_msg("send %d",rc);rc=-4;goto done;}
    if((rc=sceHttpGetStatusCode(req,&status))<0){log_msg("stat %d",rc);rc=-5;goto done;}
    char resp[2048]; int total=0,len;
    while((len=sceHttpReadData(req,resp+total,sizeof resp-total))>0){total+=len; if(total>(int)sizeof resp-1)break;}
    resp[total>= (int)sizeof resp? (int)sizeof resp-1:total]=0;
    jstr(resp,"access_token",access_token,at_cap);
    if(refresh_out) jstr(resp,"refresh_token",refresh_out,rt_cap);
    long exp=jnum(resp,"expires_in");
    if(expires_out_epoch)*expires_out_epoch=time(NULL)+exp;
    log_msg("oauth status=%d at=%s exp=%lds",status,access_token,(long)exp);
    rc = (status==200 && access_token && *access_token) ? 0 : status;
done:
    if(req>0)sceHttpDeleteRequest(req);
    if(conn>0)sceHttpDeleteConnection(conn);
    if(tpl>0)sceHttpDeleteTemplate(tpl);
    return rc;
}

int http_get(const char *url,char *out,size_t cap){
    if(http_init()) return -10;
    int32_t tpl=0,conn=0,req=0,rc=0;
    tpl=sceHttpCreateTemplate(s_http_ctx,UA,ORBIS_HTTP_VERSION_1_1,0); if(tpl<0)return -1;
    conn=sceHttpCreateConnectionWithURL(tpl,url,0); if(conn<0){rc=-2;goto done;}
    req=sceHttpCreateRequestWithURL(conn,ORBIS_METHOD_GET,url,0); if(req<0){rc=-3;goto done;}
    sceHttpAddRequestHeader(req,"User-Agent",UA,0);
    if((rc=sceHttpSendRequest(req,0,0))<0){rc=-4;goto done;}
    int total=0,len;
    while((len=sceHttpReadData(req,out+total,cap-1-total))>0){total+=len; if(total>(int)cap-1)break;}
    out[total>= (int)cap? (int)cap-1:total]=0;
    rc=0;
done:
    if(req>0)sceHttpDeleteRequest(req);
    if(conn>0)sceHttpDeleteConnection(conn);
    if(tpl>0)sceHttpDeleteTemplate(tpl);
    return rc;
}
