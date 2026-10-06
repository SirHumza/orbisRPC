# Troubleshooting

Symptom first. If your symptom isn't here, bring your `log.txt` to the
Discord (`testers-chat`) — see the repo README for the invite.

## Nothing on Discord at all

1. **First run is supposed to look broken.** A fresh payload creates
   `/data/orbisRPC/config.json` and then waits — it has no token yet.
   Open the config, put your token in the `"token": "SET_ME"` slot,
   re-run the payload.
2. **Upgrading? Delete first.** Every new test build wants a clean
   `/data/orbisRPC` folder. Delete it, run once, re-add your token.
3. **Check the log.** `/data/orbisRPC/log.txt` over FTP. Absent file =
   the payload never executed, full stop. `no token in config` = it ran
   and is waiting on you.

## "Can't connect to Discord" (console is online)

1. Open the PS4 browser and go to `discord.com`. If that doesn't load,
   it's your connection, not the payload.
2. Delete `/data/orbisRPC/log.txt`, restart the console, run the payload
   again. If it still fails, send the new `log.txt` in `testers-chat`.
3. Hotspot/mobile connections have caused oversized frames Discord
   rejects — prefer stable Wi-Fi/LAN.

## Game cover not showing (name + timer fine, no art)

Your DNS blocker is eating Sony's TMDB host. Either disable it or use
[nanoDNS](https://github.com/drakmor/nanoDNS) with an exception:

1. Open `/data/nanodns/nanodns.ini`, find `[exceptions]`, add
   `tmdb.np.dl.playstation.net` at the top.
2. Set the PS4's DNS to `127.0.0.1` so traffic actually goes through
   nanoDNS. **This is the step most people miss.**
3. Save, reboot the console.

## `?` tile / missing art

1. Daemon log first: `art: resolved mp` = our side fine, look downstream.
2. Check on the official client or phone before reporting — Vesktop
   (desktop mod) shows `?` for tiles the real clients render fine.
3. Black/blank idle tile = art URL dead. The defaults point at the
   project icon pack; a custom `large_art`/`home_art` with a typo drops
   the tile only.
4. Raw external URLs and dangling asset keys drop the *whole* activity
   silently (name, timer, everything) — see
   [research/tls-ime.md](research/tls-ime.md) for the verified rules.
5. No cover art at all (name + timer fine)? TMDB is unreachable without
   the nanoDNS exception, so there's nothing to draw — fix DNS per the
   nanoDNS section above and the art comes back on next resolve.

## Name shows raw ID (CUSA…)

The chain is config `titles` → app.db → local files → Sony TMDB →
pkg-zone (homebrew only) → raw ID. TMDB is TCP-filtered from most
consoles, so a title nobody has seen resolves raw the first time — then
self-learns, and one manual `titles` entry fixes it forever.

Known gap: some homebrew (Apollo Save Tool, Cheats Manager, Homebrew
Store) isn't detected yet — under investigation.

## Token rejected (close 4004)

Your token died (password change / logout-all). Paste a fresh one into
the config; the daemon survives 4004s and picks it up without reinstall.
Never share the config file — a user token is full account access.

## Payload won't start

- The payload lives at `/data/payloads/` — put the `.elf` there (FTP/USB)
  and launch it from the payload list. Never use Payload Guest for
  orbisRPC — it crashes.
- No sender handy? elfldr (port 9021) or the BinLoader server (port 9020)
  also work — see [`injecting.md`](injecting.md).
- Ports closed? GoldHEN's BinLoader toggle is off, or the console
  rebooted to stock — re-jailbreak first.
- Reboot wipes jailbreak + daemon (RAM-only). Re-jailbreak, re-send.
- **Auto-run is not recommended on test builds.** It exists (`/data/payloads`
  + GoldHEN autorun queue) but builds turn over too fast — run manually.
  The official release PKG will set up autorun for you.

## Installer opens then instantly exits (PKG, official release)

The first dialog was auto-dismissed (system transition). Current builds
hide the splash + settle first. Reinstall the latest PKG.

## Kernel panic on installer launch (fixed)

Dialogs were called without loading sysmodules. Fixed: module loads in
`ui_init`, quiet exit if they fail. A panic means your PKG predates the
fix — reinstall.

## No FTP (connection refused)

GoldHEN FTP toggle off, or console rebooted to stock. Re-jailbreak.
