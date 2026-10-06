# Troubleshooting

## `?` tile / missing art

1. Daemon log first: `art: resolved mp` = our side fine, look downstream.
2. Vesktop (desktop mod) shows `?` for tiles the official clients and
   phone render correctly. Check phone before reporting.
3. Black/blank idle tile = `home_art` empty or pointing at a dead URL.
   Set it to a live PNG (default: project PlayStation logo).

## Name shows raw ID (CUSA…)

Chain: config `titles` → app.db → local files → Sony TMDB → fallback.
TMDB is TCP-filtered from most consoles (proven, not fixable in code).
If app.db misses too, add one line to `titles` — or wait: first-hit
self-learns, so one manual entry fixes a title forever.

## Installer opens then instantly exits

The first dialog was auto-dismissed (system transition). Current builds
hide the splash + settle 3 s first. Reinstall the latest PKG.

## Kernel panic on installer launch (fixed)

Called dialogs without loading sysmodules. Fixed: module loads in
`ui_init`, app exits quietly if they fail. If you still panic, your PKG
predates the fix — reinstall.

## Payload won't start

The Setup PKG only writes `/data/payloads/orbisrpc.bin` — it never
launches anything. Start it from Payload Guest (GoldHEN's payload menu),
or enable AutoRun for `orbisrpc` once so it boots with every jailbreak.

Manual injection (see `injecting.md`) still exists and needs the loaders
open: ports 9021/9020 are closed whenever GoldHEN's BinLoader toggle
is off. Flip it in GoldHEN's menu. Reboot wipes jailbreak + daemon
(RAM-only), so re-jailbreak first.

## Token rejected (close 4004)

Token dead (password change / logout-all). Paste a fresh one; the daemon
survives 4004s and picks it up without reinstall.

## No FTP (connection refused)

GoldHEN FTP toggle off, or console rebooted to stock. Re-jailbreak.
