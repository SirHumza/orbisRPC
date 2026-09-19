/* detect.h - find current foreground game title + app cache dir. */
#ifndef DETECT_H
#define DETECT_H
#include <stddef.h>
/* Returns 0 and writes a display name when a foreground game is found.
 * Returns -1 when no game is active or the arguments are invalid.
 * out_path is optional and receives the per-title cache path when provided. */
int detect_current_game(char *out_name, size_t cap, char *out_path, size_t p_cap);
/* Returns 1 if a foreground user app is running, otherwise 0. */
int detect_foreground_active(void);
/* Resolve a display name for a KNOWN title id (plugin mode). */
int detect_name_for_title(const char *titleId, char *out_name, size_t cap);
/* Last resolved titleId (e.g. "CUSA00740") or NULL if none yet.
 * Used as the Discord asset key so icons follow automatically. */
const char *detect_last_titleid(void);
/* Last resolved artwork URL (Sony CDN via TMDB, or empty). */
const char *detect_last_art(void);
#endif