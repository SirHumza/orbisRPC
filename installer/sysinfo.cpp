#include "sysinfo.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <sys/statvfs.h>

#include <orbis/libkernel.h>
#include <orbis/UserService.h>

namespace {

// Paths worth reporting on. /app0 is the app's own read-only package, /user
// the save area, /data internal storage, and the rest are optional mounts.
const char *const kProbePaths[] = {
	"/app0",
	"/user",
	"/user/home",
	"/data",
	"/system",
	"/mnt/usb0",
	"/mnt/usb1",
};

const int kProbeCount = sizeof(kProbePaths) / sizeof(kProbePaths[0]);

} // namespace

SystemInfo QuerySystem()
{
	SystemInfo info;
	memset(&info, 0, sizeof(info));
	info.userId = -1;

	// Firmware. OrbisKernelSwVersion::VersionString is space padded
	// (e.g. " 9.000.000"), so trim it before display.
	OrbisKernelSwVersion version;
	memset(&version, 0, sizeof(version));

	if (sceKernelGetSystemSwVersion(&version) == 0)
	{
		version.VersionString[sizeof(version.VersionString) - 1] = '\0';
		info.firmware = Trim(std::string(version.VersionString));
		info.firmwareRaw = version.Version;
	}
	else
	{
		info.firmware = "unavailable";
	}

	info.tscFrequency = sceKernelGetTscFrequency();
	info.directMemSize = sceKernelGetDirectMemorySize();
	info.pageSize = static_cast<uint32_t>(getpagesize());

	int32_t userId = -1;
	if (sceUserServiceGetInitialUser(&userId) == 0)
		info.userId = userId;

	return info;
}

std::vector<MountInfo> QueryStorage()
{
	std::vector<MountInfo> mounts;
	mounts.reserve(kProbeCount);

	for (int i = 0; i < kProbeCount; ++i)
	{
		MountInfo entry;
		entry.path = kProbePaths[i];
		entry.mounted = false;
		entry.totalBytes = 0;
		entry.freeBytes = 0;

		struct statvfs stats;
		memset(&stats, 0, sizeof(stats));

		if (statvfs(entry.path.c_str(), &stats) == 0)
		{
			entry.mounted = true;
			entry.totalBytes = static_cast<uint64_t>(stats.f_blocks) *
			                   static_cast<uint64_t>(stats.f_frsize);
			entry.freeBytes = static_cast<uint64_t>(stats.f_bavail) *
			                  static_cast<uint64_t>(stats.f_frsize);
		}

		mounts.push_back(entry);
	}

	return mounts;
}

void Uptime::Start()
{
	frequency_ = sceKernelGetTscFrequency();
	startTsc_ = sceKernelReadTsc();
}

double Uptime::Seconds() const
{
	// Without a usable TSC frequency there is nothing sensible to report.
	if (frequency_ == 0)
		return 0.0;

	const uint64_t now = sceKernelReadTsc();
	if (now < startTsc_)
		return 0.0;

	return static_cast<double>(now - startTsc_) / static_cast<double>(frequency_);
}
