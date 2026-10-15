/* uibridge.h - the install flow's on-screen dialogs, drawn by the app.
 *
 * installer.c is C and owns the flow; main.cpp owns the surface, the fonts and
 * the pad. This is the seam between them: a flat C surface implemented in C++,
 * so the flow can put a screen up without knowing anything about SDL.
 *
 * Replaces the sceMsgDialog calls in the install path. ui.c is still used for
 * the IME, which the Settings page needs and which has no drawn equivalent worth
 * writing -- the OS keyboard is better than anything we would produce.
 *
 * Every function is safe to call before Setup() except the ones that need a
 * surface: those fall back to a no-op rather than crashing, so a flow that runs
 * without the UI (tests, or a menu that failed to initialise) degrades instead of
 * taking the process down.
 */
#ifndef INSTALLER_UIBRIDGE_H
#define INSTALLER_UIBRIDGE_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Binds the surface, console, input and FreeType library. Call once after SDL and
 * Input are up. Everything is borrowed, not owned. Returns 0 on success, -1 if
 * the fonts could not be loaded -- in which case the flow should keep using the
 * system dialogs. */
int uibridge_setup(void *surface, void *console, void *input, void *ftLib);

/* Message with one dismissal. Blocks until the user presses X (or O).
 * Returns 0. */
void uibridge_ok(const char *title, const char *body);

/* Two buttons. Blocks. 1 = Yes, 0 = No. Never returns -1: with no pad there is
 * no answer to give, so the caller gets the safe default rather than a value it
 * has to special-case. */
int uibridge_yesno(const char *title, const char *body);

/* 1 when the last uibridge_ok() was dismissed with Circle rather than X.
 *
 * This is the install flow's way back out. Circle is the console's Back button,
 * and on a one-button screen it had nothing to answer, so it behaved exactly
 * like X -- pressing it walked the user through the rest of the install rather
 * than out of it.
 *
 * Read it after the uibridge_ok() it describes, before doing anything else:
 * another dialog overwrites it. Always 0 after uibridge_yesno(), where Circle
 * means No, which is an answer rather than a way out. */
int uibridge_cancelled(void);

/* Progress. Open once, update, close. `status` may be NULL.
 *
 * These do not block: the flow updates them between steps and between network
 * ticks, so a long download keeps animating. */
void uibridge_progress_open(const char *title);
void uibridge_progress_status(const char *status);
void uibridge_progress_close(void);

/* Draws one frame of whatever screen is currently up. Safe when none is.
 * This is what the network tick calls, so the spinner keeps turning while a
 * request is in flight. */
void uibridge_tick(void);

#ifdef __cplusplus
}
#endif

#endif
