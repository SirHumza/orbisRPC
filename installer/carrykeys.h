/* carrykeys.h - the options that survive a config overwrite.
 *
 * Kept in a header rather than as a static array in installer.c for one reason:
 * installer.c includes PS4 headers and cannot be built on the host, so a list
 * defined there cannot be tested. This can. The config host test reads the
 * shipped config.json and asserts every name here actually exists in it, which
 * catches a typo -- the failure mode being a key that silently never carries,
 * with nothing to indicate why the user's setting was reset.
 *
 * Adding a user option to config.json means adding it here too. Anything not
 * named takes the incoming value from the new config, which is right for
 * daemon-owned fields and wrong for anything the user set.
 *
 * `token` is carried deliberately. That means a token entered once survives
 * future installs instead of being replaced by whatever the release ships, so a
 * release that rotates it will not take effect until this line is dropped.
 */
#ifndef INSTALLER_CARRYKEYS_H
#define INSTALLER_CARRYKEYS_H

#define CARRY_KEYS_LIST \
	"token", \
	"presence_state", \
	"show_firmware", \
	"show_idle", \
	"show_media", \
	"show_homebrew"

#endif
