/* bigapp.h - classify what the console reports as the foreground app.
 *
 * Split out of detect.c so it can be unit-tested off-console: detect.c pulls
 * in dlfcn.h and orbis/ headers and cannot be host-compiled at all. Only the
 * decision logic lives here -- the two syscalls stay in detect.c.
 *
 * The whole point of this module is the distinction between "nothing is
 * running", "something else is in front" and "we could not tell". Collapsing
 * any of those into a single "no game" answer is what made presence vanish
 * while a game was still running. */
#ifndef ORBISRPC_BIGAPP_H
#define ORBISRPC_BIGAPP_H

#include <stddef.h>
#include <stdint.h>

/* A TITLEID is exactly 9 characters of [A-Z0-9] (CUSA00740, PPSA01234, ...). */
#define BIGAPP_TITLEID_LEN 9

/* Classify a foreground-app reading.
 *
 *   app_id  what sceSystemServiceGetAppIdOfBigApp() returned
 *   title_rc  what sceLncUtilGetAppTitleId() returned (ignored unless app_id > 0)
 *   title_id  the buffer that call wrote
 *   out      receives the validated TITLEID when the result is BIGAPP_GAME
 *   cap      size of out
 *
 * Returns BIGAPP_GAME (1) with out filled, BIGAPP_NONE (0) for the home
 * screen, BIGAPP_SYSTEM (2) for a system app in front, or BIGAPP_UNKNOWN (-1)
 * when the reading is unusable. On anything but BIGAPP_GAME, out is cleared. */
#define BIGAPP_GAME     1
#define BIGAPP_NONE     0
#define BIGAPP_SYSTEM   2
#define BIGAPP_UNKNOWN (-1)

int bigapp_classify(int32_t app_id, int title_rc, const char *title_id,
                    char *out, size_t cap);

/* 1 when `s` is a well-formed TITLEID. Exposed for tests. */
int bigapp_titleid_valid(const char *s);

/* 1 when the id is homebrew as opposed to retail or retro.
 *
 * "Homebrew" here means the pkg-zone namespace: not a retail CUSA id, not a
 * system NPXS id, and not one of the retro prefixes. Retro is excluded on
 * purpose -- those are public classics, and show_homebrew is not meant to
 * silence them.
 *
 * App containers (ITEM00001 and friends) land here too. That is intended: they
 * are not retail games either, and the flag governs what you advertise rather
 * than how the name was resolved.
 *
 * Lives here rather than in detect.c because it is a namespace rule sitting
 * next to the other ones, and because detect.c cannot be host-compiled, so the
 * tests could not reach it there. */
int is_homebrew_id(const char *tid);

#endif /* ORBISRPC_BIGAPP_H */