/* ui.h - native dialog helpers for the installer wizard. */
#ifndef INSTALLER_UI_H
#define INSTALLER_UI_H
#include <stddef.h>

/* This header is included from C++ (the app shell) as well as the C wizard. */
#ifdef __cplusplus
extern "C" {
#endif

/* Must run once before any dialog: loads MsgDialog/ImeDialog sysmodules
 * and inits CommonDialog. Calling dialogs without this panics the box
 * (unloaded-sysmodule call at startup). 0 ok, -1 fatal. */
int ui_init(void);

/* Info dialog with OK. 0 shown, -1 failed to open. */
int ui_ok(const char *msg);

/* Two-button dialog. 1 = yes, 0 = no, -1 = failed or unrecognised.
 *
 * -1 means the dialog never opened, or reported a button this build does not
 * know. Callers should treat that as "no": a user who was never asked, or whose
 * answer was lost, has not agreed to anything. */
int ui_yesno(const char *msg);
/* Progress dialogs are not built here. The install flow drives the SDL-drawn
 * overlay through uibridge_progress_open/_status/_close instead, and nothing
 * ever opened a native message-dialog progress bar. */

/* The raw button value the last message dialog reported: 1 for OK/YES, 2 for
 * NO, anything else (including -1) means the answer was not readable.
 * Exposed for the install log, since ui_yesno collapses every unrecognised
 * value into a decline and that loss is what made the failure opaque. */
int ui_last_button(void);

/* OSK text input. Prefills with out's current content. Returns 1 + out
 * filled on Done, 0 on cancel, -1 on error.
 *
 * cap must not exceed UI_INPUT_MAX. Over that it returns -1 immediately, before
 * the IME is opened -- no dialog appears and nothing says why, which reads as a
 * broken row rather than a rejected argument. That happened: a caller passing a
 * 256-byte buffer got a field that silently never opened.
 *
 * The number itself is arbitrary within what the dialog's own buffer allows; it
 * is published here so callers size from it instead of picking one and having
 * the limit be invisible at the call site. */
int ui_input(const char *title, const char *placeholder, char *out, size_t cap);

/* Largest cap ui_input accepts. Callers should size their buffers from this
 * rather than picking a number, so the limit is visible where it is obeyed. */
#define UI_INPUT_MAX 192

/* Terminate dialogs + unload their sysmodules. Call once before exit.
 * Note: the merged app needs the IME for its own text entry, so it must NOT
 * call this between screens -- only on the way out. */
void ui_shutdown(void);

#ifdef __cplusplus
}
#endif

#endif
