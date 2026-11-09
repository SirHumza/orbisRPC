# Installer (orbisRPC Setup, `ORPC00001`)

The PS4 side ships as one app: **orbisRPC Setup**. It installs the daemon
payload, holds the settings UI, updates, and uninstalls. Title ID `ORPC00001`,
content ID `IV0000-ORPC00001_00-ORBISRPCSETUP000`.

Source lives in [`installer/`](../installer), built by the root
[`Makefile`](../Makefile).

## The menu

| Entry | What it does |
|---|---|
| **INSTALL / UPDATE** | Installs or updates the daemon payload and config |
| **SETTINGS** | Presence text, toggles, preferences — writes `config.json` |
| **UNINSTALL** | Removes the payload and the configs from the console |

## What it writes

All paths from [`installer/paths.h`](../installer/paths.h). The payload name is
`orbisRPC.elf` everywhere — packaged as `assets/orbisRPC.elf`, installed to
`/data/payloads/orbisRPC.elf`.

| Path | What |
|---|---|
| `/data/payloads/orbisRPC.elf` | the daemon payload |
| `/data/payloads/orbisRPC.elf.png` | icon for the GoldHEN launch row |
| `/data/orbisRPC/config.json` | config, including the Discord token |
| `/data/orbisRPC/version` | installed version marker |
| `/data/orbisRPC/install.log` | install-step log |
| `/data/orbisRPC/net.log` | connectivity log |
| `/data/orbisRPC/icon.png` | icon |
| `/data/GoldHEN/payloads.ini` | auto-run queue entry |
| `/data/nanodns/nanodns.ini` | optional Sony TMDB exception |
| `/data/temp/` | staging for downloads (`orbisRPC.elf`, `config.json`) |

## Install flow

The flow checks for a newer GitHub release first. When one is available and
accepted, it downloads the payload and config into `/data/temp/` and installs
both together. Otherwise it uses the copies packaged in the PKG. It checks the
payload before copying, preserves selected user settings from the existing
config, verifies file copies by byte count and hash, and records the installed
version. An installed payload is replaced only through the accepted update
path; a second fresh install stops and asks the user to uninstall first.

After the files are in place, the app offers to add a GoldHEN auto-run entry
and, when nanoDNS is present, the Sony TMDB exception. Replacing a running
payload or adding auto-run may require a restart.

Both the GoldHEN and nanoDNS prompts are **opt-in**, **idempotent** (an entry
that is already present reports "Nothing was changed"), and **skipped silently**
when that tool isn't installed. Neither is required — the daemon works without
them.

## Console facts encoded

- `stat()`/`fstat()` lie about sizes inside the sandbox (observed 4096/4160 for
  a 2 MB file), so install proof is byte count + FNV hash on read-back, and
  existence checks use `open()`, never `stat()`.
- Config writes are atomic (tmp + fsync + rename) with read-back proof.
- The splash is hidden first thing in `main()` so sysmodule loading happens with
  no visible gap before the first dialog.
- Dialog / IME / IME-backend sysmodules and the internal SYSTEM, USER, COMMON
  and PAD modules load before use. Order matters:
  `sceCommonDialogInitialize()` precedes the external module loads. PAD is
  unloaded on exit.
- Buttons: X = enter, O = back. The confirm dialog uses `YESNO` focused on Yes —
  OpenOrbis's `YESNO_FOCUS_NO` default puts the console's confirm button on No,
  which inverts the whole wizard.
- No branch returns to start; cancel and skip always move forward.
- The setup app checks GitHub releases and can download an accepted update.
  The daemon only notifies about a newer release; it does not replace itself.

## Building

```bash
export OO_PS4_TOOLCHAIN="$HOME/OpenOrbis-PS4-Toolchain"
make installer        # or `make all` for payload + PKG
```

| Target | Output |
|---|---|
| `make all` | `output/orbisRPC-<version>.elf` + `output/orbisRPC-<version>.pkg` |
| `make installer` | `output/orbisRPC-<version>.pkg` |
| `make elf` | compile + link only, no packaging |
| `make gfx` | `installer/artwork/*.png` → `installer/assets/images/*.bmp` |

The version comes from `VERSION` at the top of the [`Makefile`](../Makefile),
and is stamped into the output filenames.

`make installer` stages `build-sdk/orbisrpc_sdk.elf` into
`installer/assets/orbisRPC.elf` first — run `make payload` (or `make all`) at
least once beforehand, or staging fails with a clear error. Staging refuses to
run if any *other* `.elf` is sitting in `installer/assets/`, because `pkg.gp4`
is a checked-in file and a stray entry there would ship forever.

Artwork: PNGs live in `installer/artwork/`, converted to BMP by
`scripts/gfx2bmp.py`. `make gfx` is enough after changing one.

## Uninstall

The **UNINSTALL** entry removes the payload and its icon from
`/data/payloads/`, the GoldHEN auto-run entry, the nanoDNS exception, and
`/data/orbisRPC/`. It leaves the app itself installed — remove the PKG
afterwards if you want it gone.
