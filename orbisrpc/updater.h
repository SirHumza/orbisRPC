/* updater.h - update CHECK: version compare + a notification. No install. */
#ifndef UPDATER_H
#define UPDATER_H
#include <stddef.h>
/* Compare dotted versions ("0.4.0" vs "v0.10.1", leading v ok):
 * >0 a newer, <0 b newer, 0 equal. Host-testable. */
int updater_cmp(const char *a, const char *b);
/* Validate a downloaded payload: ELF magic, 64-bit, x86-64, sane size. */
int updater_elf_ok(const unsigned char *buf, size_t n);
int updater_image_ok(const unsigned char *buf, size_t n);
/* Check the latest GitHub release for a newer version tag and report it.
 *
 * This does not download or install anything. It used to fetch orbisrpc.bin
 * and activate it over the live payload, which made every boot a
 * remote-code-execution path; the setup app applies updates now.
 *
 * No signature is verified. It authenticated a binary that was then executed;
 * with the download gone, it would only guard the truth of a notification.
 *
 * Writes the newer tag into newer_out (e.g. "1.7.1") when one is found.
 * Returns 1 if a newer release exists, 0 if already current, -1 if the check
 * failed. Never fatal to the daemon. */
int updater_check_notify(char *newer_out, size_t newer_cap);
#endif
