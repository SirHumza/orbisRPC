/* main.cpp - orbisRPC app shell.
 *
 * Owns the menu and the Settings page, and calls into the installer flow and
 * the system dialogs. Two UIs are bridged here:
 *
 *   - the form and preview are drawn with our own console (SDL surface)
 *   - text entry uses the system IME via ui_input() from ui.c
 *
 * ui_input() is synchronous: it spins internally until the dialog closes. So
 * the render loop simply does not present while it is up, and the IME overlay
 * sits on top of our last frame. Same reason the install wizard hides the
 * splash before opening dialogs.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL2/SDL.h>
#include <orbis/Sysmodule.h>
#include <orbis/SystemService.h>

#include <proto-include.h> // FT_Library

#include "console.h"
#include "dbglog.h"
#include "input.h"
#include "menu.h"
#include "paint.h"
#include "settings.h"
#include "settingsview.h"
#include "sysinfo.h"

#include "installer_flow.h"
#include "uninstall.h"
#include "settingsaction.h"
#include "icfg.h"
#include "ui.h"
#include "uibridge.h"

namespace {

const int kWidth  = 1920;
const int kHeight = 1080;

// Menu's own copy of the screen height, used to place transient status text.
const int kScreenH = kHeight;

const char *const kBodyFontPath  = "/app0/assets/fonts/VeraMono.ttf";
const int         kBodyFontSize  = 30;
const char *const kTitleFontPath = "/app0/assets/fonts/Bebas-Regular.ttf";
const int         kTitleFontSize = 44;

// Vertical space the home page's footer occupies. The status line is drawn just
// above it so it never collides with the button hints.
const int kFooterHintH = 76;

// Menu colours. The Settings page keeps its own palette in settingsview.cpp, tuned
// to the mockup rather than shared, so these two do not need to agree exactly.
const Color kBackground = {  10,  11,  16 };
const Color kAccent    = { 250, 200,  40 };
const Color kBody      = { 214, 222, 235 };
const Color kDim       = { 150, 162, 180 };
const Color kWarn      = { 245, 185,  92 };
const Color kSelection = {  26,  56, 104 };
const Color kPanel     = {  24,  28,  38 };
const Color kDivider   = {  44,  50,  62 };
const Color kGood      = { 122, 220, 150 };

enum class Screen { Menu, Settings };

// The home screen is three cards, matching the mockup. Cards are MenuCard values
// rather than a label/target pair, because Menu owns the wording and the art.
const int kMenuCount = kCardCount;
const int kMenuInstall = kCardInstall;

std::string Pad(const std::string &text, int width)
{
	std::string out = text;

	// Truncate if too long, then pad, so columns never shift.
	if (static_cast<int>(out.size()) > width)
		out.resize(width);

	while (static_cast<int>(out.size()) < width)
		out += ' ';

	return out;
}




} // namespace

int main()
{
	setvbuf(stdout, NULL, _IONBF, 0);

	SDL_Log("orbisRPC starting");

	/* Hide the PS4 splash first, before anything else. Two reasons, both
	 * learned the hard way in the original wizard: dialogs opened while the
	 * splash is still up get auto-dismissed or never surface, and loading the
	 * system-UI modules underneath a live splash leaves the splash on screen
	 * forever. Must be the first statement. */
	sceSystemServiceHideSplashScreen();

	if (SDL_Init(SDL_INIT_VIDEO) != 0)
	{
		SDL_Log("Failed to initialize SDL: %s", SDL_GetError());
		for (;;)
			;
	}

	// FreeType has to be force-loaded or its NIDs stay unresolved.
	if (sceSysmoduleLoadModule(ORBIS_SYSMODULE_FREETYPE_OL) < 0)
	{
		SDL_Log("Failed to load the freetype module");
		for (;;)
			;
	}

	FT_Library ftLib = NULL;
	if (FT_Init_FreeType(&ftLib) != 0)
	{
		SDL_Log("Failed to initialize freetype");
		for (;;)
			;
	}

	SDL_Window *window = SDL_CreateWindow("orbisRPC",
		SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
		kWidth, kHeight, 0);

	if (!window)
	{
		SDL_Log("Failed to create window: %s", SDL_GetError());
		for (;;)
			;
	}

	Console console(window, ftLib, kBodyFontPath, kBodyFontSize,
	                kTitleFontPath, kTitleFontSize, kWidth, kHeight);

	if (!console.Ready())
	{
		SDL_Log("Console failed to initialise");
		for (;;)
			;
	}

	console.SetBodyColor(kBody);

	// Diagnostics to a file, before anything else can fail.
	dbglog::Begin();
	dbglog::Write("main: starting, screen %dx%d", kWidth, kHeight);

	paint::SetLibrary(ftLib);

	SDL_Surface *windowSurface = SDL_GetWindowSurface(window);

	// The surface starts as whatever SDL allocated, so paint it before the first
	// present rather than showing uninitialised framebuffer contents.
	if (windowSurface)
		paint::Clear(windowSurface, kBackground);

	Menu menu;
	if (!windowSurface || !menu.Init(windowSurface, ftLib))
		SDL_Log("Menu failed to initialise; the home page will be blank");

	// Settings shares the same surface. Written before it, so the last thing drawn
	// on a given frame is whichever page is on top.
	SettingsView settingsView;
	if (!settingsView.Init(windowSurface, ftLib))
		dbglog::Write("Settings failed to initialise; the page will be blank");

	Input input;
	if (!input.Init())
		SDL_Log("No controller yet, will keep retrying");

	// Dialog modules have to be up before ui_input() can be used, and the
	// installer's reap_stale() runs inside ui_init().
	if (ui_init() != 0)
		SDL_Log("ui_init failed; text entry will be unavailable");

	Settings settings;
	settings.Load();

	// Binds the install flow's drawn screens to the surface, console and pad.
	// Without this the flow falls back to no-op calls and runs silently, so the
	// result is checked rather than assumed.
	if (uibridge_setup(windowSurface, &console, &input, ftLib) != 0)
		dbglog::Write("flow UI failed to initialise; install screens will be blank");

	// The message the firmware toggle shows unless the user overrides it.
	// Read once; the firmware cannot change while the app runs.
	// Read once; the firmware cannot change while the app runs. Used by the
	// install flow's status text.
	std::string generatedFirmware;
	{
		const SystemInfo info = QuerySystem();

		if (!info.firmware.empty())
			generatedFirmware = "Firmware " + FormatFirmware(info.firmware);
	}

	Screen screen = Screen::Menu;
	int    selected = kMenuInstall;
	bool   running = true;
	std::string status = "ready";

	// Settings page. `tab` is the preview panel's active tab, independent of which
	// row the cursor is on.
	int tab = 0;

	// The token row, as a function because it is the one field checked before it
	// is accepted: a token is a credential, and writing a mistyped one leaves a
	// page that saves cleanly and then authenticates nothing, with nothing on
	// screen saying which of the two went wrong.
	//
	// Validation lives in token_valid, the same function the installer uses,
	// so the page and the installer cannot disagree about what a token looks like.
	struct EditToken
	{
		static void Run(Settings &settings, SettingsView *view)
		{
			// ui_input rejects a cap over UI_INPUT_MAX without opening
			// anything, so this is sized from that constant rather than guessed.
			char buf[UI_INPUT_MAX];
			snprintf(buf, sizeof buf, "%s", settings.Token().c_str());

			if (ui_input("Discord token", "text", buf, sizeof buf) != 1)
				return;

			// Clearing the field restores the placeholder rather than saving an
			// empty credential. An empty token is not a way to run without one;
			// it is a token that cannot work, and it would look deliberate.
			std::string entered = buf;
			if (entered.empty())
				entered = "SET_ME";

			if (!token_valid(entered.c_str()))
			{
				view->SetStatus("that does not look like a token");
				return;
			}

			settings.SetToken(entered);
			view->SetStatus(settings.Save() == 0 ? "saved" : "could not save");
		}
	};

	


	while (running)
	{
		input.Update();

		// ---- input -------------------------------------------------------
		if (screen == Screen::Menu)
		{
			// Cards are side by side, so both axes move the selection.
			if (input.Pressed(kPadLeft) || input.Pressed(kPadUp))
				selected = (selected + kMenuCount - 1) % kMenuCount;
			else if (input.Pressed(kPadRight) || input.Pressed(kPadDown))
				selected = (selected + 1) % kMenuCount;
			else if (input.Pressed(kPadTriangle))
				running = false;
			else if (input.Pressed(kPadCross))
			{
				if (selected == kMenuInstall)
				{
					// Hands over to the wizard, which owns the screen until it
					// finishes. ui_init() is already done and ui_shutdown() is
					// deliberately not called, so the menu still works after.
					const int rc = installer_flow();

					// Three outcomes, three words. "install stopped early" for an
					// already-installed console would be wrong -- nothing went
					// wrong, and the dialog on screen has already said why.
					switch (rc)
					{
					case INSTALLER_OK:
						status = "install finished";
						break;
					case INSTALLER_ALREADY_INSTALLED:
						status = "already installed";
						break;
					case INSTALLER_CANCELLED:
						// Not a failure, and saying "stopped early"
						// would imply something broke.
						status = "install cancelled";
						break;
					default:
						status = "install stopped early";
						break;
					}
				}
				else if (selected == kCardSettings)
				{
					screen = Screen::Settings;
					selected = kSetToken;

					// Re-read on the way in, not once at startup. The
					// availability of the config is a fact about the console
					// right now, and this app stays open across an install: a
					// console that had nothing a minute ago has a config now,
					// and a page that greys itself out for the rest of the
					// session would be lying about that.
					//
					// A full reload rather than just re-probing, because a
					// freshly installed config has values worth showing and not
					// only the fact that it exists. Nothing is lost by it --
					// every toggle saves the moment it is pressed, so there is no
					// uncommitted state to discard.
					settings.Load();

					// Said on entry rather than only on press: a page that is
					// entirely inert should explain itself before the user
					// tries anything.
					settingsView.SetStatus(settings.Available()
						? "" : "orbisRPC is not installed -- nothing to configure");
				}
				else if (selected == kCardUninstall)
				{
					// Its own flow, not a mode of the install wizard: it starts
					// from the opposite question, and it is the only path that
					// touches the shared configs at all.
					const int rc = uninstall_flow();

					switch (rc)
					{
					case UNINSTALL_OK:
						status = "uninstall finished";
						break;
					case UNINSTALL_STOPPED:
						status = "uninstall cancelled";
						break;
					default:
						status = "uninstall did not finish";
						break;
					}
				}
			}
		}
		else if (input.Pressed(kPadTriangle))
		{
			running = false;
		}
		else if (screen == Screen::Settings)
		{
			// Up/down walk the row list; left/right and the shoulders move the
			// preview tabs. Both axes are live at once because the two columns
			// are independent, exactly as on the home page.
			if (input.Pressed(kPadUp))
				selected = (selected + SettingsView::RowCount() - 1) %
				           SettingsView::RowCount();
			else if (input.Pressed(kPadDown))
				selected = (selected + 1) % SettingsView::RowCount();
			else if (input.Pressed(kPadRight))
				tab = (tab + 1) % SettingsView::TabCount();
			else if (input.Pressed(kPadLeft))
				tab = (tab + SettingsView::TabCount() - 1) %
				      SettingsView::TabCount();
			else if (input.Pressed(kPadL1))
				tab = (tab + SettingsView::TabCount() - 1) %
				      SettingsView::TabCount();
			else if (input.Pressed(kPadR1))
				tab = (tab + 1) % SettingsView::TabCount();
			else if (input.Pressed(kPadCircle))
			{
				settingsView.SetStatus("");
				screen = Screen::Menu;
			}
			else if (input.Pressed(kPadCross))
			{
				// With no config there is nothing to write into, and Save() would
				// have to invent a five-key stub that the daemon cannot use. The
				// rows are dimmed for the same reason. Checked here as well as
				// there, because a dim that failed to draw must not become a
				// working save.
				if (!settings.Available())
				{
					settingsView.SetStatus("orbisRPC is not installed");
				}
				else if (SettingsView::RowIsAction(selected))
				{
					// Own modal flow, then the page carries on as it was. The
					// row's outcome is said in its own dialog, so there is nothing
					// for the status line to add.
					settingsaction_nanodns();
				}
				else
				{
					const int id = SettingsView::RowSetting(selected);

					if (Settings::IsToggle(id))
					{
						// Persist immediately, so a toggle is durable even if
						// the app is killed rather than quit.
						settings.Toggle(id);
						settingsView.SetStatus(settings.Save() == 0
							? "saved" : "could not save");
					}
					else if (id == kSetToken)
					{
						EditToken::Run(settings, &settingsView);
					}
					else
					{
						// Synchronous: the loop does not present until this
						// returns, and the IME sits over the last frame.
						char buf[UI_INPUT_MAX];
						snprintf(buf, sizeof buf, "%s",
						         settings.CustomMessage().c_str());

						if (ui_input("Custom Message", "text", buf, sizeof buf) == 1)
						{
							settings.SetCustomMessage(buf);
							settingsView.SetStatus(settings.Save() == 0
								? "saved" : "could not save");
						}
					}
				}
			}
		}

		// ---- paint --------------------------------------------------------
		//
		// The home page does not use Console at all: Menu draws it straight to
		// the surface. Console still owns the surface on the other screens, and
		// Render() starts by clearing, so the two paths never overlap.
		if (screen == Screen::Menu)
		{
			menu.Draw(selected);

			// Transient feedback (saved, install stopped early, ...), drawn
			// centred above the footer.
			FT_Face face = paint::Face("/app0/assets/fonts/SegUIVar.ttf",
			                           28, 600.0f);
			if (face && !status.empty() && status != "ready")
			{
				const int w = paint::TextWidth(status, face);
				paint::Text(windowSurface, (kWidth - w) / 2,
				            kScreenH - kFooterHintH - 24, status, face,
				            kAccent, 255);
			}

			// Presented every frame. Skipping this when the page looked unchanged
			// stalls the displayed frame rate, because SDL_UpdateWindowSurface is
			// also what paces the loop against vsync on this platform.
			console.Present();
			continue;
		}

		// Settings is drawn by SettingsView on the same surface, for the same
		// reason the menu is: Console's row grid cannot express this page, and it
		// is still the right tool for logs.
		settingsView.Draw(settings, selected, tab);
		console.Present();
	}

	input.Shutdown();
	ui_shutdown();
	SDL_Quit();

	return 0;
}
