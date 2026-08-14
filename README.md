# orbisRPC — Discord Rich Presence for the PS4

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
| M1 — network + TLS (SceNet + LibreSSL/OpenSSL-ABI) | **done (compiles + links)** |
| M2 — Discord OAuth2 token flow + refresh | **done (compiles + links)** |
| M3 — gateway: connect / heartbeat / presence | **done (compiles + links)** |
| M4 — game detection (foreground user → CUSA → title) | **done (compiles + links)** |
| M5 — GoldHEN autoload + package + install | pending (needs on-console test) |
| On-console validation (M1's TLS + detection against live FW) | pending |

`build/orbisrpc.elf` (186 KB) and `build/orbisrpc.fself` (192 KB) link successfully
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
# -> build/orbisrpc.elf, build/orbisrpc.fself, build/orbisrpc.bin
```

The runtime artifact is `build/orbisrpc.fself` (GoldHEN payload) or
`build/orbisrpc.bin` (PKG route). No toolchain = no build.

---

## Installing on the PS4 (no PC after first build)

1. Build `orbisrpc.pkg` (or grab the `.elf` payload) on any Mac/computer.
2. Transfer to the PS4 over FTP, or on a USB stick.
3. **Daemon route (recommended):** put `orbisrpc.fself` in
   `/data/GoldHEN/payloads/` — it auto-loads on every boot and runs in the
   background while you game. (GoldHEN 2.2.)
4. Or install `orbisrpc.pkg` like any homebrew app if you prefer a foreground
   launcher (note: it suspends once a game opens, so the daemon route is what
   powers presence-during-play).

Config lives at `/data/orbisRPC/config.json` — edit it over FTP or on a USB stick.
Logs go to `/data/orbisRPC/log.txt`.

---

## One-time setup: your Discord app

The safe route (recommended; ToS-friendly) needs a Discord **Developer Application**:
it's what makes Discord show "Playing …" as an integration rather than as your own
account automating itself (which Discord bans on the user account — see notes in the
repo). Steps:

1. Open https://discord.com/developers/applications → **New Application** → name it
   `orbisRPC` (or whatever) → **Create**.
2. Copy the **Application ID** (Client ID) and **Client Secret** into
   `/data/orbisRPC/config.json` (`client_id`, `client_secret`).
3. In the app → **OAuth2 → Redirects**, add:
   `https://example.com/callback`
4. In the app → **OAuth2 → Scopes**, add `rpc.activities.write` (and `identify`).
5. Reboot the daemon: it will print an authorize URL into `log.txt`. Open that URL
   on your phone/computer, approve, then paste the `code=` back into the config
   (or let the daemon watch the config file). It exchanges the code once and then
   refreshes automatically.

After that it just runs. No PC needed at runtime.

---

## Safety / ToS notes

- We use the **OAuth2 application flow**, not your personal account token.
  Automating your *user* account is what Discord flags as a "selfbot" and bans for.
  A registered application acting on your permission is the supported integration path.
- Discord may require your application to be verified to use `rpc.activities.write`.
  For a personal, low-traffic app it usually works unverified; worst case it labels
  the activity "via orbisRPC".

---

## License

MIT — see `LICENSE`. Headers + minimal JSON parser (`jsonlite`) are self-contained
(no third-party runtime deps beyond the Orbis toolchain).
