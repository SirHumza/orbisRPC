# AGENTS.md — orbisRPC contributor notes

Beta Discord Rich Presence daemon for jailbroken PS4. Testers install the
`.elf` from Discord; no PKG ships yet. README is the tester entry point,
`docs/` is the deep end (index at `docs/README.md`).

## Never break

- `orbisrpc/` is the PS4 payload. Host-test every change:
  `make -C tests test`, `make -C tests asan`,
  `ORBISRPC_STRICT_SECRETS=1 python3 tests/e2e_consumer.py`.
- Never link `libkernel.so` (or any app-world `.so`). Daemon-world only:
  `libkernel_web.sprx`, `libSceLibcInternal.sprx`, `libSceNet.sprx`, plus
  `libSceSystemService.sprx` for foreground detection. CI gates this; a
  violation dies before `main()` with no log line.
- The SDK must be built from git, never the release ZIP (CRT firmware
  gate; see `docs/research/sdk-13.52.md`).
- `discord_build_activity()` arg order is `(state, name, details,
  asset_idle, asset_playing, ...)` — every param is `const char*`, so a
  mis-order compiles. Assert values, not arity (see `test_activity_assets`).

## Secrets

- Never commit a Discord user token or webhook URL. CI enforces this with
  `ORBISRPC_STRICT_SECRETS=1`. The Discord notify webhook lives only in
  the `DISCORD_WEBHOOK_URL` repo secret. See `SECURITY.md`.

## Conventions

- Tests link the vendored sqlite amalgamation, not system sqlite, with a
  test-only VFS shim (`tests/test_utils.c`) — stock `xFullPathname` fails
  silent-CANTOPEN on some CI runners.
- Docs: tester flow in `README.md`, reference in `docs/`
  (`TROUBLESHOOTING.md`, `CONFIG.md`, `BUILDING.md`), hardware research in
  `docs/research/`. Keep them in sync with behavior changes.
- Never tell testers to use Payload Guest for orbisRPC — it crashes. Autorun via
  `/data/payloads` is not recommended on test builds.
- License is undecided (MIT vs GPL). Don't add GPL code or a LICENSE
  file until the owners pick.
