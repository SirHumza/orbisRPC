/* uninstall.h - removing an orbisRPC install.
 *
 * The counterpart to the install flow, and deliberately built from the same
 * constants in paths.h. Install writes this set and uninstall removes it; if the
 * two disagreed the console would be left half-uninstalled, which is the failure
 * this pairing exists to prevent.
 */
#ifndef INSTALLER_UNINSTALL_H
#define INSTALLER_UNINSTALL_H

#ifdef __cplusplus
extern "C" {
#endif

/* uninstall_flow() outcomes, matching installer_flow()'s shape so main.cpp can
 * report them the same way. */
enum {
	UNINSTALL_OK = 0,          /* removed, or there was nothing to remove */
	UNINSTALL_STOPPED = 1,     /* declined at the prompt */
	UNINSTALL_FAILED = 2       /* something could not be removed */
};

/* Removes the install. Blocks on its dialogs and returns once it is done.
 *
 * Safe to call on a console with nothing installed: says so and returns
 * UNINSTALL_OK rather than reporting a failure for something that was never
 * there. */
int uninstall_flow(void);

#ifdef __cplusplus
}
#endif

#endif