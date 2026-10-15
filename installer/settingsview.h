/* settingsview.h - the Settings screen.
 *
 * Two columns, per the supplied mockup:
 *
 *   left  - an RPC section of four toggle rows plus a custom message field, and
 *           a FIXES section holding one action row
 *   right - a preview panel with three tabs, showing fixture data only
 *
 * The preview is a mock. Its contents are hard-coded rather than queried, because
 * the daemon does not report the running title or an elapsed time yet. All of it
 * lives in kPreviews, so swapping in real data is a change to one table.
 *
 * Drawing follows the menu's approach: the left column and the preview panel are
 * each rasterised into their own surface and rebuilt only when their content
 * changes, then blitted per frame. At 1080p on a software rasteriser that is the
 * difference between a page that redraws in a few milliseconds and one that does
 * not.
 */
#ifndef INSTALLER_SETTINGSVIEW_H
#define INSTALLER_SETTINGSVIEW_H

#include <stdint.h>

#include <string>

#include <SDL2/SDL.h>
#include <proto-include.h> // FT_Library

#include "console.h" // Color
#include "settings.h"

// Settings' own artwork record. menu.h has an identical one; they are kept
// separate rather than shared because neither is part of a common abstraction --
// one is for the three cards' footer, the other for a settings page that will
// grow its own set.
struct GfxIcon
{
	SDL_Surface *surface;
	int          w;
	int          h;
	bool         loaded;
};

class SettingsView
{
public:
	SettingsView();
	~SettingsView();

	// `dst` is borrowed for the object's lifetime -- in practice the window
	// surface, which Console and Menu also write to.
	bool Init(SDL_Surface *dst, FT_Library ftLib);

	// Paints the page. `selected` indexes RowCount() rows; `tab` is 0..2. Both are
	// passed in rather than owned, so main.cpp stays the single place input state
	// lives.
	void Draw(const Settings &settings, int selected, int tab);

	// Rows the up/down cursor moves through, in display order. The FIXES action
	// row is one of them, so it is reachable like everything else.
	static int RowCount();
	static int RowSetting(int row);  // SettingId, or -1 for the action row
	static bool RowIsToggle(int row);
	static int TabCount();
	static const char *TabLabel(int tab);

	// True when the row is the FIXES action, which has no SettingId.
	static bool RowIsAction(int row);

	// Transient message shown under the page, e.g. after a save or when the
	// action row is pressed. Cleared by the caller; empty means nothing to show.
	// Part of the left column's cache key, so setting it rebuilds that column.
	void SetStatus(const char *text) { status_ = text ? text : ""; }

	struct Row
	{
		int         setting;   // SettingId, or -1 for the action row
		const char *title;
		const char *desc;      // may be NULL
		bool        twoLine;   // title wraps to two lines
	};

	struct PreviewCard
	{
		const char *icon;      // artwork basename, or NULL
		const char *heading;   // "Playing" / "Watching YouTube"
		const char *title;
		const char *subtitle;  // may be NULL
		const char *time;      // "0:10", or NULL to omit
		// True when the small PS badge overlaps the icon's bottom-right corner.
		// Cards whose icon *is* a PlayStation mark leave it off, since a badge on
		// top of the same logo reads as a mistake.
		bool        badge;
	};

	struct Preview
	{
		const char *tabLabel;
		const char *firmware;
		PreviewCard cards[3];
		int         cardCount;
	};

	// Public so main.cpp can drive the cursor and tab from the same table the page
	// draws from, rather than duplicating the list.
	//
	// The sizes are stated rather than left open so RowCount() and TabCount() can
	// be sizeof over the definition. Declaring them unsized was tried and is worse:
	// it makes the type incomplete, and sizeof stops compiling. Forgetting to bump
	// a size here produces "excess elements in array initializer" at the
	// definition, which is loud and points at the actual mistake.
	static const Row kRows[7];
	static const Preview kPreviews[3];

	FT_Face titleFace_;    // row titles, card headings
	FT_Face descFace_;     // row descriptions, subtitles
	FT_Face sectionFace_;  // "RPC" / "FIXES" labels
	FT_Face tabFace_;      // tab labels
	FT_Face smallFace_;    // firmware lines, timers, button labels

	// Artwork, loaded once. A null or unloaded entry means the caller draws a
	// placeholder instead.
	GfxIcon youtube_;
	GfxIcon naruto_;
	GfxIcon webBrowser_;
	GfxIcon psLogoBig_;
	GfxIcon psLogoSmall_;

	// Footer button art, the same files the menu uses.
	GfxIcon navigate_;
	GfxIcon select_;
	GfxIcon back_;

	// Shoulder buttons, inside the preview tab bar's Previous / Next pills.
	GfxIcon l1_;
	GfxIcon r1_;

	// The FIXES action-row glyph, pre-tinted per state. Tinted at load rather
	// than per draw because the rows surface is cached and rebuilt on state
	// changes, not per frame.
	SDL_Surface *actionIconSel_;
	SDL_Surface *actionIconIdle_;

	// This console's firmware as the card draws it -- the label and the number
	// together, "Firmware 13.52" -- shortened from the kernel's three-component
	// form by FormatFirmware. Queried once at Init: it cannot change while the
	// app is running, and the preview panel is cached and rebuilt often enough
	// that asking per frame would be silly.
	//
	// Empty when the query failed, in which case the panel falls back to the
	// preview table's own placeholder rather than drawing nothing.
	std::string  firmware_;

	// Cached surfaces, rebuilt only when their content changes.
	SDL_Surface *panel_;
	int          panelTab_;
	// Part of the panel's cache key alongside the tab: the panel is where the
	// firmware toggle is demonstrated, so it changes when that does.
	bool         panelFirmware_;
	SDL_Surface *rows_;
	std::string  rowsKey_;   // what rows_ was built from
	std::string  status_;    // transient message, may be empty

	SDL_Surface *dst_;
	bool         ready_;

	std::string BuildRowKey(const Settings &settings, int selected) const;
	SDL_Surface *BuildRows(const Settings &settings, int selected) const;
	SDL_Surface *BuildPanel(int tab, const Settings &settings) const;
	void         BuildTabBar(SDL_Surface *s, int tab) const;
	void         BuildPreviewCard(SDL_Surface *s, const PreviewCard &card, int x,
	                              int y, int w, const char *firmware) const;
	void         DrawFooter() const;

	const GfxIcon *FindArt(const char *name) const;
};

#endif