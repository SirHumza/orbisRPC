/* rmtree.c - recursive directory removal. See rmtree.h.
 *
 * Two decisions carry the safety of this, and both are about what it refuses to
 * assume rather than about cleverness in what it does:
 *
 *   unlink() is tried before opendir(), so a symlink is deleted rather than
 *   followed. Doing it the other way round means a link inside the tree pointing
 *   at, say, /data/titles would take that directory with it.
 *
 *   File type is decided by which call succeeds, never by d_type. This platform
 *   does not reliably populate d_type -- it is the same sandbox that reports a
 *   2 MB file as 4096 bytes through stat() -- so a delete branching on it would
 *   quietly treat directories as files and leave the tree half-walked.
 */

#include "rmtree.h"

#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <dirent.h>

/* Nothing this app creates comes near either. Reaching a limit means the tree is
 * not what we put there, which is a reason to stop rather than to keep going. */
#define RMTREE_MAX_DEPTH   8
#define RMTREE_MAX_ENTRIES 512

static int rm_tree(const char *path, int depth)
{
	DIR *d;
	struct dirent *e;
	int ok = 1;
	int budget = RMTREE_MAX_ENTRIES;

	if (depth > RMTREE_MAX_DEPTH)
		return 0;

	/* An empty directory goes in one call, and trying that first keeps every
	 * leaf off the readdir path. */
	if (rmdir(path) == 0)
		return 1;

	d = opendir(path);
	if (!d)
	{
		/* Not a directory. If it is a file, remove it -- rmtree is asked to make
		 * a path go away, and refusing because it turned out to be a file would
		 * leave a stray in place. */
		if (unlink(path) == 0)
			return 1;

		/* Already gone. The state the caller wanted, and not a failure: a
		 * second uninstall has to be able to report success. */
		if (errno == ENOENT)
			return 1;

		return 0;
	}

	while ((e = readdir(d)) != NULL)
	{
		char child[512];

		if (budget-- <= 0)
		{
			ok = 0;
			break;
		}

		if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, ".."))
			continue;

		if (snprintf(child, sizeof child, "%s/%s", path, e->d_name)
		    >= (int)sizeof child)
		{
			/* Not reachable by anything written here. Refusing beats
			 * truncating: a truncated path is a *different* path, and
			 * deleting that would be worse than deleting nothing. */
			ok = 0;
			continue;
		}

		if (unlink(child) == 0)
			continue;

		if (!rm_tree(child, depth + 1))
			ok = 0;
	}

	closedir(d);

	/* Anything left keeps the directory from going, and this is the call that
	 * decides the outcome. */
	if (rmdir(path) != 0)
		ok = 0;

	return ok;
}

int rmtree(const char *path)
{
	if (!path || !path[0])
		return 0;

	return rm_tree(path, 0);
}