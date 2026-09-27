/* updater.h - version compare + staged-image validation helpers.
 * Network self-update was REMOVED (updates ship via reinstall PKG);
 * the remaining routines are pure/host-testable and back local
 * integrity checks (health rollback) and the installer. */
#ifndef UPDATER_H
#define UPDATER_H
#include <stddef.h>
/* Compare dotted versions ("0.4.0" vs "v0.10.1", leading v ok):
 * >0 a newer, <0 b newer, 0 equal. Host-testable. */
int updater_cmp(const char *a, const char *b);
/* Validate a downloaded payload: ELF magic, 64-bit, x86-64, sane size. */
int updater_elf_ok(const unsigned char *buf, size_t n);
/* Accepts raw ELF (payload .bin) or signed SELF (plugin .prx). */
int updater_image_ok(const unsigned char *buf, size_t n);
#endif
