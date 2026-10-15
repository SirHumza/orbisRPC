/* rmtree.h - recursive directory removal.
 *
 * Its own module, rather than a static inside uninstall.c, because that file
 * pulls in the PS4 dialog bridge and so cannot be built on a host. This is the
 * most destructive thing the app does -- it deletes a tree on someone's console
 * with no undo -- so it is the thing most in need of a test that can actually
 * run, and it can only have one if it does not drag the UI in with it.
 *
 * Pure POSIX: opendir, readdir, unlink, rmdir. No PS4 headers.
 */
#ifndef INSTALLER_RMTREE_H
#define INSTALLER_RMTREE_H

#ifdef __cplusplus
extern "C" {
#endif

/* Removes `path` and everything under it, recursively.
 *
 * Returns 1 when `path` no longer exists -- including when it never did, which
 * is the state the caller wanted -- and 0 when something survived.
 *
 * Symlinks are removed, not followed. A link under the tree that points somewhere
 * outside it has its link deleted and its target left alone.
 *
 * Stops and reports failure past the depth and entry limits, rather than
 * continuing: reaching either means the tree is not what this app created, and
 * the right response to that is to stop and say so. */
int rmtree(const char *path);

#ifdef __cplusplus
}
#endif

#endif