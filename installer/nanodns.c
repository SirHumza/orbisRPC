/* nanodns.c - reading and editing nanoDNS's ini.
 *
 * Deliberately a minimal INI reader rather than a general one: nanoDNS's config
 * is flat sections of "key = value" lines, and handling only that means no
 * quoting rules, no line continuations, and no nesting to get wrong. An
 * unrecognised line is preserved verbatim on rewrite rather than parsed, so
 * editing the exceptions never silently drops something this code did not
 * understand.
 */

#include "nanodns.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <unistd.h>

/* Big enough for a nanoDNS ini. Bounded so a corrupt file cannot make the editor
 * allocate without limit. */
#define INI_MAX (256u * 1024u)

#define LINE_MAX 1024

/* Strips leading and trailing spaces and tabs, in place. Returns the start of the
 * trimmed string, which is inside the buffer. */
static char *trim(char *s)
{
	char *end;

	while (*s == ' ' || *s == '\t')
		s++;

	end = s + strlen(s);
	while (end > s && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r'))
		end--;
	*end = 0;

	return s;
}

/* Copies the next line of `*cursor` into `buf`, terminating it, and advances the
 * cursor past the newline. Returns 0 at end of input. Newlines are stripped,
 * which is what makes the rewrite in add_exception_at able to append cleanly. */
static int next_line(char **cursor, char *buf, size_t cap)
{
	char *p = *cursor;
	char *nl;
	size_t n;

	if (!p || !*p)
		return 0;

	nl = strchr(p, '\n');
	n = nl ? (size_t)(nl - p) : strlen(p);

	if (n >= cap)
		n = cap - 1;
	memcpy(buf, p, n);
	buf[n] = 0;

	*cursor = nl ? nl + 1 : p + strlen(p);
	return 1;
}

/* Reads the whole file into a malloc'd NUL-terminated buffer. Caller frees.
 * NULL when missing, unreadable, or larger than INI_MAX. */
static char *read_all(const char *path)
{
	FILE *f = fopen(path, "rb");
	long sz;
	char *buf;

	if (!f)
		return NULL;

	if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
	sz = ftell(f);
	if (sz <= 0 || sz > (long)INI_MAX) { fclose(f); return NULL; }
	if (fseek(f, 0, SEEK_SET) != 0) { fclose(f); return NULL; }

	buf = (char *)malloc((size_t)sz + 1);
	if (!buf) { fclose(f); return NULL; }

	if (fread(buf, 1, (size_t)sz, f) != (size_t)sz)
	{
		free(buf);
		fclose(f);
		return NULL;
	}

	fclose(f);
	buf[sz] = 0;
	return buf;
}

/* Case-insensitive match of a section header's name. */
static int section_is(const char *line, const char *name)
{
	const char *p = line;

	if (*p != '[')
		return 0;

	p++;
	while (*p == ' ' || *p == '\t')
		p++;

	if (strncasecmp(p, name, strlen(name)) != 0)
		return 0;

	p += strlen(name);
	while (*p == ' ' || *p == '\t')
		p++;

	return *p == ']';
}

/* True when the line is "key = value" or "key: value" rather than a comment or a
 * section header. */
static int is_entry(const char *line)
{
	const char *p = line;

	if (*p == ';' || *p == '#' || *p == '[' || *p == 0)
		return 0;

	while (*p && *p != '=' && *p != ':')
		p++;

	return *p == '=' || *p == ':';
}

/* Compares a line's key against `key`, ignoring case and surrounding whitespace.
 * The line may already have been trimmed. */
static int entry_matches(const char *line, const char *key)
{
	char kbuf[LINE_MAX];
	const char *sep;
	size_t n;

	if (!is_entry(line))
		return 0;

	sep = strpbrk(line, "=:");
	n = (size_t)(sep - line);

	if (n >= sizeof kbuf)
		n = sizeof kbuf - 1;
	memcpy(kbuf, line, n);
	kbuf[n] = 0;

	return strcasecmp(trim(kbuf), key) == 0;
}

/* ---- generic INI editing ---------------------------------------------
 * The section is a parameter rather than fixed to "exceptions" because a second
 * consumer turned up with the identical file format and no reason to have two
 * parsers: GoldHEN's payloads.ini, whose [AutoRun] section lists payload paths
 * with a = 1 flag.
 *
 * Two line shapes, because nanoDNS's own config has both:
 *
 *   [overrides]   *.playstation.net.*=0.0.0.0     key = value
 *   [exceptions]  tmdb.np.dl.playstation.net       bare, one mask per line
 *
 * These are not interchangeable. Writing "host = 1" into an exceptions section
 * produces a line nanoDNS does not recognise as a mask and silently ignores,
 * while the presence check keeps failing to see it and appends another copy on
 * every run -- a file that grows a little further each time the installer is
 * used, with nothing on screen to suggest why. Separate paths, shared machinery.
 */

/* What a presence check compares against. */
enum { MATCH_LINE, MATCH_KEY };

/* Whole-line match, ignoring case. Used for bare entries, where the line *is*
 * the value. Case is ignored because DNS names are case-insensitive and the
 * masks inherit that.
 *
 * Comments and section headers never match, whatever they contain. That is what
 * stops a commented-out "# auth.api.playstation.net" counting as present --
 * correct, since a commented mask is not in effect and the live one is wanted. */
static int line_matches(const char *line, const char *text)
{
	if (line[0] == ';' || line[0] == '#' || line[0] == '[' || line[0] == 0)
		return 0;

	return strcasecmp(line, text) == 0;
}

/* Walks [section] looking for `needle`, matched as a whole line or as a key
 * depending on `mode`. Shared by both shapes, so the section-walking rules --
 * which are where the subtle mistakes live -- exist exactly once. */
static int ini_find_at(const char *path, const char *section, const char *needle,
                       int mode)
{
	char *text;
	char *cursor;
	char line[LINE_MAX];
	int in_section = 0;
	int found = 0;

	if (!needle || !needle[0] || !section || !section[0])
		return 0;

	text = read_all(path);
	if (!text)
		return 0;

	cursor = text;
	while (next_line(&cursor, line, sizeof line))
	{
		char *t = trim(line);

		if (*t == '[')
		{
			/* Leaving the section ends the search: the same name under a later
			 * header is not the one being asked about. */
			if (in_section)
				break;

			in_section = section_is(t, section);
		}
		else if (in_section &&
		         (mode == MATCH_KEY ? entry_matches(t, needle)
		                           : line_matches(t, needle)))
		{
			found = 1;
			break;
		}
	}

	free(text);
	return found;
}

/* Copies `path` through to a temporary, inserting `entry` at the end of
 * [section] -- or creating the section at the end of the file when it is missing.
 *
 *   1  inserted
 *   0  [section] already had it, nothing written
 *  -1  the file could not be read or written
 *
 * `entry` is the formatted line including its newline. `needle` and `mode` say
 * how to recognise that it is already there, so idempotence is decided by
 * exactly the rule the caller would otherwise have applied by hand.
 */
static int ini_insert_at(const char *path, const char *section, const char *entry,
                         const char *needle, int mode)
{
	char *text;
	char *cursor;
	char line[LINE_MAX];
	char *tmp_path;
	FILE *out;
	int in_section = 0;
	int wrote = 0;
	int rc = -1;

	if (!entry || !entry[0] || !section || !section[0])
		return -1;

	/* Checked before anything is opened for writing: re-running the installer must
	 * not append a second copy of a line that is already there. */
	if (ini_find_at(path, section, needle, mode))
		return 0;

	text = read_all(path);
	if (!text)
		return -1;

	tmp_path = (char *)malloc(strlen(path) + 8);
	if (!tmp_path) { free(text); return -1; }
	strcpy(tmp_path, path);
	strcat(tmp_path, ".tmp");

	out = fopen(tmp_path, "wb");
	if (!out) { free(tmp_path); free(text); return -1; }

	/* Copy through, stopping at the end of the target section so the new line
	 * lands inside it. */
	cursor = text;
	while (next_line(&cursor, line, sizeof line))
	{
		char *t = trim(line);

		if (*t == '[')
		{
			if (in_section)
			{
				/* The next section starts: ours ended just above. */
				if (fputs(entry, out) < 0) goto fail;
				wrote = 1;
				in_section = 0;
			}
			else if (section_is(t, section))
			{
				in_section = 1;
			}
		}

		if (fprintf(out, "%s\n", line) < 0)
			goto fail;
	}

	if (in_section)
	{
		/* Section ran to end of file. */
		if (fputs(entry, out) < 0) goto fail;
		wrote = 1;
	}
	else if (!wrote)
	{
		/* No section at all, or it was the last thing in the file and already
		 * closed above -- create it rather than writing a bare line. */
		if (fprintf(out, "\n[%s]\n", section) < 0) goto fail;
		if (fputs(entry, out) < 0) goto fail;
	}

	if (fclose(out) != 0) { remove(tmp_path); goto done; }
	out = NULL;

	/* Rename into place. A half-written payloads.ini would leave GoldHEN with a
	 * broken auto-run list, and for the nanoDNS case an unresolvable DNS config,
	 * so the destination is never touched in place. */
	if (rename(tmp_path, path) != 0) { remove(tmp_path); goto done; }

	rc = 1;
	goto done;

fail:
	if (out)
		fclose(out);
	remove(tmp_path);

done:
	free(tmp_path);
	free(text);
	return rc;
}

int ini_has_line_at(const char *path, const char *section, const char *line)
{
	return ini_find_at(path, section, line, MATCH_LINE);
}

int ini_add_entry_at(const char *path, const char *section, const char *key,
                     const char *value)
{
	char entry[LINE_MAX];

	if (!key || !key[0] || !value)
		return -1;

	snprintf(entry, sizeof entry, "%s = %s\n", key, value);
	return ini_insert_at(path, section, entry, key, MATCH_KEY);
}

int ini_add_line_at(const char *path, const char *section, const char *line)
{
	char entry[LINE_MAX];

	if (!line || !line[0])
		return -1;

	/* A newline in the parameter would smuggle a second line into the file
	 * through an argument that is meant to be one line. */
	if (strpbrk(line, "\r\n"))
		return -1;

	snprintf(entry, sizeof entry, "%s\n", line);
	return ini_insert_at(path, section, entry, line, MATCH_LINE);
}

/* Copies `path` through to a temporary, dropping the line matching `needle` from
 * [section]. Every other line is written back byte for byte, including comments,
 * spacing and blank lines -- this is the only operation that edits someone else's
 * file, so anything it does not have to touch it must not touch.
 *
 *   1  removed
 *   0  [section] did not have it, nothing written
 *  -1  the file could not be read or written
 */
static int ini_remove_at(const char *path, const char *section, const char *needle,
                         int mode)
{
	char *text;
	char *cursor;
	char line[LINE_MAX];
	char *tmp_path;
	FILE *out;
	int in_section = 0;
	int removed = 0;
	int rc = -1;

	if (!needle || !needle[0] || !section || !section[0])
		return -1;

	/* Nothing to remove means nothing to write. Skipping the rewrite is the
	 * point: a removal that always rewrites would touch a shared config even in
	 * the case where the answer is "it was never there", and that is exactly the
	 * case where the file should be left alone. */
	if (!ini_find_at(path, section, needle, mode))
		return 0;

	text = read_all(path);
	if (!text)
		return -1;

	tmp_path = (char *)malloc(strlen(path) + 8);
	if (!tmp_path) { free(text); return -1; }
	strcpy(tmp_path, path);
	strcat(tmp_path, ".tmp");

	out = fopen(tmp_path, "wb");
	if (!out) { free(tmp_path); free(text); return -1; }

	cursor = text;
	while (next_line(&cursor, line, sizeof line))
	{
		char *t = trim(line);

		if (*t == '[')
		{
			if (in_section)
				in_section = 0;
			else if (section_is(t, section))
				in_section = 1;
		}

		if (in_section &&
		    (mode == MATCH_KEY ? entry_matches(t, needle)
		                       : line_matches(t, needle)))
		{
			removed = 1;
			continue;   /* drop this line, write nothing */
		}

		/* The original line, not the trimmed pointer: padding and spacing in
		 * someone else's config are not ours to tidy. */
		if (fprintf(out, "%s\n", line) < 0)
			goto fail;
	}

	if (!removed)
	{
		/* ini_find_at said it was there and the walk disagreed. Refuse rather
		 * than write back a file whose contents we no longer understand. */
		goto fail;
	}

	if (fclose(out) != 0) { remove(tmp_path); goto done; }
	out = NULL;

	if (rename(tmp_path, path) != 0) { remove(tmp_path); goto done; }

	rc = 1;
	goto done;

fail:
	if (out)
		fclose(out);
	remove(tmp_path);

done:
	free(tmp_path);
	free(text);
	return rc;
}

int ini_remove_entry_at(const char *path, const char *section, const char *key)
{
	return ini_remove_at(path, section, key, MATCH_KEY);
}

int ini_remove_line_at(const char *path, const char *section, const char *line)
{
	return ini_remove_at(path, section, line, MATCH_LINE);
}

int ini_present_at(const char *path)
{
	char *buf = read_all(path);

	if (!buf)
		return 0;

	free(buf);
	return 1;
}

int nanodns_present_at(const char *path)
{
	return ini_present_at(path);
}

int nanodns_has_exception_at(const char *path, const char *mask)
{
	return ini_has_line_at(path, NANODNS_EXCEPTIONS_SECTION, mask);
}

int nanodns_add_exception_at(const char *path, const char *mask)
{
	return ini_add_line_at(path, NANODNS_EXCEPTIONS_SECTION, mask);
}

int nanodns_present(void)
{
	return nanodns_present_at(NANODNS_INI);
}

int nanodns_has_exception(const char *mask)
{
	return nanodns_has_exception_at(NANODNS_INI, mask);
}

int nanodns_add_exception(const char *mask)
{
	return nanodns_add_exception_at(NANODNS_INI, mask);
}

int nanodns_remove_exception(const char *mask)
{
	return ini_remove_line_at(NANODNS_INI, NANODNS_EXCEPTIONS_SECTION, mask);
}
