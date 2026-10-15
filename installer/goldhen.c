/* goldhen.c - the auto-run entry for GoldHEN.
 *
 * A thin wrapper over the shared INI editor in nanodns.c. GoldHEN's payloads.ini
 * and nanoDNS's config are the same file format -- flat sections of "key = value"
 * -- so this adds no parsing of its own.
 */

#include "goldhen.h"

#include "nanodns.h"

int goldhen_present(void)
{
	return ini_present_at(GOLDHEN_INI);
}

int goldhen_add_autorun(const char *payloadPath)
{
	if (!payloadPath || !payloadPath[0])
		return -1;

	return ini_add_entry_at(GOLDHEN_INI, GOLDHEN_SECTION, payloadPath,
	                        GOLDHEN_ENABLED);
}

int goldhen_remove_autorun(const char *payloadPath)
{
	if (!payloadPath || !payloadPath[0])
		return -1;

	return ini_remove_entry_at(GOLDHEN_INI, GOLDHEN_SECTION, payloadPath);
}
