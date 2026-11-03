# Changelog

## [1.1.0-beta] - 2026-11-03

`main` 1.0.0 → `beta`. Compare
[`main...beta`](https://github.com/SirHumza/orbisRPC/compare/main...beta).

### Stability

- Per-load token in the lock; a second load stands down instead of posting twice.
- Lock heartbeat, stale reclaim, boot-stale takeover, legacy pid-lock support.
- Stop file stays until the next launch so every running instance sees it.
- Timer is process-local; removed `session.json` and the continuation window.
- Rest Mode drift detection rebases the timer and re-posts presence.
- Presence hold (300 s) and 15-minute reconcile.
- All-or-nothing presence sends; failed clear is retried.
- RESUME with IDENTIFY fallback, heartbeat ACK timeout, close-code logging.
- Reconnect backoff capped by `reconnect_max_s`.
- Bounded TCP connect; TLS and HTTP deadlines.
- Fragmented, oversize, and early frames handled.
- Safe mode after repeated unclean boots; update rollback watchdog.

### Config

- New keys: `lock_heartbeat_s`, `lock_stale_s`, `reconnect_max_s`, `timesync_interval_s`, `log_max_kb`, `presence_details_media`.
- Schema v2: atomic save, `.bak`, `0600`, unknown keys kept, quoted/bare token repair.
- Token rejection names the exact failed rule.

### Features

- Diagnostic bundle via `diagnose.request`, token-redacted, `0600`.
- Offline art cache (256 entries / 512 KB) and local retro index; Elysium built in.
- Build-time fingerprint pipeline with CI freshness gate.
- Playtime ledger plus report script.
- Installer shares the daemon's token validator, writes `0600`, logs to `/data/orbisRPC/log.txt`.
- Startup census: log and payload paths, build hash, GoldHEN version, DNS probe.

### Fixes

- Artwork and updater socket leaks.
- `AUTH_TOKEN_REFRESH` logging the desktop client out.
- Truncated update body accepted as complete.
- Duplicate READY and missing-READY timeout handling.

### Release

- Strict first-party build flags.
- Websocket and net-connect tests, 17 gateway replay scenarios, ASan/UBSan, e2e gates.
- GPL-3.0-or-later, `NOTICE`, SPDX headers.
- Docs: roadmap, verification matrix, release notes.

Known limits: 9.00 is the only hardware baseline; 13.x unverified; Setup PKG experimental; console checklist pending.

<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- Copyright (C) 2026 SirHumza and CyberMask367 - see NOTICE for attribution terms. -->
