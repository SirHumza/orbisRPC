/* health.h - crash recovery: unclean-boot marker, counter, safe mode.
 *
 * Production semantics (v1.0.1+):
 *   boot:   health_boot_note_crash() checks for a leftover "dirty" marker.
 *           marker present  -> previous boot crashed/was killed -> counter++.
 *           marker absent   -> previous boot was clean -> counter unchanged.
 *           then writes the dirty marker for the CURRENT boot.
 *   stable: health_mark_healthy() clears the marker + resets the counter.
 *           call after HEALTH_STABLE_SECS of genuinely healthy runtime.
 *   exit:   health_mark_clean() clears both (graceful shutdown paths).
 *
 * This way three normal reboots can never trigger safe mode: each clean
 * shutdown clears the marker, so the next boot sees "clean".
 * Only consecutive UNCLEAN boots (crash before healthy) count. */
#ifndef ORBISRPC_HEALTH_H
#define ORBISRPC_HEALTH_H
#define CRASH_PATH "/data/orbisRPC/crash.count"
#define DIRTY_PATH "/data/orbisRPC/boot.dirty"
#define SAFE_THRESHOLD 3
/* Seconds of healthy runtime before a boot counts as clean. */
#define HEALTH_STABLE_SECS 60
/* Call at clean shutdown to reset the crash counter. */
void health_mark_clean(void);
/* Call once the daemon is stably healthy (presence flowing or uptime
 * past HEALTH_STABLE_SECS). Clears the dirty marker + resets counter. */
void health_mark_healthy(void);
/* Call at boot. Returns 1 when safe mode must engage (ORX-BOOT-003),
 * 0 for normal boot. Increments the persisted crash counter ONLY when
 * the previous boot left a dirty marker (i.e. crashed before healthy). */
int health_boot_note_crash(void);
/* Verify a staged target can run: ELF magic + size sane. 1 ok, 0 bad. */
int health_check_binary(const char *path);
/* Restore <path> from <path>.bak. 0 ok, -1 failed/none. */
int health_rollback(const char *path);
/* Verify + activate a staged "<path>.new" over <path> with backup.
 * Returns 0 activated, -1 refused (missing/invalid; .new removed,
 * live <path> untouched). Host-testable rollback primitive used by
 * the updater so staging can never leave a corrupt live binary. */
int health_stage_activate(const char *path);
/* Post-boot watchdog: if <path> fails health_check_binary, attempt
 * health_rollback(). Returns 0 healthy, 1 rolled back OK, -1 still bad.
 * Daemon calls this at startup for each staged target so a bad update
 * that passed staging is automatically reverted before use. */
int health_verify_or_rollback(const char *path);
#ifdef HEALTH_TESTABLE
/* Override the base dir for crash.count/boot.dirty (host tests). */
void health_set_base(const char *dir);
#endif
#endif
