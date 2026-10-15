#include "format.h"

#include <stdio.h>
#include <stdlib.h>

std::string Trim(const std::string &text)
{
	size_t begin = 0;
	size_t end = text.size();

	while (begin < end && (text[begin] == ' ' || text[begin] == '\t'))
		++begin;
	while (end > begin && (text[end - 1] == ' ' || text[end - 1] == '\t' ||
	                       text[end - 1] == '\0'))
		--end;

	return text.substr(begin, end - begin);
}

std::string FormatBytes(uint64_t bytes)
{
	static const char *const kUnits[] = { "B", "KiB", "MiB", "GiB", "TiB" };

	char buffer[64];
	double value = static_cast<double>(bytes);
	size_t unit = 0;

	while (value >= 1024.0 && unit + 1 < sizeof(kUnits) / sizeof(kUnits[0]))
	{
		value /= 1024.0;
		++unit;
	}

	if (unit == 0)
		snprintf(buffer, sizeof(buffer), "%llu B",
		         static_cast<unsigned long long>(bytes));
	else
		snprintf(buffer, sizeof(buffer), "%.2f %s", value, kUnits[unit]);

	return std::string(buffer);
}

std::string FormatDuration(double seconds)
{
	char buffer[64];

	if (seconds < 0.0)
		seconds = 0.0;

	const unsigned long total = static_cast<unsigned long>(seconds);
	const unsigned long days = total / 86400;
	const unsigned long hours = (total % 86400) / 3600;
	const unsigned long minutes = (total % 3600) / 60;
	const unsigned long secs = total % 60;

	if (days > 0)
		snprintf(buffer, sizeof(buffer), "%lu d %02lu:%02lu:%02lu",
		         days, hours, minutes, secs);
	else
		snprintf(buffer, sizeof(buffer), "%02lu:%02lu:%02lu",
		         hours, minutes, secs);

	return std::string(buffer);
}

std::string FormatFirmware(const std::string &version)
{
	const std::string trimmed = Trim(version);

	const size_t firstDot = trimmed.find('.');
	if (firstDot == std::string::npos)
		return trimmed;

	const std::string major = trimmed.substr(0, firstDot);

	const size_t secondDot = trimmed.find('.', firstDot + 1);
	const std::string minorField = (secondDot == std::string::npos)
		? trimmed.substr(firstDot + 1)
		: trimmed.substr(firstDot + 1, secondDot - firstDot - 1);

	const std::string minor = Trim(minorField);

	// Only three-component strings carry the scaled minor. With two components
	// the value is already short form ("13.52"), so dividing again would turn it
	// into "13.05".
	if (secondDot == std::string::npos)
		return minor.empty() ? major + ".00" : major + "." + minor;

	char buffer[32];
	snprintf(buffer, sizeof buffer, "%s.%02d", major.c_str(), atoi(minor.c_str()) / 10);

	return std::string(buffer);
}
