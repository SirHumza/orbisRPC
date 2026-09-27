/* fw.h - firmware detection + per-FW kinfo_proc layout table.
 * Ported pattern from OSM-Made/PS4-Kernel-SDK (detect FW, resolve per-FW
 * data at runtime) adapted to usermode: we never touch the kernel, we
 * only need the sysctl KERN_PROC record layout, which moves between
 * firmwares. Unknown firmwares fall back to a bounded in-record scan
 * instead of a hardcoded offset, so a new FW degrades, never crashes. */
#ifndef ORBISRPC_FW_H
#define ORBISRPC_FW_H

#include <stddef.h>

/* Firmware version as "MAJOR.MINOR" (e.g. "9.00", "13.52"). Never fails:
 * unknown/unparseable platforms yield "?.??". */
void fw_version(char *out, size_t cap);

/* Pure table lookup for a given version string (testable).
 * Returns 0 known, -1 unknown. */
int fw_kinfo_for(const char *ver, int *name_off, int *min_rec);

/* Fill name_off and min_rec with the kinfo_proc offsets for this box.
 * Returns 0 when the FW is in the verified table, -1 when unknown
 * (caller must use the bounded scan instead of assuming). */
int fw_kinfo(int *name_off, int *min_rec);

/* Match an "eboot.bin" process name inside one sysctl record.
 * Tries the table offset first, then a bounded scan of the record.
 * Returns 1 match, 0 no match. Never reads past rec+recsz. */
int fw_match_eboot(const unsigned char *rec, int recsz);

/* Match a ("Payload", pid) pair for lock-holder liveness.
 * pid_off is verified per-FW the same way. Returns 1 live peer. */
int fw_match_payload_pid(const unsigned char *rec, int recsz, int pid);

#endif
