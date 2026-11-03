# Changelog

Notable changes to the orbisRPC daemon. Versions track
`orbisrpc/version.h`. Entries are host-tested unless marked otherwise; the
console checklist lives in [docs/RELEASE_NOTES_1.1.md](docs/RELEASE_NOTES_1.1.md)
and [docs/VERIFICATION_MATRIX.md](docs/VERIFICATION_MATRIX.md).

## [1.1.0-beta] - 2026-11-03

First beta of the 1.1 stability line, from `main` (1.0.0) to `beta`.
145 files changed. Compare
[`main...beta`](https://github.com/SirHumza/orbisRPC/compare/main...beta).

### Daemon lifecycle and locking

- Single-writer lock now carries a random per-load instance token in
  addition to the pid. Payload Guest reuses pid 46, so a reload previously
  looked like its own stale lock; the token distinguishes a live peer from
  a leftover.
- The holder refreshes the lock mtime every `lock_heartbeat_s`. A lock
  silent for `lock_stale_s`, or dated before the current boot, is reclaimed
  with a log line naming the old pid, token, and age.
- Legacy pid-only locks are still understood and taken over by the same
  liveness and heartbeat rules.
- A second load logs `lock: held by pid ... token ...; standing down` and
  exits instead of opening a second gateway session.
- `/data/orbisRPC/stop` is no longer consumed by the first instance to poll
  it; it stays until the next launch so every running daemon sees the
  request, then `daemon_run()` clears it.
- Config is loaded before the lock is taken, so the operator's stale
  threshold applies to the decision to reclaim a wedged predecessor.
- SIGTERM/SIGINT still request a clean stop through the same path as the
  stop file; the lock is released only by its owner.
- An unclean boot increments a crash counter and enters safe mode (updates
  off) after repeated failures; a normal boot clears it.
- A staged update that never proves healthy is rolled back at the next
  boot, before the daemon starts its network work.

### Sessions and presence

- The session timer is process-local. The 10-minute continuation window and
  the cross-restart `session.json` cache are gone, so relaunch, reboot,
  Rest Mode, and reinstall always start at 0:00.
- Rest Mode is detected by wall-vs-monotonic drift (120 s). The session
  start is rebased past the sleep and presence is re-posted on wake. A
  backwards clock jump is treated as a clock correction, not a sleep.
- Closing a game keeps the last tile for `presence_hold_s` (300 s) while the
  session and timer end immediately; a relaunch inside the window starts at
  0:00.
- Presence is re-posted every `presence_reconcile_s` (900 s) even when
  nothing changed, so a silently desynced tile heals without a state change.
- Media apps use `presence_details_media` for the details line; game and
  home text remain separately configurable.
- Presence updates are built and sent all-or-nothing: a partial send drops
  the gateway connection instead of leaving Discord with half an activity.
- A failed presence clear is retried instead of forgotten.
- Playtime is recorded to a ledger (`playtime.log`), with
  `scripts/playtime_report.py` to read it back.
- "Connected to Discord" fires once per boot; reconnects are still logged
  individually with their close code or read result.

### Gateway and networking

- RESUME is used after READY (session id, resume URL, replay from the last
  sequence). A rejected session falls back to a fresh IDENTIFY.
- The heartbeat watchdog reconnects when ACKs are missed; the interval from
  HELLO is clamped to a sane range and a missing READY has its own timeout.
- Close codes are recorded per reconnect; 4004 is surfaced as a token
  problem, and duplicate READY frames advance the sequence instead of
  re-initialising state.
- Frame handling covers fragmentation, oversize frames, early bytes that
  arrive with the HTTP upgrade response, and stop requests during the
  handshake.
- Every socket connect is deadline-bounded (`net_tcp_connect`), because
  `SO_SNDTIMEO` does not bound `connect()` and a blackholed route could
  freeze the single daemon thread. TLS handshakes and HTTP bodies carry
  their own deadlines.
- Reconnect delay is exponential with jitter, capped at 60 s for the first
  ten failures and then at `reconnect_max_s` (default 600 s). The daemon
  never exits on a failed link.

### Detection, names, and artwork

- `games_cache.json` stores the resolved `mp:` art mapping. A cache hit
  posts the last known name and cover with no external-assets call; the
  file is capped at 256 entries / 512 KB and evicts the
  least-recently-refreshed row.
- Failed artwork mappings retry at most every 15 minutes, and the per-boot
  static-art cache holds 8 entries so the boot prefetch cannot evict the
  idle tile or small badge.
- Artwork resolution moved out of the op 3 builder, so a dead network
  cannot delay a presence post.
- PS1/PS2/PSP names resolve from local `/data/orbisRPC/ps1.json`,
  `ps2.json`, and `psp.json` before any network call; the remote retro
  index is a fallback with a 25 s budget across its three files.
- Homebrew names from pkg-zone.com stay opt-out (`pkgzone_enabled`); retail
  titles never reach that service.
- A built-in name covers the homebrew ID `Elysium`; unresolved titles fall
  back to their title id rather than "not recognized".

### Configuration

- Config schema v2 with atomic saves, `config.json.bak` on every rewrite, a
  one-time `config.v1.bak`, `0600` permissions, and unknown keys preserved.
- New knobs, all clamped and logged when adjusted: `lock_heartbeat_s` (60),
  `lock_stale_s` (600), `reconnect_max_s` (600), `timesync_interval_s`
  (3600), `log_max_kb` (512), `presence_details_media`.
- A reload applies `log_max_kb` and `lock_stale_s` without a restart, in
  addition to the existing debug flag.
- A token pasted with whitespace or one matching quote pair is trimmed; a
  bare unquoted value is repaired in place instead of resetting the whole
  config.
- Token validation reports the exact rejection reason (placeholder, length,
  empty segment, illegal character and position) and accepts any 3+ segment
  base64url shape.
- Each console gets a persisted installation id, generated once.

### Diagnostics, logging, and health

- Creating `/data/orbisRPC/diagnose.request` writes `diagnostic.txt` at
  `0600` within five seconds: version, firmware, uptime, last presence age,
  gateway close codes, non-secret config, and the last 8 KB of the log. The
  token is never printed and is scrubbed from the log tail. Works while
  parked on a rejected token.
- Log rotation is runtime-configurable (`log_max_kb`), keeps one previous
  generation, and logs the active path at startup.
- Startup census prints the log path, payload launch path, build hash, a
  non-fatal `discord.com` DNS probe with a nanoDNS hint, and the GoldHEN
  version from `kern.msgbuf` (Utilities needs 2.4b18.10+).
- `HEALTH` reports uptime, presence posts, reconnects, and recent failures;
  `alive:` shows the active title once a minute.

### Updates and releases

- Only a signed manifest on `stable`/`ps4-goldhen` whose `minimum_version`
  this build satisfies is accepted; every asset must match its SHA-256.
- A truncated HTTP body is refused instead of treated as complete.
- Assets named `orbisrpc.bin`, `orbisrpc.elf`, or `orbisrpc_sdk.elf` all
  stage to `/data/payloads/orbisrpc.bin`; activation verifies `.bak` before
  replacing the live binary.
- The release job attaches the ELF, `orbisrpc.bin`, `SHA256SUMS.txt`, and
  the retro index files; a fingerprint freshness gate refuses a committed
  tuple that no longer matches upstream.
- The fingerprint is build-time only; the console never fetches it at
  runtime.

### Installer

- The installer validates tokens with the same header-only `token.h` as the
  daemon, so it cannot accept a token the daemon refuses or reject one it
  accepts.
- The token-bearing config is written `0600` with `fsync` and always closed;
  a mid-copy read error removes the partial destination instead of leaving
  a file that hashes cleanly against itself.
- Installer logging goes to `/data/orbisRPC/log.txt`, the path testers
  already fetch for the daemon.
- Staging never overwrites an existing file, and the evict stage is
  reported in the log.

### Build, tests, and CI

- First-party code builds with `-Wextra -Wshadow -Wformat=2 -Wundef
  -Werror`; third-party code keeps non-fatal warnings.
- New websocket frame tests and bounded-connect tests; `test_utils.c` grew
  by roughly 1,400 lines covering lock, config, token, diagnostics, and
  schedule predicates.
- Sanitized JSONL gateway exchanges replay through the real connect/tick
  state machine (2 fixtures, 17 scenarios): fragmented frames, close codes,
  duplicate READY, ping/pong, ACK loss, malformed numerics.
- ASan/UBSan treats undefined behaviour as a failure; CI runs the same
  host gates plus the e2e consumer checks.
- E2E gates cover session freshness, lock behavior, shutdown signals and
  the stop file, the DNS hint, log accessors, build hash, GPL licensing,
  SPDX headers, attribution, and release assets.
- Fingerprint scripts and the playtime report have their own unit tests.

### Licensing and documentation

- Released under GPL-3.0-or-later. `LICENSE` carries the full text and
  `NOTICE` adds the copyright line, the GPLv3 section 7(b) attribution
  term, and third-party notices.
- Every source file carries an SPDX identifier and copyright header; the
  PFAL-1.0 text is gone.
- Docs added or rewritten: `ROADMAP.md`, `VERIFICATION_MATRIX.md`,
  `IMPROVEMENT_LOG.md`, `RELEASE_NOTES_1.1.md`, plus CONFIG, DAEMON,
  TROUBLESHOOTING, BUILDING, INSTALLER, and README updates for the new
  behavior.

### Removed

- `session.json` and the 10-minute continuation cache.
- The `AUTH_TOKEN_REFRESH` capability request, which let Discord rotate the
  token and log the desktop client out.
- The old PFAL-1.0 license.

### Known limits

- 9.00 is the only hardware baseline; 13.x is CI-built and
  community-reported.
- The Setup PKG remains experimental and is not attached to releases.
- The console checklist (game transitions, Rest Mode, stale lock, stop
  file, 4004 recovery, log cap, second-load stand-down, diagnostic bundle,
  offline boot, staged update) is still pending.
- Real-Discord confirmation that the desktop stays logged in across a long
  session is pending.
- `zlib-stream`, runtime fingerprint fetch, and custom status are deferred
  to 1.2.
