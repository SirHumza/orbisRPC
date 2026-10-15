/* supervise.h - fault isolation around the daemon body (payload ELF only).
 * A fault (SEGV/BUS/ILL/FPE) inside the gateway loop longjmps back here;
 * the supervisor runs the crash hook, backs off, and retries with clean
 * state instead of dying. Host-compilable: only the pure helpers and the
 * normal-return path are driven by tests, never a real fault. */
#ifndef ORBISRPC_SUPERVISE_H
#define ORBISRPC_SUPERVISE_H

/* Crash hook: post-fault cleanup. Returns 1 to retry the body, 0 to stop
 * (e.g. a stop was requested mid-fault). Must be one-shot and
 * non-recursive: a second fault inside the hook kills the process. */
typedef int (*supervise_hook_t)(void);
void supervise_set_crash_hook(supervise_hook_t hook);

/* Retry delay in seconds for consecutive-fault count n (0-based):
 * 5,10,20,40,80,160,320, then capped at 600. Pure, host-testable. */
int supervise_fault_delay(int nfaults);

/* Run body() under fault isolation. Returns body's code on clean return,
 * 0 when the hook declines a retry. */
int supervise_run(int (*body)(void));
#endif
