# orbisRPC 1.1 roadmap

Status: in progress, working tree uncommitted. This file tracks release
scope, not aspiration. An item is "done" only with the named evidence;
console claims require a 9.00 console session.

## Release rule

Stability, reliability, resource use and secrecy outrank feature count.
A feature that cannot be host-tested gets a console check or it does not
ship. No item is marked done on "it compiles".

Per-defect status and evidence levels: [VERIFICATION_MATRIX.md](VERIFICATION_MATRIX.md).

## Already done (host-tested)

| Area | Change | Evidence |
|---|---|---|
| Recovery | Rollback verifies `.bak` before rename | `test_health_stage_activate` |
| Config | `0600` on load/save; unknown keys preserved; no duplicate keys | `test_cfg_permissions`, `test_cfg_preserves_unknown` |
| Config | Schema v2; atomic save; `.bak` each rewrite; one-time `.v1.bak`; newer schema never rewritten | `test_cfg_migrate` |
| Tests | Header deps tracked (`-MMD -MP`), so a header edit cannot leave stale objects | `touch orbisrpc/cfg.h && make -C tests test` recompiles |
| Config | Atomic save unchanged; failed load keeps defaults | existing `test_cfg_*` |
| Config | `presence_hold_s` holds the closed-game tile 300 s; session ends immediately | `test_cfg_clamp`, `test_utils.c` |
| Sessions | No cross-restart cache; `started` is process-local, every launch starts at 0:00 | `e2e_consumer.py` contract, code read |
| Logging | Runtime rotation at 512 KB, one previous generation | `test_log_rotation` |
| Cache | `games_cache` refreshes `at` for daily-played titles | `test_gamecache` |
| Cache | Stale rows kept for offline fallback; 256 entries / 512 KB cap; oldest evicted | `test_gamecache` |
| Cache | Resolved `mp:` mapping persisted; cache hit posts art with no network call | `test_gamecache`, code read |
| Cache | 8-entry per-boot `mp:` table; boot prefetch no longer evicts earlier static art | `test_art_parse` |
| Detection | Failed art mapping retried on a 15-minute window, not every poll | `test_sched`, code read |
| Identity | Fingerprint header generated from `scripts/fingerprint.json`; official endpoints fetched by `--fetch`; release gate refuses a stale tuple | `test_fingerprint` (17 cases), `--fetch`, release workflow |
| Diagnostics | Redacted bundle; token never appears in memory or file | `test_diag` |
| Gateway | 14 sanitized replay scenarios drive the real connect/tick state machine | `test_replay` |
| Updater | Truncated HTTP bodies refused; `orbisrpc.bin`/`.elf`/`_sdk.elf` normalised | `test_updater_http`, code read |
| Updater | `minimum_version` enforced before staging | `test_manifest` |
| Network | `art.c` closes its socket on every TLS path | code read (same fix pattern as updater) |
| Detection | Expensive probes honour `poll_interval_s`; immediate first poll | `test_sched`, code read |
| Auth | Close `4004` parks until the token changes; no retry storm | code read, no host test (needs gateway) |
| Presence | Settings/browser flags reset on reconnect; 15-min reconcile | code read |
| Gateway | Fragmented messages reassembled; protocol violations surfaced | `test_ws` (10 cases) |
| Build | Daemon compiles and links against ps4-payload-sdk built from git (`573b4a0`); no app-world libraries | `scripts/build_sdk_from_source.sh`, `scripts/build_sdk.sh`, `llvm-readelf` NEEDED gate, `e2e_consumer.py` linkage PASS |
| Lifecycle | Rest Mode drift rebases `started`; stop file exits within 1 s | `test_sched` (`sched_suspend_drift`, `sched_rebase_start`), `e2e_consumer.py` |
| Lock | Lock older than boot is reclaimed even if its pid was recycled | `test_lock_stale` |
| Auth | Token shape reasons; quoted/whitespace trimming; bare token repaired without resetting the file | `test_token_shape`, `test_cfg_token_parsing` |
| Metadata | Built-in homebrew name (`Elysium`); local ps1/ps2/psp index scanned before network | `test_builtin_titles`, `e2e_consumer.py` |
| Diagnostics | Log path, launch path, GoldHEN version and build hash logged at startup | `e2e_consumer.py` contracts; console check pending |
| Network | Startup `discord.com` DNS probe with nanoDNS hint, never fatal | code read; console check pending |

Known deviations: D-08 is only partial (no offline Sony TMDB database),
D-13 keeps the console IME (with an FTP fallback), D-06/D-07 are mitigated
by excluding the PKG from releases, and D-10 is documented but untested.
See [VERIFICATION_MATRIX.md](VERIFICATION_MATRIX.md) and the release
checklist for console-only verification.

## 1.1 feature batch (accepted)

1. **Presence reconciliation** — done on host: 15-minute default, configurable
   via `presence_reconcile_s` (60–3600). Acceptance met for the predicate;
   the console check (manually cleared activity returns within one interval)
   remains in the release checklist.
2. **Diagnostic bundle** — done on host: `/data/orbisRPC/diagnose.request`
   triggers `diagnostic.txt` at `0600`. `test_diag` proves the token never
   appears in the in-memory or written bundle. Plain text opens anywhere.
3. **Regression replay** — done on host: `tests/fixtures/gateway/*.jsonl`
   (2 files, 17 scenarios) run through `discord_connect()`/`discord_tick()`
   in `make -C tests test`. A failed scenario fails the build. RESUME and
   its fallback to fresh IDENTIFY are covered.
4. **Offline metadata and art cache** — done on host: stale rows serve as
   the offline fallback, the file is capped at 256 entries / 512 KB, the
   `mp:` mapping is persisted, and the presence builder is cache-only.
   Airplane-mode behaviour needs the 9.00 console check.
5. **Safe updater audit** — done on host: manifest signature, channel,
   platform, `minimum_version`, per-asset SHA-256, ELF validation,
   verified-backup activation and boot rollback. Truncated HTTP bodies and
   the release-asset name mismatch are fixed. An interrupted update cannot
   replace the live binary because staging is atomic. Console check: let an
   update stage, reboot, confirm the new build runs and a corrupt `.bak`
   is refused.
6. **Event-driven detection spike** — decided: stay with polling for 1.1.
   No shipping code was added; there is no proven event source on 9.00 and
   the probe cost is once per configured interval. Numeric measurement is
   deferred to 1.2 with hardware.

## Release steps (last, in order)

1. Bump `orbisrpc/version.h` to `1.1.0` (done); update the `DAEMON.md`
   header and release notes.
2. Refresh the fingerprint with `python3 scripts/update_fingerprint.py
   --fetch`; the release job repeats the fetch and fails if the committed
   tuple or generated header is not current. No network fetch runs in the
   daemon in 1.1.
3. Re-run all three gates from clean. The e2e gate also verifies the
   GPL-3.0-or-later SPDX headers and that `LICENSE` and `NOTICE` are attached
   to the release.
4. Console session on 9.00:
   - boot → home → game → Settings → home → game;
   - close a game, confirm the tile holds for `presence_hold_s` and a
     relaunch inside the window shows 0:00;
   - enter Rest Mode mid-game, wake after >2 minutes, confirm the presence
     returns and playtime excludes the sleep;
   - create `/data/orbisRPC/stop`, confirm the daemon exits within a second
     and the lock is gone;
   - delete the stale lock left from a prior boot and confirm a second
     launch reclaims it instead of standing down;
   - forced Wi-Fi drop while a game is open, then reconnect;
   - edit `config.json` while parked on `4004`, confirm recovery without
     reboot;
   - paste a quoted token and a bare unquoted token, confirm both load and
     the log gives the exact reason for a bad one;
   - run with nanoDNS stopped, confirm the DNS hint appears and the daemon
     still retries;
   - confirm a PS1/PS2/PSP title resolves from a local JSON with networking
     blocked;
   - leave running long enough to cross the 512 KB log cap;
   - confirm a second payload launch stands down on the lock;
   - create `diagnose.request`, confirm `diagnostic.txt` appears and the
     token is absent;
   - play a title once online, then boot with networking blocked and
     confirm the last known name/art still post.
5. Update `README.md`, `docs/TROUBLESHOOTING.md`, `docs/CONFIG.md` with
   observed behavior only. Record anything not observed as unverified.

## Deferred to 1.2

- `zlib-stream` compressed gateway (Q2). Not needed for correctness; adds
  a decompressor and a new failure surface.
- Runtime fingerprint fetch. The build-time fetch is live (official
  endpoints, weekly PR, release gate); doing it from the console as well
  would add a network dependency to startup for no fidelity gain.
- Custom status via REST (rejected for 1.1).
- Event-driven detection spike numbers. The decision for 1.1 is polling;
  the measurement needs a 9.00 console and belongs with the 1.2 work.

## Open decisions

- License: resolved. `LICENSE` is GPL-3.0-or-later; `NOTICE` carries the
  copyright line and the GPLv3 section 7(b) attribution term. Private
  forks are permitted, public binary redistribution must ship
  corresponding source, and reused files or snippets keep the credit.
- Whether `poll_interval_s` below 5 s should be allowed. The debounce
  predicate already handles 1–2 s, but the clamp forbids it in production.
- Console verification capacity: only 9.00 hardware is available. 13.x
  stays CI-built and explicitly unverified.

<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- Copyright (C) 2026 SirHumza and CyberMask367 - see NOTICE for attribution terms. -->
