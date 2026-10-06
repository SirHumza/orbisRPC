# Daemon design (v1.0.0)

## Loop

Outer connect cycles (jittered exponential backoff, 60 s cap ×10, then
10-minute cadence, never exits) wrap an inner 1 s session loop. Token-4004
never kills the daemon at either level: it backs off and retries so a fixed
token lands on its own.

## States

`NONE -> HOME <-> GAME`, every transition logged. Game commits need 2
consecutive polls; closes need 2 misses. Home posts once (timerless logo
tile) and re-posts after every reconnect (a drop can never leave a blank
tile). Every 15 min the current state reposts regardless (reconciliation).

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
| `app_id == -1` | nothing in front | may clear (2 misses) |
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

## Names (first hit wins, no baked tables ever)

1. `titles` map in config (manual override + self-learned).
2. Local pronunciation.xml / param.sfo / app.xml.
3. Sony TMDB live (`<ID>_00` + HMAC-SHA1 URL) over TLS-443; port 80 is blocked
   on a jailbroken console, so it is the last resort, not the first.
4. pkg-zone.com, **homebrew titles only** (`pkgzone_enabled`, default on).
   Retail titles never reach it — TMDB owns them — and PS1/PS2 ids are excluded
   as a separate deferred problem. It is a scraped third-party page with no API
   contract, so a layout change degrades silently to the raw title id rather
   than blocking the presence. Every hit is written back to the `titles` map.
5. Raw ID (never shown twice; retried after 5 min, never frozen).

Icon URLs from Sony's CDN still arrive as `http://` on some titles; they are
upgraded to `https://` before use, because Discord silently drops an activity
whose artwork is a non-https external URL (the tile just shows "?").

## Art

mp: proxy via external-assets → uploaded asset key → icon-pack pattern.
Dangling keys/URLs are dropped, never sent (they blank the activity).
Game tile carries a small system badge. 7-day disk cache + memory cache.

## Time, sessions, health

SNTP (connected UDP, Nov 2023–Dec 2034 window) drives timer ms. Sessions
persist atomically with validation; 10-min resume window; playtime ledger.
Health: dirty-boot marker (marker-first ordering), crash counter, 3-strike
safe mode, signed all-or-nothing updates with boot rollback.
Single-instance lock with sysctl liveness + exact-name check.
