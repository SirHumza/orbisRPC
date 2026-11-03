/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 SirHumza and CyberMask367 - see NOTICE for attribution terms.
 */
/* lock.h - single-instance pid lockfile. */
#ifndef ORBISRPC_LOCK_H
#define ORBISRPC_LOCK_H
#include <stdint.h>

#define LOCK_PATH "/data/orbisRPC/daemon.lock"
/* The daemon touches the lock this often, and a lock that has not been
 * touched for LOCK_STALE_S is treated as abandoned even when its pid still
 * appears in the process table: a single-threaded payload that is wedged
 * inside a call cannot make progress, so a fresh injection must be able to
 * recover without a reboot. The old instance notices the takeover on its
 * next touch and exits, so a reclaim cannot leave two daemons running. */
#define LOCK_HEARTBEAT_S 60
#define LOCK_STALE_S     600

/* A lock that predates this boot is left over from a previous run: the pid
 * in it may have been recycled by an unrelated process, so liveness alone is
 * not evidence that a daemon still holds it (D-04: "another daemon holds the
 * lock" on every boot). Returns 1 when the lock must be reclaimed.
 *
 * Pure so host tests cover the decision without a console process table.
 * `lock_mtime`/`boot_wall` are epoch seconds; 0 means unknown. */
static inline int lock_entry_stale(int64_t lock_mtime, int64_t boot_wall,
                                   int pid, int pid_live){
    if(pid <= 0) return 1;                     /* empty/garbage lock */
    if(lock_mtime > 0 && boot_wall > 0 && lock_mtime < boot_wall - 5)
        return 1;                              /* from a previous boot */
    return pid_live ? 0 : 1;
}

/* The holder is a live pid but has stopped refreshing the lock: the daemon's
 * only thread is wedged, not busy. A backwards clock (negative age) is never
 * stale, and an unknown mtime proves nothing. */
static inline int lock_heartbeat_stale(int64_t lock_mtime, int64_t now,
                                       int64_t stale_s){
    if(lock_mtime <= 0 || now <= 0 || stale_s < 1) return 0;
    return now - lock_mtime > stale_s;
}

/* Token-aware reclaim decision. Payload Guest runs every load with the same
 * pid on this console, so pid equality cannot tell "my own stale lock" from
 * "another live instance". Each instance writes a random token; a fresh lock
 * with a different token is a live peer and must win, otherwise a second
 * load reclaims the lock and two daemons share one heap. Returns 1 to
 * reclaim, 0 to stand down. Legacy pid-only locks keep the liveness rule. */
static inline int lock_entry_reclaim(int64_t lock_mtime, int64_t boot_wall,
                                     int64_t now, int64_t stale_s,
                                     int has_token,
                                     uint64_t lock_token, uint64_t my_token,
                                     int legacy_live){
    if(lock_mtime > 0 && boot_wall > 0 && lock_mtime < boot_wall - 5)
        return 1;                              /* from a previous boot */
    if(has_token && lock_token != 0 && my_token != 0){
        if(lock_token == my_token) return 0;   /* this instance's own lock */
        return lock_heartbeat_stale(lock_mtime, now, stale_s);
    }
    if(!legacy_live) return 1;
    return lock_heartbeat_stale(lock_mtime, now, stale_s);
}

/* Does the parsed lock entry belong to this instance? `parsed` is the number
 * of fields fscanf("%d %llx") consumed (1 = legacy pid-only lock). Pid alone
 * is not identity on this console: every Payload Guest load reports the same
 * pid, so a token mismatch means another instance took the lock over. */
static inline int lock_token_owned(int parsed, int pid, uint64_t lock_token,
                                   int my_pid, uint64_t my_token){
    if(parsed < 1 || pid != my_pid) return 0;
    if(parsed >= 2 && lock_token != 0 && my_token != 0)
        return lock_token == my_token;
    return 1;                                  /* legacy pid-only lock */
}

/* Override LOCK_STALE_S from config (clamped by cfg.c); call before
 * lock_acquire(). */
void lock_set_stale(int seconds);
/* 0 acquired, 1 live peer holds it (stand down), -1 error/retry. */
int lock_acquire(void);
void lock_release(void);
/* Refresh the lock mtime while the daemon is making progress. No-op when
 * this process does not hold the lock. */
void lock_refresh(void);
/* 1 while this process still owns the lock (or ownership cannot be
 * verified), 0 when another pid has taken it over. */
int lock_owned(void);
#endif
