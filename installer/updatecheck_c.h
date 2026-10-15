/* updatecheck_c.h - C-visible surface of the update check.
 *
 * installer.c is C and cannot include a C++ header, so the check lives in C++
 * (it needs net::Request and jsonlite) and is reached through this flat C shim.
 * All out-parameters are plain buffers the caller owns. */
#ifndef INSTALLER_UPDATECHECK_C_H
#define INSTALLER_UPDATECHECK_C_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

enum update_status
{
	UPDATE_UNKNOWN = 0,   /* could not determine; carry on regardless */
	UPDATE_CURRENT,       /* already up to date */
	UPDATE_NEWER          /* candidate is newer */
};

/* Asks GitHub for the newest release and compares against localVersion.
 *
 * Always returns; a network failure yields UPDATE_UNKNOWN rather than an error,
 * because being offline must not stop an install.
 *
 * candidate / assetUrl receive at most cap bytes and are NUL-terminated.
 * assetSize receives the byte count, or -1 when the API did not report one.
 *
 * httpStatus and bodyBytes are diagnostics: they let the caller record what the
 * server actually returned. Without them a 403 from GitHub and an unreachable
 * host are indistinguishable from the outside, which is exactly the ambiguity
 * that made "the updater silently did nothing" hard to diagnose. httpStatus is
 * 0 when no response arrived at all. */
int update_check(const char *repo, const char *localVersion,
                 char *candidate, size_t capCandidate,
                 char *assetUrl, size_t capUrl,
                 long *assetSize,
                 int *httpStatus, long *bodyBytes,
                 void (*onTick)(int percent, void *ctx), void *ctx);

/* Fetches url to dstPath, verifying the byte count against expectedSize.
 *
 * Writes to "<dstPath>.part" and renames on success, so a failed transfer never
 * leaves a partial file at dstPath. Returns 1 on success.
 *
 * `whyNet`, `whyStatus` and `whyErrno` are diagnostics and may be NULL. They
 * receive the transport result code, the HTTP status, and the transport's own
 * error code from sceHttpGetLastErrno. Without them a failed download logged
 * only "errno=0" -- which is what a failed download that has not been diagnosed
 * looks like, since the reason went to menu.log where nobody looks after an
 * install problem. */
int update_download(const char *url, const char *dstPath, long expectedSize,
                    int *whyNet, int *whyStatus, int *whyErrno,
                    void (*onTick)(int percent, void *ctx), void *ctx);

#ifdef __cplusplus
}
#endif

#endif
