#include "fvarx.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace {

inline uint16_t ReadU16(const uint8_t *p)
{
	return static_cast<uint16_t>((p[0] << 8) | p[1]);
}

inline uint32_t ReadU32(const uint8_t *p)
{
	return (static_cast<uint32_t>(p[0]) << 24) |
	       (static_cast<uint32_t>(p[1]) << 16) |
	       (static_cast<uint32_t>(p[2]) << 8) |
	       static_cast<uint32_t>(p[3]);
}

} // namespace

namespace fvarx
{

bool ReadAxes(const char *path, Axes &out)
{
	if (!path)
		return false;

	FILE *f = fopen(path, "rb");
	if (!f)
		return false;

	// Read the whole font. These are a couple of megabytes, so this is a single
	// allocation rather than seeking around the file.
	if (fseek(f, 0, SEEK_END) != 0)
	{
		fclose(f);
		return false;
	}

	const long size = ftell(f);
	if (size <= 12 || size > 64 * 1024 * 1024 || fseek(f, 0, SEEK_SET) != 0)
	{
		fclose(f);
		return false;
	}

	uint8_t *buf = static_cast<uint8_t *>(malloc(static_cast<size_t>(size)));
	if (!buf)
	{
		fclose(f);
		return false;
	}

	if (fread(buf, 1, static_cast<size_t>(size), f) != static_cast<size_t>(size))
	{
		free(buf);
		fclose(f);
		return false;
	}
	fclose(f);

	bool ok = false;
	Axes axes;
	axes.count = 0;

	// Table directory: 12-byte offset table, then 16 bytes per record.
	const uint16_t numTables = ReadU16(buf + 4);

	const uint8_t *fvar = NULL;
	uint32_t fvarLen = 0;

	for (uint16_t i = 0; i < numTables; ++i)
	{
		const size_t rec = 12 + static_cast<size_t>(i) * 16;
		if (rec + 16 > static_cast<size_t>(size))
			break;

		if (memcmp(buf + rec, "fvar", 4) == 0)
		{
			const uint32_t off = ReadU32(buf + rec + 8);
			const uint32_t len = ReadU32(buf + rec + 12);

			if (off < static_cast<uint32_t>(size) &&
			    len <= static_cast<uint32_t>(size) - off)
			{
				fvar = buf + off;
				fvarLen = len;
			}
			break;
		}
	}

	if (fvar && fvarLen >= 16)
	{
		// fvar header: majorVersion, minorVersion, axesArrayOffset,
		// reserved, axisCount, axisSize, instanceCount, instanceSize.
		const uint16_t axisCount = ReadU16(fvar + 4);
		const uint16_t axisSize = ReadU16(fvar + 6);

		// axisSize must be at least the 20 bytes of tag/min/def/max/flags/name
		// that every axis record carries. Rejecting anything smaller keeps the
		// reads below in bounds.
		if (axisCount > 0 && axisCount <= kMaxAxes && axisSize >= 20)
		{
			const uint16_t axesOffset = ReadU16(fvar + 2);

			if (static_cast<uint32_t>(axesOffset) <= fvarLen)
			{
				const uint32_t need = static_cast<uint32_t>(axesOffset) +
				                      static_cast<uint32_t>(axisCount) * axisSize;

				if (need <= fvarLen)
				{
					ok = true;
					axes.count = axisCount;

					for (int i = 0; i < axisCount; ++i)
					{
						const uint8_t *rec = fvar + axesOffset +
						                     static_cast<size_t>(i) * axisSize;

						axes.tag[i] = FT_MAKE_TAG(rec[0], rec[1], rec[2], rec[3]);

						// Already 16.16 fixed, same as FT_Fixed.
						axes.min[i] = static_cast<int32_t>(ReadU32(rec + 4));
						axes.def[i] = static_cast<int32_t>(ReadU32(rec + 8));
						axes.max[i] = static_cast<int32_t>(ReadU32(rec + 12));
					}
				}
			}
		}
	}

	free(buf);

	if (ok)
		out = axes;

	return ok;
}

int WeightAxis(const Axes &axes)
{
	const FT_ULong want = FT_MAKE_TAG('w', 'g', 'h', 't');

	for (int i = 0; i < axes.count; ++i)
	{
		if (axes.tag[i] == want)
			return i;
	}

	return -1;
}

} // namespace fvarx