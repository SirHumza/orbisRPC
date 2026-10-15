/* dbglog.h - diagnostics that are actually readable.
 *
 * SDL_Log is not usable for this on a console. It ends up in libc's printf /
 * fprintf, and there is no terminal attached to a pkg running from the PS4's
 * dashboard, so the output goes nowhere and cannot be read back.
 *
 * So diagnostics go to a file instead. The file is on the same writable volume as
 * settings.json, which is already known to exist and to be reachable over FTP.
 *
 * Every line is flushed and closed as it is written. That costs a few
 * microseconds at startup and is the whole point: if the app crashes a few
 * instructions later, the lines that explain why are already on disk.
 */
#ifndef INSTALLER_DBGLOG_H
#define INSTALLER_DBGLOG_H

// Where the log lands. /data is writable from a pkg; /app0 is not.
#define kLogPath "/data/orbispkg_test/menu.log"

namespace dbglog
{

// Truncates the log and records that we are running, with the current process
// id. Call once, early.
void Begin();

// Appends one line. Also echoed to SDL_Log, which costs nothing and helps if the
// log file is ever unreachable.
void Write(const char *fmt, ...);

} // namespace dbglog

#endif