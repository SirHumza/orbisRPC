# TLS + IME + presence-display findings (hardware-verified)

## Presence display rules (verified 2026-09-23 via live holds + human eyes)

- Game activity with **external-URL art or dangling asset key**: Discord
  drops the ENTIRE activity (name, timer, everything). No error anywhere.
- Game activity with **live app id, no art**: renders fully
  (name/details/state/timer).
- Game activity with **live app id + uploaded asset key**: renders; tile
  shows "?" until the asset propagates/caches clear (can take a restart).
- Bare name activity: renders.
- Custom status (type 4): renders.
- `since: null` + integer-ms `start` required (0 wedges timer at 0:00).
- Asset uploads ARE possible via API: stage file through
  `POST /channels/{id}/attachments`, PUT bytes to `upload_url`, then
  `POST /applications/{app}/assets` with `key` + `upload_filename`.
  New assets report `"visibility": "private"`; display still pending
  verification after propagation.
- Lanyard is BLIND to user-token gateway activities (shows [] even for
  live-rendered ones). Human eyes only. Do not verify with it.

Symptom: every handshake died instantly with return `-1` (not a real
mbedTLS code), after DNS + TCP succeeded.

Root cause (from mbedTLS debug trace on-console):

```text
psa_crypto_init() returned -148 (-0x0094, INSUFFICIENT_ENTROPY)
```

mbedTLS 3.x routes TLS 1.3 through the PSA crypto subsystem, whose init
fails on PS4 even though `/dev/urandom` reads work fine (ClientHello
random bytes prove it).

Fix (in tree, verified: full handshake, `TLS-ECDHE-ECDSA-WITH-CHACHA20-
POLY1305-SHA256`, cert chain verifies, connection test PASSES):

```c
mbedtls_ssl_conf_max_tls_version(&t->conf, MBEDTLS_SSL_VERSION_TLS1_2);
```

Lesson: wire mbedTLS's debug layer (`mbedtls_debug_set_threshold` +
`mbedtls_ssl_conf_dbg`) into the log before guessing at handshake bugs.

## IME keyboard: init refuses (OPEN)

`sceImeDialogInit` fails on-console; codes seen:

- `-2135162872` (`0x80BC0008`) with `userId=0`
- `-2135162864` (`0x80BC0010`) with real foreground user ID

Tried, none fixed it: real user ID via UserService, centered position
(960,540), default type/label, per-dialog init/terminate lifecycle,
stale-session teardown. Reference: Apollo PS4 dialog.c pattern mirrored.
Workaround in place: token pre-seeded via config; IME still unresolved.

## App lifecycle (fixed, verified)

- Missing `sceMsgDialogInitialize` → every open failed into a black loop.
  Fixed per Apollo pattern (init + terminate per dialog).
- 30s dialog watchdog blanked idle screens — removed, blocking waits now.
- Exit path terminates dialogs, unloads modules, `exit(0)`.
