# orbisRPC — Deploy + setup guide

Target: GoldHEN 2.3+ jailbroken PS4, **GoldHEN plugin route** (the native,
auto-starting mechanism — same one the GTA mod menu uses).

**Why a plugin and not a payload:** `/data/GoldHEN/payloads/` is NOT an
auto-load folder (PPPwn stage2 only loads `goldhen.bin` from there; payload
auto-loading from a folder is an unimplemented GoldHEN feature request #296).
The only auto-start mechanism that works on-boot with zero PC involvement is
the GoldHEN **plugin loader**, driven by `/data/GoldHEN/plugins.ini`.

## 1. Copy the plugin to the console

Via FTP (console must be awake and online), build it first:

```
OO_PS4_TOOLCHAIN=~/PS4Toolchain/OpenOrbis/PS4Toolchain \
  GOLDHEN_SDK=/tmp/ghsdk LLD=/Users/mac/lldbuild/build/bin/ld.lld \
  make -C plugin
# -> plugin/build/orbisrpc_plugin.prx
```

Then deploy:

```
put plugin/build/orbisrpc_plugin.prx -> /data/GoldHEN/plugins/orbisrpc_plugin.prx
```

## 2. plugins.ini

`/data/GoldHEN/plugins.ini` must list the plugin. The `[default]` section loads
it for ANY game title, so presence auto-starts every time a game launches:

```
[default]
/data/GoldHEN/plugins/orbisrpc_plugin.prx=true
```

Existing per-title sections (e.g. the GTA menu `[CUSA00411]`) are untouched.
A ready-made file lives at `plugin/plugins.ini.ps4`.

## 3. Seed the config

```
mkdir /data/orbisRPC
put config/config.json -> /data/orbisRPC/config.json
```

The plugin reads `/data/orbisRPC/config.json` (cfg.h: `CFG_PATH`), logs to
`/data/orbisRPC/log.txt` (`LOG_PATH`), and caches per-game state in
`/data/orbisRPC/.lastgame/`.

## 4. One-time Discord setup: your user token

The daemon authenticates to the gateway with your **Discord user session
token**. There is no developer app / OAuth flow anymore — the public gateway
rejects OAuth2 access tokens with close `4004` (that was v1's fatal bug).

1. Get your user token from a logged-in Discord session (search "how to obtain
   discord user token"; only follow steps you understand).
2. Edit `/data/orbisRPC/config.json` over FTP/USB and set:
   - `"token"`: your user token
   - `"application_id"`: optional, only for custom uploaded asset images

## 5. Launch a game + validate

Launch any game (e.g. GTA V). The plugin loads into the game process, reads the
title id via the GoldHEN SDK, and starts the daemon. Check:

```
get /data/orbisRPC/log.txt
```

Expected lines:

```
[..] ws: connected (handshake ok)
[..] discord: identify sent, hb=41s
[..] discord: gateway ready
[..] presence: <Game>
```

Your Discord profile now shows **Playing <Game>** with an elapsed timer.
Exit to the home screen and presence clears while you stay shown as online
(the plugin unloads with the game process).

## 6. Token lifecycle

A user session token is long-lived; there is nothing to refresh. It becomes
invalid if you change your password or "log out of all devices" — grab a fresh
one when the log shows close code `4004` (the daemon exits instead of
hammering the gateway, since repeated bad auth can earn an IP ban).

Config lives on the PS4 at `/data/orbisRPC/config.json` — no PC needed at runtime.

## Troubleshooting

| Symptom | Cause / fix |
|---|---|
| no `log.txt` at all after launching a game | plugin didn't load: check `plugins.ini` has the `[default]` section + correct path, and GoldHEN has `[PluginLoader] PluginLoader_Enabled=1` in its config.ini. |
| `FATAL: ... put your Discord user token` | config missing or `token` still `SET_ME`. Fill in step 4. |
| `gateway closed during auth: 4004` | token wrong/expired/revoked. Re-do step 4. |
| `no 101:` in log | Discord blocked the TLS handshake — check clock / network. |
| `heartbeat timeout` / `gateway dropped` loops | network instability; reconnect is automatic with backoff. |
| game name shows `CUSAxxxxx` | app.xml/app.db lookup didn't find a human name; titleId fallback. |
| presence shows the wrong game | payload-mode detection picks the most-recently-installed title (`/data/app` mtime heuristic) — use the plugin route, which knows the exact title id. |

## Build on Mac (if rebuilding)

```
OO_PS4_TOOLCHAIN=~/PS4Toolchain/OpenOrbis/PS4Toolchain \
  LLD=/Users/mac/lldbuild/build/bin/ld.lld \
  ./scripts/build.sh
# -> build/orbisrpc.elf  (payload ELF, BinLoader route — not auto-loaded)

OO_PS4_TOOLCHAIN=~/PS4Toolchain/OpenOrbis/PS4Toolchain \
  GOLDHEN_SDK=/tmp/ghsdk LLD=/Users/mac/lldbuild/build/bin/ld.lld \
  make -C plugin
# -> plugin/build/orbisrpc_plugin.prx  (the deployed artifact)
```

## Repo layout

```
orbisrpc/daemon.c      shared daemon loop (payload + plugin use it)
orbisrpc/detect.c      foreground detection + titleid->name resolution
plugin/plugin.c        GoldHEN plugin entry (plugin_load / plugin_unload)
plugin/Makefile        builds orbisrpc_plugin.prx against GoldHEN SDK
plugin/plugins.ini.ps4 ready-to-deploy plugins.ini with [default] section
```