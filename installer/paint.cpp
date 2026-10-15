#include "paint.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "freetype_mm.h"
#include "fvarx.h"

namespace {

// Divides by 255, rounded.
//
// This is in the per-pixel blend path, which runs millions of times per frame at
// 1080p, and a plain `/ 255` compiles to a multiply plus an actual integer
// division -- division is 20-40 cycles on these cores and it dominated the frame
// time. The shift-and-add form is exact for 16-bit inputs and costs a handful of
// ALU ops instead.
inline uint8_t Div255(int v)
{
	const int t = v + 128;
	return static_cast<uint8_t>((t + (t >> 8)) >> 8);
}

// Blends src over dst by coverage, in place, without dividing.
inline void BlendRGB(uint8_t &dr, uint8_t &dg, uint8_t &db, uint8_t sr,
                     uint8_t sg, uint8_t sb, int coverage)
{
	if (coverage >= 255)
	{
		dr = sr;
		dg = sg;
		db = sb;
		return;
	}

	const int inv = 255 - coverage;
	dr = Div255(sr * coverage + dr * inv);
	dg = Div255(sg * coverage + dg * inv);
	db = Div255(sb * coverage + db * inv);
}

FT_Library g_ftLib = NULL;

// Face cache. The app needs a handful of (path, size) pairs for its lifetime and
// nothing is unloaded, so a flat array beats any container here.
const int kMaxFaces = 12;

struct CachedFace
{
	const char *path;
	int         size;
	float       weight; // 0 for "font default"
	FT_Face     face;
};

CachedFace g_faces[kMaxFaces];

// ---- surface pixel access -------------------------------------------------
//
// The PS4 window surface is 32bpp, but rather than assume a channel order the
// masks are read from the format once and pixels are packed and unpacked
// through them. Getting this wrong produces swapped red and blue channels,
// which is exactly the kind of bug that looks like "the theme is wrong".

struct Layout
{
	uint32_t rMask, gMask, bMask, aMask;
	int      rShift, gShift, bShift, aShift;
	int      rBits, gBits, bBits, aBits;
	bool     ok;
};

Layout MakeLayout(SDL_Surface *dst)
{
	Layout out;
	memset(&out, 0, sizeof out);
	out.ok = false;

	const SDL_PixelFormat *fmt = dst->format;
	out.rMask = fmt->Rmask;
	out.gMask = fmt->Gmask;
	out.bMask = fmt->Bmask;
	out.aMask = fmt->Amask;

	int rBits = 0, gBits = 0, bBits = 0, aBits = 0;

	out.rShift = 0;
	while (out.rShift < 32 && ((out.rMask >> out.rShift) & 1u) == 0)
		++out.rShift;
	rBits = 0;
	for (uint32_t m = out.rMask; m; m >>= 1)
		rBits += static_cast<int>(m & 1u);

	out.gShift = 0;
	while (out.gShift < 32 && ((out.gMask >> out.gShift) & 1u) == 0)
		++out.gShift;
	for (uint32_t m = out.gMask; m; m >>= 1)
		gBits += static_cast<int>(m & 1u);

	out.bShift = 0;
	while (out.bShift < 32 && ((out.bMask >> out.bShift) & 1u) == 0)
		++out.bShift;
	for (uint32_t m = out.bMask; m; m >>= 1)
		bBits += static_cast<int>(m & 1u);

	if (out.aMask)
	{
		out.aShift = 0;
		while (out.aShift < 32 && ((out.aMask >> out.aShift) & 1u) == 0)
			++out.aShift;
		for (uint32_t m = out.aMask; m; m >>= 1)
			aBits += static_cast<int>(m & 1u);
	}

	// Channel widths are recorded so the pack and unpack paths can scale to and
	// from 8 bits. A format with fewer bits per channel is a normal thing for a
	// console surface to have, and refusing to draw on it would leave the whole
	// screen showing uninitialised memory.
	out.rBits = rBits;
	out.gBits = gBits;
	out.bBits = bBits;
	out.aBits = aBits;
	out.ok = (rBits > 0 && gBits > 0 && bBits > 0);
	return out;
}

// Scales an 8-bit value up or down to a `bits`-wide channel.
//
// The wider-than-8 case is not a shortcut: the PS4 window surface is 10 bits per
// channel, so returning the byte unchanged writes colours about a quarter of
// their intended brightness. That is what tinted the whole menu.
inline uint32_t ScaleTo(uint8_t v, int bits)
{
	if (bits == 8)
		return v;

	const uint32_t max = (1u << bits) - 1u;
	return ((static_cast<uint32_t>(v) * max) + 127) / 255;
}

inline uint8_t ScaleFrom(uint32_t v, int bits)
{
	if (bits >= 8)
		return static_cast<uint8_t>(v);

	const uint32_t max = (1u << bits) - 1u;
	return static_cast<uint8_t>(((v * 255) + max / 2) / max);
}

inline uint32_t Pack(const Layout &l, uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
	return (ScaleTo(r, l.rBits) << l.rShift) |
	       (ScaleTo(g, l.gBits) << l.gShift) |
	       (ScaleTo(b, l.bBits) << l.bShift) |
	       ((l.aMask ? ScaleTo(a, l.aBits) : 0u) << l.aShift);
}

inline void Unpack(const Layout &l, uint32_t px, uint8_t &r, uint8_t &g, uint8_t &b)
{
	r = ScaleFrom((px & l.rMask) >> l.rShift, l.rBits);
	g = ScaleFrom((px & l.gMask) >> l.gShift, l.gBits);
	b = ScaleFrom((px & l.bMask) >> l.bShift, l.bBits);
}

// Blends `src` over the pixel at (x, y) with coverage 0..255.
inline void BlendPixel(SDL_Surface *dst, const Layout &l, int x, int y, uint8_t sr,
                       uint8_t sg, uint8_t sb, int coverage)
{
	if (coverage <= 0 || x < 0 || y < 0 || x >= dst->w || y >= dst->h)
		return;

	if (coverage > 255)
		coverage = 255;

	uint8_t *row = static_cast<uint8_t *>(dst->pixels) + y * dst->pitch;
	uint32_t *px = reinterpret_cast<uint32_t *>(row) + x;

	const uint32_t packed = Pack(l, sr, sg, sb, 255);

	if (coverage >= 255)
	{
		*px = packed;
		return;
	}

	uint8_t dr, dg, db;
	Unpack(l, *px, dr, dg, db);

	BlendRGB(dr, dg, db, sr, sg, sb, coverage);
	*px = Pack(l, dr, dg, db, 255);
}

// ---- shape helpers --------------------------------------------------------

// Clamps a rectangle to the surface, returning false if nothing is left.
bool ClipRect(SDL_Surface *dst, int &x, int &y, int &w, int &h)
{
	if (w <= 0 || h <= 0)
		return false;

	if (x < 0)
	{
		w += x;
		x = 0;
	}
	if (y < 0)
	{
		h += y;
		y = 0;
	}
	if (x + w > dst->w)
		w = dst->w - x;
	if (y + h > dst->h)
		h = dst->h - y;

	return w > 0 && h > 0;
}

// Per-pixel signed distance to a rounded rectangle's boundary. Negative inside,
// positive outside, in pixels.
//
// Returns a float, and that is the whole fix.
//
// This used to return int, which threw away the fraction. The caller then did
// `cov = 255 + dist`, so a pixel outside the shape by even one pixel got 256 and
// was clamped to fully opaque, and one sitting exactly on the boundary got 255.
// The result was a FillRounded that drew a solid rectangle: the corners were
// never cut, at any radius. The rounded look came only from the StrokeRounded
// outlines drawn on top of it, which is why shapes appeared to have a flat square
// sitting behind a rounded border.
float RoundedCoverage(float px, float py, float cx, float cy, float halfW,
                      float halfH, float r)
{
	if (r < 0.0f)
		r = 0.0f;

	// Distance from the centre, in a box-shaped space.
	const float qx = fabsf(px - cx) - (halfW - r);
	const float qy = fabsf(py - cy) - (halfH - r);

	const float ax = qx > 0.0f ? qx : 0.0f;
	const float ay = qy > 0.0f ? qy : 0.0f;

	const float outside = sqrtf(ax * ax + ay * ay);
	const float inside = (qx > qy ? qx : qy); // negative when inside

	return outside + (inside < 0.0f ? inside : 0.0f) - r;
}

// Coverage of a filled shape from its signed distance: 255 well inside, ramping
// to 0 across the single pixel that straddles the boundary.
//
// The half-pixel offset puts 50% on the boundary itself, which is what makes the
// edge look smooth rather than merely stepped.
int FillCoverage(float dist)
{
	const float cov = 0.5f - dist;

	if (cov <= 0.0f)
		return 0;
	if (cov >= 1.0f)
		return 255;

	return static_cast<int>(cov * 255.0f);
}

// Coverage for a rounded stroke band centred on the boundary: 255 within
// thickness/2 of it, falling off over one pixel either side.
int StrokeCoverage(float dist, float thickness)
{
	const float half = thickness * 0.5f;
	const float d = fabsf(dist) - half;

	// 1px linear ramp centred on the band's outer edge.
	const float cov = 0.5f - d;
	if (cov <= 0.0f)
		return 0;
	if (cov >= 1.0f)
		return 255;

	return static_cast<int>(cov * 255.0f);
}

} // namespace

namespace paint
{

void SetLibrary(FT_Library lib)
{
	g_ftLib = lib;
}

void ShutdownFaces()
{
	for (int i = 0; i < kMaxFaces; ++i)
	{
		if (g_faces[i].face)
			FT_Done_Face(g_faces[i].face);
		g_faces[i].face = NULL;
		g_faces[i].path = NULL;
		g_faces[i].size = 0;
		g_faces[i].weight = 0.0f;
	}
}

FT_Face Face(const char *path, int size, float weight)
{
	if (!g_ftLib || !path)
		return NULL;

	for (int i = 0; i < kMaxFaces; ++i)
	{
		if (g_faces[i].face && g_faces[i].size == size &&
		    g_faces[i].weight == weight && strcmp(g_faces[i].path, path) == 0)
			return g_faces[i].face;
	}

	FT_Face face = NULL;
	if (FT_New_Face(g_ftLib, path, 0, &face) != 0)
		return NULL;

	if (FT_Set_Pixel_Sizes(face, 0, size) != 0)
	{
		FT_Done_Face(face);
		return NULL;
	}

	// Select a weight on a variable font. Segoe UI Variable's default instance is
	// Regular (wght 400) and the mockup's headings are heavy, so without this
	// every heading renders far too light.
	//
	// The axis ranges come from the font file rather than FreeType, because this
	// build's FT_MM_Var cannot report them -- see freetype_mm.h.
	if (weight > 0.0f && FT_HAS_MULTIPLE_MASTERS(face))
	{
		// Cached: the same font at different weights asks the same question.
		static const char *cachedPath = NULL;
		static fvarx::Axes cachedAxes;
		static int cachedWeightAxis = -2; // -2 means "not looked up yet"

		if (cachedWeightAxis == -2 || cachedPath != path)
		{
			if (fvarx::ReadAxes(path, cachedAxes))
				cachedWeightAxis = fvarx::WeightAxis(cachedAxes);
			else
				cachedWeightAxis = -1;

			cachedPath = path;
		}

		if (cachedWeightAxis >= 0)
		{
			FT_Fixed coords[fvarx::kMaxAxes];

			// Every axis at its default, then the weight axis moved.
			for (int i = 0; i < cachedAxes.count; ++i)
				coords[i] = cachedAxes.def[i];

			FT_Fixed want = static_cast<FT_Fixed>(weight * 65536.0);
			const FT_Fixed lo = cachedAxes.min[cachedWeightAxis];
			const FT_Fixed hi = cachedAxes.max[cachedWeightAxis];

			if (want < lo)
				want = lo;
			if (want > hi)
				want = hi;

			coords[cachedWeightAxis] = want;

			// Failure is not fatal: the default instance still draws, just at the
			// wrong weight.
			if (FT_Set_Var_Design_Coordinates(face,
			                                  static_cast<FT_UInt>(cachedAxes.count),
			                                  coords) != 0)
			{
				SDL_Log("paint: variable weight %d rejected", (int)weight);
			}
		}
	}

	for (int i = 0; i < kMaxFaces; ++i)
	{
		if (!g_faces[i].face)
		{
			g_faces[i].path = path;
			g_faces[i].size = size;
			g_faces[i].weight = weight;
			g_faces[i].face = face;
			return face;
		}
	}

	// Cache full. Leak the face rather than fail a draw; this cannot happen with
	// the current font set, but a silent NULL would be worse.
	SDL_Log("paint: face cache full, loading '%s' uncached", path);
	return face;
}

// ---- shapes ---------------------------------------------------------------

void Clear(SDL_Surface *dst, Color c)
{
	if (!dst)
		return;

	const Layout l = MakeLayout(dst);
	if (!l.ok)
	{
		// Reported once. Falling through to SDL_MapRGBA matters more than the
		// log line: a surface that cannot be cleared by the fast path would
		// otherwise keep whatever was in its memory, and an unwritten 1080p
		// buffer shows up as a screen of coloured garbage.
		static bool warned = false;
		if (!warned)
		{
			warned = true;
			SDL_Log("paint: unsupported pixel layout (r=%u g=%u b=%u a=%u), "
			        "clearing via SDL_MapRGBA",
			        dst->format->Rmask, dst->format->Gmask, dst->format->Bmask,
			        dst->format->Amask);
		}

		const uint32_t packed = SDL_MapRGBA(dst->format, c.r, c.g, c.b, 255);

		for (int y = 0; y < dst->h; ++y)
		{
			uint32_t *row = reinterpret_cast<uint32_t *>(
				static_cast<uint8_t *>(dst->pixels) + y * dst->pitch);

			for (int x = 0; x < dst->w; ++x)
				row[x] = packed;
		}

		return;
	}

	const uint32_t packed = Pack(l, c.r, c.g, c.b, 255);

	for (int y = 0; y < dst->h; ++y)
	{
		uint32_t *row = reinterpret_cast<uint32_t *>(
			static_cast<uint8_t *>(dst->pixels) + y * dst->pitch);

		for (int x = 0; x < dst->w; ++x)
			row[x] = packed;
	}
}

void FillRect(SDL_Surface *dst, int x, int y, int w, int h, Color c, uint8_t alpha)
{
	if (!dst)
		return;

	if (!ClipRect(dst, x, y, w, h))
		return;

	// Opaque fills are the common case and cover whole screens, so the colour is
	// converted once through SDL rather than by hand. SDL knows the surface's
	// channel layout; that knowledge is exactly what is easy to get wrong here,
	// and the toolchain's own sample hardcodes a layout instead of asking.
	if (alpha >= 255)
	{
		const uint32_t encoded = SDL_MapRGBA(dst->format, c.r, c.g, c.b, 255);

		for (int py = y; py < y + h; ++py)
		{
			uint32_t *row = reinterpret_cast<uint32_t *>(
				static_cast<uint8_t *>(dst->pixels) + py * dst->pitch) + x;

			for (int px = 0; px < w; ++px)
				row[px] = encoded;
		}

		return;
	}

	const Layout l = MakeLayout(dst);
	if (!l.ok)
		return;

	for (int py = y; py < y + h; ++py)
	{
		uint32_t *row = reinterpret_cast<uint32_t *>(
			static_cast<uint8_t *>(dst->pixels) + py * dst->pitch) + x;

		for (int px = 0; px < w; ++px)
		{
			uint8_t dr, dg, db;
			Unpack(l, row[px], dr, dg, db);

			BlendRGB(dr, dg, db, c.r, c.g, c.b, alpha);

			row[px] = Pack(l, dr, dg, db, 255);
		}
	}
}

void FillRounded(SDL_Surface *dst, int x, int y, int w, int h, int radius, Color c,
                 uint8_t alpha)
{
	if (!dst)
		return;

	const Layout l = MakeLayout(dst);
	if (!l.ok)
		return;

	// Expand by 1px so the anti-aliased edge is not clipped away.
	int x0 = x - 1, y0 = y - 1, w0 = w + 2, h0 = h + 2;
	if (!ClipRect(dst, x0, y0, w0, h0))
		return;

	const int maxRadius = (w < h ? w : h) / 2;
	if (radius > maxRadius)
		radius = maxRadius;
	if (radius < 0)
		radius = 0;

	// Rasterised in float; the pixel centre is offset by half.
	const float cx = x + w * 0.5f;
	const float cy = y + h * 0.5f;
	const float halfW = w * 0.5f;
	const float halfH = h * 0.5f;
	const float r = radius;

	// Rows that cannot be near a corner are filled without any per-pixel maths.
	const int cornerTop = y + radius;
	const int cornerBottom = y + h - radius;

	for (int py = y0; py < y0 + h0; ++py)
	{
		const bool inCornerBand = (py < cornerTop || py >= cornerBottom);

		if (!inCornerBand && radius > 0)
		{
			// Solid interior row.
			FillRect(dst, x0, py, w0, 1, c, alpha);
			continue;
		}

		for (int px = x0; px < x0 + w0; ++px)
		{
			const float dist = RoundedCoverage(px + 0.5f, py + 0.5f, cx, cy, halfW,
			                                  halfH, r);
			const int cov = FillCoverage(dist);

			if (cov <= 0)
				continue;

			const int a = (alpha * cov) / 255;
			BlendPixel(dst, l, px, py, c.r, c.g, c.b, a);
		}
	}
}

void StrokeRounded(SDL_Surface *dst, int x, int y, int w, int h, int radius,
                   int thickness, Color c, uint8_t alpha)
{
	if (!dst || thickness <= 0)
		return;

	const Layout l = MakeLayout(dst);
	if (!l.ok)
		return;

	int x0 = x - thickness, y0 = y - thickness;
	int w0 = w + thickness * 2, h0 = h + thickness * 2;
	if (!ClipRect(dst, x0, y0, w0, h0))
		return;

	const int maxRadius = (w < h ? w : h) / 2;
	if (radius > maxRadius)
		radius = maxRadius;
	if (radius < 0)
		radius = 0;

	// Inset by half the thickness so the band straddles the requested edge.
	const float cx = x + w * 0.5f;
	const float cy = y + h * 0.5f;
	const float halfW = w * 0.5f - thickness * 0.5f;
	const float halfH = h * 0.5f - thickness * 0.5f;
	const float r = radius;

	for (int py = y0; py < y0 + h0; ++py)
	{
		for (int px = x0; px < x0 + w0; ++px)
		{
			const float dist = RoundedCoverage(px + 0.5f, py + 0.5f, cx, cy, halfW,
			                                  halfH, r);
			const int cov = StrokeCoverage(dist, thickness);

			if (cov <= 0)
				continue;

			const int a = (alpha * cov) / 255;
			BlendPixel(dst, l, px, py, c.r, c.g, c.b, a);
		}
	}
}

void StrokeCircle(SDL_Surface *dst, int cx, int cy, int radius, int thickness,
                  Color c, uint8_t alpha)
{
	if (!dst || thickness <= 0 || radius <= 0)
		return;

	const Layout l = MakeLayout(dst);
	if (!l.ok)
		return;

	int x0 = cx - radius - thickness, y0 = cy - radius - thickness;
	int w0 = radius * 2 + thickness * 2, h0 = thickness * 2 + radius * 2;
	if (!ClipRect(dst, x0, y0, w0, h0))
		return;

	const float fcx = cx, fcy = cy;
	const float fr = radius;
	const float ft = thickness;

	for (int py = y0; py < y0 + h0; ++py)
	{
		for (int px = x0; px < x0 + w0; ++px)
		{
			const float dx = px + 0.5f - fcx;
			const float dy = py + 0.5f - fcy;
			const float d = sqrtf(dx * dx + dy * dy) - fr;

			const int cov = StrokeCoverage(d, ft);
			if (cov <= 0)
				continue;

			const int a = (alpha * cov) / 255;
			BlendPixel(dst, l, px, py, c.r, c.g, c.b, a);
		}
	}
}

void FillRoundedGradientV(SDL_Surface *dst, int x, int y, int w, int h,
                          int radius, Color top, Color bottom, uint8_t alpha)
{
	if (!dst || h <= 0)
		return;

	const Layout l = MakeLayout(dst);
	if (!l.ok)
		return;

	int maxRadius = (w < h ? w : h) / 2;
	if (radius > maxRadius)
		radius = maxRadius;
	if (radius < 0)
		radius = 0;

	// Geometry of the whole shape.
	//
	// This loop used to call FillRounded once per scanline, with h = 1. That
	// handed the helper a one-pixel-tall box, and FillRounded clamps its radius to
	// min(w, h) / 2 -- which for h = 1 is zero. Every corner row was therefore
	// drawn with the radius already stripped out, so the fill came out square
	// while the outline drawn over it stayed round.
	//
	// The shape is measured here instead, once, against the real rectangle.
	const float cx = x + w * 0.5f;
	const float cy = y + h * 0.5f;
	const float halfW = w * 0.5f;
	const float halfH = h * 0.5f;
	const float r = radius;

	// Rows that cannot be near a corner skip the per-pixel maths, same as
	// FillRounded's own interior fast path.
	const int cornerTop = y + radius;
	const int cornerBottom = y + h - radius;

	for (int row = 0; row < h; ++row)
	{
		const int py = y + row;

		const int t = h > 1 ? row * 255 / (h - 1) : 0;
		const Color c = {
			static_cast<uint8_t>((top.r * (255 - t) + bottom.r * t) / 255),
			static_cast<uint8_t>((top.g * (255 - t) + bottom.g * t) / 255),
			static_cast<uint8_t>((top.b * (255 - t) + bottom.b * t) / 255)
		};

		const bool inCornerBand = (py < cornerTop || py >= cornerBottom);

		if (!inCornerBand && radius > 0)
		{
			FillRect(dst, x, py, w, 1, c, alpha);
			continue;
		}

		for (int px = x; px < x + w; ++px)
		{
			const float dist = RoundedCoverage(px + 0.5f, py + 0.5f, cx, cy,
			                                  halfW, halfH, r);
			const int cov = FillCoverage(dist);

			if (cov <= 0)
				continue;

			const int a = (alpha * cov) / 255;
			BlendPixel(dst, l, px, py, c.r, c.g, c.b, a);
		}
	}
}

void Toggle(SDL_Surface *dst, int x, int y, int w, int h, bool on,
            Color trackOn, Color trackOff, Color knobOn, Color knobOff)
{
	if (w <= 0 || h <= 0)
		return;

	const Color track = on ? trackOn : trackOff;
	const Color knob = on ? knobOn : knobOff;

	// The knob is inset from the track edge and tracks its full diameter, so at
	// both extremes it stays entirely inside the track.
	const int pad = h / 6;
	const int knobSize = h - pad * 2;
	const int travel = w - knobSize - pad * 2;

	if (knobSize < 2)
		return;

	FillRounded(dst, x, y, w, h, h / 2, track, 255);

	const int knobX = x + pad + (on ? travel : 0);
	const int knobY = y + pad;

	// A soft ring under the knob, matching the halo on a selected menu card.
	StrokeRounded(dst, knobX - 2, knobY - 2, knobSize + 4, knobSize + 4,
	              (knobSize + 4) / 2, 2, knob, 70);
	FillRounded(dst, knobX, knobY, knobSize, knobSize, knobSize / 2, knob, 255);
}

void FillTriangleDown(SDL_Surface *dst, int x, int y, int w, int h, Color c,
                      uint8_t alpha)
{
	if (!dst || w <= 0 || h <= 0)
		return;

	const Layout l = MakeLayout(dst);
	if (!l.ok)
		return;

	for (int row = 0; row < h; ++row)
	{
		// Width shrinks linearly from full to zero.
		const int inset = (h > 1) ? (row * (w / 2)) / (h - 1) : w / 2;
		const int rowW = w - inset * 2;
		if (rowW <= 0)
			continue;

		for (int px = x + inset; px < x + inset + rowW; ++px)
			BlendPixel(dst, l, px, y + row, c.r, c.g, c.b, alpha);
	}
}

void RadialGlow(SDL_Surface *dst, int cx, int cy, int radius, Color c,
                float strength)
{
	if (!dst || radius <= 0 || strength <= 0.0f)
		return;

	// Evaluated small and scaled up. 96px is enough for a smooth falloff, and
	// this keeps the expensive loop to ~9k pixels instead of a few million.
	const int small = 96;
	const float scale = static_cast<float>(radius * 2) / small;

	// ARGB8888: the falloff lives in the alpha channel, which is exactly what the
	// scale blit premultiplies against.
	SDL_Surface *tmp = SDL_CreateRGBSurfaceWithFormat(0, small, small, 32,
	                                                 SDL_PIXELFORMAT_ARGB8888);
	if (!tmp)
		return;

	const Layout l = MakeLayout(tmp);
	if (l.ok)
	{
		for (int py = 0; py < small; ++py)
		{
			uint32_t *row = reinterpret_cast<uint32_t *>(
				static_cast<uint8_t *>(tmp->pixels) + py * tmp->pitch);

			for (int px = 0; px < small; ++px)
			{
				const float dx = (px + 0.5f - small * 0.5f) * scale;
				const float dy = (py + 0.5f - small * 0.5f) * scale;
				const float d = sqrtf(dx * dx + dy * dy);

				if (d >= radius)
				{
					row[px] = Pack(l, 0, 0, 0, 0);
					continue;
				}

				// Smooth falloff to the edge, squared so the core stays tight.
				float f = 1.0f - (d / radius);
				f = f * f * strength;

				int a = static_cast<int>(f * 255.0f);
				if (a < 0)
					a = 0;
				if (a > 255)
					a = 255;

				row[px] = Pack(l, c.r, c.g, c.b, static_cast<uint8_t>(a));
			}
		}

		BlitScaled(dst, tmp, cx - radius, cy - radius, radius * 2, radius * 2, 255);
	}

	SDL_FreeSurface(tmp);
}

bool BlitOpaque(SDL_Surface *dst, SDL_Surface *src, int x, int y)
{
	if (!dst || !src)
		return false;

	SDL_Rect where;
	where.x = x;
	where.y = y;
	where.w = src->w;
	where.h = src->h;

	// No alpha: these are cached, fully opaque surfaces. SDL_BlitSurface handles
	// any format difference, which is exactly the part that is easy to get wrong
	// by hand on this platform.
	return SDL_BlitSurface(src, NULL, dst, &where) == 0;
}

bool BlitAlpha(SDL_Surface *dst, SDL_Surface *src, int x, int y)
{
	if (!dst || !src)
		return false;

	BlitScaled(dst, src, x, y, src->w, src->h, 255);
	return true;
}

SDL_Surface *Tint(SDL_Surface *src, Color c)
{
	if (!src)
		return NULL;

	SDL_Surface *out = SDL_CreateRGBSurfaceWithFormat(0, src->w, src->h, 32,
	                                                  src->format->format);
	if (!out)
		return NULL;

	const Layout ls = MakeLayout(src);
	const Layout ld = MakeLayout(out);

	// Nothing sensible to do without knowing the layouts, and a surface with no
	// alpha channel has no mask to preserve.
	if (!ls.ok || !ld.ok || !ls.aMask)
	{
		SDL_FreeSurface(out);
		return NULL;
	}

	for (int y = 0; y < src->h; ++y)
	{
		uint8_t *rs = static_cast<uint8_t *>(src->pixels) + y * src->pitch;
		uint8_t *rd = static_cast<uint8_t *>(out->pixels) + y * out->pitch;

		for (int x = 0; x < src->w; ++x)
		{
			const uint32_t sp = *reinterpret_cast<uint32_t *>(rs + x * 4);
			const uint8_t a = static_cast<uint8_t>((sp & ls.aMask) >> ls.aShift);

			// The colour is replaced outright rather than blended: the glyph's
			// own RGB is white in the file and carries no information. Keeping it
			// would tint a white outline slightly off-hue wherever alpha is
			// partial, which is exactly where the edge pixels are.
			*reinterpret_cast<uint32_t *>(rd + x * 4) = Pack(ld, c.r, c.g, c.b, a);
		}
	}

	return out;
}

void BlitScaled(SDL_Surface *dst, SDL_Surface *src, int x, int y, int w, int h,
                uint8_t alpha)
{
	if (!dst || !src || w <= 0 || h <= 0)
		return;

	const Layout ld = MakeLayout(dst);
	if (!ld.ok)
		return;

	// Source is read as ARGB8888, so both formats work; only the 8-bit-alpha case
	// is meaningful for artwork, and anything else is treated as opaque.
	const bool srcHasAlpha = (src->format->format == SDL_PIXELFORMAT_ARGB8888) ||
	                         (src->format->format == SDL_PIXELFORMAT_RGBA8888);

	// ---- fast path: 1:1 and opaque ------------------------------------------
	//
	// This is the overwhelmingly common case -- a cached background or card
	// surface drawn at its native size -- and it covers the whole screen. A
	// memcpy per row instead of a blend per pixel is the difference between a
	// few hundred microseconds and a few hundred milliseconds here.
	//
	// The cached surfaces are created without an alpha channel, so this path
	// applies to them. Comparing masks rather than the enum matters: two
	// differently-numbered formats can still have identical channel layout, and
	// SDL_PIXELFORMAT_ARGB8888 is a different enum value from
	// SDL_PIXELFORMAT_RGBA32 merely because of how the bytes are named.
	const bool oneToOne = (w == src->w && h == src->h);
	const bool sameLayout = (src->format->Rmask == dst->format->Rmask) &&
	                        (src->format->Gmask == dst->format->Gmask) &&
	                        (src->format->Bmask == dst->format->Bmask) &&
	                        (src->format->Amask == dst->format->Amask);

	if (oneToOne && alpha >= 255 && !srcHasAlpha && sameLayout)
	{
		int cx = x, cy = y, cw = w, ch = h;

		if (!ClipRect(dst, cx, cy, cw, ch))
			return;

		for (int row = 0; row < ch; ++row)
		{
			uint8_t *d = static_cast<uint8_t *>(dst->pixels) +
			             (cy + row) * dst->pitch + cx * 4;
			const uint8_t *s = static_cast<const uint8_t *>(src->pixels) +
			                   row * src->pitch;

			memcpy(d, s, static_cast<size_t>(cw) * 4);
		}

		return;
	}

	// ---- general path --------------------------------------------------------
	//
	// All 32-bit: src and dst dimensions are at most a few thousand, so the
	// products below stay well inside int and the 64-bit divides that used to be
	// here were pure cost.
	for (int dy = 0; dy < h; ++dy)
	{
		const int py = y + dy;
		if (py < 0 || py >= dst->h)
			continue;

		const int sy0 = dy * src->h / h;
		int sy1 = (dy + 1) * src->h / h;
		if (sy1 <= sy0)
			sy1 = sy0 + 1;
		if (sy1 > src->h)
			sy1 = src->h;

		// One row in, one row out. The x bounds advance with the same integer step
		// as the source position, so neither needs recomputing per pixel.
		const int stepX = src->w / w;
		const int remX = src->w % w;

		int sx0 = 0;
		int accum = 0;

		uint32_t *drow = reinterpret_cast<uint32_t *>(
			static_cast<uint8_t *>(dst->pixels) + py * dst->pitch);

		for (int dx = 0; dx < w; ++dx)
		{
			int sx1 = sx0 + stepX + (accum >= w ? 1 : 0);
			accum += remX;

			if (sx1 <= sx0)
				sx1 = sx0 + 1;
			if (sx1 > src->w)
				sx1 = src->w;

			const int px = x + dx;

			if (px >= 0 && px < dst->w)
			{
				unsigned ar = 0, ag = 0, ab = 0, aa = 0;
				unsigned count = 0;

				for (int sy = sy0; sy < sy1; ++sy)
				{
					const uint32_t *srow = reinterpret_cast<const uint32_t *>(
						static_cast<const uint8_t *>(src->pixels) + sy * src->pitch);

					for (int sx = sx0; sx < sx1; ++sx)
					{
						const uint32_t sp = srow[sx];
						const uint8_t sr = static_cast<uint8_t>((sp >> 16) & 0xFF);
						const uint8_t sg = static_cast<uint8_t>((sp >> 8) & 0xFF);
						const uint8_t sb = static_cast<uint8_t>(sp & 0xFF);
						const uint8_t sa = srcHasAlpha
							? static_cast<uint8_t>((sp >> 24) & 0xFF) : 255;

						// Premultiply, so averaging then un-premultiplying does not
						// bias the colour of partially transparent samples.
						ar += sr * sa;
						ag += sg * sa;
						ab += sb * sa;
						aa += sa;
						++count;
					}
				}

				if (count && aa)
				{
					const int cov = static_cast<int>((aa * alpha) / count);

					if (cov)
					{
						uint8_t dr, dg, db;
						Unpack(ld, drow[px], dr, dg, db);

						BlendRGB(dr, dg, db,
						         static_cast<uint8_t>(ar / aa),
						         static_cast<uint8_t>(ag / aa),
						         static_cast<uint8_t>(ab / aa), cov);

						drow[px] = Pack(ld, dr, dg, db, 255);
					}
				}
			}

			sx0 = sx1;
		}
	}
}

// ---- text -----------------------------------------------------------------

int TextWidth(const std::string &text, FT_Face face)
{
	if (!face || text.empty())
		return 0;

	const bool hasKern = FT_HAS_KERNING(face) != 0;
	int advance = 0;
	FT_UInt previous = 0;

	for (size_t n = 0; n < text.size(); ++n)
	{
		const FT_UInt index = FT_Get_Char_Index(face,
		                                       static_cast<unsigned char>(text[n]));

		if (hasKern && previous && index)
		{
			FT_Vector kern;
			if (FT_Get_Kerning(face, previous, index, FT_KERNING_DEFAULT, &kern) == 0)
				advance += kern.x >> 6;
		}

		if (FT_Load_Glyph(face, index, FT_LOAD_DEFAULT) != 0)
			continue;

		advance += face->glyph->advance.x >> 6;
		previous = index;
	}

	return advance;
}

void Text(SDL_Surface *dst, int x, int baseline, const std::string &text,
          FT_Face face, Color c, uint8_t alpha)
{
	if (!dst || !face || text.empty())
		return;

	const Layout l = MakeLayout(dst);
	if (!l.ok)
		return;

	const bool hasKern = FT_HAS_KERNING(face) != 0;
	int advance = 0;
	FT_UInt previous = 0;

	for (size_t n = 0; n < text.size(); ++n)
	{
		const FT_UInt index = FT_Get_Char_Index(face,
		                                       static_cast<unsigned char>(text[n]));

		if (hasKern && previous && index)
		{
			FT_Vector kern;
			if (FT_Get_Kerning(face, previous, index, FT_KERNING_DEFAULT, &kern) == 0)
				advance += kern.x >> 6;
		}

		if (FT_Load_Glyph(face, index, FT_LOAD_DEFAULT) != 0)
		{
			previous = index;
			continue;
		}

		if (FT_Render_Glyph(face->glyph, ft_render_mode_normal) != 0)
		{
			previous = index;
			continue;
		}

		FT_GlyphSlot slot = face->glyph;
		const int left = slot->bitmap_left;
		const int top = slot->bitmap_top;
		const int rows = static_cast<int>(slot->bitmap.rows);
		const int cols = static_cast<int>(slot->bitmap.width);
		const int pitch = static_cast<int>(slot->bitmap.pitch);

		for (int gy = 0; gy < rows; ++gy)
		{
			const int py = baseline + gy - top;
			if (py < 0 || py >= dst->h)
				continue;

			const uint8_t *bits = slot->bitmap.buffer + gy * pitch;

			for (int gx = 0; gx < cols; ++gx)
			{
				int coverage = bits[gx];
				if (!coverage)
					continue;

				coverage = (coverage * alpha) / 255;
				if (coverage <= 0)
					continue;

				BlendPixel(dst, l, x + gx + advance + left, py, c.r, c.g, c.b,
				           coverage);
			}
		}

		advance += slot->advance.x >> 6;
		previous = index;
	}
}

int TextTracked(SDL_Surface *dst, int x, int baseline, const std::string &text,
                FT_Face face, int tracking, Color c, uint8_t alpha)
{
	if (!face || text.empty())
		return 0;

	// Kerning is deliberately not applied: tracking and kerning are both
	// corrections to the same thing, and for a two or three letter label the
	// result is easier to reason about with tracking alone.
	for (size_t n = 0; n < text.size(); ++n)
	{
		const std::string one(1, text[n]);
		Text(dst, x, baseline, one, face, c, alpha);
		x += TextWidth(one, face) + tracking;
	}

	// The trailing gap after the final glyph is not part of the visible width.
	return x - tracking;
}

} // namespace paint