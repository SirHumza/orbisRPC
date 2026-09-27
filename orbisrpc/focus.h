/* focus.h - kernel message-buffer focus tracking (event-driven).
 * The kernel logs "AppFocusChanged [...]" on every focus switch, which beats
 * polling: multi-app ambiguity disappears and system screens (settings,
 * browser) become visible instead of "no game". Verified approach: a
 * third-party 18KB payload tracks focus exactly this way off kern.msgbuf.
 * Everything here fails soft to "unknown" — snapshot probes stay fallback. */
#ifndef ORBISRPC_FOCUS_H
#define ORBISRPC_FOCUS_H
#include <stddef.h>

#define FOCUS_UNKNOWN 0  /* no information (keep previous state) */
#define FOCUS_GAME    1  /* game/app title id (CUSA, PPSA, ...) */
#define FOCUS_SYSTEM  2  /* system app (NPXS...) — settings, browser, etc. */

/* Parse the LAST AppFocusChanged event in a raw msgbuf window.
 * Returns 0 and writes the focus target title id, -1 when no event found. */
int focus_parse_appfocus(const char *buf, size_t len,
                         char *out_tid, size_t cap);
/* 1 game, 2 system (NPXS), 0 unknown prefix. */
int focus_classify(const char *tid);
/* Probe kern.msgbuf candidates, parse last focus event, classify it.
 * Returns FOCUS_* class, writes tid when parsed. Never crashes, never
 * blocks: unreadable buffer just means FOCUS_UNKNOWN. */
int detect_system_screen(char *out_tid, size_t cap);
#endif
