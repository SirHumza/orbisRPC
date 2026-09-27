# Installer (Setup PKG, `ORPC00001`)

## What it does

One linear flow, forward-only (every No skips ahead, nothing loops back):
confirm → copy `orbisrpc.bin` to `/data/payloads/` (mkdir -p +
byte-count + FNV hash read-back) → pre-saved config with token
(skips entry when valid) → generation bump → done.
Read-only status screen available via decline.

There is no WiFi check. The installer runs sandboxed, so its socket probe
measures the app's network stack, not the payload's — and a FAILED line
reads like a verdict on Discord itself. The daemon reports real reachability
in its own log once started.

The installer never boots anything directly. Starting the daemon is
Payload Guest's `/data/payloads/` directory — pick `orbisrpc.bin`.
An older running daemon sees the installer's generation bump in
`daemon.gen` and exits cleanly on its own (no killer payload needed).
No loopback ports, no injection, no boot proof to go wrong.

## Install flow

```
confirm → copy orbisrpc.bin to /data/payloads/orbisrpc.bin
        → save config.json with Discord token (pre-populated)
        → (token entry skipped if already valid)
        → bump daemon.gen (old daemon exits cleanly)
        → done
```

The installer bumps `daemon.gen` after copying — any older running
daemon sees the new generation and exits cleanly by itself, so the
fresh payload takes over with no killer module and no sandbox fights.

## Navigation law

No branch ever returns to start. Cancel/skip always moves forward. The only
exits are Done, explicit close, or a payload write failure.

Buttons: X = enter, O = back. The confirm dialog uses `YESNO` focused on
Yes — the OpenOrbis default `YESNO_FOCUS_NO` puts the console's confirm
button on No, which inverts the whole wizard.

## Console facts encoded

- Splash is hidden first thing in `main()` so module loading in
  `ui_init()` happens with the splash already gone — no visible delay
  between splash hide and first dialog.
- Dialog/IME/IME-backend sysmodules + internal SYSTEM/USER/COMMON/PAD
  modules load before use (order matters — `sceCommonDialogInitialize()`
  precedes external module loads; PAD is unloaded on exit).
- `stat()`/`fstat()` lie about sizes inside the sandbox (observed 4096/4160
  for a 2071552-byte file), so install proof is byte count + FNV hash on
  read-back, and status uses open-existence, never stat.
- Config writes are atomic (tmp+fsync+rename) with read-back proof.
- IME wait is bounded; asset key charset validated (bad keys blank the
  activity).
- Old daemons are retired via the `daemon.gen` generation bump, not
  signals: `kill()` is blocked by the sandbox (EPERM), so the running
  daemon polls the generation file and exits cleanly when superseded.

## Auto-start

Payload Guest AutoRun: enable it for `orbisrpc` once and the daemon
starts on every jailbreak. The queue is menu-managed; the installer
shows where, it can't write the queue itself.

## Building

`make -f installer/Makefile` (`OO_PS4_TOOLCHAIN`, llvmshim). Staged assets:
`daemon.elf`, `config.json`. The PKG ships both —
the installer copies the payload, pre-saves the config token,
and bumps `daemon.gen` so an older daemon retires itself.
Output: `IV0000-ORPC00001_00-ORBISRPCSETUP000.pkg`.
