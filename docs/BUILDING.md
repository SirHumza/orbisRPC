# Building

## Requirements

- macOS or Linux with `clang-18`, `lld-18`, `llvm-18`.
- The ps4-payload-sdk **built from git** — never the release ZIP. The
  SDK's CRT gates startup on a per-firmware table (`crt/patch.c`), and the
  v0.9 ZIP predates the 13.52 offsets, so a ZIP-built payload dies before
  `main()` with no log line. Full story:
  [research/sdk-13.52.md](research/sdk-13.52.md).

## Scripts (`scripts/`)

| Script | What |
|---|---|
| `build_sdk_from_source.sh` | Clones `ps4-payload-dev/sdk`, pins the ref, refuses unless `crt/patch.c` has the firmware cases, `make DESTDIR=… clean install`. Run once. |
| `build_sdk.sh` | Compiles the daemon (`orbisrpc/*.c` + vendored sqlite + mbedTLS) and links `build-sdk/orbisrpc_sdk.elf` with daemon-world libs only. Needs `PS4_PAYLOAD_SDK` (and `PS4_SDK_SRC` for the firmware gate). |
| `build_evict.sh` | Builds `tools/evict.c` → `build-sdk/evict.elf`. Hot-swap helper: stops a running daemon without rebooting. Not needed for the reboot-based tester flow. |
| `build.sh` | Legacy OpenOrbis flavor. Not the shipping path. |
| `make_ca_bundle.py` | Regenerates the curated CA bundle the daemon pins TLS against. |
| `make_manifest.py` | Update-manifest tooling for the signed self-update path. |

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
