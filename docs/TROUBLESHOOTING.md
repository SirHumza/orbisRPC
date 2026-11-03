# Troubleshooting

Symptom first. If your symptom isn't here, bring your `log.txt` to the
Discord (`testers-chat`) — see the repo README for the invite.

## Nothing on Discord at all

1. **First run is supposed to look broken.** A fresh payload creates
   `/data/orbisRPC/config.json` and then waits — it has no token yet.
   Open the config, put your token in the `"token": "SET_ME"` slot,
   re-run the payload.
2. **Upgrading? You no longer delete the folder.** 1.1 keeps
   `config.json` and `games_cache.json` across payload updates; replacing
   the daemon starts a fresh timer by design (session state is
   process-local). Delete `/data/orbisRPC` only if you want a factory reset.
3. **Check the log.** The daemon prints its exact path at startup; the
   default is `/data/orbisRPC/log.txt` over FTP. Absent file = the payload
   never executed, full stop. `No usable Discord token` = it ran and is
   waiting on you, and the same line says exactly what is wrong with the
   token.

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

### GTA V / GTA Online with nanoDNS (unverified)

Dropping nanoDNS entirely is an overreaction if only GTA Online breaks:
add the PSN/GTA hostnames you use to `[exceptions]` (start with
`*.playstation.net` and the GTA Online session hosts), or switch the PS4
DNS back to automatic for that session. This has **not** been tested on
9.00 (D-10) — report the exact hosts that fix it rather than removing
the blocker for everything.

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

The chain is config `titles` → built-in homebrew names → app.db → local
files → Sony TMDB → pkg-zone (homebrew only) → local PS1/PS2/PSP index →
raw ID. TMDB is TCP-filtered from most consoles, so a title nobody has seen
resolves raw the first time — then self-learns, and one manual `titles`
entry fixes it forever. A raw ID is the documented fallback, never a
"not recognized" error.

Retro names come from `/data/orbisRPC/ps1.json`, `ps2.json`, `psp.json`
first. If those files are missing, the daemon falls back to the remote
index and needs working DNS/TLS.

## Collect a diagnostic bundle

Create an empty file at `/data/orbisRPC/diagnose.request` over FTP (or any
file manager), wait five seconds, then fetch
`/data/orbisRPC/diagnostic.txt`. The request file is removed automatically.
The bundle holds version, firmware, uptime, last presence age, recent
gateway close codes, non-secret config values, and the last 8 KB of the
log. The token is never written to it and is scrubbed from the log tail,
so it is safe to attach to an issue. It works even while the daemon is
parked on a rejected token.

## Token rejected (close 4004)

Your token died (password change / logout-all). Paste a fresh one into
`/data/orbisRPC/config.json`; the daemon parks on the rejected token,
re-reads the file every 10 seconds, and reconnects on its own — no reboot
or reinstall. It will not hammer Discord with a credential that was
refused. Never share the config file — a user token is full account access.

## "No usable Discord token" (daemon runs but never connects)

1.1 refuses to loop silently. The log line after it names the exact rule
that failed: still the placeholder, too short/long, an empty segment, or
`illegal character 0x.. at position N`. Fix the reported character, not
the whole file.

Surrounding spaces and one extra pair of matching quotes are stripped
automatically, so a browser copy/paste works. A token written without
quotes (`"token": MTIz...`) is repaired in place and the rest of the
config is kept; the log says `recovered an unquoted "token" value`.
Never add a comma after that bare value.

## Daemon will not stop / "another daemon holds the lock"

- Normal stop: create an empty file at `/data/orbisRPC/stop`. The daemon
  notices within one second and exits cleanly. The file is left in place
  so a second running instance also sees it; the next launch clears it.
  This works even when the payload was injected without a terminal.
- A lock left by a previous boot is reclaimed automatically in 1.1; the
  old "another daemon holds the lock" loop on every boot is gone. If you
  still see it, the daemon is genuinely running (check the payload list)
  or the console clock jumped; reboot and retry.
- Deleting `daemon.lock` by hand is safe only when no daemon is running.

## Startup warning: DNS cannot resolve discord.com

The daemon logs this when the console's resolver is broken (usually the
PS4 DNS is `127.0.0.1` but nanoDNS is not running). Presence will retry,
but nothing connects until DNS works:

1. Confirm nanoDNS is installed and running.
2. Set the PS4 DNS to `127.0.0.1` (or back to automatic) and reboot.
3. The warning is not fatal; the normal retry loop still runs.

## GoldHEN has no "Utilities" payload entry

The GoldHEN menu entry appears at **2.4b18.10 and newer**; the daemon logs
the version it found in `kern.msgbuf` at startup. On older builds, launch
the payload from `/data/payloads` or via elfldr/BinLoader instead of the
missing menu entry.

## Desktop client logs out while orbisRPC runs

1.1 stops requesting the `AUTH_TOKEN_REFRESH` gateway capability. With that
capability set, Discord can return a replacement user token in
`READY.auth_token`; the desktop client's stored copy then stops working and
Discord shows the login screen. orbisRPC cannot write the replacement into
the desktop's store, which is why it now opts out. Update the payload and
paste a fresh token once. This fix is host-tested (a unit test parses the
IDENTIFY payload and asserts the capability bit is absent); if the desktop
still logs out, capture `diagnostic.txt` and the daemon log around the
event — the close-code ring and recent gateway closes are there, and the
token is redacted.

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

## Installer / PKG problems

The Setup PKG is **experimental and not attached to releases**. Two issues
are open and were not reproducible without hardware:

- the installer exits with `CE-34878-0` at the config step (D-06), and
- launching it through the Discord-client PKG route triggered a kernel
  panic on the test console (D-07).

Do not install the PKG expecting a fix in 1.1. Use the payload
`.elf`/`.bin`. If you are testing the PKG, the installer now writes
`/data/orbisRPC/log.txt` (fallback `/data/log.txt`), so attach that to a
report.

## No FTP (connection refused)

GoldHEN FTP toggle off, or console rebooted to stock. Re-jailbreak.

<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- Copyright (C) 2026 SirHumza and CyberMask367 - see NOTICE for attribution terms. -->
