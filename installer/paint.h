/* paint.h - immediate-mode drawing primitives for the app shell.
 *
 * Everything here rasterises straight into an SDL surface with the CPU. There is
 * no GPU on this platform, so the primitives are built to be cheap enough to run
 * at 1080p: filled shapes skip anti-aliasing in their interior and only pay for
 * a square root in the corner regions.
 *
 * Why not SDL_Renderer? Because the Console writes glyphs into the same surface
 * and both paths would have to agree on pixel layout. Writing pixels directly,
 * the way Console already does, keeps one code path for everything.
 *
 * Shapes are alpha-blended rather than opaque-only, so panels and glows composite
 * the way CSS opacity would.
 */
#ifndef INSTALLER_PAINT_H
#define INSTALLER_PAINT_H

#include <stdint.h>

#include <string>

#include <SDL2/SDL.h>
#include <proto-include.h> // FT_Face

#include "console.h" // Color

namespace paint
{

// FreeType faces are cached by (path, pixel size) and shared between callers, so
// a heading at 34px and body text at 20px cost one face each for the process
// lifetime. Must be called once before any Face() call.
void SetLibrary(FT_Library lib);

// Returns a cached face, or NULL if the font cannot be opened or sized.
//
// `weight` is a CSS-style number on a variable font's wght axis (400 = regular,
// 700 = bold), or 0 to use the font's default instance. Ignored for fonts
// without a variable axis, and for any font that fails to answer, so it is safe
// to pass unconditionally.
FT_Face Face(const char *path, int size, float weight = 0.0f);

// Drops every cached face. Safe to call with no library set.
void ShutdownFaces();

// ---- shapes ---------------------------------------------------------------

// Opaque fill of the whole surface.
void Clear(SDL_Surface *dst, Color c);

// Alpha-blended rectangle.
void FillRect(SDL_Surface *dst, int x, int y, int w, int h, Color c,
              uint8_t alpha = 255);

// Alpha-blended filled rounded rectangle. `radius` is clamped to half the
// shorter side, so a large radius degrades to a stadium rather than breaking.
void FillRounded(SDL_Surface *dst, int x, int y, int w, int h, int radius, Color c,
                 uint8_t alpha = 255);

// Alpha-blended rounded outline, drawn inside the rectangle's edges.
void StrokeRounded(SDL_Surface *dst, int x, int y, int w, int h, int radius,
                   int thickness, Color c, uint8_t alpha = 255);

// Alpha-blendered rounded outline. Cheaper than StrokeRounded for large radii,
// and the only practical way to get a smooth circular glow ring.
void StrokeCircle(SDL_Surface *dst, int cx, int cy, int radius, int thickness,
                  Color c, uint8_t alpha = 255);

// Alpha-blended vertical gradient, optionally clipped to a rounded shape.
void FillRoundedGradientV(SDL_Surface *dst, int x, int y, int w, int h,
                          int radius, Color top, Color bottom,
                          uint8_t alpha = 255);

// A toggle switch: a rounded track with a knob, slid to one end.
//
// Both states are the same size, so a row's height does not change when it is
// toggled. `w` is the full width, `h` the full height, and the knob is inset so
// it sits inside the track at both extremes.
void Toggle(SDL_Surface *dst, int x, int y, int w, int h, bool on,
            Color trackOn, Color trackOff, Color knobOn, Color knobOff);

// Downward-pointing triangle, centred horizontally on x+w/2. Used for icons.
void FillTriangleDown(SDL_Surface *dst, int x, int y, int w, int h, Color c,
                      uint8_t alpha = 255);

// Soft radial light centred on (cx, cy), fading to nothing at `radius`.
//
// Evaluated at low resolution and scaled up with a box filter. The falloff is
// smooth, so the result is indistinguishable from per-pixel evaluation, and it
// turns a ~2M-pixel loop with a square root in it into a 96x96 one.
void RadialGlow(SDL_Surface *dst, int cx, int cy, int radius, Color c,
                float strength);

// Copies an opaque surface 1:1, at its native size.
//
// This is the path the cached background and cards use, and it is deliberately
// SDL_BlitSurface rather than hand-written pixel maths. SDL already knows how the
// console's surface is laid out, so this cannot get the channel order wrong, and
// it is a memcpy for a same-format blit -- which is what makes a full-screen copy
// affordable on a CPU rasteriser.
bool BlitOpaque(SDL_Surface *dst, SDL_Surface *src, int x, int y);

// Alpha-blended 1:1 blit, for artwork that carries transparency.
//
// Deliberately not SDL_BlitSurface: a plain blit ignores the source alpha unless
// the surface's blend mode was set, and this SDL predates
// SDL_SetSurfaceBlendMode. Composite by hand instead, so transparent pixels of a
// PNG actually become the background rather than their own stored RGB.
//
// At native size this costs one sample per destination pixel, which is nothing.
bool BlitAlpha(SDL_Surface *dst, SDL_Surface *src, int x, int y);

// Returns a new surface, same size as `src`, with every pixel's colour replaced by
// `c` and its alpha carried over. NULL on failure; the caller frees the result.
//
// So one artwork file can serve several purposes: the row glyphs are drawn once in
// white and tinted per state, rather than needing a separate copy per colour. The
// alternative is baking a colour into the file and either shipping duplicates or
// compositing the wrong hue when the state changes.
SDL_Surface *Tint(SDL_Surface *src, Color c);

// Alpha-blended box-filtered scale blit. Handles both enlargement and shrink.
//
// Slow by design: the inner loop walks every source sample under each destination
// pixel. Only use it off the frame path, for one-off setup like pre-scaling
// artwork to its draw size.
void BlitScaled(SDL_Surface *dst, SDL_Surface *src, int x, int y, int w, int h,
                uint8_t alpha = 255);

// ---- text -----------------------------------------------------------------

// Advance width of a string, kerning included.
int TextWidth(const std::string &text, FT_Face face);

// Draws text with its left edge at `x` and its baseline at `baseline`. Glyphs
// are blended, so this works over any background.
void Text(SDL_Surface *dst, int x, int baseline, const std::string &text,
          FT_Face face, Color c, uint8_t alpha = 255);

// Text with extra spacing inserted after every glyph, including the last.
//
// For the small letter-spaced section labels ("RPC", "FIXES") in the Settings
// design. Tracking is a typographic device, not something a font can express, so
// it has to be applied per glyph. Returns the total advance width.
int TextTracked(SDL_Surface *dst, int x, int baseline, const std::string &text,
                FT_Face face, int tracking, Color c, uint8_t alpha = 255);

} // namespace paint

#endif