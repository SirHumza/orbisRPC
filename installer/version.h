/* version.h - compare dot-separated version numbers.
 *
 * Deliberately free of PS4 headers so it can be host-tested, the same split the
 * project already uses for format.cpp. Version ordering is exactly the kind of
 * logic that looks obvious and is not: "10.0" is less than "9.0" as a string and
 * greater as a number.
 */
#ifndef INSTALLER_VERSION_H
#define INSTALLER_VERSION_H

namespace version
{

/* Returns true when `candidate` is strictly newer than `current`.
 *
 * Components are compared left to right as decimal numbers, so "1.10" beats
 * "1.9". A missing component counts as zero, so "1.2" ties with "1.2.0" and loses
 * to "1.2.1".
 *
 * Tags are digits and dots only. Anything else has no defined meaning; the
 * non-numeric tail is skipped rather than treated as significant.
 */
bool IsNewer(const char *candidate, const char *current);

} // namespace version

#endif