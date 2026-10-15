#include "updatecheck.h"

#include <stdio.h>
#include <string.h>

#include "updatecheck_c.h"

#include "dbglog.h"
#include "jsonlite.h"
#include "netclient.h"
#include "version.h"

namespace {

// The release JSON is a few kilobytes. The default cap is 64KB, which is plenty
// for the metadata but far too small for a multi-megabyte payload, so the two
// limits are set separately.
const size_t kMetadataMax = 256 * 1024;
const size_t kPayloadMax  = 64 * 1024 * 1024;

// Spare DlWhy for the internal callers that have nowhere to put one, so
// Download() never has to null-check it.
update::DlWhy s_scratchWhy;

void Copy(char *dst, size_t cap, const char *src)
{
	if (cap == 0)
		return;

	if (!src)
	{
		dst[0] = 0;
		return;
	}

	size_t n = strlen(src);
	if (n >= cap)
		n = cap - 1;

	memcpy(dst, src, n);
	dst[n] = 0;
}

// Pulls one string member out of a parsed object.
const char *Member(const jl_val_t *object, const char *key)
{
	if (!object || object->type != JL_OBJECT)
		return NULL;

	const jl_val_t *v = jl_obj_get(const_cast<jl_val_t *>(object), key);

	if (!v || v->type != JL_STRING || !v->str)
		return NULL;

	return v->str;
}

} // namespace

// ---- C shim ---------------------------------------------------------------
//
// installer.c is C, so these two entry points are the whole C-visible surface.
// They sit outside namespace update so the names are plain, and are declared in
// updatecheck_c.h.

extern "C" int update_check(const char *repo, const char *localVersion,
                            char *candidate, size_t capCandidate,
                            char *assetUrl, size_t capUrl, long *assetSize,
                            int *httpStatus, long *bodyBytes,
                            void (*onTick)(int percent, void *ctx), void *ctx)
{
	update::Result r = update::Check(repo, localVersion, onTick, ctx);

	Copy(candidate, capCandidate, r.candidate);
	Copy(assetUrl, capUrl, r.assetUrl);

	if (assetSize)
		*assetSize = r.assetSize;

	if (httpStatus)
		*httpStatus = r.httpStatus;

	if (bodyBytes)
		*bodyBytes = r.bodyBytes;

	switch (r.status)
	{
	case update::Result::kCurrent: return UPDATE_CURRENT;
	case update::Result::kNewer:   return UPDATE_NEWER;
	default:                       return UPDATE_UNKNOWN;
	}
}

extern "C" int update_download(const char *url, const char *dstPath,
                               long expectedSize,
                               int *whyNet, int *whyStatus, int *whyErrno,
                               void (*onTick)(int percent, void *ctx), void *ctx)
{
	update::DlWhy why;
	const bool ok = update::Download(url, dstPath, expectedSize, &why,
	                                 onTick, ctx);

	if (whyNet)
		*whyNet = why.net;

	if (whyStatus)
		*whyStatus = why.status;

	if (whyErrno)
		*whyErrno = why.lastErrno;

	return ok ? 1 : 0;
}

namespace update
{

Result Check(const char *repo, const char *localVersion,
             void (*onTick)(int percent, void *ctx), void *ctx)
{
	Result result;

	if (!net::Init())
	{
		result.detail = "networking is unavailable";
		dbglog::Write("update: net::Init failed");
		return result;
	}

	char url[192];
	snprintf(url, sizeof url, "https://api.github.com/repos/%s/releases/latest",
	         repo ? repo : "");

	dbglog::Write("update: GET %s", url);

	net::Request request;

	if (!request.Begin(url, kMetadataMax))
	{
		result.detail = "could not start the request";
		dbglog::Write("update: Begin failed");
		return result;
	}

	if (!request.Wait(onTick, ctx))
	{
		const net::Response &r = request.Result();

		result.httpStatus = r.statusCode;
		result.bodyBytes = static_cast<long>(r.body.size());
		result.detail = "could not reach GitHub";
		dbglog::Write("update: %s (status %d)", net::ResultText(r.result),
		              r.statusCode);
		net::Term();
		return result;
	}

	const net::Response &r = request.Result();

	result.httpStatus = r.statusCode;
	result.bodyBytes = static_cast<long>(r.body.size());

	if (r.statusCode != 200)
	{
		result.detail = "GitHub returned an error";
		dbglog::Write("update: status %d, body %ld bytes", r.statusCode,
		              result.bodyBytes);
		net::Term();
		return result;
	}

	jl_val_t *root = jl_parse(r.body.c_str(), r.body.size());

	if (!root || root->type != JL_OBJECT)
	{
		if (root)
			jl_free(root);

		result.detail = "could not read the reply";
		dbglog::Write("update: malformed JSON, %ld bytes", result.bodyBytes);
		net::Term();
		return result;
	}

	const char *tag = Member(root, "tag_name");
	Copy(result.candidate, sizeof result.candidate, tag);

	if (!tag)
	{
		jl_free(root);
		result.detail = "no tag in the reply";
		dbglog::Write("update: no tag_name");
		net::Term();
		return result;
	}

	// assets is an array; take the first entry that carries a download URL.
	const jl_val_t *assets = jl_obj_get(root, "assets");

	if (assets && assets->type == JL_ARRAY)
	{
		for (const jl_val_t *a = assets->child; a; a = a->next)
		{
			const char *download = Member(a, "browser_download_url");

			if (!download)
				continue;

			Copy(result.assetUrl, sizeof result.assetUrl, download);

			const jl_val_t *size = jl_obj_get(const_cast<jl_val_t *>(a), "size");

			if (size && size->type == JL_NUMBER)
				result.assetSize = size->num_is_int ? size->inum
				                                   : (long)size->num;

			break;
		}
	}

	jl_free(root);
	net::Term();

	dbglog::Write("update: tag=%s local=%s asset=%s size=%ld",
	              result.candidate, localVersion ? localVersion : "(null)",
	              result.assetUrl[0] ? result.assetUrl : "(none)", result.assetSize);

	/* Compare against result.candidate, NOT `tag`. `tag` points into the parsed
	 * tree and jl_free(root) above just released it -- reading it here is a
	 * use-after-free.
	 *
	 * This was the updater's real bug and it was invisible: on the host, freed
	 * memory still held its old bytes so every test passed, while on the console
	 * the allocator had reused the block and the compare read garbage. Every
	 * release therefore looked up to date. result.candidate is this struct's own
	 * copy, made before the free, so it stays valid for as long as the Result. */
	result.status = version::IsNewer(result.candidate, localVersion)
		? Result::kNewer
		: Result::kCurrent;

	if (result.status == Result::kNewer && !result.assetUrl[0])
	{
		// Nothing to fetch, so there is nothing to offer.
		result.status = Result::kCurrent;
	}

	return result;
}

bool Download(const char *url, const char *dstPath, long expectedSize,
              DlWhy *why, void (*onTick)(int percent, void *ctx), void *ctx)
{
	if (!why)
		why = &s_scratchWhy;

	why->net = -1;
	why->status = 0;
	why->lastErrno = 0;

	if (!url || !dstPath)
		return false;

	if (!net::Init())
	{
		dbglog::Write("update: net::Init failed for download");
		return false;
	}

	dbglog::Write("update: downloading %s", url);

	net::Request request;

	if (!request.Begin(url, kPayloadMax))
	{
		dbglog::Write("update: Begin failed for download");
		net::Term();
		return false;
	}

	const bool ok = request.Wait(onTick, ctx);
	const net::Response &r = request.Result();

	why->net = (int)r.result;
	why->status = r.statusCode;
	why->lastErrno = r.lastErrno;

	if (!ok || r.statusCode != 200)
	{
		dbglog::Write("update: download %s (status %d)",
		              net::ResultText(r.result), r.statusCode);
		net::Term();
		return false;
	}

	net::Term();

	// Size check, not a hash: the failure this guards against is a dropped
	// connection leaving a short file, which the byte count catches. GitHub also
	// publishes a sha256 digest, but verifying it means shipping a SHA-256
	// implementation, and a tampered file over a valid TLS chain is not the
	// threat here.
	const long got = static_cast<long>(r.body.size());

	if (expectedSize >= 0 && got != expectedSize)
	{
		dbglog::Write("update: size mismatch, expected %ld got %ld",
		              expectedSize, got);
		return false;
	}

	// Write to a temporary beside the target, then rename into place, so a
	// partial write never leaves a half file at the real path.
	char tmp[320];
	snprintf(tmp, sizeof tmp, "%s.part", dstPath);

	FILE *f = fopen(tmp, "wb");

	if (!f)
	{
		dbglog::Write("update: cannot open %s", tmp);
		return false;
	}

	const bool wrote = (got == 0) ||
	                   (fwrite(r.body.data(), 1, static_cast<size_t>(got), f)
	                    == static_cast<size_t>(got));

	if (fclose(f) != 0 || !wrote)
	{
		dbglog::Write("update: write failed");
		remove(tmp);
		return false;
	}

	if (rename(tmp, dstPath) != 0)
	{
		dbglog::Write("update: rename failed");
		remove(tmp);
		return false;
	}

	dbglog::Write("update: wrote %ld bytes to %s", got, dstPath);
	return true;
}

} // namespace update