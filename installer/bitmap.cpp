#include "bitmap.h"

#include <stdio.h>
#include <stdlib.h>

namespace {

inline uint16_t ReadU16(const uint8_t *p)
{
	return static_cast<uint16_t>(p[0] | (p[1] << 8));
}

inline uint32_t ReadU32(const uint8_t *p)
{
	return static_cast<uint32_t>(p[0]) |
	       (static_cast<uint32_t>(p[1]) << 8) |
	       (static_cast<uint32_t>(p[2]) << 16) |
	       (static_cast<uint32_t>(p[3]) << 24);
}

inline int32_t ReadS32(const uint8_t *p)
{
	return static_cast<int32_t>(ReadU32(p));
}

// Slurps a whole file. The assets are small (a few hundred KB at most, already
// pre-scaled at build time) and this runs once per image at startup.
uint8_t *ReadFile(const char *path, long &outLen)
{
	outLen = 0;

	FILE *f = fopen(path, "rb");
	if (!f)
		return NULL;

	if (fseek(f, 0, SEEK_END) != 0)
	{
		fclose(f);
		return NULL;
	}

	const long size = ftell(f);
	if (size <= 0 || size > 32 * 1024 * 1024 || fseek(f, 0, SEEK_SET) != 0)
	{
		fclose(f);
		return NULL;
	}

	uint8_t *buf = static_cast<uint8_t *>(malloc(static_cast<size_t>(size)));
	if (!buf)
	{
		fclose(f);
		return NULL;
	}

	if (fread(buf, 1, static_cast<size_t>(size), f) != static_cast<size_t>(size))
	{
		free(buf);
		fclose(f);
		return NULL;
	}

	fclose(f);
	outLen = size;
	return buf;
}

} // namespace

namespace bitmap
{

SDL_Surface *Load(const char *path)
{
	long len = 0;
	uint8_t *buf = ReadFile(path, len);

	if (!buf)
	{
		SDL_Log("bitmap: cannot read %s", path);
		return NULL;
	}

	// BITMAPFILEHEADER is 14 bytes: 'BM', size, reserved, pixel data offset.
	if (len < 54 || buf[0] != 'B' || buf[1] != 'M')
	{
		SDL_Log("bitmap: %s is not a BMP", path);
		free(buf);
		return NULL;
	}

	const uint32_t dataOffset = ReadU32(buf + 10);
	const uint32_t headerSize = ReadU32(buf + 14);

	// Only BITMAPINFOHEADER, which is what an uncompressed 32bpp BMP written by
	// any normal tool uses.
	if (headerSize != 40)
	{
		SDL_Log("bitmap: %s header is %u bytes, expected 40", path, headerSize);
		free(buf);
		return NULL;
	}

	const int32_t width = ReadS32(buf + 18);
	const int32_t height = ReadS32(buf + 22);
	const uint16_t planes = ReadU16(buf + 26);
	const uint16_t bpp = ReadU16(buf + 28);
	const uint32_t compression = ReadU32(buf + 30);

	// BI_RGB only. Rejecting the rest rather than half-supporting them: a
	// misread row stride would produce garbage that looks like a load failure
	// somewhere else entirely.
	if (compression != 0 || bpp != 32 || planes != 1)
	{
		SDL_Log("bitmap: %s is %ubpp comp=%u planes=%u, expected 32bpp BI_RGB",
		        path, bpp, compression, planes);
		free(buf);
		return NULL;
	}

	if (width <= 0 || height == 0 || width > 8192 || (height < -8192 || height > 8192))
	{
		SDL_Log("bitmap: %s has implausible size %dx%d", path, width, height);
		free(buf);
		return NULL;
	}

	// A negative height means the rows are stored top-down. Uncommon, but free to
	// support and getting it backwards flips the image vertically.
	const bool topDown = (height < 0);
	const int h = topDown ? -height : height;
	const int w = width;

	const long stride = static_cast<long>(w) * 4;
	const long need = static_cast<long>(dataOffset) + stride * h;

	if (dataOffset < 54 || need > len)
	{
		SDL_Log("bitmap: %s is truncated (need %ld bytes, have %ld)", path, need,
		        len);
		free(buf);
		return NULL;
	}

	SDL_Surface *surface = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32,
	                                                     SDL_PIXELFORMAT_ARGB8888);
	if (!surface)
	{
		free(buf);
		return NULL;
	}

	const uint8_t *src = buf + dataOffset;

	for (int y = 0; y < h; ++y)
	{
		const int row = topDown ? y : (h - 1 - y); // BMP is bottom-up by default
		const uint8_t *s = src + static_cast<long>(row) * stride;
		uint32_t *d = reinterpret_cast<uint32_t *>(
			static_cast<uint8_t *>(surface->pixels) + y * surface->pitch);

		for (int x = 0; x < w; ++x)
		{
			const uint8_t b = s[x * 4 + 0];
			const uint8_t g = s[x * 4 + 1];
			const uint8_t r = s[x * 4 + 2];
			const uint8_t a = s[x * 4 + 3];

			d[x] = (static_cast<uint32_t>(a) << 24) |
			       (static_cast<uint32_t>(r) << 16) |
			       (static_cast<uint32_t>(g) << 8) |
			       static_cast<uint32_t>(b);
		}
	}

	free(buf);
	return surface;
}

} // namespace bitmap