/* freetype_mm.h - variable-font weight selection.
 *
 * The toolchain ships FreeType 2.5.1 with a trimmed freetype.h: the
 * FT_HAS_MULTIPLE_MASTERS flag is present, but everything from
 * FT_MULTIPLE_MASTERS_H -- FT_MM_Var, FT_Get_MM_Var and
 * FT_Set_Var_Design_Coordinates -- was left out. The symbols are exported by
 * libSceFreeType.so regardless, so FT_Set_Var_Design_Coordinates is declared here
 * and everything else is done without the MM API.
 *
 * The reason FT_Get_MM_Var is deliberately NOT used is that 2.5.1's version is
 * missing what is needed to map a weight onto an axis:
 *
 *   - FT_MM_Var has no per-axis min/max/tag fields (those arrive in 2.9+), so
 *     there is no way to find 'wght' or to know its range.
 *   - its `axis` values are normalised to 0..1, not design coordinates.
 *
 * Segoe UI Variable's wght axis runs 300..700 with a default of 400, so the range
 * has to come from the font itself. fvarx.h reads the fvar table directly for
 * exactly that.
 *
 * If a future toolchain ships the real declarations, this file and fvarx.h can be
 * deleted in favour of the stock FT_Set_Var_* calls.
 */
#ifndef PS4UTIL_FREETYPE_MM_H
#define PS4UTIL_FREETYPE_MM_H

#include <freetype/freetype.h>

#ifdef __cplusplus
extern "C" {
#endif

// 2.5.1 signature: a plain FT_Fixed array, no trailing reserved argument as in
// 2.9+. The call must match this exactly.
FT_Error FT_Set_Var_Design_Coordinates(FT_Face face, FT_UInt num_coords,
                                       FT_Fixed *coords);

#ifdef __cplusplus
}
#endif

#endif