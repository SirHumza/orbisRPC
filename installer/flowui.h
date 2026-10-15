/* flowui.h - the drawn dialogs used by the install flow.
 *
 * One screen per step, drawn with the same primitives as the menu and Settings so
 * the flow reads as part of this app rather than a PS4 system dialog appearing on
 * top of it.
 *
 * Layout is fixed: a dimmed app behind, a centred panel, title, wrapped body, and
 * a button row. What changes between screens is the content and whether the
 * button row is one or two entries.
 */

#ifndef INSTALLER_FLOWUI_H
#define INSTALLER_FLOWUI_H

#include <string>
#include <vector>

#include <SDL2/SDL.h>
#include <proto-include.h> // FT_Library

#include "console.h" // Color

class Input;

// The colours match main.cpp's menu palette so the two do not drift apart. Kept
// as literals here rather than shared because the Settings page keeps its own set
// for the same reason.
struct FlowPalette
{
	Color backdrop;   // over the app behind
	Color panel;
	Color panelEdge;
	Color title;
	Color body;
	Color dim;
	Color accent;     // the focused button
	Color focusText;
	Color inactiveText;
	Color spinner;
};

class FlowUi
{
public:
	FlowUi();

	// Borrows everything. Returns false only if the fonts cannot be loaded, in
	// which case the caller should keep using the system dialogs rather than show
	// a blank panel.
	bool Init(SDL_Surface *surface, Console *console, Input *input,
	          FT_Library ftLib);

	bool Ready() const { return ready_; }

	// ---- screens --------------------------------------------------------

	// One button. Blocks until X or O.
	void ShowMessage(const std::string &title, const std::string &body);

	// Two buttons. Blocks. Returns true for Yes.
	bool ShowConfirm(const std::string &title, const std::string &body);

	// True when the screen just shown was dismissed with Circle rather than X.
	//
	// Circle is the console's Back button, so on a one-button screen there is no
	// "No" to give it -- it used to behave exactly like X, which meant it could
	// not leave anything either. This is how a caller tells the two apart without
	// ShowMessage having to return a value it has no use for otherwise.
	//
	// Reset at the start of every screen, so it always describes the last one
	// shown and never a stale answer to a question nobody asked again.
	//
	// Always false after ShowConfirm: there, Circle means No, which is an answer
	// the caller asked for rather than a way out of the flow.
	bool Cancelled() const { return cancelled_; }

	// Progress, non-blocking. The spinner advances on each Tick.
	void ShowProgress(const std::string &title, const std::string &status);
	void SetProgressStatus(const std::string &status);
	void HideProgress();

	// Pumps input and presents one frame. Called by the flow between steps and on
	// every network tick, so the spinner animates while a request is in flight.
	void Tick();

private:
	enum Mode
	{
		kNone,
		kMessage,
		kConfirm,
		kProgress
	};

	// Draws the whole frame for the current mode.
	void Draw();

	// Fills the backdrop and the panel, reporting the panel's screen position so
	// the text can be offset from it. Text is laid out panel-relative and then
	// translated; mixing the two is what put the first version's text outside the
	// panel.
	void DrawPanel(int w, int h, int &outX, int &outY);

	void DrawText(int x, int baseline, const std::string &s, FT_Face face,
	              Color c);

	// Greedy word wrap, because none of the paint primitives do it and every
	// message here is a sentence rather than a fixed-width table cell.
	// Newlines start a new line and a blank one is preserved.
	std::vector<std::string> Wrap(const std::string &text, FT_Face face,
	                              int maxWidth) const;

	// Text-only pill. No button glyphs: a drawn ring inside each pill read as a
	// radio button rather than an action, and which pad button does what is
	// carried by the fill instead.
	void DrawButton(int x, int baseline, const char *label, bool focused);

	SDL_Surface *surface_;
	Console      *console_;
	Input        *input_;
	FT_Library    ftLib_;

	FlowPalette  palette_;

	Mode      mode_;
	std::string title_;
	std::string body_;
	std::string status_;

	int focus_;      // 0 = Yes / OK, 1 = No
	bool cancelled_; // last one-button screen was dismissed with Circle
	int spinner_;    // frames, drives the arc angle
	bool ready_;
};

#endif
