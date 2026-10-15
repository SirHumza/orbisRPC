#include "menu.h"

#include <stdlib.h>
#include <string.h>

#include "bitmap.h"
#include "console.h"
#include "dbglog.h"
#include "paint.h"

namespace {

// ---- palette --------------------------------------------------------------
//
// Sampled from the mockup. The accent is a saturated yellow; everything else is
// deliberately desaturated so the selected card is the only bright thing on
// screen.
const Color kBackground  = {  10,  11,  16 };
const Color kCardFill    = {  22,  24,  32 };
const Color kCardFillTop = {  27,  29,  38 };
const Color kBorderIdle  = {  48,  52,  66 };
const Color kBorderActive= { 250, 200,  40 };
const Color kIconBg      = {  38,  40,  52 };
const Color kIconBgActive= {  62,  54,  22 };
const Color kIconIdle    = {  92,  98, 116 };
const Color kIconActive  = { 250, 200,  40 };
const Color kTitleIdle   = { 108, 114, 130 };
const Color kTitleActive = { 245, 247, 250 };
const Color kBodyIdle    = {  78,  84,  98 };
const Color kBodyActive  = { 150, 157, 174 };
const Color kChevronIdle = {  74,  80,  94 };
const Color kChevronAct  = { 250, 200,  40 };
const Color kFooterText  = { 178, 184, 198 };

// ---- geometry (1920x1080) -------------------------------------------------
//
// The mockup is drawn at 1920x1080 and these values were read off it directly,
// so they map 1:1 rather than needing a scale factor.
const int kScreenW = 1920;
const int kScreenH = 1080;

const int kCardW      = 388;
const int kCardH      = 366;
const int kCardTop    = 267;
const int kCardGap    = 26;
const int kFirstCardX = 320;
const int kRadius     = 26;  // card corner radius

// How far the card's border sits inside the card's edge. The border's radius is
// kRadius minus this, so that its corners stay concentric with the fill's.
const int kCardBorderInset = 1;

// Border weight, selected and idle.
const int kCardBorderOn  = 3;
const int kCardBorderOff = 2;

// The selected card's halo. How far it reaches past the card, how thick the band
// is, and how faintly it is laid down.
const int kHaloOut       = 3;
const int kHaloThickness = 4;
const int kHaloAlpha     = 70;

// StrokeRounded places a stroke's corner arc at (inset + thickness/2 + radius),
// not at (inset + radius) -- it insets the half-extents by half the thickness so
// the band straddles the edge. Matching the fill's arc therefore needs:
//
//     radius = kRadius - inset - thickness / 2
//
// The thickness term is the one that was missed, and it is why adjusting for the
// inset alone moved the border a whole pixel in the wrong direction and the
// corner never improved. Both radii below come from that formula.
//
// Integer division drops the half pixel, which puts each band a fraction outward
// rather than inward. Inward would be the wrong way to be wrong: it would leave
// the hairline gap this is meant to close.
const int kCardBorderRadius = kRadius - kCardBorderInset - kCardBorderOn / 2;
const int kHaloRadius       = kRadius + kHaloOut - kHaloThickness / 2;
const int kIconSize   = 86;
const int kIconRadius = 20;
const int kIconX      = 42;  // card-relative
const int kIconY      = 45;

const int kTitleSize = 34;
const int kBodySize  = 22;
const int kChromeSize = 26;

// Segoe UI Variable, via its wght axis. The headings in the mockup are heavy, so
// they load at 700 and everything else at the 400 the font defaults to.
const char *const kUiFont   = "/app0/assets/fonts/SegUIVar.ttf";
const float       kWeightBold = 700.0f;
const float       kWeightBody = 400.0f;

const int kFooterH = 76;

// Button art is supplied as 480x480 PNGs with transparent padding; content is
// measured, not assumed, so the glyph sits centred in its box regardless of how
// much padding the source has.
// Artwork is BMP, because SDL2_image on this platform has no decoders compiled
// in at all -- IMG_Init reports "PNG images are not supported". These files are
// pre-scaled to the sizes below at build time, so a file's pixel size is the size
// it draws at and the blit is always 1:1.
const char *const kSelectIcon  = "/app0/assets/images/select.bmp";
const char *const kBackIcon    = "/app0/assets/images/back.bmp";
const char *const kNavigateIcon = "/app0/assets/images/navigate.bmp";

// App wordmark, top-left above the cards. The mockup leaves this band empty, so
// the placement is a judgement call rather than a measurement. Drawn at whatever
// size the file is, so the BMP's dimensions are the knob to adjust.
const char *const kLogoPath = "/app0/assets/images/logo.bmp";
const int kLogoX = 72;
const int kLogoY = 40;

const int kFooterPadX = 64;
const int kFooterBtnSize = 34;  // drawn size; sources are 480px
const int kFooterIconGap = 12;
const int kFooterGroupGap = 46;

int CardX(int i) { return kFirstCardX + i * (kCardW + kCardGap); }

} // namespace

// A 32-bit surface with a wasted byte in the top position, so it carries no
// meaningful alpha channel.
//
// SDL_PIXELFORMAT_RGBX8888 rather than a hand-rolled name: this SDL has no
// XRGB8888 enum, and the point of the format is only that its Amask is zero,
// which is what lets paint::BlitScaled memcpy an opaque cached surface.
const Uint32 kOpaqueFormat = SDL_PIXELFORMAT_RGBX8888;

// ---- button art -----------------------------------------------------------
//
// Artwork arrives as 32-bit BMP, trimmed and pre-scaled to its draw size by
// scripts/gfx2bmp.py at build time. There is no runtime trim or scale: see
// LoadIcon below for why that matters.

namespace {

// Loads a 32-bit BMP and uses it at its native size.
//
// No trimming and no runtime scaling. The artwork is trimmed and pre-scaled to
// its draw size at build time by scripts/gfx2bmp.py, so by the time it gets here
// it is already the size it will be drawn at and the frame path only ever does a
// 1:1 blit. Scaling 400x400 source art down to 34px costs about 12k source
// samples per output pixel, which is fine once at startup and ruinous 180 times
// a second.
Icon LoadIcon(const char *path)
{
	Icon icon;
	icon.surface = NULL;
	icon.w = 0;
	icon.h = 0;
	icon.loaded = false;

	SDL_Surface *surface = bitmap::Load(path);

	if (!surface)
	{
		dbglog::Write("Menu: cannot load %s", path);
		return icon;
	}

	icon.surface = surface;
	icon.w = surface->w;
	icon.h = surface->h;
	icon.loaded = true;

	dbglog::Write("Menu: loaded %s at %dx%d", path, surface->w, surface->h);
	return icon;
}

// Draws an icon at its native size, centred in the box at (x, y).
void DrawIcon(SDL_Surface *dst, const Icon &icon, int x, int y, int size)
{
	if (!icon.loaded || !icon.surface)
		return;

	paint::BlitAlpha(dst, icon.surface, x + (size - icon.surface->w) / 2,
	                 y + (size - icon.surface->h) / 2);
}

// Install: a down arrow into a tray.
void DrawInstallIcon(SDL_Surface *s, int x, int y, int size, Color c)
{
	const int cx = x + size / 2;
	const int stemH = size / 2;
	const int stemW = 3;

	paint::FillRect(s, cx - stemW / 2, y + size / 6, stemW, stemH, c, 255);

	// Arrowhead.
	paint::FillTriangleDown(s, cx - size / 4, y + size / 6 + stemH - 4,
	                        size / 2, size / 4, c, 255);

	// Tray.
	const int trayY = y + size * 3 / 4;
	const int trayW = size * 3 / 5;
	paint::FillRect(s, cx - trayW / 2, trayY, trayW, 3, c, 255);
	paint::FillRect(s, cx - trayW / 2, trayY, 3, size / 6, c, 255);
	paint::FillRect(s, cx + trayW / 2 - 3, trayY, 3, size / 6, c, 255);
}

// Settings: a gear, approximated by a ring with eight teeth.
void DrawGearIcon(SDL_Surface *s, int x, int y, int size, Color c)
{
	const int cx = x + size / 2;
	const int cy = y + size / 2;
	const int r = size / 3;

	paint::StrokeCircle(s, cx, cy, r, 3, c, 255);
	paint::StrokeCircle(s, cx, cy, r / 3, 2, c, 255);

	const int toothLen = size / 6;
	const int toothW = 3;

	for (int i = 0; i < 8; ++i)
	{
		// 45-degree steps; integer trigonometry is fine at this size.
		static const int kCos[8] = { 100,  71,   0, -71, -100, -71,   0,  71 };
		static const int kSin[8] = {   0,  71, 100,  71,    0, -71,-100, -71 };

		const int dx = (kCos[i] * (r + toothLen / 2)) / 100;
		const int dy = (kSin[i] * (r + toothLen / 2)) / 100;
		const int tw = (std::abs(kCos[i]) > 50 || std::abs(kSin[i]) > 50)
			? toothW : toothW * 2;

		if (std::abs(kCos[i]) > std::abs(kSin[i]))
			paint::FillRect(s, cx + dx - tw / 2, cy + dy - toothW / 2,
			                tw * 3 / 4, toothW, c, 255);
		else
			paint::FillRect(s, cx + dx - toothW / 2, cy + dy - tw / 2,
			                toothW, tw * 3 / 4, c, 255);
	}
}

// Uninstall: a bin, lid plus body plus two ribs.
void DrawBinIcon(SDL_Surface *s, int x, int y, int size, Color c)
{
	const int cx = x + size / 2;
	const int bodyW = size * 3 / 5;
	const int bodyH = size / 2;
	const int bodyY = y + size / 3;
	const int t = 3;

	// Body: outlined so it reads as a bin rather than a solid block.
	paint::FillRect(s, cx - bodyW / 2, bodyY, bodyW, t, c, 255);
	paint::FillRect(s, cx - bodyW / 2, bodyY, t, bodyH, c, 255);
	paint::FillRect(s, cx + bodyW / 2 - t, bodyY, t, bodyH, c, 255);
	paint::FillRect(s, cx - bodyW / 2, bodyY + bodyH - t, bodyW, t, c, 255);

	// Lid, wider than the body, plus a handle.
	const int lidW = bodyW + 10;
	paint::FillRect(s, cx - lidW / 2, bodyY - 6, lidW, t, c, 255);
	paint::FillRect(s, cx - 6, bodyY - 12, 12, t, c, 255);

	// Ribs.
	paint::FillRect(s, cx - 5, bodyY + 8, 2, bodyH - 14, c, 255);
	paint::FillRect(s, cx + 3, bodyY + 8, 2, bodyH - 14, c, 255);
}

// The chevron in the bottom-right corner of a card.
void DrawChevron(SDL_Surface *s, int x, int y, int size, Color c, uint8_t alpha)
{
	const int t = 3;

	for (int i = 0; i < size; ++i)
	{
		const int j = size - 1 - i;
		paint::FillRect(s, x + i, y + j, t, t, c, alpha);
		paint::FillRect(s, x + i, y + size + i, t, t, c, alpha);
	}
}

} // namespace

// ---- Menu ------------------------------------------------------------------

const Menu::Card Menu::kCards_[kCardCount] = {
	{"INSTALL / UPDATE", "Install or update the orbisRPC\npayload"},
	{"SETTINGS", "Customize presence, configure\npreferences"},
	{"UNINSTALL", "Remove the orbisRPC payload\nand configs from console"},
};

Menu::Menu()
	: dst_(NULL)
	, ftLib_(NULL)
	, background_(NULL)
	, backgroundReady_(false)
	, ready_(false)
	, titleFace_(NULL)
	, bodyFace_(NULL)
	, chromeFace_(NULL)
{
	memset(&selectIcon_, 0, sizeof selectIcon_);
	memset(&backIcon_, 0, sizeof backIcon_);
	memset(&navigateIcon_, 0, sizeof navigateIcon_);
	memset(&logo_, 0, sizeof logo_);
	memset(cardSurfaces_, 0, sizeof cardSurfaces_);
	memset(haloSurfaces_, 0, sizeof haloSurfaces_);
	memset(cardIsSelected_, 0, sizeof cardIsSelected_);
}

Menu::~Menu()
{
	for (int i = 0; i < kCardCount; ++i)
	{
		if (cardSurfaces_[i])
			SDL_FreeSurface(cardSurfaces_[i]);

		if (haloSurfaces_[i])
			SDL_FreeSurface(haloSurfaces_[i]);
	}

	if (selectIcon_.surface)
		if (selectIcon_.surface)
		SDL_FreeSurface(selectIcon_.surface);
	if (backIcon_.surface)
		SDL_FreeSurface(backIcon_.surface);
	if (navigateIcon_.surface)
		SDL_FreeSurface(navigateIcon_.surface);
	if (logo_.surface)
		SDL_FreeSurface(logo_.surface);

	if (background_)
		SDL_FreeSurface(background_);
}

bool Menu::LoadFonts()
{
	titleFace_ = paint::Face(kUiFont, kTitleSize, kWeightBold);
	bodyFace_ = paint::Face(kUiFont, kBodySize, kWeightBody);
	chromeFace_ = paint::Face(kUiFont, kChromeSize, kWeightBody);

	if (!titleFace_ || !bodyFace_ || !chromeFace_)
	{
		dbglog::Write("Menu: fonts failed to load");
		return false;
	}

	return true;
}

bool Menu::Init(SDL_Surface *dst, FT_Library ftLib)
{
	dst_ = dst;
	ftLib_ = ftLib;

	if (!dst_ || !ftLib_)
		return false;

	// Logged once, because a mismatch here is the difference between a themed
	// screen and one full of the wrong colours, and it is invisible in the source.
	dbglog::Write("Menu: target surface %dx%d pitch=%d format=%s masks=%08x/%08x/%08x/%08x",
	        dst_->w, dst_->h, dst_->pitch, SDL_GetPixelFormatName(dst_->format->format),
	        dst_->format->Rmask, dst_->format->Gmask, dst_->format->Bmask,
	        dst_->format->Amask);

	if (!LoadFonts())
		return false;

	// Missing artwork is not fatal: the footer just loses its button glyphs.
	selectIcon_ = LoadIcon(kSelectIcon);
	backIcon_ = LoadIcon(kBackIcon);
	navigateIcon_ = LoadIcon(kNavigateIcon);
	logo_ = LoadIcon(kLogoPath);

	dbglog::Write("Menu: icons select=%d back=%d navigate=%d logo=%d",
	        selectIcon_.loaded, backIcon_.loaded, navigateIcon_.loaded,
	        logo_.loaded);

	BuildBackground();
	ready_ = backgroundReady_;

	// Prime every card surface so the first frame does not pay for three
	// rasterisations.
	for (int i = 0; i < kCardCount; ++i)
		BuildCard(i, false);

	// One line that says whether the page will draw at all. If this build goes
	// blank, this is the line to read first.
	dbglog::Write("Menu: init done, ready=%d background=%p cards=%p/%p/%p", ready_,
	        static_cast<void *>(background_),
	        static_cast<void *>(cardSurfaces_[0]),
	        static_cast<void *>(cardSurfaces_[1]),
	        static_cast<void *>(cardSurfaces_[2]));

	return ready_;
}

void Menu::BuildBackground()
{
	if (!dst_)
		return;

	// XRGB8888 rather than ARGB8888: the background is fully opaque, and dropping
	// the alpha channel is what lets BlitScaled memcpy the whole screen instead
	// of blending two million pixels.
	if (!background_)
	{
		background_ = SDL_CreateRGBSurfaceWithFormat(0, kScreenW, kScreenH, 32,
		                                            kOpaqueFormat);
		if (!background_)
			return;
	}

	// Flat black. The mockup had two soft radial glows behind the cards, but at
	// 1080p on a software rasteriser they read as a banded wash rather than a
	// subtle backdrop -- the low-resolution falloff is visible as blocks. Plain
	// black is what was asked for and costs nothing.
	paint::Clear(background_, kBackground);

	backgroundReady_ = true;
}

// The glow behind the selected card, on a surface of its own.
//
// It cannot be stroked into the card. The card surface is w*h and fully opaque:
// paint::Clear fills the whole rectangle, and the rounded fill only paints the
// rounded shape over it, so the four square corners stay card-coloured. Those
// corners fall exactly between the glow and the border -- the glow sits on the
// background, the card is blitted over it, and the corners hide the glow's inner
// half. What showed was a wedge of card fill in the gap, and no adjustment to
// either radius could move it, because the problem was the paint order rather
// than the geometry.
//
// A separate surface, blitted first, puts the glow under the card without the
// card covering it. Cached with the rest of the card, so it costs nothing per
// frame, and the border lands on the glow's inner edge because kHaloRadius is
// derived from the border's geometry.
void Menu::BuildHalo(int i)
{
	if (haloSurfaces_[i])
		SDL_FreeSurface(haloSurfaces_[i]);

	haloSurfaces_[i] = NULL;

	const int pad = kHaloOut;

	SDL_Surface *s = SDL_CreateRGBSurfaceWithFormat(0, kCardW + pad * 2,
	                                                  kCardH + pad * 2, 32,
	                                                  kOpaqueFormat);
	if (!s)
		return;

	// The background colour, so the margin is invisible against the menu's flat
	// black and only the glow shows through it. The logo is drawn well above the
	// cards and the gap between them is 26, so nothing else is covered by this.
	paint::Clear(s, kBackground);

	paint::StrokeRounded(s, pad, pad, kCardW, kCardH, kHaloRadius,
	                     kHaloThickness, kBorderActive, kHaloAlpha);

	haloSurfaces_[i] = s;
}

void Menu::BuildCard(int i, bool selected)
{
	const int x = CardX(i);
	const int y = kCardTop;
	const int w = kCardW;
	const int h = kCardH;

	// Rebuild in place when only the selection changed, so the two do not
	// alternate allocations on every input event.
	if (cardSurfaces_[i] && cardSurfaces_[i]->w == w &&
	    cardSurfaces_[i]->h == h)
		SDL_FreeSurface(cardSurfaces_[i]);

	// No alpha channel: cards are always drawn whole, and an opaque format lets
	// paint::BlitScaled take its memcpy path for them.
	SDL_Surface *card = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32,
	                                                  kOpaqueFormat);
	if (!card)
		return;

	paint::Clear(card, selected ? kCardFill : kCardFillTop);

	// Body fill, inset by the border so the border sits on the edge.
	paint::FillRoundedGradientV(card, 0, 0, w, h, kRadius,
	                            selected ? kCardFill : kCardFillTop, kCardFill,
	                            255);

	// Border.
	//
	// kCardBorderRadius rather than kRadius: see the comment on that constant for
	// why a stroke's radius is not its fill's radius minus the inset.
	paint::StrokeRounded(card, 1, 1, w - 2, h - 2, kCardBorderRadius,
	                     selected ? kCardBorderOn : kCardBorderOff,
	                     selected ? kBorderActive : kBorderIdle,
	                     selected ? 255 : 200);

	// The selected card's glow is built separately and blitted underneath -- see
// BuildHalo() for why it cannot live on this surface.

	// Icon tile.
	const Color iconBg = selected ? kIconBgActive : kIconBg;
	const Color iconFg = selected ? kIconActive : kIconIdle;
	const int iconX = kIconX;
	const int iconY = kIconY;

	paint::FillRounded(card, iconX, iconY, kIconSize, kIconSize, kIconRadius,
	                   iconBg, 255);

	if (selected)
		paint::StrokeRounded(card, iconX - 2, iconY - 2, kIconSize + 4,
		                     kIconSize + 4, kIconRadius + 2, 2, kIconActive, 120);

	const int glyphPad = 20;
	switch (i)
	{
	case kCardInstall:
		DrawInstallIcon(card, iconX + glyphPad, iconY + glyphPad,
		                kIconSize - glyphPad * 2, iconFg);
		break;
	case kCardSettings:
		DrawGearIcon(card, iconX + glyphPad, iconY + glyphPad,
		             kIconSize - glyphPad * 2, iconFg);
		break;
	default:
		DrawBinIcon(card, iconX + glyphPad, iconY + glyphPad,
		            kIconSize - glyphPad * 2, iconFg);
		break;
	}

	// Heading.
	const int titleX = 42;
	const int titleBaseline = 242;
	paint::Text(card, titleX, titleBaseline, kCards_[i].title, titleFace_,
	            selected ? kTitleActive : kTitleIdle, 255);

	// Description, split on the newline in the card text. Baseline positions are
	// fixed rather than derived from a line height, to match the mockup's
	// spacing between heading and body.
	const int bodyX = 44;
	const int bodyBaseline = 288;
	const int bodyLineGap = 34;
	const char *body = kCards_[i].body;

	for (int line = 0; line < 3; ++line)
	{
		const char *end = strchr(body, '\n');
		const std::string lineText = end ? std::string(body, end - body)
		                                  : std::string(body);

		paint::Text(card, bodyX, bodyBaseline + line * bodyLineGap, lineText,
		            bodyFace_, selected ? kBodyActive : kBodyIdle, 255);

		if (!end)
			break;

		body = end + 1;
	}

	// Corner chevron.
	DrawChevron(card, w - 46, h - 46, 12, selected ? kChevronAct : kChevronIdle,
	            selected ? 255 : 180);

	cardSurfaces_[i] = card;
	cardIsSelected_[i] = selected;
}

void Menu::Draw(int selected)
{
	if (!ready_ || !dst_)
		return;

	if (!background_)
	{
		paint::Clear(dst_, kBackground);
		return;
	}

	// Background: one opaque blit, which is a memcpy for a same-format copy.
	paint::BlitOpaque(dst_, background_, 0, 0);

	// Wordmark, above the card row.
	if (logo_.loaded)
		paint::BlitAlpha(dst_, logo_.surface, kLogoX, kLogoY);

	// Cards: only rebuild the ones whose selected state actually changed.
	for (int i = 0; i < kCardCount; ++i)
	{
		const bool isSelected = (i == selected);

		if (!cardSurfaces_[i] || cardIsSelected_[i] != isSelected)
		{
			BuildCard(i, isSelected);

			// The glow exists only for the selected card, so it is built and
			// freed alongside the card rather than kept around empty.
			if (isSelected)
				BuildHalo(i);
			else if (haloSurfaces_[i])
			{
				SDL_FreeSurface(haloSurfaces_[i]);
				haloSurfaces_[i] = NULL;
			}
		}

		// Glow first, then the card on top of it.
		if (haloSurfaces_[i])
			paint::BlitOpaque(dst_, haloSurfaces_[i],
			                  CardX(i) - kHaloOut, kCardTop - kHaloOut);

		if (cardSurfaces_[i])
			paint::BlitOpaque(dst_, cardSurfaces_[i], CardX(i), kCardTop);
	}

	// Footer: solid bar, then the button hints.
	paint::FillRect(dst_, 0, kScreenH - kFooterH, kScreenW, kFooterH,
	                kBackground, 255);
	paint::FillRect(dst_, 0, kScreenH - kFooterH, kScreenW, 2,
	                kBorderIdle, 255);

	const int baseY = kScreenH - kFooterH + (kFooterH - kFooterBtnSize) / 2;
	int cursorX = kFooterPadX;

	// Vertically centred against the button art rather than derived from a metric,
	// so the text and the icon agree regardless of the font's own metrics.
	const int textBaseline = baseY + kFooterBtnSize / 2 + kChromeSize / 4;

	if (selectIcon_.loaded)
		DrawIcon(dst_, selectIcon_, cursorX, baseY, kFooterBtnSize);
	cursorX += kFooterBtnSize + kFooterIconGap;

	paint::Text(dst_, cursorX, textBaseline, "Select", chromeFace_, kFooterText,
	            255);
	cursorX += paint::TextWidth("Select", chromeFace_) + kFooterGroupGap;

	if (backIcon_.loaded)
		DrawIcon(dst_, backIcon_, cursorX, baseY, kFooterBtnSize);
	cursorX += kFooterBtnSize + kFooterIconGap;

	paint::Text(dst_, cursorX, textBaseline, "Back", chromeFace_, kFooterText, 255);

	// Navigate hint sits on the right of the footer, since it applies to the card
	// row rather than to a single button.
	if (navigateIcon_.loaded)
	{
		const int navSize = kFooterBtnSize + 6;
		const int navX = kScreenW - kFooterPadX - navSize;
		const int navY = baseY - 3;

		DrawIcon(dst_, navigateIcon_, navX, navY, navSize);
		paint::Text(dst_, navX - kFooterGroupGap - paint::TextWidth("Navigate", chromeFace_),
		            navY + navSize / 2 + kChromeSize / 4, "Navigate", chromeFace_,
		            kFooterText, 255);
	}
}