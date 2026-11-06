# Discord user-session gateway: an evidence-based behavioural specification

Status: research complete, implementation not started
Audience: orbisRPC maintainers, anyone writing a native Discord client
Scope: everything an unprivileged user token client must do to look and behave
like the official desktop client on Discord's servers

---

## 0. TL;DR

1. The official client treats the user token as **mutable**. `READY.auth_token`
   is a replacement credential. The client dispatches `UPDATE_TOKEN`, which
   rewrites the persisted token (`localStorage.token`, optionally encrypted with
   `safeStorage`). orbisRPC ignores this field and keeps the token it started
   with.
2. The official client IDENTIFYs with **capabilities 1734653** (`0x1A77FD`)
   or **1767421** (`0x1AF7FD`) when private-channel obfuscation is enabled.
   orbisRPC sends **16125** (`0x3EFD`), which is missing bits 8, 14, 17, 19
   and 20, and includes bit 11 that the desktop client does not.
3. The official client sends a **`qos_token`** in IDENTIFY. It is a base64
   protobuf that carries the *hash of the current auth token* plus an expiry.
   Sending a stale or absent hash is a strong signal that the client is not the
   one that owns the token.
4. Sharing one token between a PC and a PS4 is structurally fragile: both
   clients present the same credential from different fingerprints, and when
   Discord rotates it neither side converges. A native login on the console is
   the only durable fix.
5. Everything else (presence schema, heartbeat shape, REST headers, activity
   flags) is a fidelity problem, not a correctness problem. It affects how
   plausible the session looks, not whether it survives.

The rest of this document is the evidence for those five claims, followed by a
concrete implementation plan.

---

## 1. Evidence base

| Artifact | Identity | Notes |
|---|---|---|
| Web client bundle | `web.0bcf9a9e83b2c3c7.js`, 12.9 MB | build `663810`, app version `0.0.421` |
| Bundle hash | `VERSION_HASH=164cf72e97d1e6032c69c2e71b81adc93ee086d4` | from `window.GLOBAL_ENV` |
| Bundle timestamp | `BUILT_AT=1793780331975` | 2026-11-04 |
| macOS app | `Discord.app` 0.0.421, `sentryRelease=discord-desktop-20bdedc5fd893290d9b86786f2d838ebc3978b0b` | bootstrap only, modules downloaded at runtime |
| Windows package | `Discord-1.0.9267-full.nupkg` | same `app.asar` bootstrapper, 3.47 MB |
| API version | `GLOBAL_ENV.API_VERSION=9` | gateway `v=9`, REST `/api/v9` |
| Gateway endpoint | `wss://gateway.discord.gg` | `GLOBAL_ENV.GATEWAY_ENDPOINT` |

All byte offsets below refer to the extracted `web.0bcf9a9e83b2c3c7.js` unless
stated otherwise. Offsets are stable for this build only.

Labels used throughout:

- **[E]** direct evidence, code or string visible in the artifact.
- **[I]** inference from evidence, not directly observable.
- **[D]** documented protocol behaviour, not visible in this artifact.

---

## 2. Identity model

### 2.1 Credentials

Discord issues two distinct kinds of credential:

| Credential | Used by | Transport |
|---|---|---|
| User token | official clients, user-account libraries | `Authorization: <token>` header, IDENTIFY `token` |
| Bot token | bots | `Authorization: Bot <token>` |
| OAuth2 access token | third-party apps acting for a user | `Authorization: Bearer <token>` |

orbisRPC uses a **user token**. That means every request is authenticated as
the account itself, with the full permission set of the account, and the gateway
opens a *user session*, not a bot session.

The user token is an opaque string. In practice it is base64url-encoded data
with three dot-separated segments: user id, issue timestamp, and an HMAC. The
codebase already validates the three-segment shape in `orbisrpc/token.h`. Do not
parse the payload beyond the shape check; the format is not contractual and has
changed repeatedly.

### 2.2 Token storage in the official client

The token store is webpack module `247775` at offset **12 361 801**.

```js
let l = "dQw4w9WgXcQ:";          // encryption marker
let c = window.DiscordNative?.safeStorage;
```

Two localStorage keys hold credentials:

| Key | Constant | Contents |
|---|---|---|
| `token` | `s.il` | active account token |
| `tokens` | `s.zy` | `{ [userId]: token }` for fast account switching |

Storage rules, from `p()` and `m()` in that module:

- On write, if `safeStorage.isEncryptionAvailable()` and the value does not
  already start with `dQw4w9WgXcQ:`, the client stores
  `dQw4w9WgXcQ:` + `safeStorage.encryptString(token)`.
- On read, a value starting with the marker is decrypted with
  `safeStorage.decryptString(value.slice(12))`. Anything else is treated as a
  plaintext token.
- `_` tracks whether any token was encrypted. If it was, `init()` removes the
  plaintext keys and keeps only the encrypted forms.

Consequences for a third-party client:

- The token is not a static secret. It has a persistence layer because it is
  expected to change.
- A client that never writes back a rotated token is, from Discord's point of
  view, a client that cannot keep its own credentials consistent.

### 2.3 Fingerprint, installation id, launch id

Three separate identifiers are involved. They are easy to conflate and doing so
produces a detectable mismatch.

**Fingerprint** (`X-Fingerprint`, stored at localStorage key `fingerprint`,
offset **9 413 363**):

- Read from storage at startup (`U = o.w.get("fingerprint")`).
- If absent, the client fetches experiments with `X-Fingerprint` omitted and the
  server returns one.
- Sent on every REST request when non-empty.
- It is a server-issued value. A client that invents a random UUID will not
  match what the server has on file for that account.

**Installation id** (`X-Installation-ID`, key `installation_id_v3`, offset
**9 413 363**):

- Read from `installation_id_v3`; falls back to legacy `analytics_installation`
  and migrates it forward.
- `getInstallationForTracking()` returns it only when `p()` is true. In the web
  build `p` is a constant `true`, so it is always sent when present.
- Also placed into IDENTIFY properties as `installation_id` (offset
  **5 291 284**).

**Client launch id** (`client_launch_id`):

- Generated per process launch, sent in super properties and in `UPDATE_TIME_SPENT_SESSION_ID`.
- It changes on every start. That is expected. Do not persist it.

**Client heartbeat session id** (`client_heartbeat_session_id`):

- Comes from `(0,r.Vc)()` and is only added to super properties when it differs
  from the cached value (offset **2 885 362**). It is a heartbeat/session
  correlation value, not the gateway session id.

### 2.4 Token rotation

This is the core finding.

READY handling, offset **5 305 357**:

```js
null != e.auth_token && eW({ type: "UPDATE_TOKEN", token: e.auth_token, userId: e.user.id })
```

`UPDATE_TOKEN` reaches the authentication store, which calls
`a.setToken(token, userId)`, which calls the persistence layer described above.
On the next request and the next IDENTIFY the new value is used.

The client never logs out because of this. It simply swaps credentials. There is
no user-visible event, no toast, no confirmation.

Two further WRITE paths return a new token and are handled the same way:

- `PATCH /users/@me` returns `{ token }` when the password or email changes
  (offset **733 710**). The client dispatches `UPDATE_TOKEN` and drops the field
  before storing the user object.
- Account switching (`MULTI_ACCOUNT_SWITCH_TOKEN_COLLISION_*`, offset
  **6 606 607**) rewrites per-user tokens and explicitly detects when two user
  ids end up holding the same token.

The existence of collision detection for *two user ids with one token* is worth
noting: it implies the client's model is one token per account, not one token
per device.

**What this means for orbisRPC [I]:** if Discord rotates the token during
orbisRPC's session, orbisRPC keeps using the old value. The old value is now
dead everywhere. The next orbisRPC reconnect gets close code 4004. If the PC
client was also running, it updated its own store, so it may survive; if the PC
client was closed at rotation time, it reconnects later with the stale token and
is logged out. That matches the reported "logged out after a few hours" symptom
on both sides.

The rotation is not necessarily periodic. It is more likely event-driven
(security events, session anomaly, long-lived session refresh). The client
handles it whenever it appears, which is the correct design.

---

## 3. Login and MFA

Even though orbisRPC currently borrows a token, a native client needs the login
flow. Endpoints and bodies below are from the bundle.

### 3.1 Password login

```
POST /api/v9/auth/login
Content-Type: application/json
X-Super-Properties: <base64 JSON>
X-Fingerprint: <fingerprint, if known>
X-Installation-ID: <installation id>
X-Discord-Locale: <locale>
X-Discord-Timezone: <IANA zone>
Accept-Language: <weighted list>
```

Body:

```json
{
  "login": "email or phone",
  "password": "…",
  "undelete": false,
  "login_source": null,
  "gift_code_sku_id": null
}
```

Response may contain any of:

```json
{
  "token": "…",
  "user_id": "…",
  "mfa": true,
  "sms": false,
  "webauthn": false,
  "totp": true,
  "ticket": "…",
  "login_instance_id": "…",
  "backup": false,
  "required_actions": []
}
```

### 3.2 MFA

```
POST /api/v9/auth/mfa/totp
POST /api/v9/auth/mfa/sms
POST /api/v9/auth/mfa/webauthn
```

Body:

```json
{
  "code": "123456",
  "ticket": "<ticket from login>",
  "login_source": null,
  "gift_code_sku_id": null,
  "login_instance_id": "<from login>"
}
```

`login_instance_id` must be echoed. The client keeps it in module state and
reuses it across the MFA retry (offset **9 416 650**, `getLoginInstanceId`).

### 3.3 Post-login

After a successful login the client:

1. Stores the token (encrypted if possible).
2. Fetches `/users/@me`, `/users/@me/settings`, guild list, read states.
3. Opens the gateway and IDENTIFYs.

A native client can skip most of step 2. Presence and session validity only need
the gateway. But skipping REST entirely makes the account look like it never
loads, which is itself unusual for a client that stays connected for hours.

---

## 4. Super properties

`X-Super-Properties` is base64 of the UTF-8 JSON produced by `eS()` at offset
**10 444 600**, base64-encoded by `p()` at **10 435 049**. The same object,
extended with a few IDENTIFY-only keys, goes into the gateway IDENTIFY.

### 4.1 Desktop key set

Built in this order by the desktop branch (`eE = window.DiscordNative`,
offset **10 439 200**):

| Key | Value / source | Notes |
|---|---|---|
| `os` | `Windows` / `Mac OS X` / `Linux` | from `process.platform` |
| `browser` | `Discord Client` | literal |
| `device` | `""` | desktop is empty; web can be `iPhone` etc. |
| `device_model_name` | `""` | |
| `system_locale` | e.g. `en-US` | |
| `has_client_mods` | `false` | true if the client is patched |
| `client_version` | `0.0.421` | `app.getVersion()` |
| `os_version` | OS release string | on macOS `systemVersion` |
| `os_version_is_release` | bool | false on macOS when the version is a beta |
| `os_sdk_version` | `10` on Windows, major on macOS | |
| `os_arch` | `x64` / `arm64` | `os.arch` |
| `app_arch` | `x64` / `arm64` / `universal` | `os.appArch` |
| `release_channel` | `stable` | |
| `client_build_number` | `663810` | integer |
| `native_build_number` | native app build | optional, integer |
| `client_event_source` | `null` | `"OVERLAY"` inside the overlay |
| `client_launch_id` | per-launch UUID | |
| `install_source` | optional | only when known |
| `browser_user_agent` | Electron UA | |
| `browser_version` | Electron version | e.g. `37.6.0` |
| `window_manager` | Linux only | |
| `distro` | Linux only | |
| `runtime_environment` | Linux only | |
| `display_server` | Linux only | |

### 4.2 Web extras

The web build adds referral and campaign keys when present:
`referrer`, `referring_domain`, `utm_source`, `utm_medium`, `utm_campaign`,
`utm_content`, `utm_term`, plus `<key>_current` variants. It also adds
`browser_user_agent`, `browser_version`, and `os_version`.

A console client should not send web-only referral keys. It should send the
desktop set.

### 4.3 IDENTIFY-only extensions

From `handleIdentify` at offset **5 291 284**:

```json
{
  "client_app_state": "focused" | "unfocused",
  "is_fast_connect": false,
  "gateway_connect_reasons": "COLD_START",
  "installation_id": "<installation id>"
}
```

`client_app_state` comes from module `183636` (offset **3 022 120**):

```js
getState: () => i.A.isAppFocused() ? "focused" : "unfocused"
```

`gateway_connect_reasons` comes from module `469177` (offset **5 386 000**
region). Default reason list is `["COLD_START"]`; other reasons are added and
removed around operations. The function sorts the keys, joins with `,`, and
returns `"NO_REASONS"` when the set is empty.

A console client has no window focus, so pick one value and keep it stable. If
the session is a background presence daemon, `"unfocused"` is the honest and
consistent choice. Do not alternate between the two; a client that flips
`client_app_state` without a corresponding REST/analytics change looks synthetic.

### 4.4 Encoding

```
X-Super-Properties: base64(utf8(json))
```

Standard base64, not base64url, no line breaks. The client uses
`Buffer.from(JSON.stringify(obj)).toString("base64")`.

---

## 5. Gateway transport

### 5.1 URL

Built in `_connect()` at offset **5 390 422**:

```js
const u = new URL(this._getGatewayUrl());
u.searchParams.append("encoding", encodingName);   // "json" | "etf"
u.searchParams.append("v", GLOBAL_ENV.API_VERSION); // "9"
if (compression !== null) u.searchParams.append("compress", compression);
```

Resulting URLs:

| Client | URL |
|---|---|
| Web | `wss://gateway.discord.gg/?encoding=json&v=9&compress=zlib-stream` |
| Desktop | `wss://gateway.discord.gg/?encoding=etf&v=9&compress=zstd-stream` |
| orbisRPC today | `wss://gateway.discord.gg/?v=10&encoding=json` |

`resume_gateway_url` from READY replaces the base URL for resumes and is
normalised by stripping a trailing slash.

Two observations:

- orbisRPC uses `v=10`, the official client uses `v=9`. v10 is valid and
  accepted, but it is a divergence. The cheapest fix is to match `v=9`, because
  matching costs nothing.
- Compression is optional. JSON with no `compress` parameter is accepted and is
  the simplest correct choice for a C client. Do not implement zstd for fidelity
  reasons; implement it only if bandwidth matters.

### 5.2 Encodings

**JSON** (`getName() === "json"`, offset **5 361 648**):
`JSON.stringify` / `JSON.parse`, `wantsString() === true`.

**ETF** (`getName() === "etf"`, offset **5 361 403**):
Erlang External Term Format via the native `discord_erlpack` module. The
packer starts every payload with byte `131` (`0x83`, ETF version). Selected when
`isPlatformEmbedded` is true and `requireModule("discord_erlpack")` succeeds.

`isDiscordGatewayPlaintextSet()` forces JSON even on desktop (debug flag).

A third-party client does not need ETF. Discord accepts JSON on the user
gateway for every client type. The `encoding` parameter is a declaration, not a
challenge.

**Compression**, selected by `F(tl)` from an ordered list:

| Handler | Algorithm | `usesLegacyCompression()` | Availability |
|---|---|---|---|
| zstd | `zstd-stream` | `false` | desktop with `discord_zstd` |
| zlib | `zlib-stream` | `false` | any `Uint8Array` |
| none | `null` | `true` | always |

zlib-stream framing: a message is a stream of deflate output; the end of a
message is marked by the four-byte suffix `00 00 FF FF` (`Z_SYNC_FLUSH`). The
decoder checks `byteLength >= 4 && getUint32(len-4, false) === 65535`
(offset **5 341 400**).

The `compress` flag in IDENTIFY is `usesLegacyCompression()`, so modern clients
send `false` in both zstd and zlib cases. orbisRPC sending `false` is correct.

### 5.3 Connection state machine

States (offset **5 390 000** region):

```
CLOSED -> CONNECTING -> IDENTIFYING -> SESSION_ESTABLISHED
                     \-> RESUMING   -> SESSION_ESTABLISHED
any    -> WILL_RECONNECT -> CONNECTING
```

Rules encoded in the client:

- `RESUMING` counts as "session established" for the purpose of sending, so
  queued presence updates are allowed to flush.
- `_doResumeOrIdentify` resumes only if `sessionId != null` **and** the last
  heartbeat ack is either null or at most **3 minutes** old. Otherwise it
  re-identifies.
- `4004` closes the socket permanently (`CLOSED`) and never retries.
- Any other close schedules a reconnect with backoff 1 s to 60 s. More than four
  consecutive failures trigger a full `_reset`, which clears `sessionId`, `seq`
  and the resume URL.
- A missing `HELLO` within **30 s** aborts the attempt.

### 5.4 HELLO and heartbeats

HELLO (op 10) carries `heartbeat_interval` in milliseconds.

First heartbeat scheduling (offset **5 390 000**, `_startHeartbeater`):

```js
setTimeout(() => {
  heartbeatAck = true;
  heartbeater = setInterval(sendHeartbeat, interval);
  sendHeartbeat();
}, Math.floor(Math.random() * interval));
```

So the first heartbeat fires at a uniformly random point inside the first
interval, not immediately and not at a fixed offset. This jitter is deliberate
and is a fingerprint: a client that always heartbeats at exactly `t = 0` or
exactly at `t = interval` is not the official client.

Heartbeat payload (op 1):

```json
{ "op": 1, "d": { "seq": 42, "qos": { "active": true, "reasons": ["..." ] } } }
```

`qos` can be `null` when no activity state has been recorded yet. `_sendHeartbeat`
uses `_consumeQOSPayload()`, which rotates `currentPayload` to `upcomingState`.

Ack handling: each interval, if the previous heartbeat was not acked, the client
declares the connection dead and closes with code **4000**, then reconnects with
backoff. orbisRPC's two-missed-interval timeout is more lenient than the official
one-miss rule. That is harmless for liveness but differs behaviourally.

Expedited heartbeats exist for power resume and network changes:

| Trigger | Timeout |
|---|---|
| power monitor resume | 5 s |
| network online | 5 s |
| network offline | 15 s, no immediate reconnect |

The payload does not change; only timing does.

Opcode 3 sends a heartbeat request from the server; the client responds with a
heartbeat immediately and restarts the interval.

### 5.5 QoS token

`qos_token` is built in `_doIdentify` (offset **5 390 422**):

```js
const clientProvided = QosToken.ClientProvided.create({ isActive: this.getIsUserActive() });
const derived = DerivedQosData.fromBinary(base64decode(storedQosToken)); // may throw
const token = base64encode(QosToken.toBinary({ clientProvided, derived }));
```

`getIsUserActive()` returns `upcomingState?.active ?? currentPayload?.active ?? false`.

The stored `derived` value is whatever Discord returned as `READY.qos_token`. It
is persisted in `DerivedQosDataStore` (`persistKey = "DerivedQosDataStore"`,
offset **5 388 697**) keyed by user id, and cleared on logout.

Protobuf schema, recovered from the bundle at offset **5 383 250**:

```proto
package discord_protos.qos_token.v1;

message QosToken {
  ClientProvidedQosData client_provided = 1;
  DerivedQosData        derived         = 2;
}

message ClientProvidedQosData {
  bool is_active = 1;
}

message DerivedQosData {
  bytes  claims    = 1;
  bytes  signature = 2;
  uint32 key_id    = 3;
}

message Claims {
  fixed64 user_id         = 1;
  fixed64 issued_at       = 2;
  bool    is_staff        = 3;
  bytes   auth_token_hash = 4;
  fixed64 expires_at      = 5;
}
```

Field numbers and wire types are confirmed by the generated
`internalBinaryRead`/`internalBinaryWrite` methods, not guessed.

Meaning [I]:

- `derived` is a server-signed statement binding the session to a specific
  `auth_token_hash` and an expiry.
- `client_provided.is_active` is the client's own claim about user activity.
- If the client echoes a `derived` block whose hash no longer matches the
  presented token, the claim is stale. If the client sends no `derived` block at
  all, it is a first-time client.
- The presence of `expires_at` implies the derived block must be refreshed, and
  the only place it appears is READY. So every fresh READY can hand back a new
  `qos_token` alongside a new `auth_token`.

Practical rule for orbisRPC: store the last `READY.qos_token` verbatim and echo
it in the next IDENTIFY, with `client_provided.is_active` reflecting actual user
activity. Never fabricate `derived`.

### 5.6 IDENTIFY

Exact payload, from `_doIdentify`:

```json
{
  "op": 2,
  "d": {
    "token": "<user token>",
    "capabilities": 1734653,
    "properties": {
      "os": "Windows",
      "browser": "Discord Client",
      "device": "",
      "system_locale": "en-US",
      "client_version": "0.0.421",
      "os_version": "10.0.22631",
      "os_arch": "x64",
      "app_arch": "x64",
      "has_client_mods": false,
      "client_launch_id": "…",
      "release_channel": "stable",
      "browser_user_agent": "Mozilla/5.0 … Discord/0.0.421 …",
      "browser_version": "37.6.0",
      "os_sdk_version": "10",
      "client_event_source": null,
      "client_build_number": 663810,
      "native_build_number": 0,
      "client_app_state": "focused",
      "is_fast_connect": false,
      "gateway_connect_reasons": "COLD_START",
      "installation_id": "…"
    },
    "presence": {
      "status": "online",
      "since": 0,
      "activities": [],
      "afk": false
    },
    "compress": false,
    "client_state": {
      "guild_versions": {},
      "highest_last_message_id": "0",
      "read_state_version": 0,
      "user_guild_settings_version": -1,
      "user_settings_version": -1,
      "private_channels_version": 0,
      "api_code_version": 0,
      "initial_guild_id": null
    },
    "qos_token": "<base64 protobuf>"
  }
}
```

Notes:

- `client_state` is only fully populated when the client has guild-version
  caching enabled (`canUseGuildVersions()`). A fresh login sends
  `guild_versions: {}` and the rest of the fields as above.
- `initial_guild_id` is included only when the client is opening a specific
  guild.
- `private_channels_version` is absent in orbisRPC today.
- `qos_token` is omitted entirely if encoding fails, but that is a bug path, not
  a normal state.

#### Capabilities

| Constant | Value | Bits set |
|---|---|---|
| orbisRPC `DISCORD_GW_CAPABILITIES` | `16125` = `0x3EFD` | 0,2,3,4,5,6,7,9,10,11,12,13 |
| Official, obfuscation off | `1734653` = `0x1A77FD` | 0,2,3,4,5,6,7,8,9,10,12,13,14,17,19,20 |
| Official, obfuscation on | `1767421` = `0x1AF7FD` | adds bit 15 |

Differences:

- orbisRPC is missing bits **8, 14, 17, 19, 20**.
- orbisRPC has bit **11**, which neither official value has.

Bit 8 is treated in this codebase as `AUTH_TOKEN_REFRESH`. That name is not
visible in the web bundle (the enum is stripped in production), so treat the
label as the project's own. What is certain is that the official desktop client
sets bit 8 and orbisRPC deliberately clears it.

Removing bit 8 does not prevent rotation; it only prevents the client from
*asking* for it. If Discord decides to rotate, it will. The correct response to
rotation is to persist the new token, not to opt out of the feature. Opting out
while still presenting the token is the worst of both worlds: the server sees a
client that claims not to support refresh, from an IP and fingerprint that do not
match the token's owner.

Bits 14, 17, 19 and 20 are undocumented in the artifact. The safe move is to
send the exact official value and not reason about individual bits.

### 5.7 READY

READY fields the official client consumes (offset **5 304 201**):

| Field | Use |
|---|---|
| `session_id` | resume handle |
| `resume_gateway_url` | base URL for resume |
| `auth_session_id_hash` | session identity, used by the sessions UI |
| `static_client_session_id` | stable per-install session identity |
| `auth_token` | **rotated token, must be persisted** |
| `qos_token` | derived QoS block, must be persisted |
| `user` | own user object |
| `users`, `guilds`, `private_channels` | initial state |
| `sessions` | other active sessions |
| `auth` | auth metadata |
| `required_action` | e.g. email verification |
| `analytics_token` | analytics |
| `experiments`, `guild_experiments`, `apex_experiments` | experiment buckets |
| `consents`, `country_code`, `notification_settings` | misc state |
| `read_state`, `user_guild_settings`, `user_settings_proto` | settings |
| `api_code_version` | protocol feature gate |

`READY_SUPPLEMENTAL` follows and completes the payload. The session is only
considered fully established after READY; the client sets
`connectionState = SESSION_ESTABLISHED`, stores `session_id`, and calls
`setResumeUrl(resume_gateway_url)`.

`RESUMED` (dispatch) means the session was replayed. The client resets its
backoff and keeps the existing `session_id`.

### 5.8 RESUME

Payload (op 6):

```json
{ "op": 6, "d": { "token": "<token>", "session_id": "<id>", "seq": 42 } }
```

Sent with `shouldQueue = false`. The official client only attempts a resume if:

- `session_id` is non-null, and
- the last heartbeat ack is less than **3 minutes** old (or never happened).

If either condition fails, it sends a fresh IDENTIFY instead. This is a
correctness detail: resuming a session whose token has been rotated will fail
with `INVALID_SESSION` and waste a round trip.

`INVALID_SESSION` (op 9) carries a boolean `d`. `true` means the session may
still be resumable; `false` means it is dead. The client:

```js
_handleInvalidSession(d) {
  d ? this._doResumeOrIdentify() : this._doIdentify();
}
```

orbisRPC treats every `INVALID_SESSION` as fatal and clears the session. That is
safe but loses a cheap recovery path when `d === true`.

### 5.9 Close codes

Gateway close codes that matter to a user-token client:

| Code | Meaning | Correct action |
|---|---|---|
| 1000 | normal closure | reconnect if not a user-initiated close |
| 1001 | going away | reconnect |
| 1006 | abnormal | reconnect with backoff |
| 4000 | unknown error | reconnect |
| 4001 | unknown opcode | reconnect, fix payload |
| 4002 | decode error | reconnect, fix payload |
| 4003 | not authenticated | re-identify |
| 4004 | authentication failed | **do not retry**, token is dead |
| 4005 | already authenticated | reconnect, do not re-identify |
| 4006 | session no longer valid | re-identify |
| 4007 | invalid seq | re-identify |
| 4008 | rate limited | backoff, honour `retry_after` |
| 4009 | session timed out | re-identify |
| 4010 | invalid shard | n/a for user clients |
| 4011 | sharding required | n/a for user clients |
| 4012 | invalid API version | fix `v` |
| 4013 | invalid intent | n/a for user clients |
| 4014 | disallowed intent | n/a |

The official client hard-codes only `4004` as terminal. Everything else goes
through the backoff/reconnect path. orbisRPC additionally treats 4003, 4007,
4009 and 1000/1001 as session-forget, which is reasonable.

`4011` is about sharding, not about the session. Do not confuse it with the RPC
close code table below, where `4004` means invalid version and `4003` means
token revoked.

### 5.10 Local RPC close codes

For completeness, the RPC socket enum (offset **12 312 000**) is separate:

```
1000 CLOSE_NORMAL
1003 CLOSE_UNSUPPORTED
1006 CLOSE_ABNORMAL
4000 INVALID_CLIENTID
4001 INVALID_ORIGIN
4002 RATELIMITED
4003 TOKEN_REVOKED
4004 INVALID_VERSION
4005 INVALID_ENCODING
```

These apply to the desktop IPC/WebSocket server, not the Discord gateway.

---

## 6. Presence

### 6.1 Presence object

`getLocalPresence()` at offset **9 770 930** returns exactly four fields:

```json
{ "status": "online", "since": 0, "activities": [], "afk": false }
```

- `status`: `online` | `idle` | `dnd` | `invisible` (never send `offline`).
- `since`: epoch milliseconds when the user went idle or AFK, otherwise `0`.
- `activities`: filtered activity list.
- `afk`: boolean.

`status` is derived:

```js
if (isAFK) status = idle;
else if (invisible) status = INVISIBLE;
else status = configuredStatus;
if (status === "online" && idleSince > 0) status = "idle";
```

The client sends presence with `op 3`:

```js
presenceUpdate(status, since, activities, afk) {
  this.send(PRESENCE_UPDATE, { status, since, activities, afk });
}
```

`since` is only non-zero for idle/AFK transitions. A presence daemon that always
sends `since: 0` is fine; one that sends a growing timestamp on every update is
not.

### 6.2 Activity schema

From the RPC `SET_ACTIVITY` validator (offset **1 946 152**). This is the
authoritative shape the client accepts, and the same shape is placed into
presence.

```json
{
  "name": "string, 1..128, required",
  "type": 0,
  "url": "https://…",
  "created_at": 0,
  "timestamps": { "start": 0, "end": 0 },
  "application_id": "snowflake",
  "details": "string, 2..128",
  "details_url": "https://…",
  "state": "string, 2..128",
  "state_url": "https://…",
  "emoji": { "name": "…", "id": "…", "animated": false },
  "party": {
    "id": "string, 2..128",
    "size": [1, 5],
    "privacy": 0
  },
  "assets": {
    "large_image": "string, 1..300",
    "large_text": "string, 2..128",
    "large_url": "https://…",
    "small_image": "string, 1..300",
    "small_text": "string, 2..128",
    "small_url": "https://…",
    "invite_cover_image": "string, 1..300"
  },
  "secrets": {
    "match": "string, 2..128",
    "join": "string, 2..128",
    "spectate": "string, 2..128"
  },
  "instance": false,
  "supported_platforms": ["desktop"],
  "buttons": [{ "label": "1..32", "url": "https://… 1..512" }],
  "status_display_type": 0,
  "flags": 0,
  "platform": "desktop",
  "metadata": {}
}
```

Rules enforced by the client before it forwards an activity:

- `buttons` and `secrets` are mutually exclusive.
- `secrets` values must be unique.
- A secret may not equal the party id.
- `buttons` max 2, `secrets` max 3.
- Timestamps may be seconds or milliseconds. The normaliser converts to
  milliseconds if the value is a 10-digit epoch (offset **1 993 400**):
  if `Date.now().toString().length - value.toString().length > 2`, multiply by 1000.
- `party.privacy` is stripped from the gateway activity and folded into flags.
- `instance` is stripped from the gateway activity and folded into flags.
- `platform` becomes `application_id`-scoped metadata, not a top-level gateway field.

### 6.3 Activity flags

Confirmed enum (offset **318 600**):

| Flag | Value |
|---|---|
| `INSTANCE` | 1 |
| `JOIN` | 2 |
| `SYNC` | 16 |
| `PLAY` | 32 |
| `PARTY_PRIVACY_FRIENDS` | 64 |
| `PARTY_PRIVACY_VOICE_CHANNEL` | 128 |
| `EMBEDDED` | 256 |
| `CONTEXTLESS` | 512 |
| `SUPPORTS_REMOTE_ACTIVITY_ACTION_JOIN` | 1024 |
| `SUPPORTS_JOIN_URL` | 2048 |

`SPECTATE` (4) and `JOIN_REQUEST` (8) exist in the protocol but are absent from
this generated enum. Both are still valid bits.

Flag computation (`d(e, instance, embedded, contextless, privacy)` at offset
**2 717 805**):

```js
flags = 0;
if (instance) flags |= INSTANCE;
if (secrets?.join != null) flags |= JOIN;
if (contextless) flags |= CONTEXTLESS;
if (embedded) { flags |= EMBEDDED; flags |= privacyFlags(); }
else if (privacy === PUBLIC || ACTIVITIES_FORCE_PUBLIC) flags |= privacyFlags();
```

`privacyFlags()` adds `PARTY_PRIVACY_FRIENDS` and `PARTY_PRIVACY_VOICE_CHANNEL`
according to user settings.

For orbisRPC, a plain "playing a game" presence should have:

- `type: 0`
- `flags: 0` (no secrets, no party)
- `instance: false`
- `application_id` set to the game's detected Discord application id
- `assets.large_image` set to the uploaded asset key or an external URL
- `timestamps.start` set to the session start in milliseconds

Setting `INSTANCE` or `JOIN` when no join secret exists is a mismatch that a
client would never produce.

### 6.4 Presence lifecycle

From `SelfPresenceStore` (offset **9 770 930**):

- Presence is recomputed from settings, activity store, AFK state, and idle time.
- On `CONNECTION_OPEN` and `CONNECTION_OPEN_SUPPLEMENTAL` the client re-emits
  the full presence.
- On `IDLE`, `AFK`, `RUNNING_GAMES_CHANGE`, `STREAMING_UPDATE`,
  `USER_SETTINGS_PROTO_UPDATE` it recomputes.
- Updates are coalesced: `didCommit` allows at most 5 commits per 20 seconds and
  schedules the rest. This matters. A daemon that sends an op 3 every second is
  visibly different from the official client.

orbisRPC currently sends op 3 on every presence change without coalescing. For a
game-presence daemon that changes rarely, this is acceptable. For anything that
polls, add the 20-second / 5-update budget.

---

## 7. REST surface

### 7.1 Base URL and version

`API_ENDPOINT = "//discord.com/api"`, `API_VERSION = 9`. So every path is
`https://discord.com/api/v9/...`.

### 7.2 Headers

Applied to every relative request (offset **1 015 765**):

| Header | Value | When |
|---|---|---|
| `Authorization` | raw token, no scheme | when a token exists |
| `X-Super-Properties` | base64 JSON | always |
| `X-Fingerprint` | server-issued fingerprint | when non-empty |
| `X-Installation-ID` | installation id | when non-empty |
| `X-Discord-Locale` | e.g. `en-US` | always |
| `X-Discord-Timezone` | IANA zone, e.g. `Europe/London` | when known |
| `Accept-Language` | weighted list | embedded only |
| `X-Debug-Options` | debug flags | optional |
| `X-Routing-Key` | routing hint | optional |
| `x-client-trace-id` | snowflake-ish trace id | when tracing enabled |
| `Content-Type` | `application/json` | bodies |

Note `Authorization` is the raw token. No `Bearer`, no `Bot`.

### 7.3 Endpoints a presence client actually needs

| Purpose | Method | Path |
|---|---|---|
| Validate token | `GET` | `/users/@me` |
| Read settings | `GET` | `/users/@me/settings` |
| Read guilds | `GET` | `/users/@me/guilds` |
| Read read-states | `GET` | `/read-states/ack-bulk` |
| List sessions | `GET` | `/auth/sessions` |
| Logout one session | `POST` | `/auth/sessions/logout` |
| Login | `POST` | `/auth/login` |
| MFA | `POST` | `/auth/mfa/{type}` |
| Experiments | `GET` | `/experiments?with_guild_experiments=true` |

`/users/@me` is the canonical token liveness check. A `401` there means the
token is dead, full stop. Use it before declaring a 4004 permanent.

### 7.4 Rate limits

Standard Discord buckets apply. The client respects `X-RateLimit-*` and
`retry_after`. A presence daemon should make almost no REST calls, so rate
limiting is only a risk during startup.

---

## 8. Local RPC protocol (reference)

orbisRPC does not use this path, but a "native Discord client" does, and the
schema is the best documentation of what an activity is allowed to contain.

### 8.1 Transports

`IPC`, `WEBSOCKET`, `POST_MESSAGE` (`kE` at offset **1 992 311**).

### 8.2 Frame opcodes

| Op | Name |
|---|---|
| 0 | `HANDSHAKE` |
| 1 | `FRAME` |
| 2 | `CLOSE` |
| 3 | `PING` |
| 4 | `PONG` |

Frame body: `{ cmd, nonce, evt, data, args }`.

### 8.3 Handshake

```json
{ "v": 1, "client_id": "<application id>" }
```

Server replies with `READY`, containing `v`, `config`, `user`, `application`.

### 8.4 Scopes

`identify`, `email`, `connections`, `guilds`, `guilds.join`,
`guilds.members.read`, `guilds.channels.read`, `gdm.join`, `bot`, `rpc`,
`rpc.notifications.read`, `rpc.voice.read`, `rpc.voice.write`,
`rpc.video.read`, `rpc.video.write`, `rpc.screenshare.read`,
`rpc.screenshare.write`, `rpc.activities.write`, `webhook.incoming`,
`messages.read`, `applications.builds.upload`, `applications.builds.read`,
`applications.commands`, `applications.commands.permissions.update`,
`applications.commands.update`, `applications.store.update`, `activities.read`,
`activities.write`, `relationships.read`, `voice`, `dm_channels.read`.

The ones that matter for activity presence are `rpc` and
`rpc.activities.write`.

### 8.5 AUTHENTICATE and AUTHORIZE

`AUTHENTICATE` takes an OAuth2 access token and validates it against
`GET /oauth2/@me` with `Authorization: Bearer <token>`.

`AUTHORIZE` runs `POST /oauth2/authorize` with `response_type: "token"` and
returns a `code`. A native client that owns the account can skip this entirely
and use the user token.

### 8.6 Errors

`1000 UNKNOWN_ERROR`, `1001 SERVICE_UNAVAILABLE`, `1002 TRANSACTION_ABORTED`,
`4000 INVALID_PAYLOAD`, `4002 INVALID_COMMAND`, `4003 INVALID_GUILD`,
`4004 INVALID_EVENT`, `4005 INVALID_CHANNEL`, `4006 INVALID_PERMISSIONS`,
`4007 INVALID_CLIENTID`, `4008 INVALID_ORIGIN`, `4009 INVALID_TOKEN`,
`4010 INVALID_USER`, `4011 INVALID_INVITE`, `4012 INVALID_ACTIVITY_JOIN_REQUEST`,
`4015 INVALID_ENTITLEMENT`, `4016 INVALID_GIFT_CODE`, `4017 INVALID_GUILD_TEMPLATE`,
`4018 INVALID_SOUND`, `4019 INVALID_PROVIDER`, `4020 INVALID_CONNECTION_CALLBACK_STATE`,
`4021 BAD_REQUEST_FOR_PROVIDER`, `5000 OAUTH2_ERROR`, `5001 SELECT_CHANNEL_TIMED_OUT`,
`5002 GET_GUILD_TIMED_OUT`, `5003 SELECT_VOICE_FORCE_REQUIRED`,
`5005 INVALID_ACTIVITY_SECRET`, `5006 NO_ELIGIBLE_ACTIVITY`, `5008 PURCHASE_CANCELED`,
`5009 PURCHASE_ERROR`, `5010 UNAUTHORIZED_FOR_ACHIEVEMENT`, `5011 RATE_LIMITED`,
`5012 UNAUTHORIZED_FOR_APPLICATION`, `5013 NO_CONNECTION_FOUND`.

---

## 9. orbisRPC gap analysis

Source of truth: `orbisrpc/discord.c`, `orbisrpc/discord.h`.

| # | Area | Official | orbisRPC | Severity |
|---|---|---|---|---|
| 1 | `READY.auth_token` | persisted, used for next connect | ignored | **critical** |
| 2 | `READY.qos_token` | persisted, echoed in IDENTIFY | absent | high |
| 3 | IDENTIFY `qos_token` | base64 protobuf | absent | high |
| 4 | capabilities | `1734653` / `1767421` | `16125` | high |
| 5 | `client_app_state` | present | absent | medium |
| 6 | `is_fast_connect` | `false` | absent | medium |
| 7 | `gateway_connect_reasons` | `"COLD_START"` | absent | medium |
| 8 | `installation_id` in properties | present | absent | medium |
| 9 | `client_state.private_channels_version` | present | absent | low |
| 10 | Gateway `v` | 9 | 10 | low |
| 11 | `compress` query | `zstd-stream` / `zlib-stream` | none | low |
| 12 | Heartbeat `d` shape | `{seq,qos}` | bare `seq` or `null` | medium |
| 13 | Heartbeat scheduling | random jitter, one miss = dead | fixed early, two misses = dead | medium |
| 14 | `INVALID_SESSION` `d=true` | resume | re-identify | low |
| 15 | Presence `since` | 0 unless idle | 0 | ok |
| 16 | Presence coalescing | 5 per 20 s | none | low |
| 17 | Activity `flags` | computed | 0 always | low |
| 18 | Activity `instance` | false | absent | low |
| 19 | Activity `platform` | `desktop` | absent | low |
| 20 | Super properties set | 20+ keys | 15 keys | medium |
| 21 | `client_launch_id` stability | per launch | per call? | verify |
| 22 | REST `X-Fingerprint` | server-issued | absent | medium |
| 23 | REST `X-Discord-Timezone` | present | absent | low |
| 24 | Resume heuristic | 3-minute ack window | none | low |
| 25 | Close-code handling | only 4004 terminal | more codes terminal | ok |

### 9.1 What actually causes logouts

Ranked by likelihood:

**1. Token rotation ignored.** Highest confidence. The server hands back a new
credential, the official client stores it, orbisRPC does not. Once rotation
happens, orbisRPC holds a dead token and the PC holds a live one until *its* next
rotation or reconnect. If the PC was not running, it holds the dead one too.

**2. qos_token / auth_token_hash mismatch.** The derived QoS block ties the
session to a token hash. A client that presents a token but no QoS block, or a
stale block, cannot prove it is the session that received the block. This is a
plausible trigger for server-side anomaly handling.

**3. Same token, two simultaneous clients, different fingerprints.** Both
clients create sessions with the same credential. Discord's session model
supports multiple sessions per account, but when the same token is used from two
very different device fingerprints the account looks shared. The client's own
`MULTI_ACCOUNT_SWITCH_TOKEN_COLLISION` telemetry shows it treats token/account
collisions as abnormal.

**4. Capability mismatch.** Asking for a non-standard capability set while
omitting refresh support is a weak but nonzero signal. It is cheap to fix.

**5. Long-lived session without REST activity.** A client connected for hours
that never fetches anything is unusual. Low impact, but free to fix.

**6. Missing `installation_id` / `fingerprint`.** The REST layer sees requests
without the identifiers every other request carries.

### 9.2 What cannot be fixed client-side

- TLS fingerprint (JA3/JA4) differs from Electron's BoringSSL.
- TCP/IP stack and MTU behaviour differ.
- Clock skew, locale, and timezone may not match the account's history.
- IP address and ASN are the console's, not the PC's.
- The PS4 has no camera, microphone, or audio devices, so voice state and
  device enumeration cannot be faked convincingly if ever queried.

No amount of payload fidelity makes a session undetectable. The goal is
consistency: every field a server can cross-check should agree with every other
field.

---

## 10. Implementation plan

Ordered by risk reduction per line of code.

### Phase 0: stop the bleeding (smallest possible diff)

1. **Persist `READY.auth_token`.** On READY, if `d.auth_token` is a non-empty
   string, write it to the config store and use it for the next connect and
   resume. Log at INFO when it changes, without logging the value.
2. **Echo `READY.qos_token`.** Store it as an opaque string, send it back in
   IDENTIFY as `qos_token`. If absent, omit the field.
3. **Match capabilities.** Replace `16125` with `1734653`. Delete the
   `AUTH_TOKEN_REFRESH` opt-out comment and the `_Static_assert`.
4. **Match gateway version.** `v=10` to `v=9`.
5. **Add the missing IDENTIFY properties.** `client_app_state`,
   `is_fast_connect: false`, `gateway_connect_reasons`, `installation_id`.

Expected effect: rotation survives a reconnect, and the IDENTIFY payload is
byte-comparable to the official one apart from values that legitimately vary.

### Phase 1: session lifecycle correctness

6. Send heartbeats as `{seq, qos}` when QoS data exists, bare `seq` otherwise.
7. Randomise the first heartbeat inside the interval.
8. Treat one missed ack as dead (official) or keep two (safer). Document the
   choice.
9. Handle `INVALID_SESSION` with `d === true` by attempting a resume.
10. Only resume when the last ack is under 3 minutes old.
11. Persist `session_id` and `resume_gateway_url` across process restarts.

### Phase 2: presence fidelity

12. Compute activity flags correctly: 0 for a plain game, `INSTANCE` only when
    an instance id exists, `JOIN` only when a join secret exists.
13. Always send `instance: false` and `platform: "desktop"`.
14. Coalesce presence updates to at most 5 per 20 seconds.
15. Send `since: 0` unless the daemon actually knows the idle transition time.
16. Set `timestamps.start` in milliseconds.

### Phase 3: native login

17. Add a login flow using `/auth/login` and `/auth/mfa/{type}`, with the ticket
    and `login_instance_id` echoed between calls.
18. Store the resulting token with the same rotation logic as Phase 0.
19. Generate and persist an installation id and client launch id.
20. Fetch `/users/@me` on startup and after every reconnect. A 401 means dead
    token, and that is the only signal worth treating as terminal besides 4004.

This is the only phase that removes the structural fragility. Everything else
makes the borrowed-token path more robust, not correct.

### Phase 4: hardening

21. Implement a token-state machine:
    `valid -> rotated -> persisted -> in-use` with an explicit generation
    counter, so a late-arriving rotation cannot overwrite a newer one.
22. Bound the reconnect backoff at 60 s, reset after a successful READY.
23. Add structured logging of gateway state transitions with timestamps, without
    tokens.
24. Add a self-check that refuses to start if the token is empty, malformed, or
    older than the last known rotation.

### Data structures

```c
typedef struct {
    char token[512];            /* current credential */
    unsigned token_gen;         /* increments on every rotation */
    char qos_token[2048];       /* opaque base64 from READY */
    char session_id[64];
    char resume_url[192];
    char installation_id[64];
    char launch_id[64];
    int64_t last_ack_ms;
    int seq;
    int state;                  /* CLOSED/CONNECTING/IDENTIFYING/READY */
} gw_state_t;
```

Rules:

- `token_gen` is bumped only by a READY `auth_token`, never by a config reload.
- `qos_token` is replaced only by a READY `qos_token`.
- `session_id` is cleared on `INVALID_SESSION` with `d == false` and on 4004.
- `resume_url` persists across reconnects, clears on 4004.

### State machine

```
CLOSED
  -> CONNECTING           (ws connect)
  -> IDENTIFYING          (op 2 sent)
  -> READY                (READY dispatch)
  -> RESUMING             (op 6 sent, on reconnect)
  -> READY                (RESUMED or READY)
  -> WILL_RECONNECT       (close != 4004)
  -> CLOSED               (close == 4004)
```

Transitions that mutate credentials must be serialised with the socket thread.
A rotation arriving while a reconnect is in flight must apply to the *next*
IDENTIFY, not the in-flight one.

---

## 11. Detection surface: honest limits

What Discord can see, and whether orbisRPC can match it:

| Signal | Matchable? | Notes |
|---|---|---|
| IDENTIFY payload | yes | after Phase 0 |
| Capabilities | yes | exact integer |
| Super properties | mostly | UA and Electron version are the hard part |
| qos_token | yes | echo verbatim |
| Heartbeat cadence | yes | needs jitter |
| Presence schema | yes | after Phase 2 |
| REST headers | yes | needs fingerprint and timezone |
| TLS fingerprint | no | BoringSSL vs mbedTLS |
| IP / ASN | no | console network |
| OS-level device list | no | no audio/video devices |
| Timing of UI actions | partially | a daemon has no UI |
| Session count per token | no | depends on user behaviour |

The realistic target is "indistinguishable at the payload layer, plausibly
distinct at the transport layer". Claiming more than that would be dishonest.

---

## 12. Test plan

### Unit

- `build_identify()` produces the exact key set, including `qos_token` only when
  present.
- `build_super_properties()` base64-decodes to valid JSON with the required keys.
- Capability constant equals `1734653`.
- Activity JSON for a plain game has `flags: 0`, `instance: false`,
  `platform: "desktop"`.
- Timestamp normalisation: seconds become milliseconds, milliseconds unchanged.

### Integration (offline fixture)

- Replay a captured READY with `auth_token` and assert the store is updated and
  the next IDENTIFY uses the new value.
- Replay READY with `qos_token` and assert it is echoed byte-for-byte.
- Replay `INVALID_SESSION` with `d: true` and assert a resume is attempted.
- Replay `INVALID_SESSION` with `d: false` and assert a fresh identify.

### Live

- Connect with a throwaway account, hold for 24 hours, log every close code and
  every `auth_token` change. Success: zero unexpected 4004 after a rotation.
- Run PC client and console client concurrently for 24 hours. Success: neither
  logs out, and both token stores converge.
- Kill the network for 90 seconds, restore, assert resume succeeds.
- Kill the network for 10 minutes, restore, assert resume falls back to identify
  and succeeds.

### Regression

- The existing `make -C tests test` and `make -C tests asan` suites must pass.
- No token material in logs at any level.

---

## 13. Open questions

1. Does Discord rotate `auth_token` on a schedule, on anomaly, or both? Client
   evidence cannot answer this.
2. Is bit 8 genuinely `AUTH_TOKEN_REFRESH`? The label is inherited from the
   project. The official client sets it; that much is certain.
3. Does presenting a stale `qos_token` cause a soft failure or a hard one? Needs
   a live test.
4. Does Discord bind a token to a fingerprint or installation id? If yes, a
   freshly generated installation id on the console makes the token look stolen.
   If no, generating one is free fidelity.
5. What is the real expiry of `derived.expires_at`? If it is short, a long-lived
   session must refresh it through a new READY, which means periodic
   re-identification.
6. Does the sessions list expose `client_info` that the console cannot fake?
   `READY.sessions` would show it.

---

## Appendix A: constants

```
GATEWAY_ENDPOINT      wss://gateway.discord.gg
API_VERSION           9
BUILD_NUMBER          663810
CLIENT_VERSION        0.0.421
VERSION_HASH          164cf72e97d1e6032c69c2e71b81adc93ee086d4
CAPABILITIES          1734653  (0x1A77FD)
CAPABILITIES_OBF      1767421  (0x1AF7FD)
ORBISRPC_CAPS         16125    (0x3EFD)
HEARTBEAT_JITTER      uniform(0, interval)
HELLO_TIMEOUT_MS      30000
RESUME_MAX_ACK_AGE    180000
BACKOFF_MIN_MS        1000
BACKOFF_MAX_MS        60000
TOKEN_KEY             "token"
TOKENS_KEY            "tokens"
TOKEN_ENC_PREFIX      "dQw4w9WgXcQ:"
FINGERPRINT_KEY       "fingerprint"
INSTALLATION_KEY      "installation_id_v3"
USER_ID_CACHE_KEY     "user_id_cache"
QOS_STORE_KEY         "DerivedQosDataStore"
```

## Appendix B: protobuf wire details

`QosToken` (offset 5 383 250):

| Message | Field | Number | Wire type |
|---|---|---|---|
| `QosToken` | `client_provided` | 1 | 2 (length-delimited) |
| `QosToken` | `derived` | 2 | 2 |
| `ClientProvidedQosData` | `is_active` | 1 | 0 (varint) |
| `DerivedQosData` | `claims` | 1 | 2 |
| `DerivedQosData` | `signature` | 2 | 2 |
| `DerivedQosData` | `key_id` | 3 | 0 |
| `Claims` | `user_id` | 1 | 1 (fixed64) |
| `Claims` | `issued_at` | 2 | 1 |
| `Claims` | `is_staff` | 3 | 0 |
| `Claims` | `auth_token_hash` | 4 | 2 |
| `Claims` | `expires_at` | 5 | 1 |

`Claims.user_id`, `issued_at` and `expires_at` are strings on the JS side
because they exceed 53-bit integer precision. In C use `uint64_t` and encode as
fixed64.

## Appendix C: extracted offsets

| Offset | Content |
|---|---|
| 318 600 | activity flag enum |
| 1 015 765 | REST request decorator |
| 733 710 | `PATCH /users/@me` token update |
| 1 946 152 | `SET_ACTIVITY` schema |
| 1 992 311 | `SET_ACTIVITY` handler, transports, flags |
| 2 717 805 | activity flag computation |
| 2 885 362 | `client_app_state` / heartbeat session super props |
| 3 022 120 | module 183636, `client_app_state` values |
| 5 291 284 | `handleIdentify`, properties extension |
| 5 304 201 | READY handler |
| 5 305 357 | `UPDATE_TOKEN` dispatch |
| 5 383 250 | QosToken protobuf |
| 5 388 697 | DerivedQosDataStore |
| 5 390 422 | `_connect`, `_doIdentify`, heartbeat |
| 5 341 400 | zlib-stream handler |
| 5 361 403 | ETF codec |
| 5 361 648 | JSON codec |
| 9 413 363 | fingerprint / installation id store |
| 9 416 650 | AuthenticationStore |
| 9 770 930 | SelfPresenceStore |
| 10 435 049 | super properties base64 encoder |
| 10 439 200 | desktop super properties builder |
| 10 444 600 | `getSuperProperties` / `extendSuperProperties` |
| 12 312 000 | RPC close code enum |
| 12 361 801 | token store module 247775 |

## Appendix D: what changed between this build and older clients

Not verified. The bundle is build 663810, dated 2026-11-04. Older public
descriptions of the gateway (v6/v8, `compress: true`, JSON only) do not apply.
In particular:

- `compress` in IDENTIFY is now `false` for both zstd and zlib transports.
- ETF is the desktop default, JSON the web default.
- QoS heartbeat payloads are objects, not bare sequence numbers.
- `qos_token` exists and is protobuf, not JSON.

Any implementation based on a pre-2024 guide will be subtly wrong.

---

## Appendix E: minimal correct IDENTIFY for orbisRPC

For a presence-only daemon on a shared token, the smallest payload that matches
the official client is:

```json
{
  "op": 2,
  "d": {
    "token": "<token>",
    "capabilities": 1734653,
    "properties": {
      "os": "Windows",
      "browser": "Discord Client",
      "device": "",
      "system_locale": "en-US",
      "client_version": "0.0.421",
      "os_version": "10.0.22631",
      "os_arch": "x64",
      "app_arch": "x64",
      "has_client_mods": false,
      "client_launch_id": "<per-launch uuid>",
      "release_channel": "stable",
      "browser_user_agent": "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) discord/0.0.421 Chrome/<chromium-major>.0.0.0 Electron/<electron-major>.<minor>.<patch> Safari/537.36",
      "browser_version": "<electron-major>.<minor>.<patch>",
      "os_sdk_version": "10",
      "client_event_source": null,
      "client_build_number": 663810,
      "native_build_number": 0,
      "client_app_state": "unfocused",
      "is_fast_connect": false,
      "gateway_connect_reasons": "COLD_START",
      "installation_id": "<persisted uuid>"
    },
    "presence": { "status": "online", "since": 0, "activities": [], "afk": false },
    "compress": false,
    "client_state": {
      "guild_versions": {},
      "highest_last_message_id": "0",
      "read_state_version": 0,
      "user_guild_settings_version": -1,
      "user_settings_version": -1,
      "private_channels_version": 0,
      "api_code_version": 0
    },
    "qos_token": "<echoed READY.qos_token, omit if none>"
  }
}
```

The UA and Electron version must be internally consistent with
`client_version` and `client_build_number`. The angle-bracket values above are
placeholders: read the real Chromium and Electron versions from the
`browser_user_agent` and `browser_version` the target desktop release actually
sends, then hard-code that tuple. Do not invent versions. A UA claiming Electron 32
while sending `client_build_number` from a different release is a mismatch that
no real client produces.

---

End of document.
