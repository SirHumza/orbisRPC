/* ui.h - native dialog helpers for the installer wizard. */
#ifndef INSTALLER_UI_H
#define INSTALLER_UI_H
#include <stddef.h>

/* Must run once before any dialog: loads MsgDialog/ImeDialog sysmodules
 * and inits CommonDialog. Calling dialogs without this panics the box
 * (unloaded-sysmodule call at startup). 0 ok, -1 fatal. */
int ui_init(void);

/* Info dialog with OK. 0 shown, -1 failed to open. */
int ui_ok(const char *msg);
/* Yes/No dialog: X = enter (Yes), O = back (No). 1 yes, 0 no/closed,
 * -1 error. */
int ui_confirm(const char *msg);

/* Progress dialog. Open once, update, close. 0 ok, -1 error. */
int ui_progress_open(const char *msg);
void ui_progress_msg(const char *msg);
void ui_progress_set(unsigned pct);
void ui_progress_close(void);

/* OSK text input. Prefills with out's current content. Returns 1 + out
 * filled on Done, 0 on cancel, -1 on error. */
int ui_input(const char *title, const char *placeholder, char *out, size_t cap);

/* Terminate dialogs + unload their sysmodules. Call once before exit. */
void ui_shutdown(void);

#endif
