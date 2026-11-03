# Changelog

Notable changes to the orbisRPC daemon. Versions track
`orbisrpc/version.h`. Entries are host-tested unless marked otherwise; the
console checklist lives in [docs/RELEASE_NOTES_1.1.md](docs/RELEASE_NOTES_1.1.md)
and [docs/VERIFICATION_MATRIX.md](docs/VERIFICATION_MATRIX.md).

## [1.1.0-beta] - 2026-11-03

First beta of the 1.1 stability line. Compare
[`main...beta`](https://github.com/SirHumza/orbisRPC/compare/main...beta)
(145 files changed).

### Added

- Instance-aware single-writer lock: a random per-load token, heartbeat
  refresh, stale reclaim, and legacy pid-only takeover. The holder logs its
  pid and token; a second load stands down instead of posting a second
  presence.
- Recovery timers in `config.json`, all clamped and documented in
  `docs/CONFIG.md`: `lock_heartbeat_s` (60), `lock_stale_s` (600),
  `reconnect_max_s` (600), `timesync_interval_s` (3600), `log_max_kb`
  (512), and `presence_details_media`.
- Redacted diagnostic bundle: creating `/data/orbisRPC/diagnose.request`
  writes `diagnostic.txt` at `0600` within five seconds (version, firmware,
  uptime, gateway close codes, non-secret config, log tail; token scrubbed).
- Deadline-bounded TCP connect helper (`netconnect.c`), a startup
  `discord.com` DNS probe, and a boot census with log path, payload path,
  build hash, and GoldHEN version.
- Offline art cache: `games_cache.json` keeps the resolved `mp:` mapping,
  so a cached title posts its last known name and cover without a network
  call. Capped at 256 entries / 512 KB with least-recently-refreshed
  eviction.
- Fingerprint pipeline: committed `scripts/fingerprint.json`, generated
  `fingerprint_gen.h`, `update_fingerprint.py`, and a CI freshness gate.
  The console never fetches the fingerprint at runtime.
- Local retro index (`ps1.json`, `ps2.json`, `psp.json`) consulted before
  any network lookup, plus a built-in name for the homebrew title Elysium.
- GPL-3.0-or-later `LICENSE` and attribution `NOTICE`; SPDX headers across
  the tree.
- Regression coverage: websocket frame tests, net-connect tests, JSONL
  gateway replay (2 fixtures, 17 scenarios), ASan/UBSan, and e2e gates for
  licensing, secret scanning, and session behavior.

### Changed

- The session timer is process-local: the 10-minute continuation window and
  `session.json` are gone, so relaunch, reboot, Rest Mode, and reinstall
  always start at 0:00. Rest Mode is detected by wall-vs-monotonic drift
  (120 s), which rebases the timer and re-posts presence on wake.
- Closing a game keeps the last tile for `presence_hold_s` (300 s) while
  the session ends immediately; presence is reconciled every
  `presence_reconcile_s` (900 s).
- Gateway reconnects use RESUME with a fresh-IDENTIFY fallback, heartbeat
  ACK timeout, close-code logging, and a configurable slow backoff ceiling.
- Artwork resolution moved out of the op 3 builder, so a dead network
  cannot delay a presence post.
- Updater accepts only a signed manifest for this version on the release
  channels, verifies every asset's SHA-256, refuses truncated bodies, and
  rolls back a build that fails verification at boot.
- Config schema v2: atomic saves, `config.json.bak`, one-time
  `config.v1.bak`, `0600`, unknown keys preserved, and in-place repair of a
  quoted or bare token instead of resetting the file.
- Log rotation size is configurable; startup prints the active log path.
- The installer validates tokens with the same `token.h` the daemon uses,
  writes payload and config `0600`, and logs to `/data/orbisRPC/log.txt`.
- First-party code builds with `-Wextra -Wshadow -Wformat=2 -Wundef
  -Werror`; third-party code keeps non-fatal warnings.

### Fixed

- A second payload load resetting the visible session timer by posting a
  fresh presence (fixed by the instance token in the lock).
- `/data/orbisRPC/stop` being consumed by the first instance, leaving a
  duplicate daemon running. It now stays until the next launch.
- Recurring "another daemon holds the lock" on boot from a stale
  previous-boot lock or a recycled pid.
- Socket leaks in artwork and updater TLS paths, and an unbounded
  `connect()` that could freeze the single daemon thread.
- Desktop logout caused by requesting the `AUTH_TOKEN_REFRESH` capability.
- Token rejections now name the failed rule (placeholder, length, segment,
  illegal character and position) instead of a generic failure.
- A failed presence clear is retried; "Connected to Discord" fires once per
  boot; fragmented closes, oversize frames, and a missing READY reconnect
  instead of wedging.

### Security

- No Discord token or webhook is committed; CI runs
  `ORBISRPC_STRICT_SECRETS=1`. Test fixtures are shape-valid for the token
  validator but do not match token patterns, so secret scanning stays
  clean.
- Diagnostics and log tails redact the token; config and payload files are
  written `0600`.

### Known limits

- 9.00 is the only hardware baseline; 13.x is CI-built and
  community-reported.
- The Setup PKG remains experimental and is not attached to releases.
- Real-Discord confirmation of the token-rotation fix and the long-run
  console checklist are still pending.
- `zlib-stream`, runtime fingerprint fetch, and custom status are deferred
  to 1.2.

<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- Copyright (C) 2026 SirHumza and CyberMask367 - see NOTICE for attribution terms. -->
