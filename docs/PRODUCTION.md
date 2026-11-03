# OrbisRPC Production Report

Status for the 1.1 daemon line. The last hardware baseline is the 0.4
daemon on PS4 9.00 (`9d9c5b7`); every 1.1 change below is **host-verified
only** (unit + ASan/UBSan + e2e). Nothing in this file claims a 1.1
on-console run, and `13.x` remains unverified.

## Architecture

- `daemon.c` — outer connect cycles (jittered backoff, 10-fail 10-min
  cadence) + inner session loop (1 s tick, 12 s poll, transition debounce,
  playback queue of one). Re-reads config every cycle; learned titles
  persist via atomic save. Token-4004 waits for a config fix, never exits.
  Stop file and SIGTERM share one clean shutdown path.
- `detect.c` — `sceSystemServiceGetAppIdOfBigApp()` + `sceLncUtilGetAppTitleId()`
  for the foreground title; name chain: config override -> built-in
  homebrew names -> local app metadata -> Sony TMDB live -> pkg-zone
  (homebrew only) -> local retro index -> raw-ID fallback. Tri-state
  unknown-hold, 5-min raw retry.
- `discord.c`/`ws.c`/`tls.c` — gateway v10, TLS 1.2 ceiling,
  VERIFY_REQUIRED against curated bundle, masked frames, HB ack tracking
  with reconnect on a missed ACK.
- `art.c` — mp: proxy via external-assets, pack-pattern fallback,
  uploaded-key fallback, memory + 7-day disk cache. Home tile: PlayStation
  logo default; game tile: small system badge.
- `health.c`/`updater.c`/`manifest.c` — dirty-boot marker, 60 s stable
  gate, safe mode, signed all-or-nothing staging, boot rollback.
- `cfg.c`/`lock.c`/`timesync.c` — schema'd fallible config with in-place
  token repair, boot-stale lock reclaim, SNTP wall clock for timer ms.
- `tools/evict.c` — SIGTERM-then-SIGKILL deploy rotation for the locked daemon.

## State machine

NONE -> HOME ("PlayStation 4" + logo) <-> GAME (name + art + badge +
ticking timer). Media titles map to Watching/Listening. A game close ends
the session immediately and holds its tile for `presence_hold_s` (default
300 s); the next launch always starts at 0:00. There is no cross-restart
timer cache. Sessions append to the `playtime.log` ledger.

## Third-party logic ported (verified against source)

- PS4-Rich-Presence (Python/C#): TMDB `_00` HMAC construction (live-200
  verified), persistent mapped cache (self-learning map), title-ID hover
  text, sandbox/NPXS-skip detect, backoff cadence, asset-key fallback.
- PSN-API tool: small system badge pattern.
- SonicLoader: read-only app.db tiers (appbrowse/appinfo/param.json).
- Deliberate skips: PSN credentials, UI editors, third-party scrapes
  (reasons in git history/discussion).

## 1.1 fixes (host-verified)

D-01 no timer continuation/cross-restart cache; D-02 heartbeat timeout and
close-code logging; D-03 Rest Mode drift rebase; D-04 boot-stale lock;
D-05 stop file; D-06 installer log at `log.txt`; D-08 local retro index;
D-09 built-in homebrew names; D-11 `presence_hold_s`; D-12 token reason
logging; D-13 installer FTP fallback text; D-14 token quote/whitespace
repair and bare-value recovery; D-15 startup DNS probe; D-16 log path
logged; D-17 launch path logged; D-18 GoldHEN version banner; D-19 build
hash in the build.

## 0.4 fixes (hardware-verified on 9.00)

Sandbox param.sfo source, details-ID duplication, user `titles` overrides,
`home_art`, shipped logo default, app.db SQLite names, self-learning map,
presence-builder test seam, small badge, strncpy NUL hardening, home_art
validation, 4004 survival, cfg_save return, evict.elf deploy tool, CI
ASan+e2e jobs.

## Not fixed (external / out of scope)

- Sony TMDB unreachable from the console (TCP filtered; correct URL
  proven from LAN). Mitigated: app.db + learning map.
- GoldHEN settings-page loader segfaults every ELF (external).
- IME dialog init errors (external API behavior).
- Vesktop `?` tile (client quirk; phone renders fine).
- Setup PKG crash (CE-34878-0) and Discord-client PKG kernel panic:
  not reproducible without hardware; PKG stays out of the release.

## Risks

- PS4 app.db schema varies by firmware: tiers fail soft, fallback chain
  covers. Worst case is today's behavior, never worse.
- Bare home_art keys are trusted: a typo drops the home tile only.
- Full titles map (64) stops learning with a log line.

## Hardware matrix (proven)

0.4/9.00 baseline: gateway ready, TLS-ECDHE suite, game detection + ticking
timer post-SNTP, mp: art serving (phone), duplicate-ID gone, evict
clean-stop rotations, reboot recovery.

1.1 is **not** hardware-verified. The fixes that depend on console behavior
(Rest Mode drift, boot-stale lock, stop file, GoldHEN banner, startup DNS,
local retro scan, token repair in the real config path) need one 9.00 run
before release notes may call them done. `13.x` stays unverified.

## Readiness

Production for the daemon scope. The release stays daemon-only (`.elf` /
`.bin`). The Setup PKG is **experimental** and is not attached to releases:
its install path crashed with CE-34878-0 on the test console and the
Discord-client PKG triggered a kernel panic, neither reproduced or fixed.
See [INSTALLER.md](INSTALLER.md).

<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- Copyright (C) 2026 SirHumza and CyberMask367 - see NOTICE for attribution terms. -->
