#include "dbglog.h"

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

namespace {

const char *g_log_path = kLogPath;

FILE *Open(bool truncate, const char *path)
{
	if (truncate)
		return fopen(path, "w");

	return fopen(path, "a");
}

} // namespace

namespace dbglog
{

void Begin()
{
	FILE *f = Open(true, kLogPath);

	if (!f)
	{
		f = Open(true, kFallbackLogPath);
		if (!f)
		{
			fprintf(stderr, "dbglog: cannot open %s or %s: %s\n",
			        kLogPath, kFallbackLogPath, strerror(errno));
			return;
		}
		g_log_path = kFallbackLogPath;
	}

	fprintf(f, "---- orbisRPC menu log, pid %d ----\n", (int)getpid());
	fclose(f);
}

void Write(const char *fmt, ...)
{
	char line[512];

	va_list ap;
	va_start(ap, fmt);
	vsnprintf(line, sizeof line, fmt, ap);
	va_end(ap);

	FILE *f = Open(false, g_log_path);
	if (!f)
		return;

	fprintf(f, "%s\n", line);

	// Closed rather than flushed so the line is definitely on disk even if the
	// process dies on the next instruction.
	fclose(f);
}

} // namespace dbglog
