/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 SirHumza and CyberMask367 - see NOTICE for attribution terms.
 */
/* sched.h - pure time-based decisions shared by the daemon and host tests.
 *
 * Header-only and free of PS4 calls so the host harness exercises exactly
 * the predicates the payload runs on console. */
#ifndef ORBISRPC_SCHED_H
#define ORBISRPC_SCHED_H
#include <stdint.h>

/* Detection cadence gate. The expensive probes (kern.msgbuf ring read,
 * process walk, app.db scan) must not run every second; poll_interval_s is
 * the documented cadence. *last == 0 forces a poll on the first tick after a
 * (re)connect, so presence is re-posted promptly instead of waiting a full
 * interval. */
static inline int sched_poll_due(int64_t now, int64_t *last, int interval_s){
    if(interval_s < 1) interval_s = 1;
    if(*last == 0 || now < *last || now - *last >= interval_s){
        *last = now;
        return 1;
    }
    return 0;
}

/* Consecutive samples a transition must show before it is accepted. At the
 * ~1s cadence two samples hide shell flicker during a launch; at the legal
 * 5s+ cadences one sample already spans several seconds, and demanding two
 * would double the user-visible latency. */
static inline int sched_transition_hits(int interval_s){
    return interval_s <= 2 ? 2 : 1;
}

/* Retry gate for a best-effort lookup that does not heal on its own (a URL
 * Discord has no mapping for). One attempt per window, and a backwards clock
 * jump retries immediately instead of stalling. */
static inline int sched_retry_due(int64_t now, int64_t *last, int window_s){
    if(window_s < 1) window_s = 1;
    if(*last == 0 || now < *last || now - *last >= window_s){
        *last = now;
        return 1;
    }
    return 0;
}

/* Idle suppression vs the post-close hold. On the home screen nothing runs
 * underneath, so with show_idle off a closed game must be cleared at once,
 * not held for presence_hold_s. A failed/uncertain detection is different:
 * the game may still be live, so it wins and the presence is held. */
static inline int sched_clear_idle_now(int gate_idle, int active,
                                       int scan_unknown){
    return gate_idle && active && !scan_unknown;
}

/* Home presence is posted only when the operator allows idle presence and
 * nothing is already on the wire or being held. With show_idle off the
 * daemon must stay silent on home from a fresh boot too, not just clear a
 * game that just closed. */
static inline int sched_home_due(int gate_idle, int home_posted,
                                 int holding, int scan_unknown){
    return !gate_idle && !home_posted && !holding && !scan_unknown;
}

/* Rest Mode / suspend detection (D-03).
 *
 * A suspended process stops running while wall time keeps advancing. On PS4
 * the monotonic clock used for durations does not count suspend; on hosts it
 * may or may not. So the signal is the DRIFT between wall and monotonic:
 * during normal runtime the two advance together (drift ~0), across a
 * suspend the wall clock runs ahead by the sleep duration. A backwards wall
 * jump or a negative drift is a clock correction, not a suspend, and returns
 * 0.
 *
 * `threshold_s` keeps ordinary scheduling jitter and NTP slews out: anything
 * below it is not a sleep. Returns the sleep duration in seconds, or 0. */
static inline int64_t sched_suspend_drift(int64_t wall_delta,
                                          int64_t mono_delta,
                                          int threshold_s){
    if(wall_delta <= 0 || mono_delta < 0) return 0;
    int64_t drift = wall_delta - mono_delta;
    if(drift < threshold_s) return 0;
    return drift;
}

/* Timer rebase after a suspend: the game was not playable while the console
 * slept, so shift the start epoch forward by the sleep duration. Elapsed
 * playtime is preserved and the sleep hours are excluded. A start of 0
 * (home / settings / no timer) stays 0. */
static inline int64_t sched_rebase_start(int64_t started, int64_t sleep_s){
    if(started <= 0 || sleep_s <= 0) return started;
    return started + sleep_s;
}

/* A gap between 1 s loop ticks far beyond the cadence means the only thread
 * was parked inside a call that ignored its deadline (scheduler starvation,
 * a filesystem stall, a resolver). Console sleep is a different signal
 * (wall-vs-monotonic drift) and must not be double-reported: callers pass
 * gap 0 for a tick that already classified the gap as suspend. The caller
 * re-arms presence and logs the size, so a residual blocker leaves evidence
 * instead of silently freezing the profile. */
static inline int sched_loop_stall(int64_t gap_s, int64_t threshold_s){
    if(threshold_s < 1) threshold_s = 1;
    return gap_s > threshold_s;
}
#endif
