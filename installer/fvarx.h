/* fvarx.h - variable-font axis ranges, read straight from the font file.
 *
 * Exists because this toolchain's FreeType (2.5.1) cannot report them:
 * FT_MM_Var has no per-axis tag/min/max fields and its values are normalised to
 * 0..1, so there is no way to ask it "what is the range of wght, and which axis
 * is wght?". Segoe UI Variable's wght axis runs 300..700, default 400, so the
 * range has to come from the font.
 *
 * Only the fvar table is parsed, and only its fixed header plus the axis records.
 * Everything is bounds-checked, and any malformed table returns false so the
 * caller falls back to the font's default instance.
 *
 * Axis values in fvar are 16.16 fixed, matching FT_Fixed, so they are used as-is.
 */
#ifndef PS4UTIL_FVARX_H
#define PS4UTIL_FVARX_H

#include <stdint.h>

#include <freetype/freetype.h>

namespace fvarx
{

// Upper bound on axes, so the fixed arrays below stay safe on a malformed file.
// Real fonts have one or two; the font here has exactly two.
const int kMaxAxes = 16;

struct Axes
{
	int     count;
	FT_Fixed min[kMaxAxes];
	FT_Fixed def[kMaxAxes];
	FT_Fixed max[kMaxAxes];
	FT_ULong tag[kMaxAxes]; // FT_MAKE_TAG of the four-character axis tag
};

// Reads the fvar table from the font file at `path`.
//
// Returns false if the file cannot be read, has no fvar table, or the table does
// not fit the checks -- in which case `out` is untouched.
bool ReadAxes(const char *path, Axes &out);

// Index of the standard weight axis ('wght'), or -1 if the font has none. -1 is
// the normal result for a static font.
int WeightAxis(const Axes &axes);

} // namespace fvarx

#endif