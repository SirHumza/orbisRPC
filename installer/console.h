#ifndef PS4UTIL_CONSOLE_H
#define PS4UTIL_CONSOLE_H

#include <stdint.h>

#include <string>
#include <vector>

#include <SDL2/SDL.h>
#include <proto-include.h> // FT_Library / FT_Face

struct Color
{
	uint8_t r;
	uint8_t g;
	uint8_t b;
};

// A fixed-pitch on-screen text console drawn into an SDL surface.
//
// SDL owns presentation here: Render() paints straight into the window surface
// and main() calls SDL_UpdateWindowSurface(). There is deliberately no
// dirty-flag and no back-buffer bookkeeping, because SDL submits the surface
// itself.
//
// A line is a sequence of coloured runs rather than a single string, so a row
// can mix colours -- that is what makes a keypad cursor possible. Text is
// positioned by baseline, so every glyph in a run shares one origin.
//
// Pixels are encoded with SDL_MapRGBA() rather than a hardcoded bit pattern,
// so this does not depend on the PS4 surface's exact channel layout.
class Console
{
public:
	// Loads both faces itself; check Ready() afterwards. All paths are runtime
	// paths inside the package, e.g. "/app0/assets/fonts/VeraMono.ttf".
	Console(SDL_Window *window, FT_Library ftLib,
	        const char *bodyFontPath, int bodyFontSize,
	        const char *titleFontPath, int titleFontSize,
	        int width, int height);
	~Console();

	bool Ready() const { return ready_; }

	// Number of text rows that fit between the header and footer bars.
	int Rows() const { return rows_; }

	// Number of lines currently buffered, including those scrolled off.
	int LineCount() const { return static_cast<int>(lines_.size()); }

	// Colour used by Write() for whole lines.
	void SetBodyColor(Color color) { bodyColor_ = color; }

	// ---- content buffer ---------------------------------------------------

	// Discards all buffered lines and any partially-built line.
	void Reset();

	// Whole-line writers. Each completes the line immediately.
	void Write(const std::string &text);
	void WriteColored(const std::string &text, Color color);

	// Run writer, for laying out several differently-coloured pieces on one
	// line. Pair with EndLine().
	void WriteRun(const std::string &text, Color color);
	void EndLine();

	// ---- painting ---------------------------------------------------------

	// Row to highlight, or -1 for none. Applied by the next Render().
	void SetSelection(int row) { selection_ = row; }

	// Limits how wide the selection bar is, measured from contentX_. Use this on
	// a two-column page so the bar does not run under the right-hand column.
	// 0 or negative means the full content width.
	void SetSelectionWidth(int columns) { selectionCols_ = columns; }

	// Queues a filled rectangle to be painted by Render(), after the background
	// and bars but before any text. This is the only way to put a background
	// behind text, because Render() starts by clearing the whole surface.
	// Cleared at the start of every Render().
	void AddPanel(int x, int y, int w, int h, Color color);

	// Repaints the whole screen: background, bars, title, footer hint, every
	// visible row, and the selection highlight if one is set.
	void Render(const char *title, const char *footerHint,
	            Color accent, Color body, Color dim);

	// Pushes the surface to the display.
	void Present();

	// Paints a filled rectangle, clipped to the surface. Used for panel
	// backgrounds, dividers and selection bars.
	void DrawRect(int x, int y, int w, int h, Color color);

	// Draws text at an absolute screen position, outside the row grid, for
	// fixed UI elements that must NOT scroll with the content -- a side panel,
	// say. `y` is the top of the text and `bg` is what sits behind it, which
	// matters because text is anti-aliased against its background.
	// Call after Render(). Returns the advance width.
	int DrawFixed(int x, int y, const std::string &text, Color color, Color bg);

	// Width in pixels a string would occupy. Used to lay out runs.
	int Measure(const std::string &text, FT_Face face);

	// Width of one character cell in the body font. Use this to convert column
	// counts into pixel geometry instead of guessing.
	int CharWidth() const { return charWidth_; }

	// Vertical geometry of the content area, so a two-column page can align a
	// panel with the rows it sits beside.
	int ContentTop() const { return contentTop_; }
	int RowHeight() const { return rowHeight_; }

private:
	struct Run
	{
		std::string text;
		Color       color;
	};

	struct Line
	{
		std::vector<Run> runs;
	};

	struct Panel
	{
		int   x, y, w, h;
		Color color;
	};

	bool InitFace(FT_Face *face, const char *path, int size);
	void FillRect(int x, int y, int w, int h, Color color);

	// Glyphs are positioned by baseline, not by their top. FreeType reports each
	// glyph's own bitmap_top, so a per-glyph top offset would leave lowercase
	// letters hanging above the capitals. Working in baseline coordinates makes
	// every glyph in a run share one origin. Returns the advance width.
	int Blit(int x, int baseline, const std::string &text,
	         FT_Face face, Color fg, Color bg);

	// Baseline that vertically centres a face's line box inside the band
	// [bandTop, bandTop + bandHeight).
	int BaselineFor(FT_Face face, int bandTop, int bandHeight) const;

	SDL_Window *window_;
	SDL_Surface *target_;
	FT_Library   ftLib_;
	FT_Face      bodyFace_;
	FT_Face      titleFace_;

	int width_;
	int height_;
	int contentX_;
	int contentTop_;
	int rowHeight_;
	int rows_;
	int first_;
	int scrollback_;
	int selection_;
	int selectionCols_;
	int charWidth_;

	Color                    bodyColor_;
	bool                     ready_;
	bool                     hasCurrent_;
	Line                     current_;
	std::vector<Panel>       panels_;
	std::vector<Line>        lines_;
	std::vector<char>        scratch_;
};

#endif
