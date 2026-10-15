/* daemon.h - orbisRPC daemon loop (payload ELF only). */
#ifndef DAEMON_H
#define DAEMON_H
#include <stdint.h>
/* Returns 0 on clean stop, 1 on missing/invalid configuration, or 2 when
 * Discord rejects the token with close code 4004. */
int daemon_run(void);
void daemon_request_stop(void);
void daemon_clear_stop(void);
int daemon_stop_requested(void);

/* Presence machine states (moved up from daemon.c so the session struct
 * below can use them). */
typedef enum { PS_NONE = 0, PS_HOME, PS_GAME } pres_state_t;

/* All session/detection state for one daemon_run. Lives in a struct (not
 * function statics) so a supervisor retry starts clean instead of
 * inheriting the faulted run's leftovers: unrestored sessions, stale
 * debounce counters, a presence machine convinced a game is still up. */
typedef struct {
    int active;
    pres_state_t pres;
    char last[128];
    char sess_tid[16];
    int64_t started;
    int64_t last_tsync;
    int home_posted;
    char prev_tid[16];
    int64_t prev_started, prev_end;
    char cand_title[16];
    int cand_hits, miss_hits;
    int healthy_marked;
    int sess_restored;
    int64_t last_reconcile;
} daemon_sess_t;

/* Zero a session struct to clean defaults. Implemented in supervise.c
 * (not daemon.c) so host tests can link and drive it without pulling in
 * the whole daemon. */
void daemon_sess_reset(daemon_sess_t *s);

/* Crash-cleanup hook for the supervisor: releases the pid lock first,
 * then tears down the live connection through a one-shot gate.
 * Returns 1 to retry the daemon body, 0 when a stop was requested. */
int daemon_crash_cleanup(void);
#endif