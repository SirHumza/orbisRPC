# orbisRPC 1.1 release notes

Status: host-tested, console checklist pending. Nothing below is a console
claim unless it says so.

## Headline

1.1 is a stability release. It fixes the update path, removes a class of
socket leaks, makes an offline console keep its last known card, and adds
a diagnostic bundle that is safe to share.

## Host-tested changes

- **Build**: the daemon compiles and links against `ps4-payload-sdk` built
  from git at `573b4a0`. The ELF needs only `libkernel_web.sprx`,
  `libSceLibcInternal.sprx`, `libSceNet.sprx` and `libSceSystemService.sprx`;
  no app-world `libkernel.so` is linked.
- **Updater**: only a signed manifest on `stable`/`ps4-goldhen` whose
  `minimum_version` this build satisfies is accepted. Every asset must
  match its SHA-256. A truncated HTTP body is refused instead of treated
  as complete. Release assets named `orbisrpc.bin`, `orbisrpc.elf` or
  `orbisrpc_sdk.elf` all stage to `/data/payloads/orbisrpc.bin`.
  Activation verifies `.bak` before replacing the live binary; the boot
  watchdog rolls back a binary that fails verification.
- **Networking**: `art.c` now closes its socket on every TLS exit, the
  same leak previously fixed in the updater.
- **Offline cache**: `games_cache.json` stores the resolved `mp:` art
  mapping. A cache hit posts the last known name and cover with no
  external-assets call. Stale rows are kept for the offline fallback.
  The file is capped at 256 entries / 512 KB and evicts the
  least-recently-refreshed row. Failed mappings retry at most every 15
  minutes, and the per-boot static-art cache holds 8 entries so the boot
  prefetch cannot evict the idle tile or small badge.
- **Auth session**: IDENTIFY no longer requests the `AUTH_TOKEN_REFRESH`
  gateway capability. Opting in lets Discord return a replacement user
  token in `READY.auth_token` and expect the old one to be discarded;
  orbisRPC cannot update the desktop client's token store, so the rotation
  logged the desktop out. The rest of the capability mask is unchanged,
  and a regression test parses the real IDENTIFY payload to keep bit 8
  off.
- **Fingerprint**: IDENTIFY and REST carry the full desktop property set.
  The version and build number are generated from
  `scripts/fingerprint.json`; `scripts/update_fingerprint.py --fetch`
  reads the stable installer redirect and `discord.com/app`
  `BUILD_NUMBER`, and the release job refuses a committed tuple that no
  longer matches. No runtime fetch runs on the console.
- **Presence path**: artwork resolution moved out of the op 3 builder.
  The builder is cache-only, so a dead network cannot delay a presence
  post. Static art is resolved once at boot.
- **Diagnostics**: creating `/data/orbisRPC/diagnose.request` writes
  `/data/orbisRPC/diagnostic.txt` at `0600` within five seconds. It holds
  version, firmware, uptime, last presence age, gateway close codes,
  non-secret config values and the last 8 KB of the log. The token is
  never printed and is scrubbed from the log tail. Works while parked on
  a rejected token.
- **Regression coverage**: sanitized JSONL gateway exchanges replay
  through the real connect/tick state machine (2 files, 17 scenarios).
  Fragmented frames, close codes, duplicate READY, ping/pong, heartbeat
  ACK loss and malformed numeric fields are covered. The ASAN/UBSan gate
  treats undefined behaviour as a failure.
- **Config**: schema v2, atomic saves, `config.json.bak` on every rewrite,
  one-time `config.v1.bak`, `0600`, unknown keys preserved.
- **Logging**: runtime rotation at 512 KB with one previous generation.
- **Presence reconciliation**: `presence_reconcile_s`, default 900,
  clamped 60–3600.
- **Timers**: the 10-minute continuation window and the cross-restart
  `session.json` cache are removed. `started` is process-local, so a
  relaunch, reboot, Rest Mode or reinstall always starts a new timer.
  This fixes the reported "timer counted hours the console was off" and
  "RPC shows a stale game after restart" cases.
- **Rest Mode**: wall-vs-monotonic drift (threshold 120 s) detects a
  suspend, rebases `started` past the sleep, and re-posts presence on
  wake. A backwards clock jump is treated as a correction, not a sleep.
- **Close behaviour**: `presence_hold_s` (default 300, clamp 0–1800)
  keeps the last tile visible after a game closes, while the session and
  timer end immediately. A relaunch inside the window starts at 0:00.
- **Shutdown**: creating `/data/orbisRPC/stop` stops the daemon within a
  second. The file stays until the next launch so a duplicate instance
  also sees it. SIGTERM keeps working.
- **Lock**: a lock whose mtime predates the current boot is reclaimed
  even if its pid was recycled, fixing the recurring "another daemon holds
  the lock" boot failure. Release only removes our own pid.
- **Token handling**: the daemon logs the exact rejection reason
  (placeholder, length, empty segment, illegal character + position).
  Whitespace and one extra matching quote pair are trimmed. A bare
  unquoted token value is repaired in place instead of resetting the whole
  config. Tokens are accepted on shape only (3+ base64url segments), so a
  valid but unusual token is not refused.
- **Metadata**: built-in name for homebrew IDs no index carries
  (`Elysium`), and PS1/PS2/PSP names resolve from local
  `/data/orbisRPC/ps1.json`, `ps2.json`, `psp.json` before any network
  call. Unresolved titles fall back to their title ID, never "not
  recognized".
- **Diagnostics**: startup logs the log path, payload launch path, GoldHEN
  version from `kern.msgbuf` (Utilities requires 2.4b18.10+), the build
  hash, and a non-fatal `discord.com` DNS probe with a nanoDNS hint.
- **Gateway**: RESUME is used after READY (session id + resume URL, replay
  from the last seq) with a fresh-IDENTIFY fallback when Discord rejects
  the session; heartbeat ACK loss and close codes force a reconnect.
- **Network timeouts**: every socket connect is deadline-bounded
  (`net_tcp_connect`), because `SO_SNDTIMEO` does not bound `connect()` and
  a blackholed route could freeze the single daemon thread for the kernel's
  SYN budget. TLS handshakes, HTTP bodies and the pkg-zone/retro lookups
  carry their own deadlines.
- **Notifications and retries**: "Connected to Discord" is once per boot,
  so a flapping link no longer re-pops it; every reconnect is still logged
  with its close code or read rc. A failed presence clear is retried
  instead of being forgotten, and the retro index fetch is non-blocking
  with a 25 s budget across its three platform files.

## Console checklist (not yet run)

- boot → home → game → Settings → home → game;
- close a game, confirm the tile holds for `presence_hold_s`, then relaunch
  inside the window and confirm the timer starts at 0:00;
- enter Rest Mode mid-game, wake after >2 minutes, confirm presence returns
  and playtime excludes the sleep;
- create `/data/orbisRPC/stop` and confirm a clean exit within one second;
- force a stale lock from a previous boot and confirm it is reclaimed;
- forced Wi-Fi drop while a game is open, then reconnect;
- edit `config.json` while parked on close 4004, confirm recovery;
- paste a quoted token and a bare unquoted token, confirm both load;
- run with nanoDNS stopped and confirm the DNS hint plus continued retry;
- resolve a PS1/PS2/PSP title from local JSON with networking blocked;
- cross the 512 KB log cap;
- second payload launch stands down on the lock;
- create `diagnose.request` and confirm the bundle, including no token;
- play once online, boot with networking blocked, confirm the last known
  name/art still post;
- stage an update, reboot, confirm the new build runs and a corrupt
  `.bak` is refused.

## Known limits

- 13.x is CI-built and community-reported, not verified on this desk.
- `zlib-stream`, runtime fingerprint fetch and custom status are deferred
  to 1.2.
- The event-driven detection spike is a documented decision to stay with
  polling for 1.1; numeric measurement needs 9.00 hardware.
- The fingerprint is build-time only; the console never fetches it.
- The token-rotation fix is compile-time and replay-tested on the host.
  Real-Discord confirmation (desktop stays logged in across a long
  orbisRPC session) still needs a console run.
- The Setup PKG is experimental and not attached to releases: the
  install path crashed with `CE-34878-0` and the Discord-client PKG route
  triggered a kernel panic. Neither is fixed in 1.1; the release is
  daemon-only.

## License

Released under the **GNU General Public License v3.0 or later**
(GPL-3.0-or-later). The full text is in [LICENSE](../LICENSE), and
[NOTICE](../NOTICE) adds the copyright line, the GPLv3 section 7(b)
attribution term, and the third-party notices.

Private, internal forks are permitted. Distributing a binary or publishing
a fork requires the complete corresponding source under GPL-3.0-or-later,
plus a visible credit to **SirHumza and CyberMask367** with a link to the
original repository in the fork's primary README. Modified versions must
not be presented as official orbisRPC releases. Third-party components
keep their own licenses; all are GPLv3-compatible.

Every source file carries an SPDX identifier and copyright header, and the
release assets include `LICENSE` and `NOTICE`.

<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- Copyright (C) 2026 SirHumza and CyberMask367 - see NOTICE for attribution terms. -->
