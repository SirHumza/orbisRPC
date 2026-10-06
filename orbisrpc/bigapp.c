/* bigapp.c - see bigapp.h. */
#include "bigapp.h"
#include "log.h"
#include "retro.h"
#include <string.h>

int is_homebrew_id(const char *tid){
    if(!tid || !tid[0]) return 0;
    size_t n = strlen(tid);
    if(n < 4) return 0;
    if(!strncmp(tid, "CUSA", 4)) return 0;   /* retail -> TMDB */
    if(!strncmp(tid, "NPXS", 4)) return 0;   /* system -> never posted */
    if(retro_wants(tid))             return 0; /* retro -> deliberately untouched */
    return 1;
}

int bigapp_titleid_valid(const char *s){
    if(!s) return 0;
    size_t n = strlen(s);
    if(n != BIGAPP_TITLEID_LEN) return 0;
    for(size_t i = 0; i < n; i++){
        char c = s[i];
        if((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) continue;
        return 0;
    }
    return 1;
}

int bigapp_classify(int32_t app_id, int title_rc, const char *title_id,
                    char *out, size_t cap){
    if(out && cap) out[0] = 0;

    /* The OS reports "no big app" and "System UI in front" differently, and
     * the difference matters: the first means a game may have closed, the
     * second means one is probably still running underneath. */
    if(app_id == -1) return BIGAPP_NONE;
    if(app_id == 0)  return BIGAPP_SYSTEM;
    if(app_id < 0){
        log_msg("big-app query returned 0x%08x", (uint32_t)app_id);
        return BIGAPP_UNKNOWN;
    }

    const char *t = title_id ? title_id : "";
    if(title_rc != 0){
        log_msg("title id for app 0x%08x rc=%d", (uint32_t)app_id, title_rc);
        return BIGAPP_UNKNOWN;
    }
    if(!bigapp_titleid_valid(t)){
        log_msg("title id for app 0x%08x is not %d chars of [A-Z0-9]: '%s'",
                (uint32_t)app_id, BIGAPP_TITLEID_LEN, t);
        return BIGAPP_UNKNOWN;
    }
    /* NPXS is the system namespace: SceShellUI (NPXS20001), the store
     * shells (NPXS21002, NPXS210xx) and similar. Verified on console
     * 2026-10-03 from the klog -- NPXS20001 backs SceShellUI,
     * SecureUIProcess.self and SecureWebProcess.self, and every
     * AppFocusChanged event starts there. Never post it.
     *
     * App and media containers use other namespaces (ItemzFlow is
     * ITEM00001) and are resolved like any other title id. */
    if(strncmp(t, "NPXS", 4) == 0) return BIGAPP_SYSTEM;

    if(!out || cap <= BIGAPP_TITLEID_LEN) return BIGAPP_UNKNOWN;
    memcpy(out, t, BIGAPP_TITLEID_LEN);
    out[BIGAPP_TITLEID_LEN] = 0;
    return BIGAPP_GAME;
}