/* paths.h - every path this app writes on the console, in one place.
 *
 * Exists because two flows have to agree exactly: install writes this set, and
 * uninstall removes it. Held as separate copies of the same strings, the two
 * drift -- and the failure is invisible until a console is left with a payload
 * and no config, or an uninstall that quietly skips the one file it was written
 * for. One header, both flows, no way for them to disagree.
 *
 * Split into two groups:
 *
 *   OURS      written by install, deleted by uninstall
 *   SHARED    someone else's file that install adds a line to and uninstall
 *             removes that line from. The file itself is never deleted: taking
 *             out payloads.ini would wipe every other autorun payload, and
 *             taking out nanodns.ini would leave the console with no DNS.
 *
 * The /app0 sources and the setup app's own version are deliberately absent --
 * they live inside the PKG, are read-only at runtime, and belong to install
 * alone.
 */
#ifndef INSTALLER_PATHS_H
#define INSTALLER_PATHS_H

/* ---- OURS ------------------------------------------------------------- */

/* The payload's file name. Note this carries its own extension: the in-package
 * copy is assets/orbisRPC.elf and the console's is /data/payloads/orbisRPC.elf.
 * Both are this one constant, and the logo below is named after it, because the
 * payload launcher looks for an image called "<payload>.png". */
#define PAYLOAD_NAME "orbisRPC.elf"

#define INST_DIR     "/data/orbisRPC"
#define INST_LOG     "/data/orbisRPC/install.log"
#define INST_VER     "/data/orbisRPC/version"
#define ICFG_PATH    "/data/orbisRPC/config.json"

/* Where the host writes network traces. Written by the shared netclient, so it
 * is named here rather than in installer.c even though the app only ever removes
 * it. */
#define NET_LOG      "/data/orbisRPC/net.log"

/* The console's own notification icon for the update toast. Not created by this
 * app -- it is the user's file -- but it lives inside INST_DIR and goes with it. */
#define ICON_PATH    "/data/orbisRPC/icon.png"

#define PAYLOAD_ELF  "/data/payloads/" PAYLOAD_NAME

/* Spelled the way GoldHEN resolves entries. /data and /user/data are one
 * location under two names, so this names the same file as PAYLOAD_ELF -- kept
 * separate rather than derived from it, because forcing one spelling onto the
 * other is exactly what goes wrong. */
#define GOLDHEN_AUTORUN_ELF "/user/data/payloads/" PAYLOAD_NAME

#define LOGO_SRC     "/app0/assets/logo.png"
#define LOGO_ICON    "/data/payloads/" PAYLOAD_NAME ".png"

/* Staging for downloads. Deliberately outside INST_DIR: a half-written payload
 * in the live install directory would be picked up as a real install next run. */
#define TEMP_DIR     "/data/temp"
#define TEMP_PAYLOAD "/data/temp/" PAYLOAD_NAME
#define TEMP_CONFIG  "/data/temp/config.json"

/* ---- SHARED ----------------------------------------------------------- */

#define GOLDHEN_INI  "/data/GoldHEN/payloads.ini"

/* Bare line, no "= value". nanoDNS reads [exceptions] as one shell-style mask
 * per line; "host = something" there is not a mask and is silently ignored. */
#define TMDB_EXCEPTION "tmdb.np.dl.playstation.net"

#endif