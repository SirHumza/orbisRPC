#ifndef INSTALLER_SETTINGS_H
#define INSTALLER_SETTINGS_H

#include <string>

// Presence settings, read and written against the daemon's own
// /data/orbisRPC/config.json.
//
// Each toggle is stored as a real boolean, not a string, so the daemon can read
// them without parsing. CustomMessage is free text in presence_state, the string
// the daemon shows as the presence line; clearing the field writes the shipped
// default ("On PS4") rather than removing the key, since an absent key is not the
// same as a defaulted one.
enum SettingId
{
	kSetToken = 0,          // text
	kSetShowFirmware,       // toggle
	kSetShowMediaApps,      // toggle
	kSetHomeScreen,         // toggle
	kSetShowHomebrew,       // toggle
	kSetCustomMessage,      // text

	kSettingCount
};

/* SETTINGS_KEYS_LIST and SETTINGS_DEFAULT_MESSAGE live in settingskeys.h, not
 * here, so that a host test can read them: this header needs <string> and so
 * cannot be compiled by one. See that file for what went wrong last time. */
#include "settingskeys.h"

class Settings
{
public:
	Settings();

	// Reads SETTINGS_PATH. Anything missing or malformed keeps its default, so
	// a hand-edited file can never brick the app.
	void Load();

	// True when there is a config to edit.
	//
	// False means no /data/orbisRPC/config.json, and every row greys out. This is
	// not a nicety: Save() writes into an existing document, and with no document
	// it would have to build one -- and a config built from this page alone is a
	// five-key stub with no token, no art URLs and no presence strings. That is
	// worse than not offering the setting at all, so the page says it cannot help
	// rather than making one.
	bool Available() const { return available_; }

	// Writes atomically: to "<path>.new", fsync, rename over the target.
	// 0 on success.
	int Save() const;

	bool ShowFirmware() const { return showFirmware_; }
	bool ShowMediaApps() const { return showMediaApps_; }
	bool ShowHomeScreen() const { return showHomeScreen_; }
	bool ShowHomebrew() const { return showHomebrew_; }
	const std::string &CustomMessage() const { return customMessage_; }
	const std::string &Token() const { return token_; }

	void SetShowFirmware(bool on) { showFirmware_ = on; }
	void SetShowMediaApps(bool on) { showMediaApps_ = on; }
	void SetHomeScreen(bool on) { showHomeScreen_ = on; }
	void SetShowHomebrew(bool on) { showHomebrew_ = on; }
	void SetCustomMessage(const std::string &text) { customMessage_ = text; }
	void SetToken(const std::string &t) { token_ = t; }

	// Flips one toggle and returns true if it changed. No-op for the message
	// field, which is not a toggle.
	bool Toggle(int id);

	bool GetToggle(int id) const;

	// True when any setting differs from the shipped default.
	bool Modified() const;

	static const char *Label(int id);
	static bool IsToggle(int id);

	// True when this row is currently shown. Every row is always visible now;
	// the conditional message field that used to depend on the firmware toggle is
	// gone, replaced by an unconditional custom message row.
	bool IsVisible(int id) const;

	// Ids of the rows currently on screen, top to bottom. Always at least one.
	void VisibleRows(int ids[], int &count) const;

private:
	bool available_;   // was there a config to read?
	bool showFirmware_;
	bool showMediaApps_;
	bool showHomeScreen_;
	bool showHomebrew_;
	std::string customMessage_;
	std::string token_;
};

#endif
