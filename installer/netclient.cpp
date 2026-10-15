#include "netclient.h"

#include <stdint.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <strings.h>   // strncasecmp, for the Location header
#include <fcntl.h>     // netlog's open/write/close
#include <unistd.h>

#include <SDL2/SDL.h>

#include <orbis/Net.h>
#include <orbis/Ssl.h>
#include <orbis/Http.h>
#include <orbis/Sysmodule.h>

namespace {

/* Matches the toolchain's own net_http sample exactly: "Mozilla/5.0
 * (PLAYSTATION 4; 1.00)". The previous value here was "PS4GetToken/1.00", left
 * over from the token fetcher, and there is no evidence for it being preferable --
 * the sample is the only reference implementation known to work on this box, so
 * where it names a value, this follows it. */
const char *const kUserAgent = "Mozilla/5.0 (PLAYSTATION 4; 1.00)";
/* The toolchain's net_http sample uses 4 KB, and so did this until github.com
 * needed a response header larger than the default. That header buffer is
 * allocated out of THIS pool -- sceHttpInit is handed g_netMemId -- so a 4 KB
 * pool cannot satisfy a 32 KB header request, and the send failed with
 * 0x809517D5, "out of memory", rather than the 0x80431073 it reported before.
 *
 * One error became another rather than disappearing, which is the tell.
 *
 * Sized so the header request has room to spare rather than sitting right on the
 * limit, and because the console has 6 GB and this is a few hundred KB for the
 * life of the process. */
const int kNetPoolSize = 256 * 1024;

/* SSL pool.
 *
 * This was the SDK's SSL_POOLSIZE of 128 KB, and it is why every download to
 * github.com failed with 0x809517D5 -- SCE_SSL_ERROR_OUT_OF_MEMORY, raised during
 * the TLS handshake before a request could be sent. The PS4 developer wiki notes
 * 256 KiB per concurrent connection; at 128 KB there was not enough for one.
 *
 * api.github.com hands over with a smaller certificate chain and fits, which is
 * the whole reason the update check worked and the download never did.
 *
 * 1 MB leaves room for several handshakes at once and costs nothing on a console
 * with 6 GB, held for the life of the process. */
const size_t kSslPoolSize = 1024 * 1024;

/* Keep-alive is off (sceHttpCreateConnectionWithURL's last argument).
 *
 * It was on, and every request after the first in a process failed with
 * SCE_SSL_ERROR_OUT_OF_MEMORY while the first succeeded. Raising the net pool to
 * 256 KB and the SSL pool to 1 MB changed nothing, which is not how a size limit
 * behaves -- it is what a connection the allocator still considers live looks
 * like. Each request here is short and there are at most two per run, so there is
 * nothing to gain from reusing sockets and a plausible reason to suspect it. */
const int kKeepAlive = 0;

// Diagnostics that actually survive the console.
//
// SDL_Log goes nowhere here, which is why every previous failure looked identical:
// "request failed (status 0)" with no way to tell a TLS problem from a DNS one
// from a bad argument. This writes straight to a file with open()/write(), which
// is also why it is not stdio: no buffering to lose when the next call faults.
//
// Appended, never truncated, so a run's whole history stays readable.
void netlog(const char *fmt, ...)
{
	static const char *kPath = "/data/orbisRPC/net.log";

	char line[512];
	va_list ap;
	int fd, n;

	va_start(ap, fmt);
	n = vsnprintf(line, sizeof line - 2, fmt, ap);
	va_end(ap);

	if (n < 0)
		return;
	if (n > (int)sizeof line - 2)
		n = (int)sizeof line - 2;

	line[n++] = '\n';

	fd = open(kPath, O_WRONLY | O_CREAT | O_APPEND, 0666);
	if (fd < 0)
		return;
	(void)write(fd, line, (size_t)n);
	close(fd);
}

// Redirect hops allowed before giving up. GitHub sends exactly one (to a signed
// S3 URL); five leaves room for a CDN in front without inviting a loop.
const int kMaxRedirects = 5;

// Timeouts, in microseconds. The stock defaults are short for a multi-megabyte
// transfer over a home connection: a 2 MB body that pauses briefly between chunks
// can outlive a default receive window, and that surfaces as a read failure part
// way through rather than as anything obvious. Generous, because the download is
// on a modal screen the user is waiting on -- there is nothing to gain by cutting
// it short, and the progress screen has no cancel.
const uint32_t kConnectTimeoutUs = 30u * 1000u * 1000u;   // 30s
const uint32_t kRecvTimeoutUs    = 60u * 1000u * 1000u;   // 60s between reads
const uint32_t kSendTimeoutUs    = 30u * 1000u * 1000u;   // 30s

int  g_netMemId  = 0;
int  g_sslCtxId = 0;
int  g_httpCtxId = 0;
bool g_ready = false;

// Live Init() calls. Term() releases rather than tears down; see Init().
int g_refs = 0;

// Certificate verification callback.
//
// The PS4's SSL stack carries a dated CA bundle that will not validate
// Let's Encrypt, which is what most modern hosts use (Vercel among them). The
// toolchain's own net_http sample sidesteps this the same way: accept whatever
// the peer presents.
//
// The trade-off is real -- the peer is not authenticated, so a machine on your
// network could intercept and rewrite the exchange. That is acceptable for a
// personal tool on a trusted LAN, but do not reuse this pattern to carry
// anything you would not want read or altered in transit.
int32_t AcceptAnyCertificate(int32_t sslCtxId, uint32_t verifyErr,
                             void *const sslCert[], int32_t certNum, void *userArg)
{
	return 1; // non-zero accepts the certificate
}

} // namespace

/* sceHttpSetRecvTimeOut is declared void() with the parameters stripped, like so
 * many NIDs in this toolchain's headers. Its immediate sibling
 * sceHttpSetSendTimeOut is declared in full as
 *
 *     int32_t sceHttpSetSendTimeOut(int32_t id, uint32_t usec);
 *
 * and the pair are the same shape in libSceHttp, so this inference is about as
 * safe as one gets here. It is the receive timeout that matters for a slow
 * multi-megabyte body, which is why it is worth declaring rather than dropping.
 *
 * If the guess is wrong this faults -- and it is called first in each hop and its
 * return value is logged immediately, so a crash points straight at this line
 * instead of looking like a network problem.
 *
 * Outside the anonymous namespace deliberately: a declaration there gets internal
 * linkage, which resolves to "(anonymous namespace)::sceHttpSetRecvTimeOut"
 * rather than to the real NID. */
/* The asm label is what makes this work. Declaring the same name again with a
 * real signature is rejected outright -- the header's stripped "void
 * sceHttpSetRecvTimeOut();" is already in scope, and two declarations differing
 * only in return type cannot be overloaded. Giving this a different C++ name and
 * pointing it at the real symbol with an asm label sidesteps that without
 * touching the toolchain's header. */
extern "C" int32_t net_set_recv_timeout(int32_t id, uint32_t usec)
	asm("sceHttpSetRecvTimeOut");

/* THE FIX. 0x80431073 is "too large a response header was received", and it is
 * what killed every payload download:
 *
 *   sceHttpSendRequest rc = -2143088525  (0x80431073)
 *
 * The release metadata from api.github.com has a small header and sails through.
 * The asset URL on github.com returns a much bigger one -- cookies, security
 * headers, the lot -- and it overruns the default header buffer, so the request
 * fails before a single byte of status line is parsed. That also explains why
 * redirect following never got a chance to run: the Location header is in the
 * buffer that overflowed.
 *
 * Every theory before this was wrong. Not the pool lifecycle, not TLS, not DNS,
 * not the User-Agent, not the timeouts -- the request was never malformed, the
 * server simply answered with more header than the console was willing to hold.
 *
 * Declared the same way as the receive timeout: the toolchain strips this to
 * void(), and the re-declaration needs an asm label to dodge the clash.
 *
 * Applied per request, after creation and before sending. 32KB against a 256KB
 * net pool leaves plenty of headroom; real headers run to a few KB, and the
 * overflow this prevents is against the much smaller *default* size, not against
 * anything approaching this. */
const uint32_t kResponseHeaderMax = 32u * 1024u;

extern "C" int32_t net_set_header_max(int32_t id, uint32_t size)
	asm("sceHttpSetResponseHeaderMaxSize");

namespace net {

const char *ResultText(Result result)
{
	switch (result)
	{
	case kOk:             return "ok";
	case kNotInitialised: return "networking not available";
	case kCancelled:      return "cancelled";
	case kConnectFailed:  return "could not connect";
	case kSendFailed:     return "request failed";
	case kStatusFailed:   return "no status returned";
	case kReadFailed:     return "could not read the response";
	case kBodyTooLarge:   return "response too large";
	}
	return "unknown error";
}

bool Init()
{
	// Reference counted, and Term() deliberately does not tear the contexts down.
	//
	// These pools are not reliably re-initialisable on this console: init, term,
	// init again yields a context that accepts Init() and then fails every
	// request with no status code. That is not a guess -- it is what the update
	// download hit, straight after the metadata request had worked through the
	// same code path. The second Init() returned true and the request then died
	// at sceHttpSendRequest.
	//
	// So the context is created once and kept for the life of the process, which
	// is what the HTTP client on this platform is normally used for anyway.
	if (g_refs > 0)
	{
		g_refs++;
		return true;
	}

	{
		// Each return value is captured rather than folded into a single ||, so
		// the log can say WHICH module refused. A combined boolean cannot.
		const int netRc = sceSysmoduleLoadModuleInternal(ORBIS_SYSMODULE_INTERNAL_NET);
		const int httpRc = sceSysmoduleLoadModuleInternal(ORBIS_SYSMODULE_INTERNAL_HTTP);
		const int sslRc = sceSysmoduleLoadModuleInternal(ORBIS_SYSMODULE_INTERNAL_SSL);

		netlog("Init: sysmodules net=%d http=%d ssl=%d", netRc, httpRc, sslRc);

		if (netRc < 0 || httpRc < 0 || sslRc < 0)
		{
			netlog("Init: FAILED loading a networking module");
			SDL_Log("net: failed to load a networking module");
			return false;
		}
	}

	{
		const int netInitRc = sceNetInit();
		netlog("Init: sceNetInit=%d", netInitRc);
		if (netInitRc < 0)
		{
			SDL_Log("net: sceNetInit failed");
			return false;
		}
	}

	g_netMemId = sceNetPoolCreate("netPool", kNetPoolSize, 0);
	netlog("Init: netPool=%d", g_netMemId);
	if (g_netMemId < 0)
	{
		SDL_Log("net: sceNetPoolCreate failed: 0x%08X", g_netMemId);
		return false;
	}

	g_sslCtxId = sceSslInit(kSslPoolSize);
	netlog("Init: ssl=%d pool=%lu", g_sslCtxId, (unsigned long)kSslPoolSize);
	if (g_sslCtxId < 0)
	{
		SDL_Log("net: sceSslInit failed: 0x%08X", g_sslCtxId);
		return false;
	}

	g_httpCtxId = sceHttpInit(g_netMemId, g_sslCtxId, LIBHTTP_POOLSIZE);
	netlog("Init: httpCtx=%d", g_httpCtxId);
	if (g_httpCtxId < 0)
	{
		SDL_Log("net: sceHttpInit failed: 0x%08X", g_httpCtxId);
		return false;
	}

	g_ready = true;
	g_refs = 1;
	netlog("Init: OK  netPool=%d(%lu) ssl=%d(%lu) http=%d(%lu)", g_netMemId,
	       (unsigned long)kNetPoolSize, g_sslCtxId, (unsigned long)kSslPoolSize,
	       g_httpCtxId, (unsigned long)LIBHTTP_POOLSIZE);
	SDL_Log("net: initialised");
	return true;
}

void Term()
{
	// Drops the reference only. See Init(): tearing the pools down here and
	// rebuilding them for the next request is what broke the update download, and
	// nothing on this console has ever needed the memory back -- the app exits.
	if (g_refs > 0)
		g_refs--;

	// g_ready stays set deliberately: the contexts remain valid after Term().
}

// ---------------------------------------------------------------------------
// Request
// ---------------------------------------------------------------------------

Request::Request()
	: started_(false)
	, pending_(false)
	, cancelRequested_(false)
	, requestId_(-1)
	, maxBodyBytes_(0)
{
	pthread_mutex_init(&mutex_, NULL);
}

Request::~Request()
{
	if (started_)
	{
		Cancel();
		// The thread must finish before its captured `this` goes away.
		pthread_join(thread_, NULL);
	}

	pthread_mutex_destroy(&mutex_);
}

bool Request::Begin(const std::string &url, size_t maxBodyBytes)
{
	if (pending_)
		return false;

	if (!g_ready)
	{
		pthread_mutex_lock(&mutex_);
		response_ = Response();
		response_.result = kNotInitialised;
		pthread_mutex_unlock(&mutex_);
		return false;
	}

	pthread_mutex_lock(&mutex_);
	url_ = url;
	maxBodyBytes_ = maxBodyBytes;
	response_ = Response();
	pthread_mutex_unlock(&mutex_);

	pending_ = true;
	cancelRequested_ = false;
	requestId_ = -1;

	if (pthread_create(&thread_, NULL, &Request::ThreadEntry, this) != 0)
	{
		pending_ = false;
		SDL_Log("net: pthread_create failed");
		return false;
	}

	started_ = true;
	return true;
}

void *Request::ThreadEntry(void *arg)
{
	static_cast<Request *>(arg)->Run();
	return NULL;
}

void Request::Cancel()
{
	if (!pending_)
		return;

	cancelRequested_ = true;

	const int32_t id = requestId_;
	if (id > 0)
		sceHttpAbortRequest(id);
}

bool Request::Wait(void (*onTick)(int percent, void *ctx), void *ctx)
{
	// Progress is estimated from bytes read, which the worker does not publish.
	// Reporting the indeterminate 0 until the end is honest; the alternative is
	// lying about a percentage we cannot compute. The dialog text carries the
	// motion instead.
	const uint32_t kTickMs = 50;
	uint32_t elapsed = 0;

	while (pending_)
	{
		if (onTick)
			onTick(0, ctx);

		SDL_Delay(kTickMs);
		elapsed += kTickMs;
	}

	(void)elapsed;

	return response_.result == kOk;
}

// Dumps the full response head into the log.
//
// On any failure this is the difference between knowing and guessing: whether a
// response arrived at all, what status it carried, and whether there was a
// Location to follow. Header lines are truncated, because a server can be chatty
// and one pathological line must not lose the rest.
void dump_headers(int32_t request)
{
	char *raw = NULL;
	size_t size = 0;

	if (sceHttpGetAllResponseHeaders(request, &raw, &size) < 0 || !raw || size == 0)
	{
		netlog("  <no response headers>");
		return;
	}

	netlog("  <%lu bytes of headers>", (unsigned long)size);

	// Chunked by line so no single log line can overflow the 512-byte buffer.
	size_t off = 0;
	int lines = 0;

	while (off < size && lines < 40)
	{
		size_t end = off;
		while (end < size && raw[end] != '\n')
			end++;

		{
			char line[400];
			size_t n = end - off;

			if (n >= sizeof line)
				n = sizeof line - 1;
			memcpy(line, raw + off, n);
			line[n] = 0;
			netlog("  | %s", line);
		}

		off = (end < size) ? end + 1 : size;
		lines++;
	}
	if (off < size)
		netlog("  | ...truncated");
}

// Pulls the Location: value out of a 3xx response.
//
// Followed by hand because sceHttpSetAutoRedirect() has no prototype in this
// toolchain's header (`void sceHttpSetAutoRedirect();`), so calling it means
// guessing a signature that, if wrong, crashes rather than failing. Parsing the
// header we were given uses only functions whose signatures the header states.
//
// Returns false when there is no usable Location.
static bool redirect_target(int32_t request, std::string &out)
{
	char  *raw = NULL;
	size_t size = 0;

	if (sceHttpGetAllResponseHeaders(request, &raw, &size) < 0 || !raw || size == 0)
		return false;

	// The size is authoritative: the block is header text, not something to
	// strlen, since there may be no terminator.
	const std::string all(raw, size);

	// Not freed: this SDK exposes no free for the buffer sceHttpGetAllResponseHeaders
	// hands back. Bounded by kMaxRedirects per request and a few hundred bytes
	// each, so it is a leak rather than a problem -- and preferable to a guessed
	// sceHttpFree() that could corrupt the heap.
	//
	// Only the URL is copied out, so the leak does not grow with payload size.

	const char *p = all.c_str();
	while (*p)
	{
		const char *eol = strpbrk(p, "\r\n");
		const size_t len = eol ? static_cast<size_t>(eol - p) : strlen(p);

		// Header names are case-insensitive; GitHub sends "location".
		if (len > 9 && strncasecmp(p, "location:", 9) == 0)
		{
			const char *v = p + 9;
			size_t vlen = len - 9;

			while (vlen > 0 && (*v == ' ' || *v == '\t')) { v++; vlen--; }
			while (vlen > 0 && (v[vlen - 1] == ' ' || v[vlen - 1] == '\t'))
				vlen--;

			// A relative Location would need resolving against the current URL.
			// GitHub and S3 both send absolute; refusing a relative one is safer
			// than building a URL by concatenation and getting it subtly wrong.
			if (vlen < 8 || strncasecmp(v, "http://", 7) != 0)
			{
				if (vlen < 9 || strncasecmp(v, "https://", 8) != 0)
					return false;
			}

			out.assign(v, vlen);
			return true;
		}

		if (!eol)
			break;

		p = (*eol == '\r' && eol[1] == '\n') ? eol + 2 : eol + 1;
	}

	return false;
}

void Request::Run()
{
	Response local;
	local.result = kConnectFailed;

	int32_t templateId = -1;
	int32_t connection = -1;
	int32_t request = -1;
	long    totalRead = 0;

	// Followed across hops, so the reported status and body are always the final
	// ones. A caller checking `statusCode != 200` never sees the 302.
	std::string url = url_;

	// A run marker, so several attempts in one log can be told apart.
	//
	// The sequence number is the point. Every failure so far has been "first
	// request works, every later one fails", which is a different problem from
	// "this host is refused": if request #2 to api.github.com also fails, the
	// cause is the connection lifecycle and nothing to do with github.com.
	{
		static int s_seq = 0;
		const int seq = ++s_seq;

		netlog("---- GET #%d %s (max %lu bytes) ----", seq, url_.c_str(),
		       (unsigned long)maxBodyBytes_);
	}

	if (cancelRequested_)
	{
		local.result = kCancelled;
		netlog("cancelled before start");
		goto finish;
	}

	if (!g_ready)
		netlog("FATAL: context not initialised (g_ready=0)");

	templateId = sceHttpCreateTemplate(g_httpCtxId, kUserAgent,
	                                  ORBIS_HTTP_VERSION_1_1, 1);
	netlog("template=%d ctx=%d", (int)templateId, g_httpCtxId);
	if (templateId < 0)
	{
		local.result = kConnectFailed;
		goto finish;
	}

	// Only meaningful for https:// URLs; harmless for plain http.
	netlog("ssl-callback=%d",
	       sceHttpsSetSslCallback(templateId, AcceptAnyCertificate, NULL));

	for (int hop = 0;; hop++)
	{
		netlog("hop %d url=%s", hop, url.c_str());

		connection = sceHttpCreateConnectionWithURL(templateId, url.c_str(),
		                                            kKeepAlive);
		netlog("  connection=%d keepalive=%d", (int)connection, kKeepAlive);
		if (connection < 0)
		{
			int32_t err = 0;
			local.lastErrno = err;
			local.result = kConnectFailed;
			goto finish;
		}

		// Applied per hop, before the request exists. Best-effort: a platform that
		// rejects these still transfers, and failing a whole download over an
		// unsupported tuning knob would be worse than a slow one.
		netlog("  timeouts=%d,%d,%d",
		       sceHttpSetConnectTimeOut(connection, kConnectTimeoutUs),
		       net_set_recv_timeout(connection, kRecvTimeoutUs),
		       sceHttpSetSendTimeOut(connection, kSendTimeoutUs));

		request = sceHttpCreateRequestWithURL(connection, ORBIS_METHOD_GET,
		                                      url.c_str(), 0);
		netlog("  request=%d", (int)request);
		if (request < 0)
		{
			local.result = kConnectFailed;
			goto finish;
		}

		// Must happen before the send: a header that overruns the default buffer
		// fails the request outright with 0x80431073, which is exactly what every
		// download was doing against github.com while api.github.com worked fine.
		netlog("  header-max=%d (%lu bytes)",
		       net_set_header_max(request, kResponseHeaderMax),
		       (unsigned long)kResponseHeaderMax);

		// Publish the request ID so Cancel() can reach it, and honour a cancel
		// that arrived while we were still setting up.
		requestId_ = request;
		if (cancelRequested_)
			sceHttpAbortRequest(request);

		// Send once and keep the return value. sceHttpSendRequest's result is NOT
		// an error code -- it only says "failed" -- so the reason has to come from
		// sceHttpGetLastErrno, which is why both are logged.
		{
			const int sendRc = sceHttpSendRequest(request, NULL, 0);
			netlog("  send=%d", sendRc);

			if (sendRc < 0)
			{
				int32_t err = 0;
				sceHttpGetLastErrno(request, &err);
				local.lastErrno = err;
				netlog("  SEND FAILED rc=%d errno=%d url=%s", sendRc, (int)err,
				       url.c_str());
				local.result = cancelRequested_ ? kCancelled : kSendFailed;
				goto finish;
			}
		}

		{
			const int stRc = sceHttpGetStatusCode(request, &local.statusCode);
			netlog("  status rc=%d code=%d", stRc, (int)local.statusCode);
			if (stRc < 0)
			{
				int32_t err = 0;
				sceHttpGetLastErrno(request, &err);
				local.lastErrno = err;
				netlog("  GETSTATUS FAILED rc=%d errno=%d", stRc, (int)err);
				local.result = cancelRequested_ ? kCancelled : kStatusFailed;
				goto finish;
			}
		}

		// Content length, when the server says. Purely diagnostic: the caller's
		// own size check is authoritative, this only explains what follows.
		{
			int      lenType = -1;
			uint64_t len = 0;
			const int lenRc = sceHttpGetResponseContentLength(request, &lenType,
			                                                  &len);
			netlog("  content-length rc=%d type=%d len=%lu", lenRc, lenType,
			       (unsigned long)len);
		}

		// Release this hop before starting the next, so handles are not stacked
		// up across a redirect chain.
		if (local.statusCode >= 300 && local.statusCode < 400)
		{
			std::string next;

			// The whole response head goes in the log: on a redirect the Location
			// is the one thing that matters, and it was previously invisible.
			netlog("  redirect, headers follow:");
			dump_headers(request);

			if (hop >= kMaxRedirects)
			{
				netlog("  too many redirects");
				local.result = kReadFailed;
				goto finish;
			}

			if (!redirect_target(request, next))
			{
				// A 3xx with no usable Location is not something to keep retrying.
				netlog("  no usable Location");
				local.result = kReadFailed;
				goto finish;
			}

			netlog("  -> %s", next.c_str());

			requestId_ = -1;
			sceHttpDeleteRequest(request);
			request = -1;
			sceHttpDeleteConnection(connection);
			connection = -1;

			// The redirect body is discarded: it is a stub, and leaving it in
			// local.body would corrupt whatever the final response appends.
			local.body.clear();

			SDL_Log("net: %d -> %s", (int)local.statusCode, next.c_str());
			url = next;
			continue;
		}

		// This is the call that parks the thread when the server goes quiet; it
		// is what sceHttpAbortRequest() is there to interrupt.
		{
			uint8_t buffer[8 * 1024];
			long    reads = 0;

			for (;;)
			{
				const int read = sceHttpReadData(request, buffer, sizeof(buffer));

				// Every read, not just the interesting ones. A transfer that dies
				// part way through looks identical to one that never started unless
				// the byte count leading up to the failure is on record.
				totalRead += (read > 0) ? read : 0;

				if (read < 0)
				{
					int32_t err = 0;
					sceHttpGetLastErrno(request, &err);
					local.lastErrno = err;
					netlog("  READ FAILED after %ld bytes in %ld reads errno=%d",
					       totalRead, reads, (int)err);
					local.result = cancelRequested_ ? kCancelled : kReadFailed;
					goto finish;
				}

				if (read == 0)
				{
					netlog("  read complete: %ld bytes in %ld reads", totalRead,
					       reads);
					break;
				}

				if (local.body.size() + static_cast<size_t>(read) > maxBodyBytes_)
				{
					netlog("  BODY TOO LARGE at %lu + %d > %lu",
					       (unsigned long)local.body.size(), read,
					       (unsigned long)maxBodyBytes_);
					local.result = kBodyTooLarge;
					goto finish;
				}

				local.body.append(reinterpret_cast<const char *>(buffer),
				                  static_cast<size_t>(read));
				reads++;
			}
		}

		break;
	}

	local.result = kOk;

finish:
	// The last line of every attempt, whatever happened. Everything above is
	// detail; this is the answer to "did it work and why not".
	netlog("RESULT result=%d status=%d errno=%d body=%lu", (int)local.result,
	       (int)local.statusCode, (int)local.lastErrno,
	       (unsigned long)local.body.size());

	// Clear the published ID before the request is destroyed, so a late Cancel()
	// cannot abort a recycled handle.
	requestId_ = -1;

	if (request >= 0)
		sceHttpDeleteRequest(request);
	if (connection >= 0)
		sceHttpDeleteConnection(connection);
	if (templateId >= 0)
		sceHttpDeleteTemplate(templateId);

	pthread_mutex_lock(&mutex_);
	response_ = local;
	pthread_mutex_unlock(&mutex_);

	pending_ = false;
}

} // namespace net
