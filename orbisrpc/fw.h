/* fw.h - console firmware version string, for display only.
 *
 * Not the same thing as the SDK's firmware gating: the CRT refuses to start
 * on unlisted firmware (see docs/research/sdk-13.52.md), while this just wants to
 * print something like "13.52" next to the presence.
 *
 * Unlike upstream's fw.c this does NOT carry a per-firmware kinfo_proc offset
 * table. That job belongs to procwalk.c, which needs no offsets at all. */
#ifndef ORBISRPC_FW_H
#define ORBISRPC_FW_H

#include <stddef.h>

/* Writes the firmware as "MAJOR.MINOR" (e.g. "13.52") into out.
 * Never fails loudly: on any error it writes "unknown" and returns -1, so a
 * missing firmware string can never stop a presence from being posted. */
int fw_version(char *out, size_t cap);

/* Normalise a raw Sony version string to "MAJOR.MINOR".
 * Pure, so the host tests can pin the exact shape: "13.520.001" ->
 * "13.52", "9.000" -> "9.00". Exposed because a wrong trim here shows
 * up verbatim on the presence card. */
void fw_normalize(char *v);

#endif /* ORBISRPC_FW_H */