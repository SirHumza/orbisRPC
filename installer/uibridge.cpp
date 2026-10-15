/* uibridge.cpp - the C shim from installer.c onto FlowUi.
 *
 * Thin on purpose. Everything interesting is in FlowUi; this exists so the C flow
 * can reach it without knowing about SDL, and so the "no UI bound" case is
 * handled in exactly one place instead of at every call site.
 */

#include "uibridge.h"

#include <proto-include.h> // FT_Library

#include "flowui.h"

namespace {

FlowUi g_ui;
bool    g_bound = false;

// No pad means no answer is possible. These return the safe end of each choice:
// an installation that stops rather than one that proceeds unasked.
int  g_lastYesNo = 0;

} // namespace

extern "C" int uibridge_setup(void *surface, void *console, void *input,
                              void *ftLib)
{
	g_bound = g_ui.Init(static_cast<SDL_Surface *>(surface),
	                    static_cast<Console *>(console),
	                    static_cast<Input *>(input),
	                    static_cast<FT_Library>(ftLib));

	return g_ui.Ready() ? 0 : -1;
}

extern "C" void uibridge_ok(const char *title, const char *body)
{
	if (!g_bound)
		return;

	g_ui.ShowMessage(title ? title : "", body ? body : "");
}

extern "C" int uibridge_cancelled(void)
{
	// Only meaningful when a dialog is actually up. Unbound means no input
	// source, so nothing was pressed and the answer is no.
	return g_bound && g_ui.Cancelled() ? 1 : 0;
}

extern "C" int uibridge_yesno(const char *title, const char *body)
{
	if (!g_bound)
		return g_lastYesNo;

	g_lastYesNo = g_ui.ShowConfirm(title ? title : "", body ? body : "") ? 1 : 0;
	return g_lastYesNo;
}

extern "C" void uibridge_progress_open(const char *title)
{
	if (!g_bound)
		return;

	g_ui.ShowProgress(title ? title : "", "");
}

extern "C" void uibridge_progress_status(const char *status)
{
	if (!g_bound)
		return;

	g_ui.SetProgressStatus(status ? status : "");
}

extern "C" void uibridge_progress_close(void)
{
	if (!g_bound)
		return;

	g_ui.HideProgress();
}

extern "C" void uibridge_tick(void)
{
	if (!g_bound)
		return;

	g_ui.Tick();
}
