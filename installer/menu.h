/* menu.h - the home screen.
 *
 * Three selectable cards on a dark background with two soft glows behind them,
 * matching the supplied mockup. Everything is drawn with the CPU via paint.h;
 * there is no GPU on this platform, so the page is split into:
 *
 *   - a static background (fill + glows), rendered once into its own surface
 *   - per-card surfaces, rebuilt only when a card's selected state changes
 *   - a per-frame blit of the cached surfaces, plus the footer
 *
 * That matters at 1080p: re-rasterising the glows every frame would cost a few
 * million blended pixels for an image that never changes.
 */
#ifndef INSTALLER_MENU_H
#define INSTALLER_MENU_H

#include <string>

#include <SDL2/SDL.h>
#include <proto-include.h> // FT_Library

struct Color;

// Supplied button artwork, trimmed to its opaque bounds when loaded.
struct Icon
{
	SDL_Surface *surface;
	int          w;
	int          h;
	bool         loaded;
};

// The three entries. Order matches the mockup left to right.
enum MenuCard
{
	kCardInstall = 0,
	kCardSettings,
	kCardUninstall,
	kCardCount
};

class Menu
{
public:
	Menu();
	~Menu();

	// `dst` is borrowed for the lifetime of the object -- in practice the window
	// surface, which Console also owns.
	bool Init(SDL_Surface *dst, FT_Library ftLib);

	// Draws the page with `selected` highlighted.
	//
	// Always paints, from the cached surfaces, and the caller must present every
	// frame. Two lessons from the toolchain's own SDL2 sample are baked in here:
	// the page is cheap because the background and cards are rasterised once and
	// blitted, not because drawing is skipped -- and skipping the present stalls
	// the displayed frame rate to whatever the user last interacted with.
	void Draw(int selected);

private:
	struct Card
	{
		const char *title;
		const char *body;
	};

	// Renders the background glows once.
	void BuildBackground();

	// Renders the selected card's glow into haloSurfaces_[i]. Kept off the card
	// surface deliberately -- see the comment in menu.cpp.
	void BuildHalo(int i);

	// Renders (or re-renders) one card into cardSurfaces_[i].
	void BuildCard(int i, bool selected);

	bool LoadFonts();

	SDL_Surface *dst_;
	FT_Library   ftLib_;

	SDL_Surface *background_;
	SDL_Surface *cardSurfaces_[kCardCount];
	SDL_Surface *haloSurfaces_[kCardCount];
	bool         cardIsSelected_[kCardCount];
	bool         backgroundReady_;
	bool         ready_;

	// Footer button art, trimmed to its opaque bounds on load.
	Icon selectIcon_;
	Icon backIcon_;
	Icon navigateIcon_;
	Icon logo_;

	// Cached faces, loaded once in LoadFonts().
	FT_Face titleFace_;   // card headings
	FT_Face bodyFace_;    // card descriptions
	FT_Face chromeFace_;  // small caps labels

	static const Card kCards_[kCardCount];
};

#endif