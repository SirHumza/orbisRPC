#include "dbglog.h"

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <SDL2/SDL.h>

namespace {

bool g_started = false;

// Opens for append, truncating on the first call of the run.
FILE *Open(bool truncate)
{
	if (truncate)
		return fopen(kLogPath, "w");

	return fopen(kLogPath, "a");
}

} // namespace

namespace dbglog
{

void Begin()
{
	FILE *f = Open(true);

	if (!f)
	{
		// Worth surfacing: if this fails, nothing below will be readable either.
		SDL_Log("dbglog: cannot open %s for writing: %s", kLogPath,
		        strerror(errno));
		return;
	}

	g_started = true;

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

	SDL_Log("%s", line);

	FILE *f = Open(false);
	if (!f)
		return;

	fprintf(f, "%s\n", line);

	// Closed rather than flushed so the line is definitely on disk even if the
	// process dies on the next instruction.
	fclose(f);
}

} // namespace dbglog