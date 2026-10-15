/* format.h - pure text formatting helpers.
 *
 * Deliberately free of any PS4 header so the logic can be compiled and
 * exercised on a host, the same split icfg.c uses for token_valid(). Nothing
 * here calls into the console.
 */
#ifndef PS4UTIL_FORMAT_H
#define PS4UTIL_FORMAT_H

#include <stdint.h>

#include <string>

std::string Trim(const std::string &text);

std::string FormatBytes(uint64_t bytes);

// Seconds as "H:MM:SS", or "d H:MM:SS" once past a day.
std::string FormatDuration(double seconds);

// Shortens a kernel firmware string to the version people actually call it.
//
// sceKernelGetSystemSwVersion reports three components with the minor field
// scaled by ten -- " 9.000.000", "13.520.000" -- whereas firmware is named
// "9.00" and "13.52". So the minor component is divided by ten and printed as
// two digits. Input that is already short is passed through unchanged.
std::string FormatFirmware(const std::string &version);

#endif
