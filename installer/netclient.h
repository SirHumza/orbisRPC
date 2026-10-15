#ifndef PS4UTIL_NETCLIENT_H
#define PS4UTIL_NETCLIENT_H

#include <pthread.h>
#include <stdint.h>

#include <string>

// A minimal HTTP(S) GET built on SceNet / SceSsl / SceHttp.
//
// The request runs on a worker thread so the render loop never blocks. There is
// no non-blocking state poll in SceHttp, so cancelling relies on
// sceHttpAbortRequest() interrupting the worker while it waits on
// sceHttpReadData().
namespace net
{

enum Result
{
	kOk = 0,
	kNotInitialised,
	kCancelled,
	kConnectFailed,
	kSendFailed,
	// sceHttpSendRequest succeeded but the status could not be read back. Separate
	// from kSendFailed because they mean different things -- a send failure is a
	// transport problem, this one means the response never arrived -- and the
	// update log collapsed them into the same "request failed".
	kStatusFailed,
	kReadFailed,
	kBodyTooLarge,
};

struct Response
{
	Result      result;
	int32_t     statusCode; // 0 when the request never produced a response
	// The transport's own error code for a failed request, from
	// sceHttpGetLastErrno. Distinguishes a TLS failure from a DNS one from a
	// refused connection -- all of which otherwise report the same thing: a
	// negative return from sceHttpSendRequest with no status and no errno from
	// the C library, which is why "request failed (status 0)" said nothing.
	int32_t     lastErrno;
	std::string body;

	Response() : result(kNotInitialised), statusCode(0), lastErrno(0) {}
};

const char *ResultText(Result result);

// Loads the net/ssl/http sysmodules and creates the shared pools.
bool Init();
void Term();

// A single in-flight GET.
//
// Usage: Begin(), then poll Pending() once per frame until it goes false, then
// read Result(). Pending() stays true after Cancel() until the worker has really
// exited, so do not Begin() another request until it clears.
class Request
{
public:
	Request();
	~Request();

	// Returns false if the worker could not be started. On success Pending()
	// becomes true.
	bool Begin(const std::string &url, size_t maxBodyBytes = 64 * 1024);

	bool Pending() const { return pending_; }

	// Asks the worker to abort. If the abort does not reach the blocking read
	// the thread stays parked until the server answers; the app keeps running
	// either way.
	void Cancel();

	// Only meaningful once Pending() has gone false.
	const Response &Result() const { return response_; }

	// Blocks until the request finishes, then returns true if it succeeded.
	//
	// `onTick`, when given, is called roughly every 50ms with a 0..100 progress
	// estimate, so a caller with a modal progress dialog can pump it. Pass NULL for
	// a plain blocking wait.
	//
	// The work still happens on the worker thread; this only parks the caller, so
	// the network is never blocked. There is no timeout -- a request that never
	// completes parks here forever, which is why Cancel() exists and why the
	// caller should hold a Request it is willing to abandon.
	bool Wait(void (*onTick)(int percent, void *ctx), void *ctx);

private:
	static void *ThreadEntry(void *arg);
	void Run();

	pthread_t         thread_;
	bool              started_;
	volatile bool     pending_;
	volatile bool     cancelRequested_;
	volatile int32_t  requestId_; // published by the worker, read by Cancel()
	pthread_mutex_t   mutex_;
	std::string       url_;
	size_t            maxBodyBytes_;
	Response          response_;
};

} // namespace net

#endif
