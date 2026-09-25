/* lock.h - single-instance pid lockfile. */
#ifndef ORBISRPC_LOCK_H
#define ORBISRPC_LOCK_H
#define LOCK_PATH "/data/orbisRPC/daemon.lock"
/* 0 acquired, 1 live peer holds it (stand down), -1 error/retry. */
int lock_acquire(void);
void lock_release(void);
#endif
