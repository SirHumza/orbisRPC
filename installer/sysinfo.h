#ifndef PS4UTIL_SYSINFO_H
#define PS4UTIL_SYSINFO_H

#include <stdint.h>

#include <string>
#include <vector>

// Formatting helpers live in format.h, free of PS4 headers so they can be
// host-tested. They are pulled in here because callers get everything from
// this header.
#include "format.h"

struct SystemInfo
{
	std::string firmware;       // full kernel string, e.g. "9.000.000"
	uint32_t    firmwareRaw;    // e.g. 0x09000000
	uint64_t    tscFrequency;   // Hz, 0 if unavailable
	size_t      directMemSize;  // bytes of carve-out video/direct memory
	uint32_t    pageSize;       // host page size in bytes
	int32_t     userId;         // initial (logged-in) user, or -1
};

// Mounted filesystems this utility cares about. Anything that fails to stat is
// reported as not mounted rather than omitted.
struct MountInfo
{
	std::string path;
	bool        mounted;
	uint64_t    totalBytes;
	uint64_t    freeBytes;
};

SystemInfo QuerySystem();
std::vector<MountInfo> QueryStorage();

// Uptime measured with the CPU timestamp counter, which -- unlike wall-clock
// time -- cannot be stepped backwards by the user changing the console clock.
class Uptime
{
public:
	void Start();
	double Seconds() const;

private:
	uint64_t startTsc_;
	uint64_t frequency_;
};

#endif
