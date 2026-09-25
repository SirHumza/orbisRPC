/* timesync.h - SNTP wall-clock correction for Discord timestamps.
 *
 * Jailbroken consoles commonly block Sony servers (ban avoidance), which
 * also kills PSN time sync — the PS4 clock then drifts (observed: +38min
 * and growing, plus wrong timezones). Discord timestamps must be true
 * UTC or the timer wedges at 0:00, so the daemon syncs itself against
 * public NTP (no Sony involved) and stamps sessions with corrected time.
 * Failure degrades to the local clock (old behavior), never fatal. */
#ifndef ORBISRPC_TIMESYNC_H
#define ORBISRPC_TIMESYNC_H
#include <stdint.h>
/* Try an SNTP exchange; on success stores offset and returns 0. -1 = keep
 * old offset (first boot without sync behaves like before). */
int time_sync(void);
/* Boot sweep: try all hosts (bounded, once per boot, before first
 * connect — never in the live loop). */
int time_sync_all(void);
/* Corrected wall clock for Discord epoch stamps. */
int64_t time_fixed(void);
#endif
