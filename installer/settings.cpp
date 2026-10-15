#include "settings.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <fcntl.h>
#include <sys/stat.h>

#include "jsonlite.h"
#include "paths.h"

// The daemon's own config, not a copy. This was deliberately a scratch path --
// /data/orbispkg_test/settings.json -- while the page was a mockup, which is
// exactly why nothing it did had any effect: the daemon never read that file.
#define SETTINGS_PATH ICFG_PATH
#define SETTINGS_DIR  INST_DIR
#define SETTINGS_MAX  (16u * 1024u)

namespace {

// Field labels, in display order.
const char *const kLabels[kSettingCount] = {
	"Token",
	"Show firmware version",
	"Show watching presence for media apps",
	"Show presence in settings and web browser",
	"Show presence for homebrew and retro games",
	"Custom Message",
};

// JSON keys -- the daemon's names, not the page's.
//
// SETTINGS_KEYS_LIST in settingskeys.h is the same list, and the host config test
// reads the shipped config.json to assert every name here is really in it. Three
// of these were invented while the page was a mockup -- show_media_apps,
// show_home_screen and custom_message -- and none of them existed in config.json,
// so a toggle flipped, saved, reported "saved", and changed nothing the daemon
// would read. The list is kept twice so that cannot come back unnoticed.
const char *const kTokenKey         = "token";
const char *const kShowFirmwareKey = "show_firmware";
const char *const kMediaAppsKey    = "show_media";
const char *const kHomeScreenKey   = "show_idle";
const char *const kHomebrewKey     = "show_homebrew";
const char *const kCustomMsgKey    = "presence_state";

// What presence_state carries when the field is left empty. The key is always
// written -- an absent one is not the same as a defaulted one, and the daemon
// reads this string directly as the presence line.
const char *const kDefaultCustomMsg = SETTINGS_DEFAULT_MESSAGE;

// Shipped defaults. The message starts empty, meaning "no override".
const bool kDefaultShowFirmware = true;
const bool kDefaultShowMediaApps = true;
const bool kDefaultHomeScreen   = true;
const bool kDefaultHomebrew     = true;

// mkdir -p, so the test directory appears on first save.
int EnsureDir(const char *path)
{
	char tmp[256];
	size_t i, n;

	if (!path || !path[0])
		return -1;

	n = strlen(path);
	if (n >= sizeof tmp)
		return -1;

	memcpy(tmp, path, n + 1);

	for (i = 1; i < n; ++i)
	{
		if (tmp[i] == '/')
		{
			tmp[i] = 0;
			mkdir(tmp, 0777);
			tmp[i] = '/';
		}
	}

	if (mkdir(path, 0777) != 0 && errno != EEXIST)
		return -1;

	return 0;
}

} // namespace

Settings::Settings()
	: showFirmware_(kDefaultShowFirmware)
	, showMediaApps_(kDefaultShowMediaApps)
	, showHomeScreen_(kDefaultHomeScreen)
	, showHomebrew_(kDefaultHomebrew)
{
}

const char *Settings::Label(int id)
{
	if (id < 0 || id >= kSettingCount)
		return "";
	return kLabels[id];
}

bool Settings::IsToggle(int id)
{
	return id == kSetShowFirmware || id == kSetShowMediaApps ||
	       id == kSetHomeScreen || id == kSetShowHomebrew;
}

bool Settings::IsVisible(int id) const
{
	// Every row is unconditional. The old firmware-message row was hidden unless
	// the firmware toggle was on; custom message is not tied to any toggle.
	(void)id;
	return true;
}

void Settings::VisibleRows(int ids[], int &count) const
{
	count = 0;

	for (int i = 0; i < kSettingCount; ++i)
	{
		if (IsVisible(i))
			ids[count++] = i;
	}

	if (count == 0)
		ids[count++] = kSetShowFirmware; // cannot happen, but never return none
}

bool Settings::GetToggle(int id) const
{
	switch (id)
	{
	case kSetShowFirmware: return showFirmware_;
	case kSetShowMediaApps: return showMediaApps_;
	case kSetHomeScreen:    return showHomeScreen_;
	case kSetShowHomebrew:  return showHomebrew_;
	default:                return false;
	}
}

bool Settings::Toggle(int id)
{
	const bool before = GetToggle(id);
	const bool after = !before;

	switch (id)
	{
	case kSetShowFirmware: showFirmware_ = after;  break;
	case kSetShowMediaApps: showMediaApps_ = after; break;
	case kSetHomeScreen:    showHomeScreen_ = after; break;
	case kSetShowHomebrew:  showHomebrew_ = after;   break;
	default:
		// Not a toggle; the custom message is edited, not flipped.
		return false;
	}

	return before != after;
}

bool Settings::Modified() const
{
	return showFirmware_ != kDefaultShowFirmware ||
	       showMediaApps_ != kDefaultShowMediaApps ||
	       showHomeScreen_   != kDefaultHomeScreen ||
	       showHomebrew_     != kDefaultHomebrew ||
	       customMessage_    != kDefaultCustomMsg ||
	       !token_.empty();
}

// Reads a whole file into a malloc'd NUL-terminated buffer. NULL when missing,
// unreadable, empty, or larger than SETTINGS_MAX -- a bound, because this is
// about to be parsed and a corrupt file should not be able to allocate freely.
char *ReadWholeFile(const char *path)
{
	long size;
	char *buf;
	FILE *f = fopen(path, "rb");

	if (!f)
		return NULL;

	if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
	size = ftell(f);
	if (size <= 0 || size > static_cast<long>(SETTINGS_MAX)) { fclose(f); return NULL; }
	if (fseek(f, 0, SEEK_SET) != 0) { fclose(f); return NULL; }

	buf = static_cast<char *>(malloc(static_cast<size_t>(size) + 1));
	if (!buf) { fclose(f); return NULL; }

	if (fread(buf, 1, static_cast<size_t>(size), f) != static_cast<size_t>(size))
	{
		free(buf);
		fclose(f);
		return NULL;
	}

	fclose(f);
	buf[size] = 0;
	return buf;
}

void Settings::Load()
{
	// Start from the defaults so a partial file still yields a complete set.
	available_ = false;
	showFirmware_ = kDefaultShowFirmware;
	showMediaApps_ = kDefaultShowMediaApps;
	showHomeScreen_ = kDefaultHomeScreen;
	showHomebrew_ = kDefaultHomebrew;
	customMessage_.clear();
	token_.clear();

	char *buf = ReadWholeFile(SETTINGS_PATH);
	if (!buf)
		return;

	// The file was there and readable. Whether it parses is a separate question
	// -- a malformed config still counts as present, because refusing to edit it
	// would leave the user with no way to fix it from here. Save() falls back to a
	// fresh object in that case, which loses the malformed file's contents; that
	// is the intended trade for making the page usable again.
	available_ = true;

	const size_t size = strlen(buf);
	jl_val_t *root = jl_parse(buf, size);
	free(buf);

	if (!root || root->type != JL_OBJECT)
	{
		if (root)
			jl_free(root);
		return;
	}

	// Toggles are read as JSON booleans. A numeric 0/1 is accepted too, since
	// that is an easy thing to write by hand.
	struct ToggleField { const char *key; bool *target; };
	const ToggleField toggles[] = {
		{ kShowFirmwareKey, &showFirmware_ },
		{ kMediaAppsKey,    &showMediaApps_ },
		{ kHomeScreenKey,   &showHomeScreen_ },
		{ kHomebrewKey,     &showHomebrew_ },
	};

	for (size_t i = 0; i < sizeof(toggles) / sizeof(toggles[0]); ++i)
	{
		jl_val_t *v = jl_obj_get(root, toggles[i].key);

		if (v && v->type == JL_BOOL)
			*toggles[i].target = (v->boolean != 0);
		else if (v && v->type == JL_NUMBER)
			*toggles[i].target = (v->num_is_int ? v->inum != 0 : v->num != 0.0);
	}

	jl_val_t *msg = jl_obj_get(root, kCustomMsgKey);
	if (msg && msg->type == JL_STRING && msg->str)
		customMessage_ = msg->str;

	// The token is read but never interpreted. Validation belongs to the edit
	// path: a token that is already in the config has been working, or the daemon
	// has not run yet, and neither is the page's business to second-guess. The
	// shipped placeholder is deliberately not valid input, and refusing to load
	// because of it would grey the whole page out on a fresh install.
	jl_val_t *tok = jl_obj_get(root, kTokenKey);
	if (tok && tok->type == JL_STRING && tok->str)
		token_ = tok->str;

	jl_free(root);
}

int Settings::Save() const
{
	if (EnsureDir(SETTINGS_DIR) != 0)
		return -1;

	// Read-modify-write, not write.
	//
	// This used to build a fresh object and set five keys on it. That is fine for
	// a file that holds five keys and catastrophic for the daemon's config, which
	// holds the token, the art URLs, the presence strings and a dozen more:
	// repointing this at config.json without changing it would have wiped all of
	// them on the first toggle press. So the existing document is parsed, the five
	// keys are set on *that*, and the whole thing is written back.
	//
	// A missing or unparseable file falls back to a fresh object rather than
	// failing: losing a settings page is better than losing the config, and the
	// installer can put the config back.
	jl_val_t *root = nullptr;
	{
		char *buf = ReadWholeFile(SETTINGS_PATH);

		if (buf)
		{
			root = jl_parse(buf, strlen(buf));
			free(buf);
		}

		if (root && root->type != JL_OBJECT)
		{
			jl_free(root);
			root = nullptr;
		}
	}

	if (!root)
	{
		root = jl_new_object();
		if (!root)
			return -1;
	}

	// Real booleans, so the daemon can read them without parsing.
	jl_obj_set(root, kShowFirmwareKey, jl_new_bool(showFirmware_));
	jl_obj_set(root, kMediaAppsKey,    jl_new_bool(showMediaApps_));
	jl_obj_set(root, kHomeScreenKey,   jl_new_bool(showHomeScreen_));
	jl_obj_set(root, kHomebrewKey,     jl_new_bool(showHomebrew_));

	// Always written, defaulting rather than being omitted. An empty field means
	// "show the normal presence", and that is this exact string -- leaving the key
	// out would instead leave whatever the shipped config happened to say.
	jl_obj_set(root, kCustomMsgKey,
	           jl_new_string(customMessage_.empty() ? kDefaultCustomMsg
	                                               : customMessage_.c_str()));

	// Written back exactly as loaded. Validated when it is edited, not here --
	// see Load(). Writing an empty token over a working one because the field
	// happened to be left blank would lock the user out of their own presence.
	jl_obj_set(root, kTokenKey, jl_new_string(token_.c_str()));

	char *text = jl_stringify(root);
	jl_free(root);

	if (!text)
		return -1;

	char tmp[512];
	if (snprintf(tmp, sizeof tmp, "%s.new", SETTINGS_PATH) <= 0 ||
	    strlen(SETTINGS_PATH) + 5 >= sizeof tmp)
	{
		free(text);
		return -1;
	}

	FILE *f = fopen(tmp, "wb");
	if (!f)
	{
		free(text);
		return -1;
	}

	int ok = 1;
	if (fputs(text, f) < 0)
		ok = 0;

	if (ok)
	{
		const int fd = fileno(f);
		if (fd >= 0 && fsync(fd) != 0)
			ok = 0;
	}

	if (ok && fclose(f) != 0)
		ok = 0;

	free(text);

	if (!ok)
	{
		remove(tmp);
		return -1;
	}

	if (rename(tmp, SETTINGS_PATH) != 0)
	{
		remove(tmp);
		return -1;
	}

	return 0;
}
