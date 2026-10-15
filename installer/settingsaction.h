/* settingsaction.h - the action rows on the Settings page.
 *
 * Separate from settings.cpp because those are the page's own model, while this
 * is modal work that happens on top of it: the page keeps its state, a dialog
 * opens, and the page is drawn again afterwards. Keeping them apart is what lets
 * settings.cpp stay free of UI calls.
 *
 * C, so main.cpp can call it directly.
 */
#ifndef INSTALLER_SETTINGSACTION_H
#define INSTALLER_SETTINGSACTION_H

#ifdef __cplusplus
extern "C" {
#endif

/* The nano DNS row: adds or removes the Sony TMDB exception, whichever is
 * currently not true. Shows its own dialogs and returns once they are answered.
 *
 * Safe on a console with no nanoDNS -- says so, rather than reporting a failure
 * for something that was never installed.
 *
 * Always returns 0. The caller has nothing to do with the outcome: every path
 * ends on a dialog that has already said what happened. */
int settingsaction_nanodns(void);

#ifdef __cplusplus
}
#endif

#endif