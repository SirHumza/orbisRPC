/* notify.h - the post-update toast. */
#ifndef INSTALLER_NOTIFY_H
#define INSTALLER_NOTIFY_H

#include <stddef.h>

#include "paths.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Sends "orbisRPC updated to <version>. Restart your console." as a system
 * toast, carrying the icon at ICON_PATH if that file is readable and the
 * built-in system icon if it is not.
 *
 * Called only after the install is confirmed on disk, so the user is never told
 * to restart for an update that did not land.
 *
 * Returns 1 if the request was accepted. Failure is logged and ignored: the
 * "Done." dialog has already said the same thing on screen. */
int notify_update_installed(const char *version);

/* The icon, if present. Absent is normal, not an error.
 *
 * /data and /user/data are the same location under two names -- a shortcut, not
 * two mounts -- so this is where WE look, because we are the process asking, and
 * /data is where this app already writes config.json, install.log and the payload.
 *
 * It is also not where the toast reads from: ShellUI fetches the icon itself, and
 * names the same file with the /user/data spelling. So the check happens against
 * ICON_PATH and the URI handed over uses ICON_URI below. Reading the file through
 * the /user/data name from here is what failed before -- that name is for the
 * consumer, not for us.
 *
 * ICON_PATH is defined in paths.h, since uninstall removes it along with the rest
 * of /data/orbisRPC. */

/* ShellUI's spelling of the same file. Two slashes, not three: "file://" plus a
 * path whose leading slash has been dropped, so file://user/data/... rather than
 * file:///user/data/... */
#define ICON_URI "file://user/data/orbisRPC/icon.png"

#ifdef __cplusplus
}
#endif

#endif
