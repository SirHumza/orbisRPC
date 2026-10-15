/* uninstall.c - taking an orbisRPC install back off the console.
 *
 * Two kinds of thing get removed, and the difference is the whole design:
 *
 *   ours    files this app created, deleted outright
 *   shared  lines this app added to someone else's config, and only those lines
 *
 * payloads.ini and nanodns.ini are not ours. Deleting either would take out
 * every other autorun payload, or leave the console with no name resolution at
 * all. So the shared files are edited, never removed, and the edit is a single
 * line out of a file that may hold dozens.
 */

#include "uninstall.h"

#include "paths.h"
#include "nanodns.h"
#include "goldhen.h"
#include "rmtree.h"
#include "uibridge.h"

#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <dirent.h>

/* Appends one line to the install log.
 *
 * Deliberately not the ilog helpers from installer.c: this is a separate flow,
 * and reaching across for them would couple the two. The log path is a constant
 * both flows share, which is the part that actually has to agree.
 *
 * Opened per call like the install flow's, rather than held open, because this
 * file deletes that log at the end -- see the note on remove_ours() about
 * deleting a file that is still being written to. */
static void ulog(const char *what, int err)
{
	FILE *f = fopen(INST_LOG, "a");

	if (!f)
		return;

	if (err)
		fprintf(f, "%s errno=%d %s\n", what, err, strerror(err));
	else
		fprintf(f, "%s ok\n", what);

	fclose(f);
}

/* 1 when a readable file is at `path`. Opening is the test, not stat(): the
 * sandbox lies about sizes and stat is not reliable here, so whether the file can
 * be opened is the only answer that is actually true. */
static int readable(const char *path)
{
	FILE *f = fopen(path, "rb");

	if (!f)
		return 0;

	fclose(f);
	return 1;
}

/* Deletes `path`, treating "it was not there" as success.
 *
 * The distinction that matters: a file that could not be removed because the
 * console said no is a failure the user needs to hear about, while one that was
 * already absent is the state we wanted anyway. Collapsing the two would mean a
 * successful uninstall reporting a failure, or the reverse.
 *
 * Returns 1 removed or already absent, 0 could not remove. */
static int drop(const char *path)
{
	if (!readable(path))
		return 1;   /* already gone, which is the outcome we wanted */

	if (remove(path) == 0)
		return 1;

	/* Race: gone between the check and the remove. Also success. */
	if (!readable(path))
		return 1;

	return 0;
}

/* What the console actually has, probed once before anything is removed.
 *
 * The point is that removal only ever targets what was found. Relying on each
 * delete call to shrug off a path that is not there works, but it means the
 * outcome of a partly-installed console is indistinguishable from a failed one:
 * both end up reporting things that could not be removed, and the user cannot
 * tell which is which.
 *
 * The two halves are separate because they live in separate places, and missing
 * either is a real state rather than an error: a console can have the folder but
 * no payload if an install was interrupted between the two copies, and can have
 * the payload with no folder if someone deleted the folder by hand. */
typedef struct
{
	int payload;    /* /data/payloads/orbisRPC.elf     */
	int logo;       /* /data/payloads/orbisRPC.elf.png */
	int instDir;    /* /data/orbisRPC/                   */
} installed_t;

static int dir_exists(const char *path)
{
	DIR *d = opendir(path);

	if (!d)
		return 0;

	closedir(d);
	return 1;
}

static installed_t probe_install(void)
{
	installed_t s;

	s.payload = readable(PAYLOAD_ELF);
	s.logo = readable(LOGO_ICON);
	s.instDir = dir_exists(INST_DIR);

	return s;
}

static int is_installed(const installed_t *s)
{
	return s->payload || s->logo || s->instDir;
}

/* Removes the install: the payload and its icon from /data/payloads, and the
 * whole of /data/orbisRPC.
 *
 * INST_DIR is taken as a tree rather than as the list of files this app is known
 * to write. A fixed list is a list that goes stale -- anything added later, by
 * this app or by the user, is left behind and the uninstall quietly does less
 * than it says on screen. Walking the directory cannot go stale.
 *
 * install.log is inside the tree this deletes while it is being written to. On
 * this filesystem an open file survives being unlinked -- the writing process
 * keeps its descriptor and the name disappears -- so the log records the whole
 * uninstall and is gone once the app exits. Anything logged after this point
 * finds no directory and is silently dropped, which is why the outcome is
 * reported on screen rather than only in the log.
 *
 * Bounds on the recursion live in rmtree.c, next to the code that applies them. */
static int remove_ours(const installed_t *s)
{
	int ok = 1;

	/* Staging first: it is small, it is not what the user asked about, and
	 * getting it out of the way means a failure there is reported before the
	 * real removal rather than after. */
	drop(TEMP_PAYLOAD);
	drop(TEMP_CONFIG);
	drop(TEMP_PAYLOAD ".part");
	drop(TEMP_CONFIG ".part");
	rmdir(TEMP_DIR);

	if (s->payload && !drop(PAYLOAD_ELF))
		ok = 0;

	if (s->logo && !drop(LOGO_ICON))
		ok = 0;

	if (s->instDir && !rmtree(INST_DIR))
	{
		ulog("uninstall-rmtree-failed", errno);
		ok = 0;
	}

	return ok;
}

/* Takes our line back out of a config someone else owns.
 *
 *   1  removed
 *   0  the line was not there, and the file was not written to
 *  -1  the file could not be read or written
 *
 * Only -1 is a failure. See the call site: "not there" is the wanted state. */
static int unshare(const char *path, const char *section, const char *needle)
{
	int rc = ini_remove_line_at(path, section, needle);

	ulog(rc == 1 ? "uninstall-nanodns-removed"
	             : rc == 0 ? "uninstall-nanodns-absent"
	                       : "uninstall-nanodns-failed",
	     rc < 0 ? errno : 0);

	return rc;
}

int uninstall_flow(void)
{
	int ok = 1;
	installed_t have;

	/* Probed once, up front, and the same probe decides both whether there is
	 * anything to do and what to do to it. Asking twice is how the payload and
	 * its icon came to be missed: the "is it installed" check and the removal
	 * each listed what they thought was there, and the two lists disagreed. */
	have = probe_install();

	/* Nothing installed is not a failure. The flow is offered on any console,
	 * and the answer on a clean one is "there is nothing there", not an error. */
	if (!is_installed(&have))
	{
		uibridge_ok("Nothing to remove",
		            "orbisRPC is not installed on this console.");
		return UNINSTALL_OK;
	}

	/* What goes, and -- as much as what goes -- what stays.
	 *
	 * Short on the removal: "the payload and the folder" covers everything,
	 * since the folder takes the config, the logs and the icon with it and the
	 * payload sits beside it. Itemising those was written when the uninstall did
	 * not actually remove them, and listing files that a user cannot act on
	 * individually is worse than not listing them.
	 *
	 * The second half is the part worth reading. payloads.ini and nanodns.ini
	 * belong to other things, and "only your own line goes" is the difference
	 * between a user trusting this and a user backing out of it. */
	if (uibridge_yesno("Remove orbisRPC",
	                  "This will remove the orbisRPC payload and\n"
	                  "its folder.\n\n"
	                  "Your GoldHEN and nano DNS settings stay as\n"
	                  "they are. Only orbisRPC's own line is taken\n"
	                  "out of each.\n\n"
	                  "Remove orbisRPC?") != 1)
		return UNINSTALL_STOPPED;

	/* Shared configs first. Only a genuine failure counts against the uninstall:
	 * "that line was not there" is the state we wanted anyway, and treating it as
	 * a failure would make a clean uninstall report itself as incomplete on every
	 * console without GoldHEN -- which is most of them.
	 *
	 * Doing them before the payload deletion is deliberate. If this half fails,
	 * an autorun line is left pointing at a file that still exists, and GoldHEN
	 * skips it without comment. The other order leaves a deleted payload that
	 * something still tries to start at boot on every reboot. */
	{
		int rc = goldhen_remove_autorun(GOLDHEN_AUTORUN_ELF);

		ulog(rc == 1 ? "uninstall-goldhen-removed"
		             : rc == 0 ? "uninstall-goldhen-absent"
		                       : "uninstall-goldhen-failed",
		     rc < 0 ? errno : 0);

		if (rc < 0)
			ok = 0;
	}

	/* The TMDB exception is asked about rather than assumed, and the reason is
	 * that it is not really orbisRPC's: it is a general fix for PlayStation's
	 * blocked endpoints, and a user who has come to rely on it -- or who put it
	 * there themselves -- would reasonably not want it taken away by uninstalling
	 * something unrelated. The GoldHEN line above gets no such question, because
	 * nothing about it survives this removal.
	 *
	 * Asked only when the line is actually there, so the question never appears
	 * for a console that never had it. */
	if (nanodns_has_exception(TMDB_EXCEPTION))
	{
		if (uibridge_yesno("nanoDNS",
		                   "The Sony TMDB exception is in your nanoDNS\n"
		                   "config:\n\n" TMDB_EXCEPTION "\n\n"
		                   "It is not part of orbisRPC and can stay.\n\n"
		                   "Remove it as well?") == 1)
		{
			int rc = unshare(NANODNS_INI, NANODNS_EXCEPTIONS_SECTION,
			                 TMDB_EXCEPTION);

			if (rc < 0)
				ok = 0;
		}
	}

	if (!remove_ours(&have))
		ok = 0;

	if (ok)
	{
		uibridge_ok("Removed",
		            "orbisRPC has been removed from this console.");
		return UNINSTALL_OK;
	}

	/* Named rather than generic: something is still there and the user needs to
	 * know what, because it may be the payload, which is the part that matters. */
	uibridge_ok("Not everything",
	            "Some files could not be removed.\n\n"
	            "Check the console's write permissions for /data,\n"
	            "then run Uninstall again.");

	return UNINSTALL_FAILED;
}