/* nanodns.h - reading and editing /data/nanodns/nanodns.ini.
 *
 * The install flow offers to add a Sony TMDB exception when nanoDNS is present,
 * because that is the only place the host needs to know about it -- everything
 * else works without the exception.
 *
 * nanoDNS's config has two line shapes and this deals with both, because one file
 * has both:
 *
 *   [overrides]   *.playstation.net.*=0.0.0.0    key = value
 *   [exceptions]  tmdb.np.dl.playstation.net      bare mask, one per line
 *
 * The exceptions section is the bare one, which is why there is no value
 * parameter below. Passing one would mean writing "host = something" into a
 * section that does not read it that way.
 *
 * Editing is idempotent by construction: a line that is already there is left
 * alone rather than appended twice, so re-running the installer cannot grow the
 * file without bound or end up with the same line several times over.
 *
 * Pure string and file handling with no PS4 headers, so it is host-tested.
 */
#ifndef INSTALLER_NANODNS_H
#define INSTALLER_NANODNS_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NANODNS_INI "/data/nanodns/nanodns.ini"
#define NANODNS_EXCEPTIONS_SECTION "exceptions"

/* ---- generic INI editing ---------------------------------------------
 * The parser behind the nanoDNS helpers below, and behind GoldHEN's
 * payloads.ini too -- same file format, same job, no reason to have two. The
 * section is a parameter so one implementation serves both.
 *
 * All take an explicit path rather than using a macro internally. That is what
 * makes them host-testable: these files live under /data, which a host test
 * cannot write, so the test points the same code at a temp file instead of the
 * real one being hardcoded past it. */

/* 1 when the file exists and opens for reading. */
int ini_present_at(const char *path);

/* 1 when [section] already holds `line` on its own, ignoring case and surrounding
 * whitespace. Comments and section headers never count as a match. 0 when absent,
 * or the section is missing. */
int ini_has_line_at(const char *path, const char *section, const char *line);

/* Adds "key = value" under [section], creating the section at the end of the file
 * if it is not already there.
 *
 *   1  added
 *   0  already present, nothing written
 *  -1  the file could not be read or written
 *
 * Idempotent, and writes to a temporary beside the file and renames, so a failed
 * edit cannot leave the console with a half-written config. */
int ini_add_entry_at(const char *path, const char *section, const char *key,
                     const char *value);

/* Adds `line` on its own under [section], creating the section at the end of the
 * file if it is not already there. For sections whose entries carry no value,
 * which is what nanoDNS's [exceptions] is.
 *
 *   1  added
 *   0  already present, nothing written
 *  -1  the file could not be read or written, or `line` was not one line
 *
 * Idempotent, and writes to a temporary beside the file and renames. */
int ini_add_line_at(const char *path, const char *section, const char *line);

/* Removes the line matching `key` under [section], everything else preserved byte
 * for byte -- including comments, padding and blank lines.
 *
 *   1  removed
 *   0  not there, and the file was not written to at all
 *  -1  the file could not be read or written
 *
 * Only ever used on a file this app shares with something else, so it writes
 * through a temporary and renames, and refuses to rewrite anything it could not
 * confirm it had changed. */
int ini_remove_entry_at(const char *path, const char *section, const char *key);

/* As above, matching the whole line rather than a key. For bare-entry sections. */
int ini_remove_line_at(const char *path, const char *section, const char *line);

/* ---- nanoDNS ---------------------------------------------------------
 * Thin wrappers fixing the path and the section name. */
int nanodns_present_at(const char *path);
int nanodns_has_exception_at(const char *path, const char *mask);
int nanodns_add_exception_at(const char *path, const char *mask);

/* 1 when the ini exists and opens for reading. */
int nanodns_present(void);

/* 1 when [exceptions] already holds `mask` on its own line, matched on the whole
 * line ignoring case. A commented-out mask does not count -- it is not in effect.
 * 0 when absent or the section is missing. */
int nanodns_has_exception(const char *mask);

/* Adds `mask` as its own line under [exceptions], creating the section at the end
 * of the file if it is not already there.
 *
 *   1  added
 *   0  already present, nothing written
 *  -1  the ini could not be read or written
 *
 * Writes to a temporary beside the file and renames, so a failed edit cannot
 * leave nanoDNS with a truncated config -- which for a DNS daemon means a
 * console with no name resolution. */
int nanodns_add_exception(const char *mask);

/* Takes `mask` back out of [exceptions], preserving everything else in the file
 * byte for byte.
 *
 *   1  removed
 *   0  not there, and the file was not written to at all
 *  -1  the ini could not be read or written
 *
 * Used by uninstall and by the Settings page's nano DNS row, which offers to undo
 * what the install added. Same file as above and the same section, so the two
 * cannot end up addressing different lines. */
int nanodns_remove_exception(const char *mask);

#ifdef __cplusplus
}
#endif

#endif
