# orbisRPC — Deploy + setup guide

Target: GoldHEN 2.2 jailbroken PS4 (FW 9.00), daemon route.

## 1. Copy the payload to the console

Via FTP (the console must be awake and online). GoldHEN's `/data/GoldHEN/payloads/`
auto-loader takes a **raw ELF** payload named `.bin`, so deploy `orbisrpc.elf`
(the real ELF — `build/orbisrpc.bin` is the fself tool's eboot output, not this):

```
put build/orbisrpc.elf -> /data/GoldHEN/payloads/orbisrpc.bin
```

GoldHEN auto-loads payloads from `/data/GoldHEN/payloads/` at boot and keeps
them running in the background while games run in the foreground.

Also make the config dir + seed the config:

```
mkdir /data/orbisRPC
put config/config.json -> /data/orbisRPC/config.json
```

## 2. First boot check

Reboot (or run the payload from the GoldHEN loader) and check:

```
get /data/orbisRPC/log.txt
```

Expected lines:

```
[..] config not found at /data/orbisRPC/config.json; defaults applied   (only first run)
[..] FATAL: set client_id + auth_code (or refresh) in /data/orbisRPC/config.json
```

The FATAL exit is expected until you supply Discord credentials (step 3).

## 3. One-time Discord app setup

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

## 4. Reboot + validate

Reboot the console. `log.txt` should show:

```
[..] token refreshed, expires_in=604799s
[..] gateway connected, hb=41250ms
```

Play a game. After the poll interval (`poll_interval_s`, default 12 s):

```
[..] detect: active name=<Game> tid=CUSAxxxxx
[..] presence: <Game>
```

Your Discord profile now shows **Playing <Game>** with a timer. Exit to the
home screen and presence clears:

```
[..] presence: (cleared)
```

## 5. Token lifecycle

The daemon exchanges the `auth_code` once for an access + refresh token
(`token_expires_at`, `refresh_token` in config). It refreshes automatically
before expiry. The one-time `auth_code` is blanked after use.

Config lives on the PS4 at `/data/orbisRPC/config.json` — no PC needed at runtime.

## Troubleshooting

| Symptom | Cause / fix |
|---|---|
| `FATAL: set client_id` | config missing / `SET_ME` placeholder. Fill in step 3. |
| `oauth failed rc=401` | wrong client_id/secret, or the auth_code expired (10 min). Re-do step 3.6. |
| `no 101:` in log | Discord blocked the TLS handshake — check clock / network. |
| `gateway dropped` every poll | WS keepalive mismatch; the reconnect loop is automatic. |
| presence shows but clears instantly | foreground detection returned active=0 (home screen). |
| game name shows `CUSAxxxxx` | app.xml/app.db lookup didn't find a human name; titleId fallback. |

## Build on Mac (if rebuilding)

```
OO_PS4_TOOLCHAIN=~/PS4Toolchain/OpenOrbis/PS4Toolchain \
  LLD=/Users/mac/lldbuild/build/bin/ld.lld \
  ./scripts/build.sh
# -> build/orbisrpc.fself
```
