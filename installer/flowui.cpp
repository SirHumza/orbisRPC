/* flowui.cpp - drawn dialogs for the install flow.
 *
 * Everything here rasterises through paint::, the same as the menu and Settings.
 * There is no GPU on this platform, so the cost is the thing to watch: the frame
 * is a dimmed copy, one rounded panel and a few hundred glyphs, which is cheap.
 * The backdrop is a flat fill rather than a copy of the previous frame -- the
 * install flow owns the screen outright and no page is live underneath it.
 */

#include "flowui.h"

#include <stdio.h>
#include <string.h>

#include <vector>

#include "input.h"
#include "paint.h"

namespace {

const int kScreenW = 1920;
const int kScreenH = 1080;

const char *const kUiFont     = "/app0/assets/fonts/SegUIVar.ttf";
const char *const kMonoFont   = "/app0/assets/fonts/VeraMono.ttf";

const int kTitleSize  = 40;
const int kBodySize   = 27;
const int kButtonSize = 25;

const int kPanelW     = 900;
const int kPanelPadX  = 56;
const int kPanelPadY  = 46;
const int kPanelGap   = 34;   // title baseline to first body line
const int kPanelRadius = 18;

} // namespace

FlowUi::FlowUi()
	: surface_(NULL), console_(NULL), input_(NULL), ftLib_(NULL),
	  mode_(kNone), focus_(0), cancelled_(false), spinner_(0), ready_(false)
{
	palette_.backdrop     = Color{   0,   0,   0 };
	palette_.panel        = Color{  24,  28,  38 };
	palette_.panelEdge    = Color{  62,  70,  88 };
	palette_.title        = Color{ 250, 200,  40 };
	palette_.body         = Color{ 214, 222, 235 };
	palette_.dim          = Color{ 150, 162, 180 };
	palette_.accent       = Color{ 250, 200,  40 };
	palette_.focusText    = Color{  16,  18,  24 };
	palette_.inactiveText = Color{ 150, 162, 180 };
	palette_.spinner      = Color{ 250, 200,  40 };
}

bool FlowUi::Init(SDL_Surface *surface, Console *console, Input *input,
                  FT_Library ftLib)
{
	surface_ = surface;
	console_ = console;
	input_ = input;
	ftLib_ = ftLib;

	if (!surface || !console || !input || !ftLib)
		return false;

	// Load the faces once. A missing font leaves the flow unusable, and silently
	// drawing nothing is worse than telling main.cpp to keep the system dialogs.
	if (!paint::Face(kUiFont, kTitleSize) || !paint::Face(kUiFont, kBodySize) ||
	    !paint::Face(kUiFont, kButtonSize))
		return false;

	ready_ = true;
	return true;
}

// ---- text ------------------------------------------------------------------

void FlowUi::DrawText(int x, int baseline, const std::string &s, FT_Face face,
                      Color c)
{
	if (!s.empty() && face)
		paint::Text(surface_, x, baseline, s, face, c, 255);
}

std::vector<std::string> FlowUi::Wrap(const std::string &text, FT_Face face,
                                      int maxWidth) const
{
	std::vector<std::string> lines;

	if (!face)
		return lines;

	// Split on newlines first so an authored blank line survives, then greedy-wrap
	// each paragraph. paint::Text measures, so no font metrics are needed here.
	size_t pos = 0;

	for (;;)
	{
		const size_t nl = text.find('\n', pos);
		const std::string para = (nl == std::string::npos)
		    ? text.substr(pos)
		    : text.substr(pos, nl - pos);

		if (para.empty())
		{
			lines.push_back(std::string());
		}
		else
		{
			std::string line;
			size_t i = 0;

			while (i < para.size())
			{
				// Next whitespace-delimited token.
				while (i < para.size() && para[i] == ' ') ++i;
				if (i >= para.size()) break;

				const size_t sp = para.find(' ', i);
				const std::string tok = (sp == std::string::npos)
				    ? para.substr(i)
				    : para.substr(i, sp - i);

				const std::string candidate = line.empty() ? tok : line + " " + tok;

				if (!line.empty() && paint::TextWidth(candidate, face) > maxWidth)
				{
					lines.push_back(line);
					line = tok;
				}
				else
				{
					line = candidate;
				}

				if (sp == std::string::npos) break;
				i = sp + 1;
			}

			if (!line.empty())
				lines.push_back(line);
		}

		if (nl == std::string::npos)
			break;

		pos = nl + 1;
	}

	return lines;
}

// ---- frame -----------------------------------------------------------------

void FlowUi::DrawPanel(int w, int h, int &outX, int &outY)
{
	outX = (kScreenW - w) / 2;
	outY = (kScreenH - h) / 2;

	// Dimmed app behind. A flat fill rather than a copy of whatever was last
	// drawn: the flow owns the screen and nothing is live underneath it.
	paint::Clear(surface_, palette_.backdrop);

	paint::FillRounded(surface_, outX, outY, w, h, kPanelRadius, palette_.panel,
	                   246);
	paint::StrokeRounded(surface_, outX, outY, w, h, kPanelRadius, 2,
	                     palette_.panelEdge, 200);
}

// Text-only pill.
//
// There was a drawn ring and cross here before, so the pad buttons were shown
// inside each pill. Removed: the ring was decoration that read as a radio button
// rather than an action, and it left "Yes" looking selectable-but-not-pressed.
// Which pad button does what is carried by the fill instead -- one option filled,
// the other empty -- plus circle as a shortcut for No.
void FlowUi::DrawButton(int x, int baseline, const char *label, bool focused)
{
	FT_Face face = paint::Face(kUiFont, kButtonSize);
	if (!face)
		return;

	const int h = kButtonSize + 26;
	const int y = baseline - kButtonSize - 9;
	const int labelW = paint::TextWidth(label, face);
	const int w = 26 + labelW + 26;

	// The pill. Only the focused one is filled, so the unfocused option recedes
	// rather than competing.
	if (focused)
		paint::FillRounded(surface_, x, y, w, h, h / 2, palette_.accent, 255);

	const Color c = focused ? palette_.focusText : palette_.inactiveText;
	paint::Text(surface_, x + 26, baseline, label, face, c, 255);
}

void FlowUi::Draw()
{
	if (!ready_ || mode_ == kNone || !surface_)
		return;

	FT_Face titleFace = paint::Face(kUiFont, kTitleSize);
	FT_Face bodyFace  = paint::Face(kUiFont, kBodySize);
	FT_Face buttonFace = paint::Face(kUiFont, kButtonSize);

	const int inner = kPanelW - kPanelPadX * 2;
	const int lineH = kBodySize + 10;

	const std::vector<std::string> lines = Wrap(body_, bodyFace, inner);

	// Measured PANEL-RELATIVE, then offset by the panel's own screen position.
	//
	// Getting this wrong is what put the title and body above the panel in the
	// first version: the offsets below ran from 0 while the panel itself was
	// drawn centred, so every glyph landed near the top of the screen instead of
	// inside the box.
	const int titleRel  = kPanelPadY + kTitleSize;
	const int bodyRel   = titleRel + kPanelGap;
	const int buttonRel = bodyRel + static_cast<int>(lines.size()) * lineH + 40 +
	                     kButtonSize;
	const int panelH    = buttonRel + kPanelPadY;

	int px = 0, py = 0;
	DrawPanel(kPanelW, panelH, px, py);

	const int tx = px + kPanelPadX;

	DrawText(tx, py + titleRel, title_, titleFace, palette_.title);

	for (size_t i = 0; i < lines.size(); ++i)
		DrawText(tx, py + bodyRel + static_cast<int>(i) * lineH, lines[i],
		         bodyFace, palette_.body);

	if (mode_ == kMessage || mode_ == kConfirm)
	{
		// Button width is computed the same way DrawButton lays it out, so the
		// centring cannot drift from the pill.
		const int pad = 52;

		if (mode_ == kMessage)
		{
			// Centred: a single dismissal has nothing to choose between.
			const int w = paint::TextWidth("OK", buttonFace) + pad;
			DrawButton(px + (kPanelW - w) / 2, py + buttonRel, "OK", true);
		}
		else
		{
			const int yesW = paint::TextWidth("Yes", buttonFace) + pad;
			const int noW  = paint::TextWidth("No", buttonFace) + pad;
			const int gap  = 36;
			const int bx   = px + (kPanelW - (yesW + gap + noW)) / 2;

			DrawButton(bx, py + buttonRel, "Yes", focus_ == 0);
			DrawButton(bx + yesW + gap, py + buttonRel, "No", focus_ == 1);
		}
	}
	else if (mode_ == kProgress)
	{
		// Indeterminate: animated dots rather than a percentage, because the byte
		// count is not known until the transfer has already finished.
		const int cy = py + buttonRel - kButtonSize / 2;
		const int sx = px + kPanelPadX;

		// A faint dot holds the row's left edge so the highlight has somewhere to
		// travel from.
		paint::FillRounded(surface_, sx, cy - 6, 12, 12, 6, palette_.spinner, 40);

		for (int i = 0; i < 4; ++i)
		{
			const int lit = (spinner_ + i) % 4;
			paint::FillRounded(surface_, sx + 28 + i * 24, cy - 6, 12, 12, 6,
			                   palette_.spinner, lit == 0 ? 255 : 70);
		}

		if (!status_.empty())
		{
			FT_Face dim = paint::Face(kUiFont, kBodySize - 3);
			DrawText(sx + 28 + 4 * 24 + 20, cy + 8, status_, dim, palette_.dim);
		}
	}
}

// ---- input -----------------------------------------------------------------

void FlowUi::Tick()
{
	if (!ready_ || mode_ == kNone)
		return;

	if (input_)
	{
		input_->Update();

		if (mode_ == kConfirm)
		{
			if (input_->Pressed(kPadRight))
				focus_ = 1;
			else if (input_->Pressed(kPadLeft))
				focus_ = 0;
		}
	}

	if (mode_ == kProgress)
		spinner_++;

	Draw();

	if (console_)
		console_->Present();
}

// ---- screens ---------------------------------------------------------------

void FlowUi::ShowMessage(const std::string &title, const std::string &body)
{
	mode_ = kMessage;
	title_ = title;
	body_ = body;
	focus_ = 0;

	// Draw once before waiting, so the screen is on the display immediately rather
	// than after the first tick.
	Draw();
	if (console_)
		console_->Present();

	// Ignores the first frames so a button held to dismiss the previous screen
	// does not also dismiss this one.
	for (int i = 0; i < 8; ++i)
		Tick();

	cancelled_ = false;

	for (;;)
	{
		Tick();

		if (!input_)
			break;

		if (input_->Pressed(kPadCross))
			break;

		// Circle is Back. On a screen with only one button there is no answer to
		// give it, so it is recorded as a way out rather than treated as X --
		// otherwise holding Circle walks straight through the whole flow.
		if (input_->Pressed(kPadCircle))
		{
			cancelled_ = true;
			break;
		}

		SDL_Delay(16);
	}

	mode_ = kNone;
}

bool FlowUi::ShowConfirm(const std::string &title, const std::string &body)
{
	mode_ = kConfirm;
	title_ = title;
	body_ = body;
	focus_ = 0;

	// Cleared so a Circle on an earlier one-button screen cannot be mistaken for
	// a cancel of this one. Here Circle is simply No, and the caller asked for an
	// answer rather than a way out.
	cancelled_ = false;

	for (int i = 0; i < 8; ++i)
		Tick();

	for (;;)
	{
		Tick();

		if (!input_)
			break;

		// X takes whichever button is focused, so the answer is never accidental:
		// default focus is Yes, and moving to No is deliberate.
		if (input_->Pressed(kPadCross))
			break;

		if (input_->Pressed(kPadCircle))
		{
			focus_ = 1;
			break;
		}

		SDL_Delay(16);
	}

	const bool yes = (focus_ == 0);
	mode_ = kNone;
	return yes;
}

void FlowUi::ShowProgress(const std::string &title, const std::string &status)
{
	mode_ = kProgress;
	title_ = title;
	body_ = "";
	status_ = status;
	spinner_ = 0;

	Tick();
}

void FlowUi::SetProgressStatus(const std::string &status)
{
	status_ = status;
}

void FlowUi::HideProgress()
{
	mode_ = kNone;
	status_.clear();
}
