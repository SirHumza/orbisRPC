/* settingsaction.c - the action rows on the Settings page.
 *
 * The TMDB exception is the same edit the install flow offers and the uninstall
 * flow offers to undo, so it is done here with the same tested INI editor rather
 * than a third implementation of it.
 *
 * Which way round it goes is decided by what is already there, so pressing the
 * row always does the useful thing without the page having to display state it
 * would then have to keep in sync.
 */

#include "settingsaction.h"

#include "paths.h"
#include "nanodns.h"
#include "uibridge.h"

int settingsaction_nanodns(void)
{
	int rc;

	if (!nanodns_present())
	{
		uibridge_ok("nanoDNS",
		            "No nanoDNS config was found.\n\n"
		            "There is nothing to change.");
		return 0;
	}

	if (nanodns_has_exception(TMDB_EXCEPTION))
	{
		if (uibridge_yesno("nanoDNS",
		                   "The Sony TMDB exception is in place:\n\n"
		                   TMDB_EXCEPTION "\n\n"
		                   "It lets PlayStation's blocked movie and TV\n"
		                   "endpoints resolve instead of being blackholed\n"
		                   "by the *.playstation.net rules.\n\n"
		                   "Remove it?") != 1)
			return 0;

		rc = nanodns_remove_exception(TMDB_EXCEPTION);

		if (rc < 0)
		{
			uibridge_ok("nanoDNS",
			            "Could not write to the nanoDNS config.\n\n"
			            "It was left as it was.");
		}
		else if (rc == 0)
		{
			/* The add and the remove agreed on where it was; nothing to do. */
			uibridge_ok("nanoDNS",
			            "The exception was already absent.\n\n"
			            "Nothing was changed.");
		}
		else
		{
			uibridge_ok("nanoDNS", "The TMDB exception was removed.");
		}

		return 0;
	}

	if (uibridge_yesno("nanoDNS",
	                   "The Sony TMDB exception is not in place.\n\n"
	                   "It lets PlayStation's Game metadata\n"
	                   "endpoints resolve instead of being blackholed by\n"
	                   "the *.playstation.net rules.\n\n"
	                   "Add it?") != 1)
		return 0;

	rc = nanodns_add_exception(TMDB_EXCEPTION);

	if (rc < 0)
	{
		uibridge_ok("nanoDNS",
		            "Could not write to the nanoDNS config.\n\n"
		            "It was left as it was.");
	}
	else if (rc == 0)
	{
		uibridge_ok("nanoDNS",
		            "The exception was already there.\n\n"
		            "Nothing was changed.");
	}
	else
	{
		uibridge_ok("nanoDNS", "The Sony TMDB exception was added.");
	}

	return 0;
}