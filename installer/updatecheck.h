/* updatecheck.h - ask GitHub whether a newer orbisRPC release exists.
 *
 * C++ because it needs net::Request and jsonlite; the wizard in installer.c is C.
 * Only the blocking entry point is exposed, since the install flow is modal end
 * to end and has nowhere to poll.
 *
 * Everything here fails soft. An offline console must still be able to install,
 * so no failure path blocks the caller -- they are logged and reported as "no
 * update known".
 */
#ifndef INSTALLER_UPDATECHECK_H
#define INSTALLER_UPDATECHECK_H

#include <stddef.h>

namespace update
{

struct Result
{
	enum Status
	{
		kUnknown = 0,   // could not determine; carry on regardless
		kCurrent,       // up to date
		kNewer,         // candidate is newer than the local build
	};

	Status     status;
	char       candidate[32];   // e.g. "1.70", empty when unknown
	char       assetUrl[256];   // browser_download_url, empty when not kNewer
	long       assetSize;       // bytes, -1 when unknown
	int        httpStatus;      // 0 when no response arrived at all
	long       bodyBytes;       // body length actually received
	const char *detail;         // short reason for kUnknown, may be NULL

	Result()
	{
		status = kUnknown;
		candidate[0] = 0;
		assetUrl[0] = 0;
		assetSize = -1;
		httpStatus = 0;
		bodyBytes = 0;
		detail = NULL;
	}
};

// Fetches the latest release and compares tag_name against `localVersion`.
//
// Blocking: it drives net::Request::Wait internally. `onTick` is forwarded to the
// progress dialog, so the caller can show motion while this runs.
Result Check(const char *repo, const char *localVersion,
             void (*onTick)(int percent, void *ctx), void *ctx);

// Downloads the asset to `dstPath`. Blocking, same progress hook.
//
// Verifies the byte count against the size the API reported, which catches a
// truncated transfer -- the realistic failure for a large file. Returns false on
// any error, leaving `dstPath` incomplete; the caller must treat a failure as
// fatal for this step rather than install a partial payload.
// Why a download failed. Recorded because a failure that logs no reason is
// indistinguishable from a failure that has not been investigated yet.
struct DlWhy
{
	int net;      // net::Result value; 0 (kOk) when the transfer itself worked
	int status;   // HTTP status, 0 when none was obtained
	int lastErrno; // transport error code, 0 when none was reported

	DlWhy() : net(-1), status(0), lastErrno(0) {}
};

// Fetches the asset to `dstPath`. Blocking, same progress hook.
//
// Verifies the byte count against the size the API reported, which catches a
// truncated transfer -- the realistic failure for a large file. Returns false on
// any error, leaving `dstPath` incomplete; the caller must treat a failure as
// fatal for this step rather than install a partial payload.
bool Download(const char *url, const char *dstPath, long expectedSize,
              DlWhy *why, void (*onTick)(int percent, void *ctx), void *ctx);;

} // namespace update

#endif