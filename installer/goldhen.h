/* goldhen.h - adding orbisRPC to GoldHEN's auto-run list.
 *
 * GoldHEN runs the entries in the [AutoRun] section of payloads.ini at boot, so
 * adding the payload there is what makes it start without the user launching it.
 *
 * The line added and the line taken away again by uninstall both live in paths.h
 * -- GOLDHEN_INI, GOLDHEN_AUTORUN_ELF -- for the same reason the INI editor is
 * shared: a mismatched path is the worst outcome here. GoldHEN accepts an entry
 * it cannot resolve and silently does nothing, which is indistinguishable from the
 * feature not working at all.
 */
#ifndef INSTALLER_GOLDHEN_H
#define INSTALLER_GOLDHEN_H

#include "paths.h"

#ifdef __cplusplus
extern "C" {
#endif

#define GOLDHEN_SECTION "AutoRun"

/* The value that enables an entry. GoldHEN treats 0 as present-but-disabled, so
 * this is what turns a line on rather than merely adding it. */
#define GOLDHEN_ENABLED "1"

/* 1 when payloads.ini exists. 0 when GoldHEN is not installed, which is the
 * common case and must not be reported as a failure. */
int goldhen_present(void);

/* Adds "<payload path> = 1" to [AutoRun], creating the section if needed.
 *
 *   1  added
 *   0  already there
 *  -1  could not read or write
 *
 * Idempotent: a payload already listed is not added a second time, however many
 * times the installer runs. */
int goldhen_add_autorun(const char *payloadPath);

/* Takes that same entry back out again, for uninstall. Same path and section as
 * the add, so the two cannot end up addressing different lines.
 *
 *   1  removed
 *   0  it was not there, and payloads.ini was not written to
 *  -1  could not read or write
 *
 * The file itself is never removed, only the line: it holds every other payload
 * the user has queued to start at boot. */
int goldhen_remove_autorun(const char *payloadPath);

#ifdef __cplusplus
}
#endif

#endif
