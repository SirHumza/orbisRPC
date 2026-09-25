# OrbisRPC Production Report

Build under review: `9d9c5b7` (daemon 0.4.0 line, `main`), payload
`build-sdk/orbisrpc_sdk.elf` (2,071,536 B, daemon-world linkage only,
zero baked titles). Verified: host unit + ASan/UBSan + e2e ALL PASS;
linkage gate (libkernel_web / libSceLibcInternal / libSceNet, never
libkernel.so). Running on-console at 192.168.1.136 via elfldr:9021.

## Architecture

- `daemon.c` — outer connect cycles (jittered backoff, 10-fail 10-min
  cadence) + inner session loop (12 s poll, 2-hit commit debounce,
  playback queue of one). Re-reads config every cycle; learned titles
  persist via atomic save. Token-4004 waits for a config fix, never exits.
- `detect.c` — sandbox-mount scan (authoritative ID) + save/appdir/atime
  signals + eboot-count fast-switch. Name chain: config override ->
  app.db (SQLite readonly) -> appmeta/sfo/appxml -> Sony TMDB live ->
  raw-ID fallback. Tri-state unknown-hold, 5-min raw retry.
- `discord.c`/`ws.c`/`tls.c` — gateway v10, TLS 1.2 ceiling,
  VERIFY_REQUIRED against curated bundle, masked frames, HB ack tracking.
- `art.c` — mp: proxy via external-assets, pack-pattern fallback,
  uploaded-key fallback, memory + 7-day disk cache. Home tile: PlayStation
  logo default; game tile: small system badge.
- `health.c`/`updater.c`/`manifest.c` — dirty-boot marker, 60 s stable
  gate, safe mode, signed all-or-nothing staging, boot rollback.
- `cfg.c`/`lock.c`/`timesync.c` — schema'd fallible config, sysctl-liveness
  single instance, SNTP wall clock for timer ms.
- `tools/evict.c` — SIGTERM-then-SIGKILL deploy rotation for the locked daemon.

## State machine

NONE -> HOME ("PlayStation 4" + logo) <-> GAME (name + art + badge +
ticking timer). Media titles map to Watching/Listening. Resume window:
same title back within 10 min (or cross-restart via session.json) resumes
the timer. Sessions append to playtime.log ledger.

## Third-party logic ported (verified against source)

- PS4-Rich-Presence (Python/C#): TMDB `_00` HMAC construction (live-200
  verified), persistent mapped cache (self-learning map), title-ID hover
  text, sandbox/NPXS-skip detect, backoff cadence, asset-key fallback.
- PSN-API tool: small system badge pattern.
- SonicLoader: read-only app.db tiers (appbrowse/appinfo/param.json).
- Deliberate skips: PSN credentials, UI editors, PS1/PS2 lists, extra
  toggles, third-party scrapes (reasons in git history/discussion).

## Fixed this cycle

Sandbox param.sfo source, details-ID duplication, user `titles`
overrides, `home_art`, shipped logo default, app.db SQLite names,
self-learning map, presence-builder test seam, small badge, strncpy NUL
hardening, home_art validation, 4004 survival, cfg_save return,
evict.elf deploy tool, CI ASan+e2e jobs.

## Not fixed (external / out of scope)

- Sony TMDB unreachable from the console (TCP filtered; correct URL
  proven from LAN). Mitigated: app.db + learning map.
- GoldHEN settings-page loader segfaults every ELF (external).
- IME dialog init errors (external API behavior).
- Vesktop `?` tile (client quirk; phone renders fine).
- Dead Discord notify webhook (cosmetic post-push hook).

## Risks

- PS4 app.db schema varies by firmware: tiers fail soft, fallback chain
  covers. Worst case is today's behavior, never worse.
- Bare home_art keys are trusted: a typo drops the home tile only.
- Full titles map (64) stops learning with a log line.

## Hardware matrix (proven)

Gateway ready, TLS-ECDHE suite, game detection + ticking timer post-SNTP,
mp: art serving (phone), duplicate-ID gone, evict clean-stop rotations,
reboot recovery. Pending eyes: appdb name flip, badge render, logo tile.

## Readiness

Production for the daemon scope. Installer/PKG remains out of scope per
directive. Ship it.
