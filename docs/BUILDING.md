# Building

## Requirements

- Linux or macOS with `clang-18`, `lld-18`, `llvm-18`.
- The [OpenOrbis PS4 toolchain](https://github.com/OpenOrbis/OpenOrbis-PS4-Toolchain)
  for the Setup app — `OO_PS4_TOOLCHAIN`.
- The ps4-payload-sdk **built from git** — never the release ZIP. The
  SDK's CRT gates startup on a per-firmware table (`crt/patch.c`), and the
  v0.9 ZIP predates the 13.52 offsets, so a ZIP-built payload dies before
  `main()` with no log line. Full story:
  [research/sdk-13.52.md](research/sdk-13.52.md).

```bash
export OO_PS4_TOOLCHAIN="$HOME/OpenOrbis-PS4-Toolchain"
export PS4_PAYLOAD_SDK="$HOME/ps4-payload-sdk"
export PS4_SDK_SRC="$HOME/ps4-payload-sdk-src"
./scripts/build_sdk_from_source.sh   # once
```

## Root `Makefile`

| Target | Builds | Output |
|---|---|---|
| `make all` | payload → stage into `installer/assets/` → package | `output/orbisRPC-<version>.elf` + `output/orbisRPC-<version>.pkg` |
| `make payload` | daemon payload only | `output/orbisRPC-<version>.elf` |
| `make installer` | Setup PKG only | `output/orbisRPC-<version>.pkg` |
| `make pkg` | stage + package, no payload build | — |
| `make elf` | app compile + link, no packaging | — |
| `make gfx` | `installer/artwork/*.png` → `assets/images/*.bmp` | — |
| `make clean` | remove build output, including `output/` | — |

`payload` needs the ps4 SDK; `installer` and everything else need
`OO_PS4_TOOLCHAIN`. `all` needs both, so it is the one target that requires
the full environment.

The payload name inside the package is derived from `#define PAYLOAD_NAME` in
`installer/paths.h`, and the same string is what `pkg.gp4` lists — so the
staged copy, the packaged file, and the console destination cannot drift apart.
Staging fails loudly if any other `.elf` is sitting in `installer/assets/`,
because `pkg.gp4` is checked in and a stale entry there would ship on every
future build.

App internals: [INSTALLER.md](INSTALLER.md).

## Scripts (`scripts/`)

| Script | What |
|---|---|
| `build_sdk_from_source.sh` | Clones `ps4-payload-dev/sdk`, pins the ref, refuses unless `crt/patch.c` has the firmware cases, `make DESTDIR=… clean install`. Run once. |
| `build_sdk.sh` | Compiles the daemon (`orbisrpc/*.c` + vendored sqlite + mbedTLS) and links `build-sdk/orbisrpc_sdk.elf` with daemon-world libs only. Needs `PS4_PAYLOAD_SDK` (and `PS4_SDK_SRC` for the firmware gate). Called by `make payload`. |
| `build_pkg.sh` | Wrapper that exports `OO_PS4_TOOLCHAIN` and runs `make`. |
| `gfx2bmp.py` | Artwork PNG → BMP, used by `make gfx`. |
| `gen_icon_header.py` | Regenerates `orbisrpc/icon_embedded.h` from the notification icon. |
| `add_custom_title.py` | Adds/removes entries in `config/custom.json`. |
| `build.sh` | Legacy OpenOrbis flavor of the payload build. Not the shipping path. |
| `make_ca_bundle.py` | Regenerates the curated CA bundle the daemon pins TLS against. |

## The one rule

Never link `libkernel.so` (or any app-world `.so`). A spawned payload
with unresolvable imports is SIGKILLed before `main()` — silent by
design. Daemon-world set only: `libkernel_web.sprx`,
`libSceLibcInternal.sprx`, `libSceNet.sprx`, plus `libSceSystemService.sprx`
(foreground detection). Why: [research/elf-linkage.md](research/elf-linkage.md).

## CI (`.github/workflows/ci.yml`)

| Job | What |
|---|---|
| `host` | `make -C tests test` (unit), `make -C tests asan` (ASan/UBSan), `python3 tests/e2e_consumer.py` with `ORBISRPC_STRICT_SECRETS=1` (contracts + secret scan). |
| `sdk-payload` | Checks out the SDK from git, pins it, builds it from source, gates 13.52 offsets, builds the daemon ELF, gates daemon-world linkage, uploads `orbisrpc-test-build` (`orbisrpc_sdk.elf`). |
| `notify` | Push-only: posts the commit to Discord via the `DISCORD_WEBHOOK_URL` repo secret. Forks without the secret skip instead of failing. |

## Host tests (`tests/`)

- `make -C tests test` — unit tests (json, sfo, tmdb, updater, art,
  health, cfg, appdb, discord builder, …).
- `make -C tests asan` — same suite under ASan/UBSan.
- `python3 tests/e2e_consumer.py` — source-contract checks (no dead
  secrets in shipped files, session/health/clock/art wiring, detection
  shape). Needs no console; the payload-linkage check skips outside the
  SDK job.

SQLite note: tests link the vendored amalgamation
(`third_party/sqlite/sqlite-amalgamation-3510100`), not system sqlite,
and install a test-only VFS shim — the stock `xFullPathname` path fails
silent-CANTOPEN on some CI runners.
