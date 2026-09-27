# orbisRPC Architecture

Discord Rich Presence for jailbroken PS4. No PC at runtime: the daemon
talks to the Discord gateway directly.

## Binaries

| Binary | Built by | Runs as |
|---|---|---|
| `orbisrpc.elf` | `scripts/build.sh` (OpenOrbis) | BinLoader / Payload Guest payload |
| `orbisrpc_sdk.elf` | `scripts/build_sdk.sh` (ps4-payload-sdk) | PKG `assets/daemon.elf`, copied to `/data/payloads/orbisrpc.bin` |
| Installer PKG (`ORPC00001`) | `installer/Makefile` | Setup app: SDL2 UI, token entry, file staging |
| GoldHEN plugin | frozen | not shipped; payload is the supported path |

Both payload builds share `orbisrpc/*.c`. `ORBISRPC_SDK_PAYLOAD`
selects toolchain/libc differences only — detection strategy is
identical (probe-first, see below).

## Detection cascade (ordered, first signal wins)

1. **msgbuf events** (`focus.c`): `AppFocusChanged [...]` in
   kern.msgbuf. Event-driven, sees system screens (settings/browser),
   resolves multi-app ambiguity. Unreadable buffer = skip.
2. **ShellCoreUtil dlopen** (`detect.c` `scu_init`): `sceShellCoreUtilIsAppLaunched`.
   Firmware-independent (libkernel exports dlopen/dlsym). Fail = skip.
3. **sysctl eboot scan** with the verified kinfo table (`fw.c`):
   exact offset only on firmwares in the table (currently 9.00);
   everywhere else a bounded in-record scan. Never reads past recsz,
   never assumes.
4. **Sandbox/save fallbacks**: `/mnt/sandbox/<TITLE>_000`, freshest
   savedata / app dir / `app.pkg` atime.

New title needs 2 consecutive polls (~2s debounce). Same title
returning within 10 min resumes its timer; cross-restart resume seeds
from `session.json` (validated: 9-char id, sane epoch).

## Gateway

Explicit states, logged on transition: `DOWN -> CONNECTING ->
HELLO_WAIT -> IDENTIFYING -> READY`. Fresh IDENTIFY every connect
(no RESUME). Close 4004 (bad token) is a warning + backoff + config
reload — never an exit. Reconnect: exponential backoff with
deterministic jitter, 60s cap for 10 fails, then 10-min cadence.
Presence re-posts after every (re)connect plus a 15-min reconcile.

## No-exit policy

The daemon never exits on transient failure: no token (waits, FTP
edit lands without reboot), network drops, corrupt session file,
detection misses. Exits only on: stop request, lock held by a peer
(stands down), superseded generation, fatal `/data` unwritable.

Crash safety: consecutive unclean boots (never marked healthy after
`HEALTH_STABLE_SECS`) enter safe mode. Single-writer lock
(`/data/orbisRPC/lock`, O_EXCL + pid liveness). `daemon.gen`
generation protocol: installer bumps, older daemons exit cleanly.

## On-disk state (`/data/orbisRPC/`)

| File | What |
|---|---|
| `config.json` | token, prefs, learned titles. Atomic save, `0600` |
| `session.json` | live session for timer resume. Validated on load |
| `status.json` | heartbeat (state/title/version/ts), every 60s |
| `diag.json` | boot census: fw, kinfo-table hit, msgbuf/sandbox/shellcore reachability |
| `daemon.gen` | install generation for supersede |
| `install.log` | installer stage log (errno + sizes per step) |
| `log.txt` | daemon log, mirrored to klog |
| `playtime.log` | append-only ledger, readers total it |

## Installer

Thin layer: preflight assets, `copy_file` with FNV read-back proof
(no stat — sandbox lies), config preserved on reinstall, 3-try IME
token entry with FTP fallback, gen bump, done screen reports live
daemon state from `status.json`. Every step logs to `install.log`;
crash guard turns faults into log lines. UI is custom SDL2
(`installer/ui.c`, same `ui.h` API); font is build-time rasterized
(`scripts/genfont.py` -> `installer/font.h`).

## Compatibility

Verified baseline: **9.00**. All other firmwares: probe-and-degrade
by design, `diag.json` proves per-box signal availability. The
firmware matrix fills in from user-submitted `diag.json` files —
see `SUPPORT.md` and `docs/`.
