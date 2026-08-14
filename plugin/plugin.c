/* plugin.c - orbisRPC as a GoldHEN plugin (.prx loaded into the game process).
 *
 * GoldHEN loads plugins per game title via /data/GoldHEN/plugins.ini. A
 * plugin runs inside the game's process, so:
 *   - we KNOW which game is running: sys_sdk_proc_info() -> procInfo.titleid
 *   - the game has full network access (this is the whole point)
 *   - it auto-starts at every boot/jailbreak with zero PC involvement
 *
 * plugin_load() resolves the titleid to a display name and starts the shared
 * daemon thread (daemon.c) which handles token refresh, the Discord gateway
 * and presence. plugin_unload() stops it cleanly.
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <orbis/libkernel.h>
#include "Common.h"
#include "daemon.h"
#include "detect.h"

/* Only post presence for real game titles (CUSA/PPSA/etc). System apps
 * (NPXS..., shell) launching would otherwise post bogus "playing" state. */
static int is_game_titleid(const char *t){
    static const char *pfx[] = { "CUSA", "PPSA", "PCSE", "PCJS", "EPSA", "PCSB", NULL };
    for(int i=0; pfx[i]; i++) if(strncmp(t, pfx[i], 4)==0) return 1;
    return 0;
}

#define attr_public      __attribute__((visibility("default")))
#define attr_module_hidden __attribute__((weak)) __attribute__((visibility("hidden")))

attr_public const char *g_pluginName  = "orbisrpc";
attr_public const char *g_pluginDesc  = "Discord Rich Presence for PS4";
attr_public const char *g_pluginAuth  = "orbisRPC";
attr_public uint32_t    g_pluginVersion = 0x00000100; /* 1.00 */

struct proc_info procInfo;

static void *daemon_thread(void *args){
    /* args = display name of the game we are loaded into */
    char name[128];
    strncpy(name, (const char *)args, sizeof name - 1);
    name[sizeof name - 1] = 0;
    daemon_clear_stop();
    daemon_run(name);
    return NULL;
}

int32_t attr_public plugin_load(int32_t argc, const char *argv[]){
    (void)argc; (void)argv;
    klog("[orbisrpc] <%s\\Ver.0x%08x> %s\n", g_pluginName, g_pluginVersion, __func__);
    klog("[orbisrpc] Plugin Author(s): %s\n", g_pluginAuth);

    if(sys_sdk_proc_info(&procInfo) != 0){
        klog("[orbisrpc] sys_sdk_proc_info failed; not starting\n");
        return 0;
    }
    klog("[orbisrpc] loaded into process: pid=%d name=%s titleid=%s\n",
         procInfo.pid, procInfo.name, procInfo.titleid);

    if(!is_game_titleid(procInfo.titleid)){
        klog("[orbisrpc] not a game title (system app?); not starting daemon\n");
        return 0;
    }

    /* Resolve a display name for the title we are loaded into. */
    static char game_name[128];
    detect_name_for_title(procInfo.titleid, game_name, sizeof game_name);
    klog("[orbisrpc] game: %s\n", game_name);

    OrbisPthread thread;
    scePthreadCreate(&thread, NULL, daemon_thread, game_name, "orbisrpc_daemon");
    return 0;
}

int32_t attr_public plugin_unload(int32_t argc, const char *argv[]){
    (void)argc; (void)argv;
    klog("[orbisrpc] <%s\\Ver.0x%08x> %s\n", g_pluginName, g_pluginVersion, __func__);
    daemon_request_stop();
    return 0;
}

int32_t attr_module_hidden module_start(int64_t argc, const void *args){
    (void)argc; (void)args;
    return 0;
}

int32_t attr_module_hidden module_stop(int64_t argc, const void *args){
    (void)argc; (void)args;
    return 0;
}