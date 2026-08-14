/* detect.h - find current foreground game title + app cache dir. */
#ifndef DETECT_H
#define DETECT_H
#include <stddef.h>
/* Returns game display name + an asset/cache path the daemon can own. */
int detect_current_game(char *out_name, size_t cap, char *out_path, size_t p_cap);
/* Returns nonzero if a foreground user app is running */
int detect_foreground_active(void);
/* Resolve a display name for a KNOWN title id (plugin mode). */
int detect_name_for_title(const char *titleId, char *out_name, size_t cap);
#endif