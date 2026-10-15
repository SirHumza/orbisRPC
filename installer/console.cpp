#include "console.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace {

const int kMargin  = 48;
const int kHeaderH = 76;
const int kFooterH = 56;
const int kRowGap  = 12;  // vertical slack around a row's centred line box
const int kIndent  = 20;  // selection indent, so the cursor reads as a marker

const Color kBackground = { 0, 0, 0 };
const Color kBar        = { 18, 22, 30 };
const Color kSelection  = {  26,  56, 104 };

} // namespace

Console::Console(SDL_Window *window, FT_Library ftLib,
                 const char *bodyFontPath, int bodyFontSize,
                 const char *titleFontPath, int titleFontSize,
                 int width, int height)
	: window_(window)
	, target_(NULL)
	, ftLib_(ftLib)
	, bodyFace_(NULL)
	, titleFace_(NULL)
	, width_(width)
	, height_(height)
	, contentX_(kMargin)
	, contentTop_(kMargin + kHeaderH)
	, rowHeight_(bodyFontSize + kRowGap)
	, rows_(0)
	, first_(0)
	, scrollback_(512)
	, selection_(-1)
	, selectionCols_(0)
	, charWidth_(1)
	, bodyColor_(Color{ 214, 222, 235 })
	, ready_(false)
	, hasCurrent_(false)
{
	scratch_.resize(1024);

	if (!window_)
	{
		SDL_Log("Console: no window");
		return;
	}

	target_ = SDL_GetWindowSurface(window_);
	if (!target_)
	{
		SDL_Log("Console: SDL_GetWindowSurface failed: %s", SDL_GetError());
		return;
	}

	if (!InitFace(&bodyFace_, bodyFontPath, bodyFontSize))
	{
		SDL_Log("Console: failed to load body font '%s'", bodyFontPath);
		return;
	}

	if (!InitFace(&titleFace_, titleFontPath, titleFontSize))
	{
		SDL_Log("Console: failed to load title font '%s'", titleFontPath);
		return;
	}

	// One character cell, so callers can turn column counts into pixels instead
	// of assuming an advance. max_advance is 26.6 fixed point.
	charWidth_ = bodyFace_->size->metrics.max_advance >> 6;
	if (charWidth_ < 1)
		charWidth_ = 1;

	// Rows start one margin below the header bar; each row's text is then
	// centred within its own band, so no extra per-row offset is needed.
	const int contentHeight = height_ - contentTop_ - kMargin - kFooterH;
	rows_ = contentHeight / rowHeight_;
	if (rows_ < 1)
		rows_ = 1;

	ready_ = true;
}

Console::~Console()
{
	if (bodyFace_)
		FT_Done_Face(bodyFace_);
	if (titleFace_)
		FT_Done_Face(titleFace_);
}

bool Console::InitFace(FT_Face *face, const char *path, int size)
{
	if (FT_New_Face(ftLib_, path, 0, face) != 0)
		return false;

	if (FT_Set_Pixel_Sizes(*face, 0, size) != 0)
	{
		FT_Done_Face(*face);
		*face = NULL;
		return false;
	}

	return true;
}

// ---- content buffer ---------------------------------------------------------

void Console::Reset()
{
	lines_.clear();
	current_ = Line();
	hasCurrent_ = false;
	first_ = 0;
}

void Console::Write(const std::string &text)
{
	WriteColored(text, bodyColor_);
}

void Console::WriteColored(const std::string &text, Color color)
{
	EndLine();
	WriteRun(text, color);
	EndLine();
}

void Console::WriteRun(const std::string &text, Color color)
{
	Run run;
	run.text = text;
	run.color = color;
	current_.runs.push_back(run);
	hasCurrent_ = true;
}

void Console::EndLine()
{
	if (!hasCurrent_)
	{
		// An empty line still has to occupy a row.
		WriteRun(std::string(), bodyColor_);
	}

	lines_.push_back(current_);
	current_ = Line();
	hasCurrent_ = false;

	// Trim the scrollback so a chatty screen cannot grow without bound.
	const int maxLines = rows_ + scrollback_;
	if (static_cast<int>(lines_.size()) > maxLines)
		lines_.erase(lines_.begin());

	// Keep the view pinned to the bottom.
	first_ = static_cast<int>(lines_.size()) - rows_;
	if (first_ < 0)
		first_ = 0;
}

// ---- painting ----------------------------------------------------------------

void Console::FillRect(int x, int y, int w, int h, Color color)
{
	if (!target_ || w <= 0 || h <= 0)
		return;

	// Clip to the surface.
	if (x < 0) { w += x; x = 0; }
	if (y < 0) { h += y; y = 0; }
	if (x + w > target_->w) w = target_->w - x;
	if (y + h > target_->h) h = target_->h - y;
	if (w <= 0 || h <= 0)
		return;

	// Encode once for the whole rectangle rather than per pixel.
	const uint32_t encoded = SDL_MapRGBA(target_->format, color.r, color.g, color.b, 255);

	for (int row = y; row < y + h; ++row)
	{
		uint32_t *dst = reinterpret_cast<uint32_t *>(
			static_cast<char *>(target_->pixels) + row * target_->pitch) + x;

		for (int col = 0; col < w; ++col)
			dst[col] = encoded;
	}
}

int Console::Blit(int x, int baseline, const std::string &text,
                  FT_Face face, Color fg, Color bg)
{
	if (!target_ || !face || text.empty())
		return 0;

	const size_t len = text.size();
	if (len + 1 > scratch_.size())
		scratch_.resize(len + 1);

	memcpy(scratch_.data(), text.c_str(), len);
	scratch_[len] = '\0';
	const char *txt = scratch_.data();

	// Anti-aliasing needs a blend of every coverage value between bg and fg.
	// Building a 256-entry palette up front keeps SDL_MapRGBA out of the
	// per-pixel path, which would otherwise run hundreds of thousands of
	// times a frame.
	uint32_t palette[256];
	for (int i = 0; i < 256; ++i)
	{
		const int inv = 255 - i;
		palette[i] = SDL_MapRGBA(target_->format,
			(i * fg.r + inv * bg.r) / 255,
			(i * fg.g + inv * bg.g) / 255,
			(i * fg.b + inv * bg.b) / 255,
			255);
	}

	FT_GlyphSlot slot = face->glyph;
	int advance = 0;

	for (size_t n = 0; n < len; ++n)
	{
		const FT_UInt glyphIndex = FT_Get_Char_Index(face, txt[n]);

		if (FT_Load_Glyph(face, glyphIndex, FT_LOAD_DEFAULT) != 0)
			continue;

		if (FT_Render_Glyph(slot, ft_render_mode_normal) != 0)
			continue;

		const int left = slot->bitmap_left;
		const int glyphTop = slot->bitmap_top;
		const int rows = static_cast<int>(slot->bitmap.rows);
		const int cols = static_cast<int>(slot->bitmap.width);

		for (int yPos = 0; yPos < rows; ++yPos)
		{
			// The glyph bitmap's own top offset moves us up to the baseline.
			const int py = baseline + yPos - glyphTop;
			if (py < 0 || py >= target_->h)
				continue;

			uint32_t *dst = reinterpret_cast<uint32_t *>(
				static_cast<char *>(target_->pixels) + py * target_->pitch);

			for (int xPos = 0; xPos < cols; ++xPos)
			{
				const int px = x + xPos + advance + left;
				if (px < 0 || px >= target_->w)
					continue;

				const int coverage = slot->bitmap.buffer[yPos * slot->bitmap.width + xPos];
				if (coverage == 0)
					continue;

				dst[px] = palette[coverage];
			}
		}

		advance += slot->advance.x >> 6;
	}

	return advance;
}

void Console::DrawRect(int x, int y, int w, int h, Color color)
{
	FillRect(x, y, w, h, color);
}

void Console::AddPanel(int x, int y, int w, int h, Color color)
{
	Panel panel;
	panel.x = x;
	panel.y = y;
	panel.w = w;
	panel.h = h;
	panel.color = color;

	panels_.push_back(panel);
}

int Console::DrawFixed(int x, int y, const std::string &text, Color color,
                       Color bg)
{
	if (!ready_ || !bodyFace_ || text.empty())
		return 0;

	// 26.6 fixed point. The caller gives the visual top, so shift down by the
	// ascent to land on the baseline that Blit() expects.
	const int ascent = bodyFace_->size->metrics.ascender >> 6;

	return Blit(x, y + ascent, text, bodyFace_, color, bg);
}

int Console::Measure(const std::string &text, FT_Face face)
{
	if (!face || text.empty())
		return 0;

	FT_GlyphSlot slot = face->glyph;
	int advance = 0;

	for (size_t n = 0; n < text.size(); ++n)
	{
		const FT_UInt glyphIndex = FT_Get_Char_Index(face, text[n]);

		if (FT_Load_Glyph(face, glyphIndex, FT_LOAD_DEFAULT) != 0)
			continue;

		advance += slot->advance.x >> 6;
	}

	return advance;
}

int Console::BaselineFor(FT_Face face, int bandTop, int bandHeight) const
{
	// FT metrics are 26.6 fixed point.
	const int lineHeight = face->size->metrics.height >> 6;
	const int ascent = face->size->metrics.ascender >> 6;

	return bandTop + (bandHeight - lineHeight) / 2 + ascent;
}

void Console::Render(const char *title, const char *footerHint,
                     Color accent, Color body, Color dim)
{
	if (!ready_)
		return;

	// Background, then the header/footer bars with an accent rule.
	FillRect(0, 0, width_, height_, kBackground);
	FillRect(0, 0, width_, kHeaderH, kBar);
	FillRect(0, kHeaderH - 3, width_, 3, accent);
	FillRect(0, height_ - kFooterH, width_, kFooterH, kBar);

	// Queued panels sit above the background but below all text.
	for (size_t i = 0; i < panels_.size(); ++i)
	{
		const Panel &panel = panels_[i];
		FillRect(panel.x, panel.y, panel.w, panel.h, panel.color);
	}

	// Panels are per-frame; clear so they must be re-queued each Render.
	panels_.clear();

	// Centre the title in the header bar and the hint in the footer bar.
	Blit(contentX_, BaselineFor(titleFace_, 0, kHeaderH),
		title ? title : "", titleFace_, accent, kBar);

	Blit(contentX_, BaselineFor(bodyFace_, height_ - kFooterH, kFooterH),
		footerHint ? footerHint : "", bodyFace_, dim, kBar);

	for (int row = 0; row < rows_; ++row)
	{
		const int index = first_ + row;
		if (index < 0 || index >= static_cast<int>(lines_.size()))
			break;

		const int y = contentTop_ + row * rowHeight_;
		const bool selected = (row == selection_);

		if (selected)
		{
			const int selWidth = selectionCols_ > 0
				? selectionCols_ * charWidth_
				: width_ - 2 * kMargin;

			FillRect(contentX_, y, selWidth, rowHeight_, kSelection);
		}

		const Color rowBg = selected ? kSelection : kBackground;
		int x = contentX_ + (selected ? kIndent : 0);
		const int baseline = BaselineFor(bodyFace_, y, rowHeight_);

		// Lay the line out run by run, so mixed colours sit side by side.
		for (size_t r = 0; r < lines_[index].runs.size(); ++r)
		{
			const Run &run = lines_[index].runs[r];
			x += Blit(x, baseline, run.text, bodyFace_, run.color, rowBg);
		}
	}

	(void)body; // per-run colours are authoritative for row text
}

void Console::Present()
{
	if (window_)
		SDL_UpdateWindowSurface(window_);
}
