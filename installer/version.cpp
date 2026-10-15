#include "version.h"

#include <string.h>

namespace version
{

// Numeric components, compared left to right. No pre-release handling: the tag
// scheme is digits and dots only, and pretending otherwise would only add ways to
// be wrong.
bool IsNewer(const char *candidate, const char *current)
{
	if (!candidate || !current)
		return false;

	while (*candidate || *current)
	{
		// One component each. A missing component counts as 0, so "1.2" loses to
		// "1.2.1" and ties with "1.2.0".
		long a = 0;
		long b = 0;

		while (*candidate >= '0' && *candidate <= '9')
			a = a * 10 + (*candidate++ - '0');
		while (*current >= '0' && *current <= '9')
			b = b * 10 + (*current++ - '0');

		if (a != b)
			return a > b;

		// Equal so far. Step over the separator; stop when both strings end.
		while (*candidate && *candidate != '.')
			candidate++;
		while (*current && *current != '.')
			current++;

		if (*candidate == '.')
			candidate++;
		if (*current == '.')
			current++;

		if (!*candidate && !*current)
			return false;   // equal
	}

	return false;
}

} // namespace version