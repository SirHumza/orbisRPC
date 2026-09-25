/* ui.c - native PS4 dialogs: MsgDialog (ok / yes-no / progress) + ImeDialog.
 * Everything here needs the console (dialog system calls); pure logic
 * (validation, config merge) lives in icfg.c and is host-tested. */
#include "ui.h"
#include <stdio.h>
#include <string.h>
#include <wchar.h>
#include <orbis/CommonDialog.h>
#include <orbis/MsgDialog.h>
#include <orbis/ImeDialog.h>
#include <orbis/UserService.h>
#include <orbis/Sysmodule.h>
#include <orbis/Pad.h>
#include <orbis/libkernel.h>
#include <orbis/_types/user.h>
#include <fcntl.h>
#include <unistd.h>

static void ime_dbg(const char *msg){
    int fd = open("/data/ime_debug.log", O_WRONLY|O_CREAT|O_APPEND, 0666);
    if(fd >= 0){ write(fd, msg, strlen(msg)); close(fd); }
}

static void base_init(OrbisMsgDialogParam *param){
    memset(param, 0, sizeof(*param));
    param->baseParam.size = (uint32_t)sizeof(param->baseParam);
    param->baseParam.magic =
        (uint32_t)(ORBIS_COMMON_DIALOG_MAGIC_NUMBER + (uint64_t)&param->baseParam);
    param->size = sizeof(OrbisMsgDialogParam);
    param->mode = ORBIS_MSG_DIALOG_MODE_USER_MSG;
}

static int ui_ready = 0;
static int ime_dialog_running = 0;

/* Reap any stale dialog from a previous session that died
 * mid-flow. Called by ui_init() to ensure a clean start. */
static void reap_stale(void);

int ui_init(void){
    if(ui_ready) return 0;
    /* Reap any stale dialog from a previous session that died
     * mid-flow. This ensures a clean start. */
    reap_stale();
    {
        /* UserService first: dialogs + pad + IME all key off the user.
         * Best-effort (already-initialized is fine); uid fallbacks
         * downstream keep every path forward-safe. */
        OrbisUserServiceInitializeParams up;
        memset(&up, 0, sizeof up);
        up.priority = ORBIS_KERNEL_PRIO_FIFO_LOWEST;
        (void)sceUserServiceInitialize(&up);
    }
    /* Mirrors apollo-ps4 initInternal()/initPad():
     * 1. internal modules (SYSTEM, USER, COMMON_DIALOG)
     * 2. sceCommonDialogInitialize()  <- BEFORE external modules
     * 3. PAD module + scePadInit()
     * 4. external modules (MESSAGE_DIALOG, IME_DIALOG, IME_BACKEND)
     * Order matters: dialog init must precede external module loads,
     * and COMMON_DIALOG must precede sceCommonDialogInitialize(). */
    if(sceSysmoduleLoadModuleInternal(ORBIS_SYSMODULE_INTERNAL_SYSTEM_SERVICE) != 0) return -1;
    if(sceSysmoduleLoadModuleInternal(ORBIS_SYSMODULE_INTERNAL_USER_SERVICE) != 0) return -1;
    if(sceSysmoduleLoadModuleInternal(ORBIS_SYSMODULE_INTERNAL_COMMON_DIALOG) != 0) return -1;
    if(sceCommonDialogInitialize() < 0) return -1;
    if(sceSysmoduleLoadModuleInternal(ORBIS_SYSMODULE_INTERNAL_PAD) != 0) return -1;
    (void)scePadInit();
    if(sceSysmoduleLoadModule(ORBIS_SYSMODULE_MESSAGE_DIALOG) < 0) return -1;
    if(sceSysmoduleLoadModule(ORBIS_SYSMODULE_IME_DIALOG) < 0){
        sceSysmoduleUnloadModule(ORBIS_SYSMODULE_MESSAGE_DIALOG);
        return -1;
    }
    if(sceSysmoduleLoadModule(ORBIS_SYSMODULE_IME_BACKEND) < 0){
        sceSysmoduleUnloadModule(ORBIS_SYSMODULE_IME_DIALOG);
        sceSysmoduleUnloadModule(ORBIS_SYSMODULE_MESSAGE_DIALOG);
        return -1;
    }
    ui_ready = 1;
    return 0;
}

/* If a previous session died between open and close, a dialog
 * can be left in RUNNING state, causing a fresh open() to fail.
 * This function is called by ui_init() to reclaim any stale
 * dialog before starting fresh. Currently unused (no stale
 * dialogs observed after ui_init), kept for safety. */
static void reap_stale(void){
    if(sceMsgDialogGetStatus() == ORBIS_COMMON_DIALOG_STATUS_RUNNING)
        sceMsgDialogTerminate();
    if(sceImeDialogGetStatus() == ORBIS_DIALOG_STATUS_RUNNING)
        sceImeDialogTerm();
}

int ui_ok(const char *msg){
    OrbisMsgDialogParam param;
    OrbisMsgDialogUserMessageParam um;
    OrbisMsgDialogResult res;
    memset(&res, 0, sizeof res);
    sceMsgDialogTerminate();
    if(sceMsgDialogInitialize() < 0) return -1;
    base_init(&param);
    memset(&um, 0, sizeof um);
    um.msg = msg;
    um.buttonType = ORBIS_MSG_DIALOG_BUTTON_TYPE_OK;
    param.userMsgParam = &um;
    if(sceMsgDialogOpen(&param) < 0){ sceMsgDialogTerminate(); return -1; }
    /* Yield while waiting: a hot spin starves the dialog service on the
     * console (the proven pattern on this box sleeps 20 ms per tick). */
    while(sceMsgDialogUpdateStatus() != ORBIS_COMMON_DIALOG_STATUS_FINISHED)
        sceKernelUsleep(20000);
    sceMsgDialogClose();
    sceMsgDialogGetResult(&res);
    sceMsgDialogTerminate();
    return 0;
}

static int progress_open = 0;

int ui_progress_open(const char *msg){
    OrbisMsgDialogParam param;
    OrbisMsgDialogProgressBarParam bar;
    if(progress_open){
        /* Self-healing: a prior session that died between open and close
         * leaves the flag set with no dialog behind it. Reclaim it. */
        ui_progress_close();
    }
    sceMsgDialogTerminate();
    if(sceMsgDialogInitialize() < 0) return -1;
    base_init(&param);
    param.mode = ORBIS_MSG_DIALOG_MODE_PROGRESS_BAR;
    memset(&bar, 0, sizeof bar);
    bar.barType = ORBIS_MSG_DIALOG_PROGRESSBAR_TYPE_PERCENTAGE;
    bar.msg = msg;
    param.progBarParam = &bar;
    if(sceMsgDialogOpen(&param) < 0){ sceMsgDialogTerminate(); return -1; }
    progress_open = 1;
    return 0;
}

void ui_progress_msg(const char *msg){
    if(progress_open && msg) sceMsgDialogProgressBarSetMsg(0, msg);
}

void ui_progress_set(unsigned pct){
    if(progress_open) sceMsgDialogProgressBarSetValue(0, pct > 100 ? 100 : pct);
}

void ui_progress_close(void){
    if(!progress_open) return;
    progress_open = 0;
    sceMsgDialogClose();
    sceMsgDialogTerminate();
}

int ui_input(const char *title, const char *placeholder, char *out, size_t cap){
    static wchar_t wbuf[256];
    static wchar_t wtitle[64];
    static wchar_t wplace[64];
    OrbisImeDialogSetting st;
    int32_t uid = 0;
    size_t i;
    if(!out || cap == 0 || cap > 200) return -1;
    /* Prefill with current value (ASCII round-trip). */
    for(i = 0; i < cap - 1 && out[i]; i++) wbuf[i] = (wchar_t)(unsigned char)out[i];
    wbuf[i] = 0;
    for(i = 0; i < 63 && title && title[i]; i++) wtitle[i] = (wchar_t)(unsigned char)title[i];
    wtitle[i] = 0;
    for(i = 0; i < 63 && placeholder && placeholder[i]; i++) wplace[i] = (wchar_t)(unsigned char)placeholder[i];
    wplace[i] = 0;
    if(sceUserServiceGetForegroundUser(&uid) < 0 || uid <= 0) uid = 1;
    memset(&st, 0, sizeof st);
    st.userId = (uint32_t)uid;
    st.type = 0;
    st.supportedLanguages = 0;
    st.enterLabel = ORBIS_BUTTON_LABEL_DEFAULT;
    st.inputMethod = 0;
    st.filter = 0;
    st.option = 0;
    st.maxTextLength = (uint32_t)(cap - 1);
    st.inputTextBuffer = wbuf;
    /* Center of 1920x1080. At (0,0) the panel anchors up-left and the
     * keyboard renders half off-screen; 960,540 is the layout that is
     * known-good on this console (rutracker-ps4 uses the same values). */
    st.posx = 960;
    st.posy = 540;
    st.horizontalAlignment = ORBIS_H_CENTER;
    st.verticalAlignment = ORBIS_V_CENTER;
    st.placeholder = wplace;
    st.title = wtitle;
    /* apollo-ps4 pattern: guard against re-entry, terminate stale
     * state, then init. The dialog owns the running flag. */
    if(ime_dialog_running){ sceImeDialogTerm(); ime_dialog_running = 0; }
    ime_dbg("ime_begin\n");
    if(sceImeDialogInit(&st, NULL) < 0){ ime_dbg("ime_init_fail\n"); return -1; }
    ime_dialog_running = 1;
    {
        int spins = 0;
        OrbisDialogStatus ds;
        ds = sceImeDialogGetStatus();
        if(ds == ORBIS_DIALOG_STATUS_RUNNING) ime_dbg("ime_status_running\n");
        else if(ds == ORBIS_DIALOG_STATUS_NONE) ime_dbg("ime_status_none\n");
        else if(ds == ORBIS_DIALOG_STATUS_STOPPED) ime_dbg("ime_status_stopped\n");
        while(ime_dialog_running){
            ds = sceImeDialogGetStatus();
            if(ds == ORBIS_DIALOG_STATUS_STOPPED){
                OrbisDialogResult res;
                memset(&res, 0, sizeof res);
                sceImeDialogGetResult(&res);
                sceImeDialogTerm();
                ime_dialog_running = 0;
                if(res.endstatus != ORBIS_DIALOG_OK) return 0;
                break;
            }
            if(ds == ORBIS_DIALOG_STATUS_NONE && ++spins > 250){
                ime_dbg("ime_timeout\n");
                sceImeDialogTerm();
                ime_dialog_running = 0;
                return -1;
            }
            if(ds != ORBIS_DIALOG_STATUS_NONE) spins = 0;
            sceKernelUsleep(20000);
        }
    }
    for(i = 0; i < cap - 1 && wbuf[i]; i++)
        out[i] = (wbuf[i] < 128 && wbuf[i] >= 32) ? (char)wbuf[i] : '?';
    out[i] = 0;
    return 1;
}

/* Teardown for exit: terminate any live dialog, then unload the
 * modules loaded in ui_init. Called before _exit so no dialog
 * outlives the app. Must unload PAD too since it was loaded
 * internally (ORBIS_SYSMODULE_INTERNAL_PAD). */
void ui_shutdown(void){
    if(!ui_ready) return;
    ui_ready = 0;
    progress_open = 0;
    ime_dialog_running = 0;
    sceMsgDialogTerminate();
    sceImeDialogTerm();
    sceSysmoduleUnloadModule(ORBIS_SYSMODULE_IME_BACKEND);
    sceSysmoduleUnloadModule(ORBIS_SYSMODULE_IME_DIALOG);
    sceSysmoduleUnloadModule(ORBIS_SYSMODULE_MESSAGE_DIALOG);
    sceSysmoduleUnloadModule(ORBIS_SYSMODULE_INTERNAL_PAD);
}
