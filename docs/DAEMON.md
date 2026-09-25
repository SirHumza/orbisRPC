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

1. `/mnt/sandbox/<ID>_000` mount = authoritative running ID.
2. eboot-process count change = launch/close right now (fast-switch).
3. Save/appdir/app.pkg-atime signals disambiguate.

## Names (first hit wins, no baked tables ever)

1. `titles` map in config (manual override + self-learned).
2. System app.db, read-only SQLite: `tbl_appbrowse.titleName`, then
   `tbl_appinfo` TITLE keys, then `/user/appmeta/<id>/param.json`.
3. Local pronunciation.xml / param.sfo / app.xml.
4. Sony TMDB live (`<ID>_00` + HMAC-SHA1 URL — byte-identical to the PC
   tools; unreachable from most consoles, kept as fallback).
5. Raw ID (never shown twice; retried after 5 min, never frozen).

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
