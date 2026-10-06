/* detect.h - find current foreground game title + app cache dir. */
#ifndef DETECT_H
#define DETECT_H
#include <stddef.h>
#include "retro.h"

/* Returns 0 and writes a display name when a foreground game is found.
 * Returns -1 when no game is active, -2 when the read failed and the caller
 * should hold its current state, and -3 when a system app (Settings, browser,
 * store) is covering a game that is still running -- also hold, but it means
 * the game did not close.
 * out_path is optional and receives the per-title cache path when provided. */
int detect_current_game(char *out_name, size_t cap, char *out_path, size_t p_cap);

/* 1 game in front, 0 nothing in front, 2 system app in front, -1 unknown. */
int detect_foreground_active(void);

/* 1 = Settings in front, 0 = a different scene, -1 = unknown. Unknown must
 * never be treated as 0: an unavailable probe is not evidence that Settings
 * closed. */
int detect_settings_active(void);

/* 1 when the id is in the pkg-zone homebrew namespace: not retail (CUSA), not
 * system (NPXS), and not a retro prefix. Retro is excluded on purpose so
 * show_homebrew never silences the classic titles. Declared in bigapp.h with
 * the other namespace rules. */
int is_homebrew_id(const char *tid);

/* 1 = web browser in front, 0 = another scene, -1 = unknown. Unknown is
 * never folded into 0, for the same reason as detect_settings_active. */
int detect_browser_active(void);

/* Resolve a display name for a KNOWN title id. */
int detect_name_for_title(const char *titleId, char *out_name, size_t cap);

/* Last resolved titleId (e.g. "CUSA00740") or NULL if none yet.
 * Used as the Discord asset key so icons follow automatically. */
const char *detect_last_titleid(void);
/* 1 when the last resolve produced a real name (0 = raw-ID fallback). */
int detect_last_ok(void);/* Last resolved artwork URL (Sony CDN via TMDB, or empty). */
const char *detect_last_art(void);
/* Media type for presence: 0 Playing (games), 2 Listening, 3 Watching.
 * Only known media apps map; everything else is 0. Extend freely. */
int detect_media_type(const char *title_id);
#endif