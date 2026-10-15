/* settingskeys.h - the config.json keys the Settings page writes.
 *
 * A C header for the same reason carrykeys.h is one: settings.h needs <string>
 * and so cannot be included by a host test, but the key list is exactly the kind
 * of thing a test should be able to check. The config test reads the shipped
 * config.json and asserts every name here is really in it.
 *
 * Three of these were wrong for the whole life of the page. It wrote
 * show_media_apps, show_home_screen and custom_message -- none of which exist in
 * config.json. A toggle flipped, the save reported success, and the daemon read
 * nothing: no build error, no log line, nothing on the console. They look like
 * the more descriptive names, which is precisely why they survived review.
 *
 * Renaming a key here means renaming it in config.json too, and the test is what
 * makes the half of that you forget fail instead of pass quietly.
 */
#ifndef INSTALLER_SETTINGSKEYS_H
#define INSTALLER_SETTINGSKEYS_H

/* In Settings row order: token, show_firmware, show_media, show_idle,
 * show_homebrew, presence_state. */
#define SETTINGS_KEYS_LIST \
	"token", \
	"show_firmware", \
	"show_media", \
	"show_idle", \
	"show_homebrew", \
	"presence_state"

/* What presence_state is set to when the custom message field is cleared.
 * Written rather than omitted: an absent key is not the same as a defaulted
 * one, and the daemon reads this string directly as the presence line. */
#define SETTINGS_DEFAULT_MESSAGE "On PS4"

/* The names that were once used here. Kept so the test can assert they stay
 * absent -- they are plausible enough to be reintroduced by accident. */
#define SETTINGS_RETIRED_KEYS_LIST \
	"show_media_apps", \
	"show_home_screen", \
	"custom_message"

#endif