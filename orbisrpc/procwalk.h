/* procwalk.h - firmware-proof process-table record scanning.
 *
 * sysctl KERN_PROC_ALL (mib {1,14,8,0}) hands back a buffer of
 * variable-length struct kinfo_proc records. Each record leads with
 * ki_structsize, its own byte length, so the *framing* is self-describing
 * and survives firmware changes. The field offsets *inside* a record are not
 * self-describing and do drift between firmware versions: orbisRPC used to
 * hard-code "eboot.bin" at byte 447 and reject any record under 479 bytes,
 * which was 9.00-specific. On any firmware where that layout differs the scan
 * reported "no game running" forever and the daemon posted nothing.
 *
 * So: keep ki_structsize framing, but locate the process name by searching
 * inside the record instead of trusting an offset.
 *
 * Pure C99 over <string.h> only -- no PS4 headers, no sysctl, no libc beyond
 * memcpy/memcmp/strlen. detect.c itself cannot be host-compiled (it pulls in
 * dlfcn.h and orbis/), so the parsing lives here where the unit tests can
 * reach it.
 */
#ifndef PROCWALK_H
#define PROCWALK_H

#include <stddef.h>
#include <stdint.h>

/* The comm we care about: a launched game runs as eboot.bin. */
#define PROCWALK_EBOOT "eboot.bin"

/* Count records whose NUL-terminated name field equals `needle`.
 * Returns the count, or -1 when the buffer is malformed: a record with a
 * non-positive size, or one claiming more bytes than the buffer holds. A
 * truncated final record is indistinguishable from corruption, so it also
 * reports -1 rather than a partial count — callers read 0 as "no game
 * running" and would blank an active presence, so a guess is worse than
 * "unknown". */
int procwalk_count_comm(const unsigned char *buf, size_t sz, const char *needle);

/* 1 if any record matches, 0 if none, -1 if malformed. */
int procwalk_has_comm(const unsigned char *buf, size_t sz, const char *needle);

/* ki_structsize of the first record, or 0 when the buffer is empty or the
 * first record is malformed. Lets the daemon log what the running firmware
 * actually reports instead of guessing and staying silent. */
size_t procwalk_first_structsize(const unsigned char *buf, size_t sz);

/* Locate the first record whose NUL-terminated name equals `needle`.
 * Returns 0 and sets rec/recsz to that record's extent, -2 when no record
 * matches, -1 when the buffer is malformed. Both outputs are cleared first.
 * Splitting "which record" out of "does it match" is what lets callers narrow
 * a record by its unambiguous process name before looking for fields whose
 * offset is not knowable. */
int procwalk_find_comm(const unsigned char *buf, size_t sz, const char *needle,
                       const unsigned char **rec, size_t *recsz);

/* Does this record contain `want` as a 32-bit integer anywhere inside it?
 *
 * For fields like the pid there is no self-describing marker, so the only
 * firmware-proof option is to search the record. Always narrow the record
 * with procwalk_find_comm() first: a hit inside a record already known to be
 * the right process is meaningful, whereas a hit anywhere in the table would
 * be noise.
 *
 * A hit is corroboration, not proof. Records are zero-padded and a process
 * name ends in a NUL, so a record routinely contains the int32 0 in its
 * padding -- callers MUST reject pid <= 0 before asking.
 *
 * Bias note: callers should treat a miss as "probably stale" rather than
 * certain. For the single-instance lock a false positive merely causes an
 * unnecessary stand-down, while a false negative lets a second daemon run
 * alongside the first, so erring toward matching is the safe direction. */
int procwalk_record_has_i32(const unsigned char *rec, size_t recsz, int want);

#endif /* PROCWALK_H */