/* clock.h - monotonic time for measuring durations.
 *
 * Rule: wall clock (time(NULL)) is ONLY for values that must be wall
 * clock (Discord epoch timestamps, log stamps). Every duration/deadline
 * uses orbis_mono_s() so a clock jump can never break local timing.
 *
 * PS4: sceKernelGetProcessTime (microseconds, monotonic).
 * Host: clock_gettime(CLOCK_MONOTONIC). */
#ifndef ORBISRPC_CLOCK_H
#define ORBISRPC_CLOCK_H
#include <stdint.h>
/* Monotonic seconds (arbitrary epoch, never goes backward). */
int64_t orbis_mono_s(void);
/* Step the wall clock to the given Unix time (NTP self-heal; the only
 * sanctioned wall-clock write). 0 = ok, -1 = failed. */
int compat_set_clock(int64_t unix_time);
#endif
