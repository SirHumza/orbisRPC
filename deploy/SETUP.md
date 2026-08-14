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

## 4. One-time Discord app setup

1. Open https://discord.com/developers/applications -> New Application -> name it
   (e.g. `orbisRPC`) -> Create.
2. Copy **Application ID** and **Client Secret**.
3. In the app -> **OAuth2 -> General**, add a Redirect URL:
   `http://localhost:6770/callback`
4. In the app -> **OAuth2 -> Scopes**, enable `identify` (the `rpc.*` scopes
   are restricted to whitelisted apps and are NOT needed: the daemon uses the
   gateway directly, and the gateway accepts an `identify`-scoped user token).
5. Edit `/data/orbisRPC/config.json` over FTP/USB:
   - `client_id`: your Application ID
   - `client_secret`: your Client Secret
6. Generate an authorization code on any device (phone/PC — one time only):
   open this URL in a browser, approve, and copy the `code=` from the redirect:

   ```
   https://discord.com/api/oauth2/authorize?client_id=<CLIENT_ID>&response_type=code&redirect_uri=http://localhost:6770/callback&scope=identify
   ```

   The browser will hit `http://localhost:6770/callback?code=XXXX` (connection
   refused is fine — just copy the `code=XXXX` part).
7. Paste that code into `config.json` as `auth_code`.

## 5. Launch a game + validate

Launch any game (e.g. GTA V). The plugin loads into the game process, reads the
title id via the GoldHEN SDK, and starts the daemon. Check:

```
get /data/orbisRPC/log.txt
```

Expected lines:

```
[..] token refreshed, expires_in=604799s
[..] gateway connected, hb=41250ms
[..] presence: <Game>
```

Your Discord profile now shows **Playing <Game>** with a timer. Exit to the home
screen and presence clears (the plugin unloads with the game process).

## 6. Token lifecycle

The daemon exchanges the `auth_code` once for an access + refresh token
(`token_expires_at`, `refresh_token` in config). It refreshes automatically
before expiry. The one-time `auth_code` is blanked after use.

Config lives on the PS4 at `/data/orbisRPC/config.json` — no PC needed at runtime.

## Troubleshooting

| Symptom | Cause / fix |
|---|---|
| no `log.txt` at all after launching a game | plugin didn't load: check `plugins.ini` has the `[default]` section + correct path, and GoldHEN has `[PluginLoader] PluginLoader_Enabled=1` in its config.ini. |
| `FATAL: set client_id` | config missing / `SET_ME` placeholder. Fill in step 4. |
| `oauth failed rc=401` | wrong client_id/secret, or the auth_code expired (10 min). Re-do step 4.6. |
| `no 101:` in log | Discord blocked the TLS handshake — check clock / network. |
| `gateway dropped` every poll | WS keepalive mismatch; the reconnect loop is automatic. |
| game name shows `CUSAxxxxx` | app.xml/app.db lookup didn't find a human name; titleId fallback. |
| plugin loads into a system app | `plugin.c` filters non-game titleids (NPXS...) and skips them. |

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