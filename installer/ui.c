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

/* What show_message() last read out of a dialog. Exposed for the install log:
 * the yes/no verdict is lossy (everything unknown collapses to "decline") and
 * this is the raw value behind it. */
static int g_last_button = -1;

/* Reap any stale dialog from a previous session that died
 * mid-flow. Called by ui_init() to ensure a clean start. */
static void reap_stale(void);

int ui_init(void){
    if(ui_ready) return 0;
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
    /* Must come AFTER the loads above. reap_stale() calls into MsgDialog and
     * ImeDialog, so running it before their sysmodules are loaded is an
     * unresolved-NID call at startup -- the exact crash ui_init exists to
     * prevent. It was the first statement in this function. */
    reap_stale();
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

/* Reads back the button the user actually pressed.
 *
 * The header's OrbisMsgDialogResult is not trusted for layout. This toolchain's
 * headers have already been shown to be wrong twice -- SysUtil.h declared every
 * notification NID as void(void), and FreeType's headers were trimmed -- so a
 * plausible-but-mismatched struct would explain the observed failure exactly:
 * the dialog renders Yes/No correctly, GetResult returns success, and yet every
 * field reads zero because the real values landed at offsets this declaration
 * does not describe.
 *
 * So the result is read into an oversized zeroed buffer and scanned for the two
 * values an answer can take. That both recovers the answer and reports which
 * offset carried it, which is what a permanent fix needs. The buffer is also
 * what makes the call safe: if the real struct is larger than declared, a
 * stack-allocated OrbisMsgDialogResult would be overflowed by the write.
 *
 * Every non-zero word and its offset goes to the install log, so one run
 * settles the layout rather than a series of guesses. */
#define DIALOG_RAW_BYTES 256

static void dialog_scan_log(const char *tag, int rc, const unsigned char *buf){
    FILE *f = fopen("/data/orbisRPC/install.log", "a");
    int i;
    if(!f) f = fopen("/data/install.log", "a");
    if(!f) return;
    fprintf(f, "ui %s rc=%d raw:", tag, rc);
    for(i = 0; i + 4 <= DIALOG_RAW_BYTES; i += 4){
        unsigned int v = (unsigned int)buf[i]
                       | ((unsigned int)buf[i+1] << 8)
                       | ((unsigned int)buf[i+2] << 16)
                       | ((unsigned int)buf[i+3] << 24);
        if(v) fprintf(f, " +%d=%u", i, v);
    }
    fprintf(f, "\n");
    fclose(f);
}

/* Returns the button value found in the raw result, or -1 when it cannot be
 * determined with confidence.
 *
 * Deliberately strict. The answer is 1 (OK/YES) or 2 (NO) and nothing else, so
 * exactly one matching word has to be present. Two candidates means the layout
 * is not what is assumed here, and guessing between them could invert the
 * answer -- which would be far worse than declining. */
static int dialog_scan_button(const unsigned char *buf){
    int found = -1;
    int hits = 0;
    int i;

    for(i = 0; i + 4 <= DIALOG_RAW_BYTES; i += 4){
        unsigned int v = (unsigned int)buf[i]
                       | ((unsigned int)buf[i+1] << 8)
                       | ((unsigned int)buf[i+2] << 16)
                       | ((unsigned int)buf[i+3] << 24);

        if(v == 1 || v == 2){
            found = (int)v;
            hits++;
        }
    }

    return hits == 1 ? found : -1;
}

/* Shared body of ui_ok and ui_yesno: both are the same dialog with a
 * different buttonType, and the wait/teardown dance is the fiddly part, so it
 * lives in one place. Returns the button the user pressed, or -1. */
static int show_message(const char *msg, OrbisMsgDialogButtonType buttons){
    OrbisMsgDialogParam param;
    OrbisMsgDialogUserMessageParam um;
    int answer = -1;

    sceMsgDialogTerminate();
    if(sceMsgDialogInitialize() < 0) return -1;
    base_init(&param);
    memset(&um, 0, sizeof um);
    um.msg = msg;
    um.buttonType = buttons;
    param.userMsgParam = &um;
    if(sceMsgDialogOpen(&param) < 0){ sceMsgDialogTerminate(); return -1; }
    /* Yield while waiting: a hot spin starves the dialog service on the
     * console (the proven pattern on this box sleeps 20 ms per tick). */
    while(sceMsgDialogUpdateStatus() != ORBIS_COMMON_DIALOG_STATUS_FINISHED)
        sceKernelUsleep(20000);
    /* Read the result BEFORE closing: Close() discards it.
     *
     * buttonId sits at offset 8, which is where the header puts it (mode at 0,
     * result at 4, buttonId at 8) and where the raw dumps confirm it: a Yes/No
     * dialog reports +8=1 for X and +8=2 for O. So the declared field is read
     * normally, and the raw scan is only a fallback for when it comes back
     * unusable -- which is what it was invented for, before the offset was
     * known. Both are logged either way. */
    {
        unsigned char raw[DIALOG_RAW_BYTES];
        int rc;

        memset(raw, 0, sizeof raw);
        rc = sceMsgDialogGetResult((OrbisMsgDialogResult *)raw);
        dialog_scan_log("pre-close", rc, raw);

        if(rc >= 0){
            const OrbisMsgDialogResult *res = (const OrbisMsgDialogResult *)raw;
            if(res->buttonId > 0) answer = (int)res->buttonId;
        }

        if(answer < 0){
            int scanned = dialog_scan_button(raw);
            if(scanned > 0) answer = scanned;
        }
    }

    sceMsgDialogClose();

    {
        unsigned char raw[DIALOG_RAW_BYTES];
        int rc;

        memset(raw, 0, sizeof raw);
        rc = sceMsgDialogGetResult((OrbisMsgDialogResult *)raw);
        dialog_scan_log("post-close", rc, raw);

        /* Only consulted when the first read gave nothing usable, so a real
         * answer is never overwritten by an empty one. */
        if(answer < 0){
            if(rc >= 0){
                const OrbisMsgDialogResult *res = (const OrbisMsgDialogResult *)raw;
                if(res->buttonId > 0) answer = (int)res->buttonId;
            }
            if(answer < 0){
                int scanned = dialog_scan_button(raw);
                if(scanned > 0) answer = scanned;
            }
        }
    }

    sceMsgDialogTerminate();
    g_last_button = answer;
    return answer;
}

int ui_ok(const char *msg){
    return show_message(msg, ORBIS_MSG_DIALOG_BUTTON_TYPE_OK) < 0 ? -1 : 0;
}

int ui_yesno(const char *msg){
    int r = show_message(msg, ORBIS_MSG_DIALOG_BUTTON_TYPE_YESNO);
    /* OK and YES share the value 1, so only NO is 0. Anything unrecognised --
     * including the 0 that a discarded result leaves behind -- is treated as a
     * decline rather than consent, so a failure here cannot silently install
     * something. */
    if(r == ORBIS_MSG_DIALOG_BUTTON_ID_NO) return 0;
    if(r == ORBIS_MSG_DIALOG_BUTTON_ID_YES || r == ORBIS_MSG_DIALOG_BUTTON_ID_OK)
        return 1;
    return -1;
}

/* The value show_message() read, for the install log. Lets installer.c report
 * what the dialog actually returned instead of only the yes/no verdict, which
 * is what makes "both buttons do the same thing" diagnosable. */
int ui_last_button(void){ return g_last_button; }

int ui_input(const char *title, const char *placeholder, char *out, size_t cap){
    static wchar_t wbuf[256];
    static wchar_t wtitle[64];
    static wchar_t wplace[64];
    OrbisImeDialogSetting st;
    int32_t uid = 0;
    size_t i;
    /* Bound first, before anything is opened. Over it, nothing is shown at all,
     * which is a silent failure for the caller -- hence ui.h's UI_INPUT_MAX. */
    if(!out || cap == 0 || cap > UI_INPUT_MAX) return -1;
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
    ime_dialog_running = 0;
    sceMsgDialogTerminate();
    sceImeDialogTerm();
    sceSysmoduleUnloadModule(ORBIS_SYSMODULE_IME_BACKEND);
    sceSysmoduleUnloadModule(ORBIS_SYSMODULE_IME_DIALOG);
    sceSysmoduleUnloadModule(ORBIS_SYSMODULE_MESSAGE_DIALOG);
    /* Must be the ...Internal unload, to match the load in ui_init. PAD is an
     * OrbisSysModuleInternal value (0x80000024), but sceSysmoduleUnloadModule
     * takes an OrbisSysModule, whose underlying type is uint16_t -- so passing
     * it there truncates to 0x0024 and asks the kernel to unload some unrelated
     * public module. */
    sceSysmoduleUnloadModuleInternal(ORBIS_SYSMODULE_INTERNAL_PAD);
}
