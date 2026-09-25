/* updater.h - self-updater: version compare, staged install. */
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
/* Check latest GitHub release; download + atomically stage newer
 * artifacts the daemon actually runs from. Returns 1 updated,
 * 0 already current, -1 failed/checked-off. Never fatal. */
int updater_check_and_stage(void);
#endif
