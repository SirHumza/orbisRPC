# orbisRPC — Discord Rich Presence for the PS4

## Status (Sep 2026)

Backend verified working on hardware: TLSv1.3 through Cloudflare,
8MB READY handling, JSON-safe gateway parsing, heartbeats, reconnects
with the timer intact. User flow is token plus `.elf` injection.

Names resolve in cost tiers (param.sfo, appmeta, app cache, baked
table, Sony TMDB, raw titleId last resort). Cover art uses Sony CDN
icon URLs from the same lookup, falling back to a hosted pack or
uploaded app assets. On-console proof for sandboxed processes and
rendered-art proof are still pending.

A background daemon that runs **entirely on your jailbroken PS4** and posts what you're
playing to your Discord profile as Rich Presence — "Playing *Call of Duty: Black
Ops III* — 1h 23m". No laptop, no phone bridge, no secondary device at runtime.
Just the console.

It's Discord's "now-playing" integration, the way the PS5 does it — but for a
9.00 / GoldHEN PS4.

---

## How it works

The PS4 only runs one foreground app at a time, so a normal homebrew app gets
suspended the moment you launch a game and can't report your activity while you
play. orbisRPC avoids that by running as a **GoldHEN payload daemon**: it loads at
boot (from `GoldHEN/payloads/`) and keeps running in the background while games
launch in the foreground.

```
            +--------------------------+      HTTPS/WebSocket        +-------------------+
            |   orbisRPC (payload daemon) |  wss://gateway.discord.gg  |   Discord         |
            |  on your PS4 @ 192.168.1.136  |  <-- presence -->      |   (your profile)  |
            +-------------+--------------+                              +-----------------+
                          |
       detects running game (process list + app.db) | config + token (/data/orbisRPC)
                          v
            +--------------------------+
            |  /user/app/CUSAxxxx      |
            |  /system_data/priv/mms   |
            +--------------------------+
```

1. The daemon notices which game is running.
2. Looks up its title + cover.
3. Connects to the Discord gateway over TLS.
4. Sets your activity: `Playing <Game>` with an elapsed timer.
5. Clears it when the game exits.

Everything stays on the PS4. Your Mac only touches this repo to *build* it.

---

## Status

| Milestone | Progress |
|---|---|
| Toolchain / build (OpenOrbis, macOS native, LLVM 21 + lld) | **done** |
| M1 — network + TLS (SceNet + LibreSSL/OpenSSL-ABI) | **done** |
| M2 — auth: Discord user session token | **done** (v1's OAuth2 flow was dead-on-arrival: the public gateway rejects OAuth2 access tokens with close 4004) |
| M3 — gateway: connect / heartbeat / presence | **done** (handshake + close-code behavior verified against the live gateway) |
| M4 — game detection (foreground user → CUSA → title) | **done (compiles + links)** |
| M5 — GoldHEN autoload + package + install | pending (needs on-console test) |
| On-console validation (TLS + detection against live FW) | pending |

`build/orbisrpc.elf` (182 KiB) and `build/orbisrpc.fself` (188 KiB) link successfully
against OpenOrbis v0.5.4; the TLS layer uses `libSceLibreSSL`'s OpenSSL-ABI
exports (`SSL_CTX_new` / `SSL_connect` / `SSL_write` / `SSL_read`).

---

## Building (macOS, native — no Docker needed)

Verified path on this Mac (LLVM 21 via brew + OpenOrbis toolchain v0.5.4):

```bash
# 1. one-time toolchain (OpenOrbis v0.5.4, ~160 MB)
mkdir -p ~/PS4Toolchain && cd ~/PS4Toolchain
curl -L -o toolchain-llvm-18.tar.gz \
  https://github.com/OpenOrbis/OpenOrbis-PS4-Toolchain/releases/download/v0.5.4/toolchain-llvm-18.tar.gz
tar -xzf toolchain-llvm-18.tar.gz     # -> ~/PS4Toolchain/OpenOrbis/PS4Toolchain

# 2. one-time toolchain deps
brew install llvm          # clang 21
# lld 21 (linker) — built from source or brew lld@21, see scripts/build.sh

# 3. build
OO_PS4_TOOLCHAIN=~/PS4Toolchain/OpenOrbis/PS4Toolchain \
  LLD=/Users/mac/lldbuild/build/bin/ld.lld \
  ./scripts/build.sh
# -> build/orbisrpc.elf (raw payload), build/orbisrpc.fself, build/orbisrpc-eboot.bin
```

The GoldHEN payload to deploy is the **raw ELF**, `build/orbisrpc.elf`,
uploaded as `orbisrpc.bin` (the `orbisrpc.fself`/`orbisrpc-eboot.bin` outputs
are for the PKG/fself route, not GoldHEN's auto-payload loader). No toolchain
= no build.

---

## Installing on the PS4 (no PC after first build)

1. Build `orbisrpc.pkg` (or grab the `.elf` payload) on any Mac/computer.
2. Transfer to the PS4 over FTP, or on a USB stick.
3. **Daemon route (recommended):** put `orbisrpc.elf` in
   `/data/GoldHEN/payloads/` (named `orbisrpc.bin`) — it auto-loads on every
   boot and runs in the background while you game. (GoldHEN 2.2.)
4. Or install `orbisrpc.pkg` like any homebrew app if you prefer a foreground
   launcher (note: it suspends once a game opens, so the daemon route is what
   powers presence-during-play).

Config lives at `/data/orbisRPC/config.json` — edit it over FTP or on a USB stick.
Logs go to `/data/orbisRPC/log.txt`.

---

## Releases (the update channel)

Consoles self-update from GitHub releases. To ship one:

1. Bump `ORBISRPC_VERSION` in `orbisrpc/version.h`.
2. Full build, tests, commit, push.
3. `gh release create vX.Y.Z /tmp/orbisrpc.bin /tmp/orbisrpc_plugin.prx`
   (renamed from `build/orbisrpc.elf` and `plugin/build/orbisrpc_plugin.prx`).
4. Consoles on older versions download, validate ELF magic, and stage
   both files on next boot. Set `"auto_update": 0` in config to opt out.

---

## One-time setup: your Discord user token

The daemon connects to Discord's gateway as *you*, so it needs your **user
session token** (the same string the Discord client itself uses). This is the
only auth Discord accepts on the gateway without a running client — OAuth2
app tokens are rejected with close `4004` (v1 of this repo tried and failed
exactly that way).

1. Get your user token from a logged-in Discord session (search "how to obtain
   discord user token" — many guides exist; only follow steps you understand).
2. Paste it into `/data/orbisRPC/config.json` over FTP:
   `"token": "your-token-here"`.
3. Reboot / relaunch the game. The log prints `discord: gateway ready`, then
   `presence: <Game>`.

That's it — no developer app, no OAuth dance, nothing else.

Optional: `"application_id"` in config is only needed if you upload custom
asset images to a Discord application and want them attached to the activity.

## Safety / ToS notes

- Using your user token programmatically is technically against Discord's
  Terms of Service. This is exactly how every working headless presence tool
  operates (multi-scrobbler's headless mode, etc.). There is no precedent of
  bans for non-spam presence usage, but the risk is yours.
- Treat the token like a password: it grants full account access. Never share
  the config file or commit a real token to this repo.
- Changing your password or "log out of all devices" invalidates the token;
  grab a fresh one if the log shows close code `4004`.

---

## License

MIT — see `LICENSE`. Headers + minimal JSON parser (`jsonlite`) are self-contained
(no third-party runtime deps beyond the Orbis toolchain).
