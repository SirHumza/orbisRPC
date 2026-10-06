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
#include "cfg.h"
#include "log.h"
#include "tmdb.h"
#include "clock.h"
#include "procwalk.h"
#include "bigapp.h"
#include "pkgzone.h"
#include "gamecache.h"
#ifdef ORBISRPC_SDK_PAYLOAD
/* Payload-SDK build: dlopen/dlsym come from the SDK libc (dlfcn);
 * there is no UserService here — user_init() below degrades. */
#include <dlfcn.h>
#include <stdint.h>
#else
#include <orbis/UserService.h>
#include <orbis/libkernel.h>
#include <orbis/Sysmodule.h>
#endif
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <dirent.h>
#include <sys/stat.h>
#include <errno.h>
#ifdef ORBISRPC_SDK_PAYLOAD
#include <sys/types.h>
#include <sys/sysctl.h>
#include <sys/mman.h>

#ifndef ORBISRPC_SDK_PAYLOAD
static int is_title_prefix(const char *n);
#endif

/* --- payload-build foreground, identity and screen --------------------
 * Everything below asks the OS a direct question. None of it reads a struct
 * offset, scans a directory for the "newest" title, or infers activity from
 * file mtimes -- so none of it can be invalidated by a firmware that moves
 * things around, which is what made the previous heuristics unusable past
 * 9.00.
 *
 * Foreground + title id: sceSystemServiceGetAppIdOfBigApp() names the app in
 * front, sceLncUtilGetAppTitleId() turns that app id into a TITLEID. Both come
 * from libSceSystemService.sprx, so the payload now links it.
 *
 * Screen: the kernel logs every ShellUI scene change into kern.msgbuf, so
 * "am I looking at Settings right now" is answerable without inference.
 *
 * Prototypes are local because this file also builds for the OpenOrbis app,
 * where these symbols arrive via <orbis/...> headers. */
int32_t sceSystemServiceGetAppIdOfBigApp(void);
int sceLncUtilGetAppTitleId(uint32_t app_id, char *title_id);

#define TITLEID_LEN 9
/* Buffer handed to the OS for the title id. Sony writes a fixed-width field,
 * so size it wider than the 9 valid characters and terminate before use. */
#define TITLEID_BUF 16

/* Classify what is in the foreground.
 *   1  a game: TITLEID written to out
 *   0  no big app at all (home screen)
 *   2  a system app is in front (app id 0, or an NPXS id)
 *  -1  the read failed; the caller must treat this as "unknown"
 *
 * The 0/2/-1 split is the load-bearing part. A system app in front is not a
 * closed game, and a failed read is not a closed game either; collapsing
 * either into "gone" is how presence got cleared while a game was running. */
static int read_big_app(char *out, size_t cap){
    if(!out || cap < TITLEID_LEN + 1) return -1;
    out[0] = 0;
    int32_t app_id = sceSystemServiceGetAppIdOfBigApp();
    /* The decision lives in bigapp.c so it can be unit-tested off-console. */
    char raw[TITLEID_BUF];
    memset(raw, 0, sizeof raw);
    int rc = 0;
    if(app_id > 0) rc = sceLncUtilGetAppTitleId((uint32_t)app_id, raw);
    raw[TITLEID_BUF - 1] = 0;
    return bigapp_classify(app_id, rc, raw, out, cap);
}

/* --- Settings screen, read from the kernel's own scene log ------------
 * ShellUI logs a line per scene change; the newest one is the current scene.
 * Scenes that matter carry ": SettingPage" (verified against a real console
 * capture: settings_root, storage_data#storage, id_goldhen_menu). */

/* kern.msgbuf is a fixed ring and sysctlbyname() fails with ENOMEM when the
 * supplied buffer is smaller than it, so the size is taken from the kernel's
 * own answer rather than hardcoded. A fixed 128 KB read fails on every poll
 * against a larger ring, which leaves the feature permanently dead while
 * still looking configured. */
#define MSGBUF_MIN (64u * 1024u)
#define MSGBUF_MAX (2u * 1024u * 1024u)
#define SCENE_MAX  192

static char *g_msgbuf = NULL;
static size_t g_msgbuf_sz = 0;
static int g_msgbuf_state = 0;     /* 0 untried, 1 ready, -1 unavailable */
static int g_msgbuf_logged = 0;

static void settings_msgbuf_failed(const char *why){
    if(g_msgbuf_logged) return;
    g_msgbuf_logged = 1;
    log_msg("settings probe unavailable: %s (screen state text disabled)", why);
}

static int settings_msgbuf_acquire(void){
    if(g_msgbuf_state) return g_msgbuf_state > 0 ? 0 : -1;
    size_t need = 0;
    if(sysctlbyname("kern.msgbuf", NULL, &need, NULL, 0) != 0 || need == 0){
        settings_msgbuf_failed("kern.msgbuf size query failed");
        g_msgbuf_state = -1;
        return -1;
    }
    if(need < MSGBUF_MIN) need = MSGBUF_MIN;
    if(need > MSGBUF_MAX){
        settings_msgbuf_failed("kern.msgbuf exceeds the 2 MB cap");
        g_msgbuf_state = -1;
        return -1;
    }
    /* Round up: mmap does not require the exact length to be page sized. */
    size_t page = 4096;
    size_t rounded = (need + page - 1) & ~(page - 1);
    void *p = mmap(NULL, rounded, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANON, -1, 0);
    if(p == MAP_FAILED){
        settings_msgbuf_failed("mmap failed");
        g_msgbuf_state = -1;
        return -1;
    }
    /* Prove the buffer really is large enough before committing to it. */
    size_t probe = rounded;
    if(sysctlbyname("kern.msgbuf", p, &probe, NULL, 0) != 0){
        settings_msgbuf_failed("kern.msgbuf read failed at the queried size");
        munmap(p, rounded);
        g_msgbuf_state = -1;
        return -1;
    }
    g_msgbuf = (char *)p;
    g_msgbuf_sz = rounded;
    g_msgbuf_state = 1;
    log_msg("settings probe: kern.msgbuf %zu bytes", g_msgbuf_sz);
    return 0;
}

/* Newest focused scene out of the kernel ring: 1 + fills out, 0 when the ring
 * holds no scene change yet, -1 when the probe is unavailable.
 *
 * Shared by the settings and browser matchers so both always agree on which
 * event is current. If they disagreed, one would post its presence while the
 * other believed it owned the screen. */
static int last_focused_scene(char *out, size_t cap){
    if(!out || cap == 0) return -1;
    out[0] = 0;
#ifdef ORBISRPC_SDK_PAYLOAD
    if(settings_msgbuf_acquire() != 0) return -1;
    size_t len = g_msgbuf_sz;
    if(sysctlbyname("kern.msgbuf", g_msgbuf, &len, NULL, 0) != 0){
        settings_msgbuf_failed("kern.msgbuf read failed");
        return -1;
    }
    if(len >= g_msgbuf_sz) len = g_msgbuf_sz - 1;
    g_msgbuf[len] = 0;
    /* The ring is chronological, so the newest entry is the live scene. */
    const char *cur = g_msgbuf;
    const char *last = NULL;
    while((cur = strstr(cur, "OnFocusActiveSceneChanged")) != NULL){
        last = cur;
        cur += sizeof("OnFocusActiveSceneChanged") - 1;
    }
    if(!last) return 0;
    const char *target = strstr(last, "-> [");
    if(!target) return 0;
    const char *end = strchr(target + 4, ']');
    if(!end) return 0;
    size_t n = (size_t)(end - (target + 4));
    if(n >= cap) return 0;             /* implausible; stay honest */
    memcpy(out, target + 4, n);
    out[n] = 0;
    return 1;
#else
    return -1;   /* app build: no kern.msgbuf story */
#endif
}
#endif

/* Defined outside the payload block so the app build links too; it has no
 * kern.msgbuf story there and reports unknown rather than guessing.
 *
 * 1 = Settings in front, 0 = a different scene, -1 = unknown.
 *
 * -1 means "no opinion" and must never be folded into 0: the probe being
 * unavailable is not evidence that Settings closed. It also covers "the ring
 * holds no scene change yet", which resolves on its own once the user moves
 * around the UI. */
int detect_settings_active(void){
#ifndef ORBISRPC_SDK_PAYLOAD
    return -1;   /* not wired for the app build */
#else
    char scene[SCENE_MAX];
    if(last_focused_scene(scene, sizeof scene) != 1) return -1;
    return strstr(scene, "SettingPage") != NULL ? 1 : 0;

#endif
}

/* Web browser. Scene names verified on console 2026-10-04 from a live klog:
 *   [] -> [WebBrowserScene : WebBrowserScene]    browser opens
 *   [SettingsScene] -> [WebViewDialog : ...]    login popup over the page
 *   [WebViewDialog] -> [BrowserMain : MainScene] page loaded
 *   [BrowserMain] -> [ContentAreaScene]           browser closed
 * "WebBrowserPlugin" is a plugin load, not a focus target, so it never reaches
 * this matcher; including "WebBrowser" costs nothing either way. */
int detect_browser_active(void){
#ifndef ORBISRPC_SDK_PAYLOAD
    return -1;   /* not wired for the app build */
#else
    char scene[SCENE_MAX];
    if(last_focused_scene(scene, sizeof scene) != 1) return -1;
    if(strstr(scene, "WebBrowser") || strstr(scene, "BrowserMain") ||
       strstr(scene, "WebViewDialog")) return 1;
    return 0;
#endif
}

#ifndef ORBISRPC_SDK_PAYLOAD
static int s_user_inited = 0;
static int s_user_ok = 0;
static int user_init(void){
    if(s_user_inited) return s_user_ok ? 0 : -1;
    s_user_inited = 1;
#ifdef ORBISRPC_SDK_PAYLOAD
    /* No UserService in payload-SDK builds: ShellCoreUtil (dlopen) is the
     * only foreground signal; without it we report inactive (fail closed). */
    log_msg("UserService unavailable in SDK build; ShellCoreUtil only");
    return -1;
#else
    /* UserService is an external module -> load via internal id */
    uint32_t r = sceSysmoduleLoadModuleInternal(ORBIS_SYSMODULE_INTERNAL_USER_SERVICE);
    if(r != 0){ int32_t ir=(int32_t)r; log_msg("load UserService fail %d", ir); return -1; }
    int32_t rc = sceUserServiceInitialize(NULL);
    if(rc != 0){ log_msg("UserService init fail %d", rc); return -1; }
    s_user_ok = 1;
    return 0;
#endif
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
#endif

/* --- foreground-active: the core "a game is running" signal ---------- */
int detect_foreground_active(void){
#ifdef ORBISRPC_SDK_PAYLOAD
    char tid[16];
    return read_big_app(tid, sizeof tid);
#else
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
#endif
}

/* --- title naming ---------------------------------------------------- */
#ifndef ORBISRPC_SDK_PAYLOAD
static int is_title_prefix(const char *n){
    /* CUSA (PS4) + PPSA (PS5-backport) + EU/JP/indie variants */
    return strncmp(n,"CUSA",4)==0 || strncmp(n,"PPSA",4)==0 ||
           strncmp(n,"PCSE",4)==0 || strncmp(n,"PCSB",4)==0 ||
           strncmp(n,"PCSG",4)==0 || strncmp(n,"EPSA",4)==0;
}
#endif
/* last resolved titleId (for Discord asset key); valid after a successful
 * detect_current_game / detect_name_for_title. */
static char s_last_titleid[16] = "";
static char s_last_art[256] = "";
/* Last resolved title: same title in a row reuses its name/art with zero
 * I/O. Any title change resolves fresh — no cross-title cache exists, so
 * stale identities are impossible by construction. */
static char s_rs_tid[16] = "";
static char s_rs_name[128] = "";
static char s_rs_art[256] = "";
/* Raw-ID (unresolved) entries are retried, not frozen: a transient miss
 * (slow I/O at launch) must not lock the raw ID in forever. Successful
 * resolves reuse indefinitely; raw fallbacks re-resolve after 5 min. */
static int s_rs_ok = 0;
static int64_t s_rs_at = 0;
static int resolve_reuse(const char *ti, char *out_name, size_t cap, int *was_resolved){
    if(was_resolved) *was_resolved = 0;
    if(!ti || !ti[0] || strcmp(ti, s_rs_tid) != 0 || !s_rs_name[0]) return 0;
    if(!s_rs_ok){
        /* Inside the grace window the raw id is the best thing we have, so
         * show it -- but do not claim it was a resolve. */
        if(orbis_mono_s() - s_rs_at <= 300){
            strncpy(out_name, s_rs_name, cap - 1);
            out_name[cap - 1] = 0;
            return 1;
        }
        log_dbg("name: cached raw id for %s expired; re-resolving", ti);
        return 0;   /* expired: the network sources get another turn */
    }
    if(was_resolved) *was_resolved = 1;
    strncpy(out_name, s_rs_name, cap - 1);
    out_name[cap - 1] = 0;
    strncpy(s_last_art, s_rs_art, sizeof s_last_art - 1);
    s_last_art[sizeof s_last_art - 1] = 0;
    return 1;
}
static void resolve_remember(const char *ti, const char *name, const char *art, int ok){
    if(!ti || !name) return;
    s_rs_ok = ok;
    s_rs_at = orbis_mono_s();
    strncpy(s_rs_tid, ti, sizeof s_rs_tid - 1);
    s_rs_tid[sizeof s_rs_tid - 1] = 0;
    strncpy(s_rs_name, name, sizeof s_rs_name - 1);
    s_rs_name[sizeof s_rs_name - 1] = 0;
    if(art){
        strncpy(s_rs_art, art, sizeof s_rs_art - 1);
        s_rs_art[sizeof s_rs_art - 1] = 0;
    } else s_rs_art[0] = 0;
}
/* Media apps post Watching/Listening instead of Playing. IDs verified
 * against Sony TMDB (names resolve there too). */
int detect_media_type(const char *title_id){
    static const struct { const char *id; int type; } media[] = {
        { "CUSA00127", 3 }, /* Netflix -> Watching */
        { "CUSA01015", 3 }, /* YouTube -> Watching */
    };
    if(!title_id) return 0;
    for(unsigned i = 0; i < sizeof media/sizeof media[0]; i++){
        if(!strcmp(title_id, media[i].id)) return media[i].type;
    }
    return 0;
}

const char *detect_last_titleid(void){ return s_last_titleid[0] ? s_last_titleid : NULL; }
int detect_last_ok(void){ return s_rs_ok; }
const char *detect_last_art(void){ return s_last_art[0] ? s_last_art : NULL; }
static void remember_titleid(const char *ti){
    if(!ti) return;
    strncpy(s_last_titleid, ti, sizeof s_last_titleid - 1);
    s_last_titleid[sizeof s_last_titleid - 1] = 0;
}
#ifndef ORBISRPC_SDK_PAYLOAD
static long scan_one_appdir(const char *base, char *out, size_t cap, long best){
    if(!base || !out || cap < 2) return best;
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
#endif
#ifndef ORBISRPC_SDK_PAYLOAD
static long scan_recent_titleid(char *out, size_t cap){
    out[0]=0;
    long best = scan_one_appdir("/user/app", out, cap, -1);
    best = scan_one_appdir("/data/app", out, cap, best);
    return (out[0])? 0 : -1;
}
#endif /* scan_recent_titleid unused in SDK builds */

int detect_current_game(char *out_name, size_t cap, char *out_path, size_t p_cap){
    if(!out_name || cap==0) return -1;
    out_name[0] = 0;
    if(out_path && p_cap) out_path[0] = 0;
    char titleId[16]=""; int named=0, have_tid=0;
#ifdef ORBISRPC_SDK_PAYLOAD
    /* One query yields both state and identity, so nothing can disagree with
     * itself.
     *
     *   -1 uncertain -> -2: hold position, never clear
     *    2 system app in front -> -3: the game has not closed, just covered
     *    0 nothing in front -> -1: genuinely gone
     *    1 a game -> carry on with the TITLEID the read gave us */
    int fg = read_big_app(titleId, sizeof titleId);
    if(fg < 0){ log_dbg("detect: big-app read uncertain"); return -2; }
    if(fg == 2){
        /* Name the system app when we can: a media app id landing here is
         * the explanation for an unexpected "not playing" presence. */
        const char *last = detect_last_titleid();
        log_dbg("detect: system app in front (%s)", last ? last : "unresolved");
        return -3;
    }
    if(fg == 0) return -1;
    have_tid = 1;
    remember_titleid(titleId);
    s_last_art[0] = 0;
#else
    {
        int fg = detect_foreground_active();
        if(fg < 0) return -2;
        if(fg == 2) return -3;
        if(!fg) return -1;
    }
    have_tid = (scan_recent_titleid(titleId,sizeof titleId)==0);
    if(have_tid){
        remember_titleid(titleId);
        s_last_art[0] = 0;
    }
#endif
    if(have_tid){
        remember_titleid(titleId);
        s_last_art[0] = 0;
        /* A cache hit only counts when it WAS a resolve. A cached raw id
         * still pre-fills the name so the poll is cheap, but the network
         * sources below must get their turn -- claiming success here is what
         * pinned a title to its raw id for the whole session. */
        int cached_ok = 0;
        if(resolve_reuse(titleId, out_name, cap, &cached_ok) && cached_ok){
            log_dbg("name: %s via cache", out_name);
            named = 1;
        }
        /* games_cache.json persists BOTH the name and the cover URL, so
         * it is consulted before any network call. A title resolved on an
         * earlier boot arrives with its art attached; without this, art is
         * RAM-only and the presence falls through to a pack URL that may
         * not exist, which is exactly the missing-icon symptom. */
        char cc_art[256] = "";
        if(!named && gamecache_get(titleId, out_name, cap, cc_art, sizeof cc_art)){
            named = 1;
            if(cc_art[0]){
                strncpy(s_last_art, cc_art, sizeof s_last_art-1);
                s_last_art[sizeof s_last_art-1] = 0;
            }
            log_msg("name: %s via games_cache%s", out_name,
                    cc_art[0] ? " (+art)" : " (no art)");
        }
        /* config.json titles are still READ: hand-edited overrides win, and
         * names learned before games_cache existed keep resolving. The daemon
         * no longer writes here -- that is games_cache.json now. */
        if(!named && cfg_title(&g_cfg, titleId, out_name, cap)==0){ named=1; log_msg("name: %s via config", out_name); }
        /* Three mutually exclusive sources, one per class of title id:
         *   CUSA*  -> Sony TMDB
         *   retro  -> retro-games (ps1.json, then ps2.json, then psp.json)
         *   else   -> pkg-zone (homebrew)
         * Never chained: each class has exactly one index that knows it, so
         * asking a second one costs a full TLS handshake for a guaranteed
         * miss. This is what let SCUS97399 hammer pkg-zone at 1 Hz. */
        int want_retro    = (!named && g_cfg.retro_enabled && retro_wants(titleId));
        int try_pkgzone   = (!named && !want_retro && g_cfg.pkgzone_enabled
                             && pkgzone_wants(titleId));
        if(!named && !try_pkgzone && !want_retro){
            char art[256] = "";
            if(tmdb_resolve(titleId, out_name, cap, art, sizeof art)==0){
                named = 1;
                strncpy(s_last_art, art, sizeof s_last_art-1);
            } else s_last_art[0] = 0;
        }
        /* Homebrew: no on-box source and no TMDB entry, so this is the only
         * chance at a real name. Failure is silent by design: the raw title id
         * below is a perfectly good fallback. */
        if(try_pkgzone){
            char art[256] = "";
            if(pkgzone_resolve(titleId, out_name, cap, art, sizeof art)==0){
                named = 1;
                strncpy(s_last_art, art, sizeof s_last_art-1);
                s_last_art[sizeof s_last_art-1] = 0;
            }
        }
        /* Retro: fetches each platform index in turn (ps1 -> ps2 -> psp) and
         * stops at the first hit. An index miss is normal, not an error, so
         * only a hard failure is logged. */
        if(want_retro){
            char art[RETRO_MAX_URL] = "";
            if(retro_resolve(titleId, out_name, cap, art, sizeof art)==0){
                named = 1;
                if(art[0]){
                    strncpy(s_last_art, art, sizeof s_last_art-1);
                    s_last_art[sizeof s_last_art-1] = 0;
                }
            }
        }
        /* Only a genuine resolve is worth keeping. detect_last_ok() is 0 for
         * a raw-id fallback, so a network miss never poisons the cache. */
        if(named && detect_last_ok())
            gamecache_put(titleId, out_name, s_last_art,
                          want_retro ? "retro" : try_pkgzone ? "pkgzone" : "tmdb");
        if(!named){
            strncpy(out_name, titleId, cap-1); out_name[cap-1]=0;
            /* Every source missed. Without this line the presence just shows
             * a bare title id and there is no way to tell a network failure
             * from a genuinely unknown title. */
            log_msg("name: %s UNRESOLVED (tried: config, %s)",
                    titleId, want_retro ? "retro" : try_pkgzone ? "pkgzone" : "tmdb");
        }
        resolve_remember(titleId, out_name, s_last_art, named);
    }else{
        remember_titleid("");
        s_last_art[0] = 0;
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
    int cached_ok = 0;
    if(resolve_reuse(titleId, out_name, cap, &cached_ok) && cached_ok){
        log_dbg("name: %s via cache", out_name);
        return 0;
    }
    if(cfg_title(&g_cfg, titleId, out_name, cap)==0){ log_msg("name: %s via config", out_name); resolve_remember(titleId, out_name, "", 1); return 0; }
    {
        char art[256] = "";
        if(tmdb_resolve(titleId, out_name, cap, art, sizeof art)==0){
            strncpy(s_last_art, art, sizeof s_last_art-1);
            resolve_remember(titleId, out_name, art, 1);
            return 0; /* tmdb_resolve already logged */
        }
        s_last_art[0] = 0;
    }
    /* Same three-way split as detect_name_for_app(): CUSA -> TMDB, retro ->
     * retro-games, everything else -> pkg-zone. */
    if(g_cfg.retro_enabled && retro_wants(titleId)){
        char art[RETRO_MAX_URL] = "";
        if(retro_resolve(titleId, out_name, cap, art, sizeof art)==0){
            if(art[0]){
                strncpy(s_last_art, art, sizeof s_last_art-1);
                s_last_art[sizeof s_last_art-1] = 0;
            }
            resolve_remember(titleId, out_name, art, 1);
            return 0;
        }
        /* Every platform index missed. Fall through to the raw id. */
        log_msg("retro: %s not found in any index", titleId);
    } else if(g_cfg.pkgzone_enabled && pkgzone_wants(titleId)){
        char art[256] = "";
        if(pkgzone_resolve(titleId, out_name, cap, art, sizeof art)==0){
            if(art[0]){
                strncpy(s_last_art, art, sizeof s_last_art-1);
                s_last_art[sizeof s_last_art-1] = 0;
            }
            resolve_remember(titleId, out_name, art, 1);
            return 0;
        }
    }
    strncpy(out_name, titleId, cap-1); out_name[cap-1]=0;
    resolve_remember(titleId, out_name, "", 0);
    return 0;
}