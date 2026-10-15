/* settingsview.cpp - the Settings screen.
 *
 * Two columns, per the supplied mockup:
 *
 *   left  - an RPC section of four toggle rows plus a custom message field, and
 *           a FIXES section holding one action row
 *   right - a preview panel with three tabs, showing fixture data only
 *
 * The preview is a mock. Its contents are hard-coded rather than queried, because
 * the daemon does not report the running title or an elapsed time yet. Everything
 * fixture lives in kPreviews, so swapping in real data is a change to one table
 * and not to the drawing.
 *
 * Drawing follows the menu's approach: the left column and the preview panel are
 * each rasterised into their own surface and rebuilt only when their content
 * changes, then blitted per frame. At 1080p on a software rasteriser that is the
 * difference between a page that redraws in a few milliseconds and one that does
 * not.
 */
#include "settingsview.h"

#include <stdio.h>
#include <string.h>

#include "bitmap.h"
#include "console.h"
#include "dbglog.h"
#include "paint.h"
#include "sysinfo.h"   // QuerySystem, and FormatFirmware via it

namespace {

// A 32-bit surface with a wasted byte in the top position, so it carries no
// meaningful alpha channel.
//
// SDL_PIXELFORMAT_RGBX8888 rather than a hand-rolled name: this SDL has no
// XRGB8888 enum, and the point is only that its Amask is zero, which is what
// lets paint::BlitOpaque memcpy an opaque cached surface.
const Uint32 kOpaqueFormat = SDL_PIXELFORMAT_RGBX8888;

// ---- palette --------------------------------------------------------------
//
// Matches the menu so the two screens read as one app: the same accent yellow,
// the same near-black, the same selected-row treatment.
const Color kBackground = { 10, 11, 16 };
const Color kAccent     = { 250, 200, 40 };

const Color kRowFill      = {  21,  23,  30 };
const Color kRowFillTop   = {  26,  28,  36 };
const Color kRowBorder    = {  40,  43,  55 };
const Color kRowBorderSel = { 250, 200,  40 };

const Color kTitleNormal  = { 228, 232, 240 };
const Color kDescNormal   = { 122, 128, 143 };
const Color kSectionLabel = { 108, 114, 130 };

const Color kTrackOn  = {  92,  74,  18 };
const Color kTrackOff = {  44,  47,  58 };
const Color kKnobOn   = { 250, 200,  40 };
const Color kKnobOff  = {  96, 101, 115 };

const Color kPanelFill   = {  17,  19,  25 };
const Color kPanelCard   = {  27,  29,  37 };
const Color kPanelBorder = {  38,  41,  52 };
const Color kTabActive   = { 236, 239, 246 };
const Color kTabInactive = {  96, 102, 117 };
const Color kChromeText  = { 140, 146, 162 };
const Color kTimerText   = {  96, 190, 130 };
const Color kSubtitle    = { 150, 156, 170 };

// ---- geometry (1920x1080) -------------------------------------------------
//
// Read off the mockup by proportion. The screenshots are not exactly 1920x1080,
// so these are scaled rather than measured, then tuned to the menu's own row
// rhythm so the two pages feel related rather than merely adjacent.
const int kTitleBaseline = 76;   // "Settings"

const int kWidth  = 1920;
const int kHeight = 1080;

const int kColLX = 64;
const int kColLW = 845;
const int kColRX = 961;
const int kColRW = 895;

const int kListTop    = 132;
// Bottom of the list, which is the top of the footer -- not the bottom of the
// surface. The footer paints an opaque rectangle over the full width, so anything
// drawn below this is not merely off-screen, it is covered. Clipping to kHeight
// instead meant the last row drew perfectly and was then hidden by the footer,
// which is what made the list look stuck.
const int kListBottom = 1080 - 76;   // kFooterH
// The designed row height, restored after being shrunk. Shrinking it to 92 made
// the seven rows fit, which solved the reachability problem by removing the
// overflow -- but scrolling was what was asked for, and quietly replacing that
// with a different design is not a fix, it is a substitution.
const int kRowH       = 108;
const int kRowGap     = 18;
const int kRowPadX    = 34;
const int kRowRadius  = 18;
const int kSectionGap = 40;

// How far every border on this page sits inside the shape it outlines.
//
// A rounded rect's corner arcs are centred (radius) from its own origin, so
// insetting the rect by a pixel while keeping the radius slides the arcs one
// pixel in and the two shapes stop being concentric. The border band then misses
// the fill's edge at the corner and shows as a step where the curve meets the
// straight run. Every stroke here therefore uses radius - kBorderInset.
const int kBorderInset = 1;

// The preview panel's corner radius. Named because the fill and the border that
// outlines it both use it and they have to stay equal for the border to sit on
// the fill's edge.
const int kPanelRadius = 22;

const int kToggleW = 66;
const int kToggleH = 36;

const int kSizeTitle   = 28;
const int kSizeDesc    = 19;
const int kSizeSection = 15;
const int kSizeTab     = 22;
const int kSizeSmall   = 17;
const int kTracking    = 5;   // letter spacing on the section labels

const int kPanelTop = 104;
const int kPanelBottom = 940;

const int kFooterH = 76;

// ---- preview panel --------------------------------------------------------

const int kTabBarH      = 62;
const int kTabPadX      = 22;
const int kTabGap       = 10;
const int kTabUnderline = 3;

// Shoulder button art, drawn inside the Previous / Next pills. Icon only -- no
// text label.
//
// Sized to match the footer's button art (34px) so both read at the same weight.
// The sources are landscape (408x272 trimmed), so the pills hug the artwork
// rather than padding out to a square: a square pill holding a wide icon looks
// like a mistake, whereas one that fits its content looks deliberate.
const int kShoulderSize = 34;
const int kShoulderPad  = 11;

const int kCardX      = 24;
const int kCardY      = 24;
const int kCardPad    = 22;
const int kCardH      = 152;
const int kCardGap    = 14;
const int kCardRadius = 14;

const int kIconSize = 76;
const int kIconPadY = 42;
const int kTextPadX = 22;

// The small PlayStation badge that overlaps a card icon's bottom-right corner.
const int kBadgeSize    = 30;
const int kBadgeOverlap = 8;

// Baseline positions within one preview card, relative to its top.
const int kCardHeadBaseline  = 34;
const int kCardTitleBaseline = 62;
const int kCardSubBaseline   = 86;
const int kCardFirmBaseline  = 110;
const int kCardTimeBaseline  = 132;

const int kLineGap = 24;

// The box an action-row glyph is centred in. Wider than the artwork inside it:
// the padded margin is what keeps the icon off the neighbouring title text.
const int kActionIconBox = 28;

// ---- artwork --------------------------------------------------------------

GfxIcon LoadArt(const char *name)
{
	GfxIcon icon;
	icon.surface = NULL;
	icon.loaded = false;

	char path[96];
	snprintf(path, sizeof path, "/app0/assets/images/%s.bmp", name);

	icon.surface = bitmap::Load(path);

	if (!icon.surface)
	{
		dbglog::Write("Settings: missing artwork %s", path);
		return icon;
	}

	icon.loaded = true;
	return icon;
}

// A neutral tile for titles with no shipped artwork. Icons vary per title, so a
// fixed set cannot cover everything.
void DrawPlaceholder(SDL_Surface *s, int x, int y, int size)
{
	paint::FillRounded(s, x, y, size, size, size / 5, kPanelCard, 255);
	paint::StrokeRounded(s, x, y, size, size, size / 5, 1, kPanelBorder, 255);
}

// The little chain glyph before the elapsed timer.
void DrawLinkGlyph(SDL_Surface *s, int x, int y, Color c)
{
	const int w = 7, h = 5, t = 2;

	for (int i = 0; i < w; ++i)
		paint::FillRect(s, x + i, y, t, t, c, 255);

	paint::FillRect(s, x, y, 2, h, c, 255);
	paint::FillRect(s, x + w - 2, y, 2, h, c, 255);

	paint::FillRect(s, x + w / 2 - 1, y + h / 2 - 1, 2, 2, c, 255);
}

// Three dots, top right of a preview card.
void DrawDots(SDL_Surface *s, int x, int y, Color c)
{
	for (int i = 0; i < 3; ++i)
		paint::FillRounded(s, x + i * 9, y, 4, 4, 2, c, 200);
}

// The glyph in front of an action row under FIXES.
//
// One wrench for every action row rather than a per-row icon: a FIXES section is
// a list of things the app can do, and a wrench covers "the app will fix this"
// for all of them. A row that needs a distinct glyph later wants an id on the
// Row struct and a switch here -- keying it off `setting < 0` means every action
// row added in future picks this up without being touched.
void DrawActionIcon(SDL_Surface *s, int x, int centreY, SDL_Surface *icon)
{
	if (!icon)
		return;

	paint::BlitAlpha(s, icon, x + (kActionIconBox - icon->w) / 2,
	                 centreY - icon->h / 2);
}

} // namespace

// ---- static row table -----------------------------------------------------

const SettingsView::Row SettingsView::kRows[] = {
	{kSetToken, "Discord token",
	 "The credential orbisRPC authenticates with.", false},
	{kSetShowFirmware, "Show firmware version",
	 "Adds the console firmware to the presence line.", false},
	{kSetShowMediaApps, "Show watching presence for media apps",
	 "Reports what app you are watching.", false},
	{kSetHomeScreen, "Show presence in settings and web browser",
	 "Keeps the presence visible outside of full screen games.", true},
	{kSetShowHomebrew, "Show presence for homebrew and retro games",
	 "Includes cutom titles in presence.", false},
	{kSetCustomMessage, "Custom Message on Presence",
	 "Text appended to your presence line.", false},
	{-1, "Add Sony TMDB exception to nanoDNS", NULL, false},
};

const int kRowCount = static_cast<int>(sizeof SettingsView::kRows /
                                       sizeof SettingsView::kRows[0]);
const int kTabCount = static_cast<int>(sizeof SettingsView::kPreviews /
                                       sizeof SettingsView::kPreviews[0]);

// The preview panel is a mock: its card titles, headings and timers are fixture
// data because the daemon does not report them yet. The one thing that is real is
// the firmware line, which comes from this console.
//
// Each tab's `firmware` is a fallback shown only if that query failed. A card
// with no firmware at all reads as a rendering fault rather than as a setting
// being off, so the placeholder is worth having even though it should never
// appear.
// The word in front of the number. A macro rather than a const because it has to
// be usable inside a string literal: the fallbacks below are literals, and
// building them at runtime would mean three separate std::strings in a table
// that is otherwise all plain C strings.
//
// Concatenation is the point -- "Firmware "13.52" is one literal, so the label
// cannot be left out of one of the three entries while the other two have it.
#define kFirmwarePrefix "Firmware "

const SettingsView::Preview SettingsView::kPreviews[3] = {
	// The badge marks a card as "a PlayStation title". Cards that are not playing
	// something do not get one: ps_logo_big already is the console mark, and
	// Using Web Browser is a system app rather than a title, so a badge there
	// would claim something untrue.
	{ "Main Menu", kFirmwarePrefix "13.52", {
	    { "ps_logo_big", "Playing", "PlayStation 4", "Idling on Home Menu",
	      "0:10", false },
	    { "images_web_browser", "Playing", "PlayStation 4", "Using Web Browser",
	      "1:01", false },
	    { "ps_logo_big", "Playing", "PlayStation 4", "In Settings", "0:03", false },
	  }, 3 },

	{ "Playing", kFirmwarePrefix "13.52", {
	    { "naruto_storm_connections", "Playing",
	      "NARUTO X BORUTO Ultimate Ninja STORM CON...",
	      "Playing on PlayStation 4", "0:21", true },
	  }, 1 },

	{ "Watching", kFirmwarePrefix "13.52", {
	    { "youtube", "Watching YouTube", "Watching on PlayStation 4", NULL,
	      "0:13", true },
	  }, 1 },
};

int SettingsView::RowCount()
{
	return kRowCount;
}

int SettingsView::RowSetting(int row)
{
	if (row < 0 || row >= kRowCount)
		return -1;
	return kRows[row].setting;
}

bool SettingsView::RowIsToggle(int row)
{
	if (row < 0 || row >= kRowCount)
		return false;
	return Settings::IsToggle(kRows[row].setting);
}

bool SettingsView::RowIsAction(int row)
{
	return (row >= 0 && row < kRowCount && kRows[row].setting < 0);
}

int SettingsView::TabCount()
{
	return kTabCount;
}

const char *SettingsView::TabLabel(int tab)
{
	if (tab < 0 || tab >= kTabCount)
		return "";
	return kPreviews[tab].tabLabel;
}

// ---- lifecycle ------------------------------------------------------------

SettingsView::SettingsView()
	: actionIconSel_(NULL)
	, actionIconIdle_(NULL)
	, titleFace_(NULL)
	, descFace_(NULL)
	, sectionFace_(NULL)
	, tabFace_(NULL)
	, smallFace_(NULL)
	, panel_(NULL)
	, panelTab_(-1)
	, rows_(NULL)
	, dst_(NULL)
	, ready_(false)
{
	memset(&youtube_, 0, sizeof youtube_);
	memset(&naruto_, 0, sizeof naruto_);
	memset(&webBrowser_, 0, sizeof webBrowser_);
	memset(&psLogoBig_, 0, sizeof psLogoBig_);
	memset(&psLogoSmall_, 0, sizeof psLogoSmall_);
	memset(&navigate_, 0, sizeof navigate_);
	memset(&select_, 0, sizeof select_);
	memset(&back_, 0, sizeof back_);
	memset(&l1_, 0, sizeof l1_);
	memset(&r1_, 0, sizeof r1_);
}

SettingsView::~SettingsView()
{
	if (panel_)
		SDL_FreeSurface(panel_);
	if (rows_)
		SDL_FreeSurface(rows_);

	if (youtube_.surface)
		SDL_FreeSurface(youtube_.surface);
	if (naruto_.surface)
		SDL_FreeSurface(naruto_.surface);
	if (webBrowser_.surface)
		SDL_FreeSurface(webBrowser_.surface);
	if (psLogoBig_.surface)
		SDL_FreeSurface(psLogoBig_.surface);
	if (psLogoSmall_.surface)
		SDL_FreeSurface(psLogoSmall_.surface);
	if (navigate_.surface)
		SDL_FreeSurface(navigate_.surface);
	if (select_.surface)
		SDL_FreeSurface(select_.surface);
	if (back_.surface)
		SDL_FreeSurface(back_.surface);
	if (l1_.surface)
		SDL_FreeSurface(l1_.surface);
	if (r1_.surface)
		SDL_FreeSurface(r1_.surface);

	if (actionIconSel_)
		SDL_FreeSurface(actionIconSel_);
	if (actionIconIdle_)
		SDL_FreeSurface(actionIconIdle_);
}

bool SettingsView::Init(SDL_Surface *dst, FT_Library ftLib)
{
	dst_ = dst;

	if (!dst_ || !ftLib)
		return false;

	const char *const path = "/app0/assets/fonts/SegUIVar.ttf";

	titleFace_ = paint::Face(path, kSizeTitle, 700.0f);
	descFace_ = paint::Face(path, kSizeDesc, 400.0f);
	sectionFace_ = paint::Face(path, kSizeSection, 600.0f);
	tabFace_ = paint::Face(path, kSizeTab, 700.0f);
	smallFace_ = paint::Face(path, kSizeSmall, 400.0f);

	if (!titleFace_ || !descFace_ || !sectionFace_ || !tabFace_ || !smallFace_)
	{
		dbglog::Write("Settings: fonts failed to load");
		return false;
	}

	youtube_ = LoadArt("youtube");
	naruto_ = LoadArt("naruto_storm_connections");
	webBrowser_ = LoadArt("images_web_browser");
	psLogoBig_ = LoadArt("ps_logo_big");
	psLogoSmall_ = LoadArt("ps_logo_small");

	// Footer buttons, the same art the menu uses.
	navigate_ = LoadArt("navigate");
	select_ = LoadArt("select");
	back_ = LoadArt("back");

	// Shoulder buttons for the preview tab bar.
	l1_ = LoadArt("L1");
	r1_ = LoadArt("R1");

	// The FIXES glyph, tinted once per state rather than per draw. The file is
	// white; the page's colours come from here, so the same artwork serves a
	// selected row and an idle one.
	{
		GfxIcon action = LoadArt("wrench");

		if (action.loaded)
		{
			actionIconSel_  = paint::Tint(action.surface, kAccent);
			actionIconIdle_ = paint::Tint(action.surface, kDescNormal);
		}

		SDL_FreeSurface(action.surface);

		dbglog::Write("Settings: action icon %s (%dx%d)",
		              action.loaded ? "loaded" : "MISSING",
		              actionIconSel_ ? actionIconSel_->w : 0,
		              actionIconSel_ ? actionIconSel_->h : 0);
	}

	// This console's real firmware, for the preview panel. Queried once here
	// rather than per frame: it cannot change while the app is running, and the
	// panel surface is rebuilt on every toggle and tab change.
	//
	// Stores the label too, not just the number, so what gets drawn is exactly
	// what the table's fallback strings look like. A bare "13.52" with no word in
	// front of it reads as a stray value rather than as a firmware line.
	//
	// Left empty if the query fails, and the panel falls back to the table's
	// placeholder rather than drawing nothing -- a card with no firmware line at
	// all reads as a rendering fault rather than as a setting being off.
	{
		const std::string number = FormatFirmware(QuerySystem().firmware);

		firmware_ = number.empty() ? std::string()
		                           : std::string(kFirmwarePrefix) + number;
	}

	ready_ = true;

	dbglog::Write("Settings: init ok, artwork %d/%d/%d/%d/%d",
	              youtube_.loaded, naruto_.loaded, webBrowser_.loaded,
	              psLogoBig_.loaded, psLogoSmall_.loaded);
	dbglog::Write("Settings: firmware '%s'", firmware_.c_str());
	return true;
}

// ---- left column ----------------------------------------------------------

// Everything the left column depends on, as one comparable string. Rebuilt only
// when this changes, so holding a toggle for several frames costs one blit.
std::string SettingsView::BuildRowKey(const Settings &s, int selected) const
{
	char buf[80];

	// The token is hashed rather than included verbatim. It is the longest string
	// on the page by a wide margin, and this key is compared every frame -- putting
	// a 150-character credential in it would mean rebuilding that comparison for
	// no benefit. A hash is enough: this only has to change when the value does.
	unsigned long h = 1469598103934665603UL;
	for (size_t i = 0; i < s.Token().size(); ++i)
	{
		h ^= static_cast<unsigned char>(s.Token()[i]);
		h *= 1099511628211UL;
	}

	// available_ is in here because it changes how the rows are painted, and a
	// cached surface built for one would be wrong for the other.
	snprintf(buf, sizeof buf, "%d|%d%d%d%d|%d|%08lx|", selected,
	         s.ShowFirmware() ? 1 : 0, s.ShowMediaApps() ? 1 : 0,
	         s.ShowHomeScreen() ? 1 : 0, s.ShowHomebrew() ? 1 : 0,
	         s.Available() ? 1 : 0, h & 0xffffffffUL);

	return std::string(buf) + s.CustomMessage() + "|" + status_;
}

// ---- row layout ------------------------------------------------------------
//
// Seven rows do not fit: laid out end to end they reach about y=1140, and the
// column is 1080 tall from a list top of 132. The FIXES row was therefore drawn
// below the surface and could not be reached at all -- the cursor moved, the row
// did not come with it.
//
// So the geometry is computed first, in one pass, with no drawing in it. That
// gives a second pass something to scroll against and something to skip rows by,
// which is what keeps the cost proportional to what is actually on screen rather
// than to how many rows exist.

struct RowBox
{
	int labelY;   // where the section label above this row sits, or -1
	int y;        // top of the row
	int h;
};

// Fills `boxes` and returns the y just past the last row.
int LayoutRows(RowBox boxes[], int count)
{
	int y = kListTop;

	for (int i = 0; i < count; ++i)
	{
		boxes[i].labelY = -1;

		if (SettingsView::kRows[i].setting < 0 && i > 0)
		{
			// Section break before the FIXES action row: one gap to make room
			// for the label, one to clear it.
			boxes[i].labelY = y;
			y += kSectionGap;
			y += kSectionGap;
		}

		boxes[i].y = y;
		boxes[i].h = SettingsView::kRows[i].twoLine ? kRowH + kLineGap : kRowH;
		y += boxes[i].h + kRowGap;
	}

	return y;
}

// How far the list must be scrolled for every row up to and including `selected`
// to fit above the bottom edge.
//
// Derived from `selected` alone rather than accumulated across frames, so it
// needs no state to keep in step and cannot drift out of sync with the cursor.
// The property that makes it feel right: moving down only ever scrolls further,
// so the list does not jump back up under the cursor on the way down.
int ScrollFor(const RowBox boxes[], int count, int selected)
{
	int scroll = 0;

	if (selected < 0)
		selected = 0;
	if (selected >= count)
		selected = count - 1;

	for (int i = 0; i <= selected; ++i)
	{
		const int bottom = boxes[i].y + boxes[i].h;

		if (bottom > kListBottom)
			scroll = bottom - kListBottom;
	}

	return scroll;
}

SDL_Surface *SettingsView::BuildRows(const Settings &settings, int selected) const
{
	RowBox boxes[16];

	// kRows is a fixed 7 and this is 16. Not worth a length check on a constant
	// table, but the bound is named so the two are visibly related.
	const int count = (kRowCount <= 16) ? kRowCount : 16;

	LayoutRows(boxes, count);
	const int scroll = ScrollFor(boxes, count, selected);

	SDL_Surface *s = SDL_CreateRGBSurfaceWithFormat(0, kColLW, kHeight, 32,
	                                                 kOpaqueFormat);
	if (!s)
		return NULL;

	paint::Clear(s, kBackground);

	// Clip to the list area for the duration. A row scrolled half off the top
	// would otherwise paint over the page's header, and a half-off row at the
	// bottom would paint over the footer.
	SDL_Rect clip = { 0, kListTop, kColLW, kListBottom - kListTop };
	SDL_SetClipRect(s, &clip);

	// RPC heading. Pinned rather than scrolled: it names the section the list
	// opens with, and it is the one label that should stay put when the list
	// moves underneath it.
	paint::TextTracked(s, 8, kListTop + 14, "RPC", sectionFace_, kTracking,
	                   kSectionLabel, 255);

	for (int i = 0; i < count; ++i)
	{
		const Row &row = kRows[i];
		const bool sel = (i == selected);

		// Scrolled position.
		int y = boxes[i].y - scroll;

		// Entirely off the list: nothing to draw, and drawing it anyway is what
		// made this page slow once the rows grew.
		if (y + boxes[i].h < kListTop || y > kListBottom)
			continue;

		if (boxes[i].labelY >= 0)
		{
			const int ly = boxes[i].labelY - scroll;

			if (ly + 14 >= kListTop && ly < kListBottom)
				paint::TextTracked(s, 8, ly + 14, "FIXES", sectionFace_, kTracking,
				                   kSectionLabel, 255);
		}

		const int h = row.twoLine ? kRowH + kLineGap : kRowH;

		paint::FillRoundedGradientV(s, 0, y, kColLW, h, kRowRadius,
		                            kRowFillTop, kRowFill, 255);

		if (sel)
		{
			// Same halo treatment as a selected menu card, so the two pages match.
			// The halo is already concentric: outset 3 with radius + 3 leaves the
			// arc centre exactly where the fill's is.
			paint::StrokeRounded(s, -3, y - 3, kColLW + 6, h + 6, kRowRadius + 3,
			                     4, kRowBorderSel, 70);
			paint::StrokeRounded(s, 1, y + 1, kColLW - 2, h - 2,
			                     kRowRadius - kBorderInset,
			                     3, kRowBorderSel, 255);
		}
		else
		{
			paint::StrokeRounded(s, 1, y + 1, kColLW - 2, h - 2,
			                     kRowRadius - kBorderInset, 1,
			                     kRowBorder, 180);
		}

		// Baselines, as fractions of the row's own height rather than fixed pixel
	// offsets. kRowH changed from 108 to 92 when the token row was added, and
	// hardcoded offsets would have left every description sitting 16px past the
	// bottom of its row.
const int kTitleDySingle = kRowH / 2 - 2;    // 44
const int kDescDySingle  = kRowH / 2 + 26;   // 72
const int kTitleDyDouble = kRowH / 2 - 6;    // 40, in a row of kRowH + kLineGap
const int kDescDyDouble  = kRowH / 2 + 34;   // 80
const int kFieldDy       = kRowH / 2 + 7;    // 53, single-line text fields

// An action row carries the FIXES glyph where a toggle would sit.
		if (row.setting < 0)
		{
			DrawActionIcon(s, kRowPadX, y + h / 2,
			               sel ? actionIconSel_ : actionIconIdle_);
			paint::Text(s, kRowPadX + 44, y + h / 2 + 8, row.title, titleFace_,
			            sel ? kTitleNormal : kDescNormal, 255);
		}
		else if (Settings::IsToggle(row.setting))
		{
			const bool on = settings.GetToggle(row.setting);

			paint::Text(s, kRowPadX,
			            y + (row.twoLine ? kTitleDyDouble : kTitleDySingle),
			            row.title, titleFace_, kTitleNormal, 255);

			if (row.desc)
			{
				paint::Text(s, kRowPadX,
				            y + (row.twoLine ? kDescDyDouble : kDescDySingle),
				            row.desc, descFace_, kDescNormal, 255);
			}

			paint::Toggle(s, kColLW - kRowPadX - kToggleW,
			              y + h / 2 - kToggleH / 2, kToggleW, kToggleH, on,
			              kTrackOn, kTrackOff, kKnobOn, kKnobOff);
		}
		else
		{
			// Two text fields -- the token and the custom message -- sharing a
			// shape. They differ in what is shown on the right: a credential is
			// masked, a presence line is shown as typed.
			const bool isToken = (row.setting == kSetToken);
			const std::string &text = isToken ? settings.Token()
			                                  : settings.CustomMessage();

			// The shipped placeholder is not a token. Counting it as one would show
			// a masked "SET_ME" on a console that has never had a token entered,
			// which reads as though one were set.
			const bool unset = text.empty() || (isToken && text == "SET_ME");

			char shown[64];

			if (unset)
			{
				snprintf(shown, sizeof shown, "press X to edit");
			}
			else if (isToken)
			{
				// Masked, keeping the last four so a user holding more than one
				// token can tell which is loaded. Printing a working credential
				// across a screen someone may be filming is not worth the
				// convenience.
				snprintf(shown, sizeof shown, "........%s",
				         text.size() > 4 ? text.c_str() + text.size() - 4 : "");
			}
			else
			{
				// Truncated: a long presence line would otherwise run leftwards
				// off the row and under its own title.
				snprintf(shown, sizeof shown, "%s%s",
				         text.size() > 30 ? text.substr(0, 29).c_str()
				                          : text.c_str(),
				         text.size() > 30 ? "..." : "");
			}

			paint::Text(s, kRowPadX, y + kFieldDy, row.title, titleFace_, kTitleNormal,
			            255);

			const int valueW = paint::TextWidth(shown, smallFace_);

			paint::Text(s, kColLW - kRowPadX - valueW, y + kFieldDy, shown,
			            smallFace_, unset ? kChromeText : kTitleNormal, 255);
		}
	}

	SDL_SetClipRect(s, NULL);

	if (!settings.Available())
	{
		// Everything on this page edits a config that is not there. Dimming the
		// whole column says so without needing a word for it; the reason is spelled
		// out in the status line the caller sets on the way in. Laid over the rows
		// rather than instead of them, so the page still reads as a page -- and
		// after the clip is released, so it covers the pinned heading too.
		paint::FillRect(s, 0, kListTop - 60, kColLW, kListBottom - kListTop + 60,
		                kBackground, 150);
	}

	return s;
}

// ---- right panel ----------------------------------------------------------

SDL_Surface *SettingsView::BuildPanel(int tab, const Settings &settings) const
{
	SDL_Surface *s = SDL_CreateRGBSurfaceWithFormat(0, kColRW, kHeight, 32,
	                                                 kOpaqueFormat);
	if (!s)
		return NULL;

	paint::Clear(s, kBackground);

	const int panelH = kPanelBottom - kPanelTop;

	// Outer panel.
	paint::FillRoundedGradientV(s, 0, kPanelTop, kColRW, panelH, kPanelRadius, kPanelFill,
	                            kPanelFill, 255);
	paint::StrokeRounded(s, 1, kPanelTop + 1, kColRW - 2, panelH - 2,
	                     kPanelRadius - kBorderInset, 1,
	                     kPanelBorder, 255);

	BuildTabBar(s, tab);

	const Preview &preview = kPreviews[tab];
	const int cardW = kColRW - kCardX * 2;

	// Empty when the toggle is off, which is what hides the line. The preview
	// table's own firmware field is the fallback for the case where the console
	// would not tell us -- a placeholder beats a blank card, since a card with
	// no firmware at all reads as a rendering fault rather than a setting.
	const char *firmware = settings.ShowFirmware()
		? (firmware_.empty() ? preview.firmware : firmware_.c_str())
		: "";

	int y = kPanelTop + kTabBarH + kCardY;

	for (int i = 0; i < preview.cardCount; ++i)
	{
		BuildPreviewCard(s, preview.cards[i], kCardX, y, cardW, firmware);
		y += kCardH + kCardGap;
	}

	return s;
}

void SettingsView::BuildTabBar(SDL_Surface *s, int tab) const
{
	const int barY = kPanelTop + 12;
	const int barH = 48;

	// Measure the tabs so they can be centred as a group.
	int widths[3];
	int total = 0;

	for (int i = 0; i < kTabCount; ++i)
	{
		widths[i] = paint::TextWidth(kPreviews[i].tabLabel, tabFace_) + kTabPadX * 2;
		total += widths[i] + (i < kTabCount - 1 ? kTabGap : 0);
	}

	// Shoulder buttons sit hard against each side, icon only.
	//
	// The pill takes its height from the artwork rather than the reverse: the
	// sources are landscape, so a fixed square would leave the glyph floating with
	// slack above and below.
	const int artH = l1_.loaded ? l1_.surface->h : kShoulderSize;
	const int artW = l1_.loaded ? l1_.surface->w : kShoulderSize;
	const int pillH = artH + kShoulderPad * 2;
	const int pillW = artW + kShoulderPad * 2;

	// The bar is 48px tall; centre the pills within whatever height they came out.
	const int pillY = barY + (kTabBarH - 12 - pillH) / 2;

	paint::FillRounded(s, 18, pillY, pillW, pillH, pillH / 3, kPanelCard, 255);
	paint::FillRounded(s, kColRW - 18 - pillW, pillY, pillW, pillH, pillH / 3,
	                    kPanelCard, 255);

	if (l1_.loaded)
		paint::BlitAlpha(s, l1_.surface, 18 + kShoulderPad, pillY + kShoulderPad);

	if (r1_.loaded)
		paint::BlitAlpha(s, r1_.surface,
		                 kColRW - 18 - pillW + kShoulderPad, pillY + kShoulderPad);

	int x = (kColRW - total) / 2;

	for (int i = 0; i < kTabCount; ++i)
	{
		const bool active = (i == tab);
		const std::string label = kPreviews[i].tabLabel;
		const int tx = x + kTabPadX;

		paint::Text(s, tx, barY + barH / 2 + 7, label, tabFace_,
		            active ? kTabActive : kTabInactive, 255);

		if (active)
		{
			const int uw = paint::TextWidth(label, tabFace_);
			paint::FillRounded(s, tx, barY + barH - 4, uw, kTabUnderline, 1,
			                    kAccent, 255);
		}

		x += widths[i] + kTabGap;
	}
}

void SettingsView::BuildPreviewCard(SDL_Surface *s, const PreviewCard &card,
                                    int x, int y, int w,
                                    const char *firmware) const
{
	paint::FillRounded(s, x, y, w, kCardH, kCardRadius, kPanelCard, 255);
	paint::StrokeRounded(s, x + 1, y + 1, w - 2, kCardH - 2,
	                     kCardRadius - kBorderInset, 1,
	                     kPanelBorder, 160);

	// Heading, with the overflow menu at the opposite corner.
	paint::Text(s, x + kCardPad, y + kCardHeadBaseline, card.heading, titleFace_,
	            kTitleNormal, 255);
	DrawDots(s, x + w - kCardPad - 26, y + 20, kChromeText);

	// Icon tile.
	const GfxIcon *icon = FindArt(card.icon);
	const int iconX = x + kCardPad;
	const int iconY = y + kIconPadY;

	if (icon && icon->loaded)
		paint::BlitAlpha(s, icon->surface, iconX, iconY);
	else
		DrawPlaceholder(s, iconX, iconY, kIconSize);

	// The PS badge overlaps the icon's bottom-right corner. Sized well below the
	// icon and inset from two edges, so it reads as an overlay rather than a
	// second icon.
	if (card.badge && psLogoSmall_.loaded)
	{
		const int badgeX = iconX + kIconSize - kBadgeSize + kBadgeOverlap;
		const int badgeY = iconY + kIconSize - kBadgeSize + kBadgeOverlap;

		paint::BlitAlpha(s, psLogoSmall_.surface, badgeX, badgeY);
	}

	const int textX = iconX + kIconSize + kTextPadX;

	paint::Text(s, textX, y + kCardTitleBaseline, card.title, titleFace_,
	            kTitleNormal, 255);

	if (card.subtitle)
		paint::Text(s, textX, y + kCardSubBaseline, card.subtitle, smallFace_,
		            kSubtitle, 255);

	// Driven by the same toggle the daemon reads, so the preview shows what the
	// presence would actually contain rather than always showing a line. When it
	// is off the space is simply left empty: the mock's baselines are fixed, and
	// reflowing them per state would make the card jump on every toggle press.
	if (firmware && firmware[0])
		paint::Text(s, textX, y + kCardFirmBaseline, firmware, smallFace_,
		            kSubtitle, 255);

	if (card.time)
	{
		DrawLinkGlyph(s, textX, y + kCardTimeBaseline - 6, kTimerText);
		paint::Text(s, textX + 14, y + kCardTimeBaseline, card.time, smallFace_,
		            kTimerText, 255);
	}
}

const GfxIcon *SettingsView::FindArt(const char *name) const
{
	if (!name)
		return NULL;

	if (strcmp(name, "youtube") == 0)
		return &youtube_;
	if (strcmp(name, "naruto_storm_connections") == 0)
		return &naruto_;
	if (strcmp(name, "images_web_browser") == 0)
		return &webBrowser_;
	if (strcmp(name, "ps_logo_big") == 0)
		return &psLogoBig_;
	if (strcmp(name, "ps_logo_small") == 0)
		return &psLogoSmall_;

	return NULL;
}

// ---- footer ---------------------------------------------------------------

void SettingsView::DrawFooter() const
{
	const int y = kHeight - kFooterH;

	paint::FillRect(dst_, 0, y, kWidth, kFooterH, kBackground, 255);

	// Centred group, same button art as the menu.
	const int artSize = 34;
	const int gap = 12;
	const int groupGap = 46;

	const int entries = 3;
	int widths[3];
	int total = 0;

	widths[0] = artSize + gap + paint::TextWidth("Navigate", smallFace_);
	widths[1] = artSize + gap + paint::TextWidth("Select", smallFace_);
	widths[2] = artSize + gap + paint::TextWidth("Back", smallFace_);

	for (int i = 0; i < entries; ++i)
		total += widths[i] + (i < entries - 1 ? groupGap : 0);

	int x = (kWidth - total) / 2;
	const int baseline = y + kFooterH / 2 + kSizeSmall / 2;
	const int artY = y + (kFooterH - artSize) / 2;

	// Missing art must not take the labels with it: only draw the text when the
	// glyph it belongs to is there.
	if (navigate_.loaded)
		paint::BlitAlpha(dst_, navigate_.surface, x, artY);
	if (navigate_.loaded)
		paint::Text(dst_, x + artSize + gap, baseline, "Navigate", smallFace_,
		            kChromeText, 255);
	x += widths[0] + groupGap;

	if (select_.loaded)
		paint::BlitAlpha(dst_, select_.surface, x, artY);
	paint::Text(dst_, x + artSize + gap, baseline, "Select", smallFace_,
	            kChromeText, 255);
	x += widths[1] + groupGap;

	if (back_.loaded)
		paint::BlitAlpha(dst_, back_.surface, x, artY);
	paint::Text(dst_, x + artSize + gap, baseline, "Back", smallFace_,
	            kChromeText, 255);
}

// ---- entry point ----------------------------------------------------------

void SettingsView::Draw(const Settings &settings, int selected, int tab)
{
	if (!ready_ || !dst_)
		return;

	if (tab < 0)
		tab = 0;
	if (tab >= kTabCount)
		tab = kTabCount - 1;

	paint::Clear(dst_, kBackground);

	// Title.
	//
	// Centred on the left column, not on the screen. Centring on kWidth put the
	// text at about x=895, and the right panel is blitted over the screen from
	// kColRX = 961 afterwards -- so the panel covered everything past that and
	// the title rendered as "Setti". The title belongs to the column the RPC
	// rows are in, and it has to be placed against that column for the same
	// reason.
	{
		const std::string title = "Settings";
		const int w = paint::TextWidth(title, titleFace_);
		const int leftCentre = (kColLX + kColRX) / 2;

		paint::Text(dst_, leftCentre - w / 2, kTitleBaseline, title, titleFace_,
		            kAccent, 255);
	}

	// Left column. Rebuilt only when a row's content or the selection changes, so
	// moving the cursor does not re-rasterise the preview and vice versa.
	const std::string key = BuildRowKey(settings, selected);

	if (!rows_ || key != rowsKey_)
	{
		if (rows_)
			SDL_FreeSurface(rows_);

		rows_ = BuildRows(settings, selected);
		rowsKey_ = key;
	}

	if (rows_)
		paint::BlitOpaque(dst_, rows_, kColLX, 0);

	// Right panel. Rebuilt when the tab changes, and also when the firmware
	// toggle does -- the panel is where that toggle is demonstrated, so keying
	// the cache on the tab alone would leave the card showing a line the user
	// has just switched off.
	const bool showFirmware = settings.ShowFirmware();

	if (!panel_ || panelTab_ != tab || panelFirmware_ != showFirmware)
	{
		if (panel_)
			SDL_FreeSurface(panel_);

		panel_ = BuildPanel(tab, settings);
		panelTab_ = tab;
		panelFirmware_ = showFirmware;
	}

	if (panel_)
		paint::BlitOpaque(dst_, panel_, kColRX, 0);

	DrawFooter();
}