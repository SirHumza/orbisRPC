/* ui.c - custom SDL2 installer UI: branded fullscreen cards instead of
 * stock Sony MsgDialogs. Same ui.h API, so installer.c is untouched.
 * Token entry still uses the system IME (proven overlay); everything
 * else is rendered by us: Discord-blurple card, progress bar, X-to-continue.
 * Needs the console (SDL/Pad/IME system calls); pure logic lives in icfg.c. */
#include "ui.h"
#include "font.h"
#include <stdio.h>
#include <string.h>
#include <wchar.h>
#include <SDL2/SDL.h>
#include <orbis/CommonDialog.h>
#include <orbis/ImeDialog.h>
#include <orbis/UserService.h>
#include <orbis/Sysmodule.h>
#include <orbis/Pad.h>
#include <orbis/libkernel.h>
#include <orbis/_types/user.h>
#include <fcntl.h>
#include <unistd.h>

#define SCR_W 1920
#define SCR_H 1080

/* Palette */
#define C_BG_R 30  /* near-black warm gray */
#define C_BG_G 31
#define C_BG_B 34
#define C_CARD_R 88 /* Discord blurple */
#define C_CARD_G 101
#define C_CARD_B 242
#define C_INK_R 255 /* white text */
#define C_INK_G 255
#define C_INK_B 255
#define C_DIM_R 200
#define C_DIM_G 203
#define C_DIM_B 220
#define C_BAR_BG_R 40
#define C_BAR_BG_G 43
#define C_BAR_BG_B 55
#define C_BAR_FG_R 255
#define C_BAR_FG_G 255
#define C_BAR_FG_B 255

static SDL_Window *g_win = NULL;
static SDL_Surface *g_surf = NULL;
static int ui_ready = 0;
static int ime_dialog_running = 0;
static int g_pad[4] = { -1, -1, -1, -1 };
static uint32_t g_prevbtn[4] = { 0, 0, 0, 0 };

static void ime_dbg(const char *msg){
    int fd = open("/data/ime_debug.log", O_WRONLY|O_CREAT|O_APPEND, 0666);
    if(fd >= 0){ write(fd, msg, strlen(msg)); close(fd); }
}

/* --- pixel helpers (format-safe via SDL_GetRGB/MapRGB) ---------------- */
static void blend_px(int x, int y, int r, int g, int b, unsigned a){
    Uint8 dr, dg, db;
    Uint32 *p;
    if(!g_surf || x < 0 || y < 0 || x >= g_surf->w || y >= g_surf->h || a == 0) return;
    p = (Uint32 *)((Uint8 *)g_surf->pixels + (size_t)y * (size_t)g_surf->pitch + (size_t)x * 4);
    SDL_GetRGB(*p, g_surf->format, &dr, &dg, &db);
    dr = (Uint8)((dr * (255 - a) + (unsigned)r * a) / 255);
    dg = (Uint8)((dg * (255 - a) + (unsigned)g * a) / 255);
    db = (Uint8)((db * (255 - a) + (unsigned)b * a) / 255);
    *p = SDL_MapRGB(g_surf->format, dr, dg, db);
}

static void fill_rect(int x, int y, int w, int h, int r, int g, int b){
    SDL_Rect rc;
    if(!g_surf) return;
    rc.x = x; rc.y = y; rc.w = w; rc.h = h;
    SDL_FillRect(g_surf, &rc, SDL_MapRGB(g_surf->format,
                 (Uint8)r, (Uint8)g, (Uint8)b));
}

static unsigned text_w(const char *s){
    unsigned w = 0;
    for(; *s; s++){
        unsigned c = (unsigned char)*s;
        if(c < FONT_FIRST || c >= FONT_FIRST + FONT_COUNT) c = (unsigned)'?';
        w += font_adv[c - FONT_FIRST] + 2;
    }
    return w;
}

static void draw_text(int x, int y, const char *s, int r, int g, int b){
    if(!g_surf || !s) return;
    if(SDL_MUSTLOCK(g_surf)) SDL_LockSurface(g_surf);
    for(; *s; s++){
        unsigned c = (unsigned char)*s;
        unsigned gi, gw, gx, gy;
        if(c == '\n'){ continue; } /* caller splits lines */
        if(c < FONT_FIRST || c >= FONT_FIRST + FONT_COUNT) c = (unsigned)'?';
        gi = c - FONT_FIRST;
        gw = font_adv[gi];
        for(gy = 0; gy < FONT_H; gy++)
            for(gx = 0; gx < gw; gx++)
                blend_px(x + (int)gx, y + (int)gy, r, g, b,
                         font_px[font_off[gi] + gy * gw + gx]);
        x += (int)gw + 2;
    }
    if(SDL_MUSTLOCK(g_surf)) SDL_UnlockSurface(g_surf);
}

static void present(void){
    if(g_win) SDL_UpdateWindowSurface(g_win);
}

/* --- card layout ------------------------------------------------------ */
#define CARD_X 260
#define CARD_Y 240
#define CARD_W (SCR_W - 2 * CARD_X)
#define CARD_H 600
#define PAD_X 64
#define TITLE_Y (CARD_Y + 48)
#define BODY_Y (CARD_Y + 150)
#define LINE_H (FONT_H + 12)
#define MAX_LINES 12

/* Greedy word wrap into fixed rows. Unknown glyphs count as '?'. */
static int wrap_lines(const char *msg, char lines[MAX_LINES][128]){
    int n = 0;
    char cur[128];
    size_t cl = 0;
    unsigned px = 0;
    int have_space = 0; /* a wrap point exists inside cur */
    size_t space_at = 0;
    unsigned px_after_space = 0;
    cur[0] = 0;
    while(*msg && n < MAX_LINES){
        unsigned c, a;
        if(*msg == '\n'){
            strncpy(lines[n], cur, 127); lines[n][127] = 0; n++;
            cur[0] = 0; cl = 0; px = 0;
            have_space = 0; space_at = 0; px_after_space = 0;
            msg++;
            continue;
        }
        c = (unsigned char)*msg;
        a = (c < FONT_FIRST || c >= FONT_FIRST + FONT_COUNT)
            ? font_adv['?' - FONT_FIRST] : font_adv[c - FONT_FIRST];
        if(px + a + 2 > (unsigned)(CARD_W - 2 * PAD_X) && cl > 0){
            if(have_space){
                /* break after the last space */
                size_t tail = cl - (space_at + 1);
                memcpy(lines[n], cur, space_at);
                lines[n][space_at] = 0; n++;
                if(n >= MAX_LINES) break;
                memmove(cur, cur + space_at + 1, tail + 1);
                cl = tail; px = px - px_after_space;
                have_space = 0; space_at = 0; px_after_space = 0;
                continue; /* re-process this char */
            }
            strncpy(lines[n], cur, 127); lines[n][127] = 0; n++;
            if(n >= MAX_LINES) break;
            cur[0] = 0; cl = 0; px = 0;
            have_space = 0; space_at = 0; px_after_space = 0;
            continue; /* re-process this char */
        }
        if(cl < sizeof cur - 1){ cur[cl++] = *msg; cur[cl] = 0; px += a + 2; }
        if(c == ' '){ have_space = 1; space_at = cl - 1; px_after_space = px; }
        msg++;
    }
    if(cl > 0 && n < MAX_LINES){ strncpy(lines[n], cur, 127); lines[n][127] = 0; n++; }
    return n;
}

static void render_card(const char *title, const char *msg, int pct, int show_bar,
                        const char *footer){
    char lines[MAX_LINES][128];
    int n, i;
    fill_rect(0, 0, SCR_W, SCR_H, C_BG_R, C_BG_G, C_BG_B);
    fill_rect(CARD_X, CARD_Y, CARD_W, CARD_H, C_CARD_R, C_CARD_G, C_CARD_B);
    draw_text(CARD_X + PAD_X, TITLE_Y, title, C_INK_R, C_INK_G, C_INK_B);
    fill_rect(CARD_X + PAD_X, TITLE_Y + FONT_H + 16, CARD_W - 2 * PAD_X, 3,
              C_DIM_R, C_DIM_G, C_DIM_B);
    n = wrap_lines(msg ? msg : "", lines);
    for(i = 0; i < n; i++)
        draw_text(CARD_X + PAD_X, BODY_Y + i * LINE_H, lines[i],
                  C_INK_R, C_INK_G, C_INK_B);
    if(show_bar){
        int bx = CARD_X + PAD_X, bw = CARD_W - 2 * PAD_X, by = CARD_Y + CARD_H - 170;
        if(pct < 0) pct = 0;
        if(pct > 100) pct = 100;
        fill_rect(bx, by, bw, 26, C_BAR_BG_R, C_BAR_BG_G, C_BAR_BG_B);
        fill_rect(bx, by, (bw * pct) / 100, 26, C_BAR_FG_R, C_BAR_FG_G, C_BAR_FG_B);
        {
            char pb[16];
            snprintf(pb, sizeof pb, "%d%%", pct);
            draw_text(bx + bw / 2 - (int)text_w(pb) / 2, by + 2, pb,
                      C_CARD_R, C_CARD_G, C_CARD_B);
        }
    }
    if(footer)
        draw_text(CARD_X + PAD_X, CARD_Y + CARD_H - 64, footer,
                  C_DIM_R, C_DIM_G, C_DIM_B);
    present();
}

/* --- pad: Cross-to-continue ------------------------------------------- */
static void pad_open_all(void){
    int i;
    for(i = 0; i < 4; i++){
        if(g_pad[i] < 0){
            int h = scePadOpen(i + 1, 0, 0, NULL);
            if(h >= 0){ g_pad[i] = h; g_prevbtn[i] = 0; }
        }
    }
}

/* 1 when Cross is newly pressed on any handle. */
static int pad_cross_pressed(void){
    int i;
    OrbisPadData d;
    for(i = 0; i < 4; i++){
        uint32_t now;
        if(g_pad[i] < 0) continue;
        memset(&d, 0, sizeof d);
        if(scePadReadState(g_pad[i], &d) < 0) continue;
        now = d.buttons;
        if((now & ORBIS_PAD_BUTTON_CROSS) && !(g_prevbtn[i] & ORBIS_PAD_BUTTON_CROSS)){
            g_prevbtn[i] = now;
            return 1;
        }
        g_prevbtn[i] = now;
    }
    return 0;
}

static void wait_cross(void){
    /* consume a held Cross first so the press that opened this
     * screen does not instantly dismiss it */
    int i;
    for(i = 0; i < 40; i++){
        pad_cross_pressed();
        sceKernelUsleep(20000);
    }
    for(;;){
        if(pad_cross_pressed()) break;
        sceKernelUsleep(20000);
    }
    for(i = 0; i < 25; i++){ /* let go before the next screen arms */
        OrbisPadData d;
        int j, held = 0;
        for(j = 0; j < 4; j++){
            if(g_pad[j] < 0) continue;
            memset(&d, 0, sizeof d);
            if(scePadReadState(g_pad[j], &d) == 0 && (d.buttons & ORBIS_PAD_BUTTON_CROSS))
                held = 1;
        }
        if(!held) break;
        sceKernelUsleep(20000);
    }
}

/* --- public API -------------------------------------------------------- */
static void reap_stale(void){
    if(sceImeDialogGetStatus() == ORBIS_DIALOG_STATUS_RUNNING)
        sceImeDialogTerm();
}

int ui_init(void){
    if(ui_ready) return 0;
    reap_stale();
    {
        OrbisUserServiceInitializeParams up;
        memset(&up, 0, sizeof up);
        up.priority = ORBIS_KERNEL_PRIO_FIFO_LOWEST;
        (void)sceUserServiceInitialize(&up);
    }
    /* Same proven order as before (apollo pattern): internal modules,
     * CommonDialog init BEFORE external loads, PAD, then IME modules.
     * MESSAGE_DIALOG is gone (we render our own cards). */
    if(sceSysmoduleLoadModuleInternal(ORBIS_SYSMODULE_INTERNAL_SYSTEM_SERVICE) != 0) return -1;
    if(sceSysmoduleLoadModuleInternal(ORBIS_SYSMODULE_INTERNAL_USER_SERVICE) != 0) return -1;
    if(sceSysmoduleLoadModuleInternal(ORBIS_SYSMODULE_INTERNAL_COMMON_DIALOG) != 0) return -1;
    if(sceCommonDialogInitialize() < 0) return -1;
    if(sceSysmoduleLoadModuleInternal(ORBIS_SYSMODULE_INTERNAL_PAD) != 0) return -1;
    (void)scePadInit();
    if(sceSysmoduleLoadModule(ORBIS_SYSMODULE_IME_DIALOG) < 0) return -1;
    if(sceSysmoduleLoadModule(ORBIS_SYSMODULE_IME_BACKEND) < 0){
        sceSysmoduleUnloadModule(ORBIS_SYSMODULE_IME_DIALOG);
        return -1;
    }
    if(SDL_Init(SDL_INIT_VIDEO) < 0) return -1;
    g_win = SDL_CreateWindow("orbisRPC Setup", SDL_WINDOWPOS_CENTERED,
                             SDL_WINDOWPOS_CENTERED, SCR_W, SCR_H, SDL_WINDOW_SHOWN);
    if(!g_win){ SDL_Quit(); return -1; }
    g_surf = SDL_GetWindowSurface(g_win);
    if(!g_surf || g_surf->format->BytesPerPixel != 4){ ui_shutdown(); return -1; }
    pad_open_all();
    ui_ready = 1;
    return 0;
}

int ui_ok(const char *msg){
    if(!ui_ready) return -1;
    render_card("orbisRPC Setup", msg, 0, 0, "Press X to continue");
    wait_cross();
    return 0;
}

static int progress_open = 0;
static char progress_msg[256] = "";
static int progress_pct = 0;

static void progress_repaint(void){
    render_card("orbisRPC Setup", progress_msg, progress_pct, 1, NULL);
}

int ui_progress_open(const char *msg){
    if(!ui_ready) return -1;
    if(msg){ strncpy(progress_msg, msg, sizeof progress_msg - 1); progress_msg[sizeof progress_msg - 1] = 0; }
    else progress_msg[0] = 0;
    progress_pct = 0;
    progress_open = 1;
    progress_repaint();
    return 0;
}

void ui_progress_msg(const char *msg){
    if(!progress_open || !msg) return;
    strncpy(progress_msg, msg, sizeof progress_msg - 1);
    progress_msg[sizeof progress_msg - 1] = 0;
    progress_repaint();
}

void ui_progress_set(unsigned pct){
    if(!progress_open) return;
    progress_pct = pct > 100 ? 100 : (int)pct;
    progress_repaint();
}

void ui_progress_close(void){
    if(!progress_open) return;
    progress_open = 0;
    fill_rect(0, 0, SCR_W, SCR_H, C_BG_R, C_BG_G, C_BG_B);
    present();
}

int ui_input(const char *title, const char *placeholder, char *out, size_t cap){
    static wchar_t wbuf[256];
    static wchar_t wtitle[64];
    static wchar_t wplace[64];
    OrbisImeDialogSetting st;
    int32_t uid = 0;
    size_t i;
    if(!ui_ready || !out || cap == 0 || cap > 200) return -1;
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
    st.posx = 960;
    st.posy = 540;
    st.horizontalAlignment = ORBIS_H_CENTER;
    st.verticalAlignment = ORBIS_V_CENTER;
    st.placeholder = wplace;
    st.title = wtitle;
    if(ime_dialog_running){ sceImeDialogTerm(); ime_dialog_running = 0; }
    ime_dbg("ime_begin\n");
    if(sceImeDialogInit(&st, NULL) < 0){ ime_dbg("ime_init_fail\n"); return -1; }
    ime_dialog_running = 1;
    {
        int spins = 0, rc = 0;
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
                if(res.endstatus != ORBIS_DIALOG_OK){ rc = 0; break; }
                for(i = 0; i < cap - 1 && wbuf[i]; i++)
                    out[i] = (wbuf[i] < 128 && wbuf[i] >= 32) ? (char)wbuf[i] : '?';
                out[i] = 0;
                rc = 1;
                break;
            }
            if(ds == ORBIS_DIALOG_STATUS_NONE && ++spins > 250){
                ime_dbg("ime_timeout\n");
                sceImeDialogTerm();
                ime_dialog_running = 0;
                rc = -1;
                break;
            }
            if(ds != ORBIS_DIALOG_STATUS_NONE) spins = 0;
            sceKernelUsleep(20000);
        }
        /* IME is a system overlay; repaint our card underneath on return. */
        if(progress_open) progress_repaint();
        else { fill_rect(0, 0, SCR_W, SCR_H, C_BG_R, C_BG_G, C_BG_B); present(); }
        return rc;
    }
}

void ui_shutdown(void){
    int i;
    if(!ui_ready){
        /* Still unload in case init died halfway. */
    }
    ui_ready = 0;
    progress_open = 0;
    ime_dialog_running = 0;
    for(i = 0; i < 4; i++){
        if(g_pad[i] >= 0){ scePadClose(g_pad[i]); g_pad[i] = -1; }
    }
    if(g_win){ SDL_DestroyWindow(g_win); g_win = NULL; g_surf = NULL; }
    SDL_Quit();
    sceImeDialogTerm();
    sceSysmoduleUnloadModule(ORBIS_SYSMODULE_IME_BACKEND);
    sceSysmoduleUnloadModule(ORBIS_SYSMODULE_IME_DIALOG);
    sceSysmoduleUnloadModule(ORBIS_SYSMODULE_INTERNAL_PAD);
}
