/* bitmap.h - minimal 32-bit BMP reader.
 *
 * The only image loader this platform can rely on is BMP. SDL2_image here has no
 * decoders compiled in at all -- IMG_Init reports "PNG images are not supported",
 * and the toolchain ships no PNG decoder header either -- so SDL_LoadBMP_RW is
 * the sole way in.
 *
 * Rolling our own rather than using SDL_LoadBMP is a deliberate trade. BMP is
 * about forty lines in the configuration we actually produce (32bpp, BI_RGB,
 * uncompressed), and reading it directly means the alpha channel is guaranteed to
 * arrive as alpha instead of depending on how SDL decides to interpret the mask.
 * Get that wrong and the button artwork composites against its own stored RGB and
 * appears as invisible dark blobs on a dark footer.
 *
 * Anything outside that configuration is rejected rather than guessed at.
 */
#ifndef INSTALLER_BITMAP_H
#define INSTALLER_BITMAP_H

#include <SDL2/SDL.h>

namespace bitmap
{

// Loads a 32-bit uncompressed BMP from inside the package, e.g.
// "/app0/assets/gfx/select.bmp".
//
// Returns an ARGB8888 surface, or NULL if the file is missing or is not a BMP in
// the supported form. Never returns a surface without an alpha channel.
SDL_Surface *Load(const char *path);

} // namespace bitmap

#endif