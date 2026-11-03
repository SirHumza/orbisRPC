# Daemon design (v1.1.0)

## Loop

Outer connect cycles (jittered exponential backoff, 60 s cap ×10, then
10-minute cadence, never exits) wrap an inner 1 s session loop. The inner
loop only services heartbeats, config, and STOP every second; the expensive
detection probes run at `poll_interval_s` (default 12 s, clamped 5–300).

Token-4004 is different from a network drop: retrying a credential Discord
already refused burns IDENTIFY attempts and is what trips abuse protection.
At both the connect and mid-session sites the daemon **parks**: it re-reads
`config.json` every 10 s, logs a reminder about once every 10 minutes, and
reconnects as soon as `token` differs from the rejected value. A takedown
notification fires once per boot. No reboot or reinstall is needed to
recover, and the daemon never spins on a dead token.

The "Connected to Discord" toast is also once per boot. Reconnects after an
outage are logged (`gateway ready`, close code, read rc) but do not re-pop
the dialog; a link that flaps more often than the old five-minute throttle
used to stack one dialog per reconnect.

Every network call runs on the single daemon thread, so all of them are
deadline-bounded. `net_tcp_connect()` does a non-blocking connect and waits
in `select()`; `SO_SNDTIMEO` alone does not bound `connect()` (a blackholed
route still burned the kernel's 75 s SYN budget in a host measurement).
TLS handshakes, HTTP bodies, the retro index budget, and the pkg-zone read
each carry their own deadline on top of that. A dead route therefore costs
seconds, not the session: heartbeats and the STOP check keep their cadence.

## States

`NONE -> HOME <-> GAME`, every transition logged. Transitions are decided on
one detection sample: at the configured cadence (5 s minimum, 12 s default) a
single sample already spans the shell flicker that used to require two 1 s
polls, and demanding more would add a whole interval of latency. Home posts
once (timerless logo tile) and re-posts after every reconnect, together with
Settings/browser state (those flags describe what is on the wire, and the
wire was just replaced), so a drop can never leave a blank tile. Every
`presence_reconcile_s` (default 900 s) the current state reposts regardless.

## Detection

The OS is asked directly: `sceSystemServiceGetAppIdOfBigApp()` names the app in
front, `sceLncUtilGetAppTitleId()` turns that into a TITLEID. One call yields
both state and identity, so they cannot disagree. No struct offsets, no
directory scans, no mtime inference — nothing here a firmware can invalidate.
`research/proc-table-offsets.md` records what this replaced and why.

Three answers stay distinct, because collapsing any two of them clears
presence while a game is open:

| Reading | Meaning | Presence |
|---|---|---|
| `app_id == -1` | nothing in front | may clear after the transition debounce |
| `app_id == 0` / `NPXS` id | system app in front | **hold** — the game is still running |
| unreadable / malformed id | no opinion | **hold** |

## Settings

ShellUI logs every scene change to `kern.msgbuf`; the newest
`OnFocusActiveSceneChanged` entry is the live scene. Settings scenes carry
`: SettingPage`.

Settings **replaces** the whole presence — its own name (`presence_settings_text`),
no artwork, no timer, no small icon — rather than the game carrying a changed
state line. The game underneath keeps its `started` epoch, so returning
restores the original elapsed time instead of restarting the clock. The buffer
is sized from the kernel's own answer and `mmap`ed, because a hardcoded size
fails with ENOMEM on any console whose ring is larger.

## Names (first hit wins; one small built-in table)

1. `titles` map in config (manual override + self-learned).
2. Built-in names for homebrew IDs no index carries (`builtin_titles.h`).
3. Local pronunciation.xml / param.sfo / app.xml.
4. Sony TMDB live (`<ID>_00` + HMAC-SHA1 URL) over TLS-443; port 80 is blocked
   on a jailbroken console, so it is the last resort, not the first.
5. pkg-zone.com, **homebrew titles only** (`pkgzone_enabled`, default on).
   Retail titles never reach it — TMDB owns them — and PS1/PS2 ids are excluded
   as a separate deferred problem. It is a scraped third-party page with no API
   contract, so a layout change degrades silently to the raw title id rather
   than blocking the presence. Every hit is written back to the `titles` map.
6. Retro classics: `/data/orbisRPC/ps1.json`, `ps2.json`, `psp.json` are
   scanned locally first, so a fresh console resolves PS1/PS2/PSP names with no
   DNS, TLS, or nanoDNS exception. The remote retro index is only fetched when
   no local file matches.
7. Raw ID (never shown twice; retried after 5 min, never frozen). A title ID
   is always better than "not recognized".

Icon URLs from Sony's CDN still arrive as `http://` on some titles; they are
upgraded to `https://` before use, because Discord silently drops an activity
whose artwork is a non-https external URL (the tile just shows "?").

## Art

mp: proxy via external-assets → uploaded asset key → icon-pack pattern.
Dangling keys/URLs are dropped, never sent (they blank the activity).
Game tile carries a small system badge.

Artwork is resolved once, outside the presence send: a title's raw cover
URL is converted to an `mp:` path when the name is resolved and the mapping
is persisted in `games_cache.json`. The op 3 builder only reads cached
mappings, so a slow or dead network can never block a presence post. On a
cache hit — including an offline boot — the last known `mp:` path is sent
with no external-assets call at all. The configured static art
(`large_art`, `small_art`, `browser_art`) is resolved once at boot and kept
in an 8-entry per-boot table; one slot was not enough, because resolving
three boot images evicted the earlier ones and silently dropped the idle
tile and small badge.

A URL that Discord has no mapping for is retried at most once per 15
minutes, not once per poll: a dead network must not stall detection (and
the gateway heartbeat that shares its loop) every 12 s. When metadata has
no cover at all, the icon-pack fallback (`<art_base_url><key>.png`) is
resolved in detection and persisted like any other cover.

`games_cache.json` is capped at 256 entries / 512 KB. Stale rows are kept
(so an offline console still shows the last known card) and ignored by the
normal fresh lookup; when the table fills, the least-recently-refreshed
entry is evicted.

## Diagnostics

Create `/data/orbisRPC/diagnose.request` (FTP/SMB) and within five seconds
the daemon writes `/data/orbisRPC/diagnostic.txt` at `0600`, then removes
the request. The bundle contains version, firmware, uptime, last presence
age, the close-code ring, non-secret config keys, and the last 8 KB of the
log. The token is never printed and is scrubbed from the log tail. This
works while the daemon is parked on a rejected token.

## Fingerprint

IDENTIFY and the REST headers carry the properties a real desktop client
sends, including `client_version`, `client_build_number`, `capabilities`,
`client_state` and `X-Super-Properties`. The defaults are generated from
`scripts/fingerprint.json` into `orbisrpc/fingerprint_gen.h`, so refreshing
the tuple needs no C edit. Config keys (`fp_*`) still override the defaults.

`python3 scripts/update_fingerprint.py --fetch` reads two official Discord
endpoints: the stable Windows x64 installer redirect for the version, and
`discord.com/app` for the web bundle's `BUILD_NUMBER` (the number a desktop
client reports as `client_build_number`). It validates the tuple, refuses a
backwards build number, and writes only when the tuple changed. The weekly
`.github/workflows/fingerprint.yml` job runs it and opens a PR on a change;
the release job runs it and fails if the committed tuple is not current.
There is no runtime fetch in 1.1 — the daemon only uses the reviewed values
compiled into the build.

IDENTIFY deliberately does **not** request the `AUTH_TOKEN_REFRESH`
capability (bit 8). When a client opts in, Discord may return a replacement
token in `READY.auth_token` and expect the client to discard the old one.
orbisRPC cannot write that replacement into the desktop client's token
store, so asking for it logged the desktop out of Discord. The rest of the
capability mask is unchanged; a unit test parses the real IDENTIFY payload
and fails if bit 8 ever comes back.

## Updates and rollback

`auto_update` accepts only a signed manifest (`manifest.json` +
`manifest.sig`) whose channel is `stable`, whose platform is `ps4`/
`ps4-goldhen`, whose `minimum_version` this build satisfies, and whose
SHA-256 hashes match every downloaded asset. A release may name the payload
`orbisrpc.bin`, `orbisrpc.elf` or `orbisrpc_sdk.elf`; all normalise to
`/data/payloads/orbisrpc.bin`. Downloads are refused on a truncated HTTP
body (declared `Content-Length` larger than the bytes received). Staging
validates the ELF and activates through `health_stage_activate()`, which
verifies `.bak` before replacing anything; the boot watchdog rolls back a
binary that fails verification.

## Time, sessions, health

SNTP (connected UDP, Nov 2023–Dec 2034 window) drives timer ms.

`started` is process-local. Every new game start is a fresh timer; the old
10-minute continuation window and the cross-restart `session.json` cache were
removed in 1.1 (they counted time the console was off and made a relaunch
look like a continuation). Restarting the daemon is the reset path.

On game close the tile is held for `presence_hold_s` (default 300 s, clamp
0–1800) so a quick menu hop does not flash an empty profile, but the session
itself ends immediately and is written to the playtime ledger. A relaunch
inside the hold window therefore starts at 0:00 and replaces the held tile.

Rest Mode is detected from wall-vs-monotonic drift (threshold 120 s): the
sleep duration is excluded from playtime by rebasing `started` forward, and
presence is re-posted on wake. A backwards wall jump is treated as a clock
correction, not a sleep.

Health: dirty-boot marker (marker-first ordering), crash counter, 3-strike
safe mode, signed all-or-nothing updates with boot rollback.
Single-instance lock with sysctl liveness + exact-name check; a lock whose
mtime predates this boot is reclaimed even if its pid was recycled.

## Stopping and startup log

A daemon injected without a controlling terminal stops when
`/data/orbisRPC/stop` appears. The file is checked every second and the
daemon exits cleanly through the same path as SIGTERM. It is left in place
until the next launch, so every running instance sees the request instead
of only the first poller. No lock deletion or reboot is needed.

The startup log names the file receiving log lines (D-16), the payload path
it was launched from (D-17), the GoldHEN version from `kern.msgbuf` (D-18),
the exact token rejection reason (D-12), and the build hash (D-19). A failed
`discord.com` DNS probe is logged with a nanoDNS hint (D-15) but is not
fatal.

## Logging

`log.txt` is capped at 512 KB while the daemon runs, not only at startup:
the active file rolls to `log.txt.1` (one previous generation kept) and a
fresh file is opened. A failed roll keeps writing to the current stream — a
full disk must not turn logging into a crash loop.

<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- Copyright (C) 2026 SirHumza and CyberMask367 - see NOTICE for attribution terms. -->
