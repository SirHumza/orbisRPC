# Improvement log

Working record of the 1.1 audit loop. One entry per batch of related
findings. "Host-tested" means the three gates below; nothing here is a
console claim unless it says so explicitly.

## Gate commands

```sh
make -C tests test
make -C tests asan
ORBISRPC_STRICT_SECRETS=1 python3 tests/e2e_consumer.py
```

Baseline and current results are recorded at the bottom of this file.

---

## Iteration 1 — 2026-10-28 — baseline

- Read the plan at `.opencode/plan/2026-10-27-orbisrpc-1.1.md` (rev 6),
  `orbisrpc/daemon.c`, `cfg.c`, `health.c`, `gamecache.c`, `ws.c`,
  `tests/test_utils.c`, `tests/e2e_consumer.py`, and the `docs/` set.
- Captured the baseline: all three gates green before any change.
- No code change in this iteration. Purpose was to separate real defects
  from plan assumptions (for example, timestamps and media typing already
  existed in the builder).

## Iteration 2 — 2026-10-28 — data safety and recovery

Files: `health.c`, `cfg.c`, `cfg.h`, `tests/test_utils.c`.

Defects found:

1. **Rollback trusted an unverified backup.** `health_rollback()` renamed
   `*.bak` over the live binary without checking that the backup was a
   loadable image. A truncated `.bak` turned a recoverable update failure
   into a dead payload. Root cause: the stage path validated `.new`, the
   rollback path did not.
2. **Config file was world-readable.** `/data/orbisRPC/config.json` holds a
   full-access Discord token but was created with the process umask
   (typically `0644`). Root cause: no explicit mode on load or save.
3. **Every save deleted unknown keys.** `cfg_save()` rebuilt the document
   from a blank object, so a key written by a newer build, or by hand, was
   silently removed. Root cause: save serialised only `cfg_t`.
4. **Duplicate keys on rewrite.** `jl_obj_set()` always appends; rebuilding
   without replacing would leave two `token` pairs with the on-disk order
   deciding which one wins.
5. **Stale session resurrected an elapsed timer.** `session.json` was
   accepted on epoch sanity alone, so a file from an overnight shutdown
   made the timer count hours the console was off. (Removed entirely in
   1.1: `started` is process-local, no session state survives a restart.)

Changes:

- `health.c`: `.bak` must pass `health_check_binary()` before the rename;
  the live file is untouched when it does not.
- `cfg.c`: `chmod(path, 0600)` after load, `fchmod(fileno(f), 0600)` in
  `cfg_save()`. Save now starts from `cfg_read_root(path)` and replaces
  known keys with `cfg_set()` (in place, no duplicate), preserving unknown
  top-level keys.
- `sched.h` (new): `sched_session_fresh()` requires a save no older than
  30 minutes, tolerates small future skew, and rejects an impossible
  `started > saved_at`.
- `daemon.c`: session restore calls the predicate; the log line stays
  `session file failed validation` (the e2e gate matches that text).

Tests added: `test_health_stage_activate` rollback case (RED first: the
corrupt `.bak` was renamed into place), `test_cfg_permissions`,
`test_cfg_preserves_unknown`, `test_sched`.

## Iteration 3 — 2026-10-28 — scheduling, auth, presence

Files: `daemon.c`, `cfg.c`, `cfg.h`, `tests/test_utils.c`.

Defects found:

1. **Detection ran every second.** The msgbuf ring, process walk and app.db
   probes ran at 1 Hz even though `poll_interval_s` documented a 12 s
   cadence. Root cause: the cadence key was read and clamped but never used.
2. **Refused credentials were retried.** Close `4004` fell into the normal
   backoff path, so the daemon kept sending IDENTIFY with a token Discord
   had refused. That is the behaviour that trips abuse protection. Root
   cause: auth failure and transport failure shared one policy.
3. **Reconnect could leave a blank tile.** `home_posted` was reset on
   reconnect but the Settings/browser posted flags were not, so a drop
   while Settings was open restored the gateway with no presence update.
4. **Detection latency would have doubled** if the old 2-sample debounce
   was simply kept on the new cadence.

Changes:

- `sched_poll_due()` gates the expensive probes; first tick after a
  (re)connect polls immediately (`last == 0`), and a backwards monotonic
  jump also forces a poll.
- `sched_transition_hits()` returns 2 only at ≤2 s cadences; at the
  configured 5 s+ cadence one sample already spans the flicker window.
- `park_until_token_change()`: both 4004 sites (connect and mid-session)
  now park, re-read `config.json` every 10 s, log about every 10 minutes,
  and resume only when `token` differs. One on-screen notification per boot.
- Settings/browser posted flags reset with `home_posted` on reconnect.
- Reconciliation every 15 minutes re-posts the current state.

Tests: `test_sched` covers cadence, exact boundary, backwards clock,
interval `0`, debounce collapse, and session freshness. Full gates green.

Known deviation at the time: `presence_reconcile_s` was hardcoded at 900 s
(plan §5 names a clamped 60–3600 key). Resolved in iteration 7.

## Iteration 4 — 2026-10-28 — WebSocket fragmentation and log growth

Files: `ws.c`, `ws.h`, `log.c`, `log.h`, `tests/test_ws.c` (new),
`tests/Makefile`.

Defects found:

1. **Fragmented messages aborted the stream.** The parser treated FIN=0 as
   a complete message, returned the first fragment, and left the
   continuation frame to be parsed as a new message. A fragmented READY or
   gateway event is legal RFC 6455 traffic, so a proxy or a future server
   change could stall auth permanently. Root cause: no reassembly state.
2. **`log.txt` grew without bound.** The 512 KB cap was checked only in
   `open_log()` at startup; a daemon that runs for weeks never re-checked.

Changes:

- `ws_t` gains a reassembly buffer (`frag`, `frag_len`, `frag_cap`,
  `frag_op`, `in_frag`, `frag_drop`). Continuations append until FIN;
  interleaved control frames are delivered without disturbing the buffer;
  a continuation without a start, or a new data frame inside a message,
  is a protocol error (`-2`); oversize (`> 8 MB`) drops to FIN and returns
  `-3`; `frag_reset()` runs on close and on skipped oversized frames.
- `log.c` rotates at the cap before each write: `log.txt` → `log.txt.1`,
  one previous generation, new file reopened; a failed rename keeps the
  current stream rather than losing logs.
- `tests/test_ws.c` drives the real parser through a scripted `tls_read()`
  stub: single frame, truncated payload, fragmented message, one-byte
  reads, control between fragments, orphan continuation, nested data
  frame, close mid-fragment, reserved control opcode, empty frame.

RED evidence: the pre-change parser fails the fragmented case
(`Assertion failed: ws_recv_frame(...) == 21 ... line 105`); the new parser
passes. ASan runs clean.

## Iteration 5 — 2026-10-28 — edge cases found after the parser fix

Files: `ws.c`, `ws.h`, `discord.c`, `discord.h`, `cfg.c`, `tests/test_ws.c`.

Defects found:

1. **Reserved control opcodes were delivered as frames.** `valid_frame_header()`
   rejected reserved data opcodes 3–7 but let 0xB–0xF through, so a broken
   or hostile server could hand the caller fake control frames that Discord's
   loop silently ignored. RFC 6455 requires failing the connection.
2. **Reconnect notifications were unthrottled.** Every successful READY
   called `notify_show("Connected to Discord")`, so a flapping link stacked
   on-screen dialogs every few seconds. Failure notifications were already
   throttled; success was not.
3. **`cfg_set()` could null a key under OOM.** A failed `jl_new_string()`
   passed NULL, and the existing-key branch freed the old child and stored
   NULL. The function now refuses a NULL value and leaves the old one.
4. **Reconnect contract was implicit.** `discord_connect()` zeroes the whole
   `discord_t`; a caller passing a live WebSocket would leak an fd and a TLS
   context. The requirement is now documented at the declaration. Audited
   every current caller: each closes first, so no live path is affected.

Changes:

- `valid_frame_header()` rejects `op >= 0xB`.
- `notify_throttled("connected", ..., 300)` replaces the unthrottled call.
- `cfg_set()` rejects NULL values.
- `discord.h` documents the close-before-connect contract.

Tests: `test_reserved_control_opcode` (RED before the header fix, GREEN
after). Full gates green.

Also reviewed and left unchanged:

- **Suspend/resume**: recovery is already automatic. A dead socket surfaces
  through the heartbeat-ack timeout (`hb*2+15`) or a TLS read error, and
  every error path closes and reconnects. Worst case is roughly two
  heartbeat intervals of stale presence; a resume notification could make
  it instant, but it is not console-testable here and is not a correctness
  gap. (1.1 later found the real gap: the timer kept the pre-sleep start,
  so Rest Mode is now handled with wall-vs-monotonic drift — see D-03.)
- **`detect_current_game()` cost**: per poll it is two OS calls plus RAM
  lookups; the expensive msgbuf ring copy is in the Settings/browser probes
  and is now behind `poll_interval_s`. No further change warranted.
- A suspected double `sceNetSocketClose()` turned out to be two overlapping
  `sed` ranges printing the same line twice, not a defect. Verified in the
  file before editing.

## Iteration 6 — 2026-10-29 — verification integrity and upgrade safety

Files: `tests/Makefile`, `cfg.h`, `cfg.c`, `tests/test_utils.c`.

Defects found:

1. **Header edits did not rebuild their dependents.** The test Makefile had
   no `-MMD`/`-MP`, so touching `cfg.h` or `sched.h` left stale objects and
   a "green" run that never compiled the change. That makes every earlier
   test result suspect after a header-only edit. Root cause: dependency
   generation was never enabled.
2. **Upgrades had no config backup and no schema stamp.** `cfg_save()`
   rewrote `config.json` with no undo; a bad edit or a bad write had no
   recovery. `cfg_load()` stamped the in-memory schema but the file stayed
   at v1 (or had no key), and there was no migration path or backstop.

Changes:

- `tests/Makefile`: `-MMD -MP`, explicit `DEPS`, `-include $(DEPS)` for
  both the normal and ASan object sets.
- `cfg.h`: `CFG_SCHEMA_VERSION` 2; declares `cfg_migrate()`.
- `cfg.c`: `cfg_migrate()` refuses malformed documents and files from a
  newer schema; `cfg_save()` backs up the current file to
  `config.json.bak` before every rewrite and writes a one-time
  `config.v1.bak` for a pre-v2 file. Backups are created `0600` with
  `open(..., 0600)`, not chmod-after-write.
- Tests: `test_cfg_migrate` covers the version stamp, unknown-key survival,
  both backups, `.v1.bak` immutability across a second save, backup mode
  `0600`, and refusal to rewrite a v99 file.

Verification: `touch orbisrpc/cfg.h && make -C tests test` now recompiles
`test_utils.o`, `cfg.o`, `discord.o` and relinks before passing. Full gates
green.

## Iteration 7 — 2026-10-29 — reconciliation interval is configurable

Files: `cfg.h`, `cfg.c`, `daemon.c`, `tests/test_utils.c`, `docs/CONFIG.md`,
`docs/DAEMON.md`.

Defect: the plan documents `presence_reconcile_s` (60–3600, default 900) but
the daemon hardcoded 900 and the key did not exist. Reconciling more often
than the gateway's rate limit or never at all were both inexpressible.

Changes:

- `cfg_t.presence_reconcile_s`, default 900, loaded/saved, clamped 60–3600
  with a log line (same pattern as `poll_interval_s`).
- `daemon.c` reconciliation uses the configured value.
- `test_cfg_clamp` covers both bounds and the untouched default.

Gates green.

---

## Iteration 8 — 2026-10-29 — updater audit, replay, diagnostics, offline cache

Files: `updater.c`, `updater_http.c`, `art.c`, `art.h`, `discord.c`,
`discord.h`, `detect.c`, `gamecache.c`, `gamecache.h`, `daemon.c`, new
`diag.c`/`diag.h`, `tests/test_utils.c`, `tests/fixtures/gateway/*.jsonl`,
`tests/Makefile`, `.github/workflows/release.yml`, `LICENSE`, docs.

Updater audit findings fixed:

- A declared `Content-Length` longer than the received body was treated as
  a complete response (`updater_http.c`). A truncated download could pass
  the release-JSON path; binaries were saved only by the SHA-256 check.
  Now the parser fails closed. `test_updater_http` pins truncated,
  chunked, redirect and oversized-body cases.
- The updater only matched an asset literally named `orbisrpc.bin`, while
  the release workflow uploads `orbisrpc_sdk.elf`; auto-update therefore
  never found a real release. It now accepts `orbisrpc.bin`,
  `orbisrpc.elf` and `orbisrpc_sdk.elf`, all staging to
  `/data/payloads/orbisrpc.bin`. The release workflow also publishes the
  `.bin` name so the manifest and the release agree.
- `art.c` leaked one socket on every post-`tls_start()` error and on
  success (`tls_free` does not close the fd), the same class of bug fixed
  in `updater.c`. All paths now close through `art_close()`.

Gateway regression replay (T0.12):

- New JSONL fixtures in `tests/fixtures/gateway/` (lifecycle + session),
  run by `test_replay()` through the real `discord_connect()` /
  `discord_tick()` and the host `ws` stubs. 2 files, 14 scenarios.
- Covered: HELLO→READY, close 4004 at auth, close 4009, fragmented close,
  close before HELLO, op 7 reconnect, op 9 invalid session, close 1006,
  ping/pong, server op 2 ignored, duplicate READY without a second
  IDENTIFY, heartbeat sent, heartbeat ACK loss.
- Fixtures contain a placeholder token only; no account data, no real
  session ids. RESUME scenarios were absent at this point; the protocol
  and its replay fixtures were added in 1.1 (D-02).

Diagnostic bundle:

- `diag_build()` emits a plain-text bundle; `diag_write_file()` writes it
  atomically at `0600`. The token is never printed (only length and a
  6-hex fingerprint), and is scrubbed from the log tail. The trigger is
  `/data/orbisRPC/diagnose.request`, serviced from both the live loop and
  `sleep_stop()` so it works while parked on 4004.
- `test_diag` proves the secret never appears in either the in-memory
  bundle or the written file, and checks the mode.

Offline metadata and art:

- `games_cache.json` now persists the resolved `mp:` mapping next to the
  raw art URL. A fresh cache hit sends the cover with no external-assets
  call; an offline boot uses the last known `mp:` path.
- Stale rows are kept on load (`gamecache_get_any` is the offline
  fallback); the normal `gamecache_get()` still enforces the 30-day TTL so
  an online console refreshes old names.
- The table is capped at 256 entries / 512 KB and evicts the
  least-recently-refreshed row instead of refusing new titles.
- The presence builder now uses `art_resolve_mp_cached()` only; network
  resolution moved to detection (`art_resolve_mp` + `gamecache_set_mp`)
  and to a boot prefetch of the static art. An op 3 send cannot block on
  external-assets any more.

Event-driven detection spike:

- Decision: stay with `poll_interval_s` polling for 1.1. No shipping code
  was added. There is no proven notification/kernel-event source on 9.00,
  and the measured cost is one probe batch per configured interval (12 s
  default), not per second. The spike needs a 9.00 console to produce
  numbers that mean anything; it is deferred to 1.2 with the hardware
  checklist.

License:

- Added the `orbisRPC Public-Fork Attribution License 1.0`: README credit
  and a public repository with corresponding source are required for every
  published fork. It is source-available, not OSI open source. Third-party
  components keep their own licenses. (Superseded in Iteration 18 by
  GPL-3.0-or-later; kept here as history.)

## Iteration 8 gate evidence

```
$ make -C tests test
replay: 2 fixtures, 14 scenarios, 0 failed
utility tests passed

$ make -C tests asan
replay: 2 fixtures, 14 scenarios, 0 failed
utility tests passed

$ ORBISRPC_STRICT_SECRETS=1 python3 tests/e2e_consumer.py
E2E host simulation: ALL PASS
```

The first `asan` attempt failed to link because `.objs-asan` was stale
from an interrupted build; `make -C tests clean && make -C tests asan`
rebuilt everything and passed. No source change was needed for that.

---

## Iteration 9 (fingerprint and art cache)

- The per-boot `mp:` cache was a single slot. Boot prefetch resolves three
  static images, so the first two were evicted and the idle tile/small
  badge silently lost their art. It is now an 8-entry table; a regression
  test fills three URLs and verifies all three still resolve.
- A failed art resolve used to run again on every detection poll. That can
  stall the shared gateway loop for up to 12 s per attempt. Retries now
  happen at most once per 15 minutes per URL (`sched_retry_due`), and the
  icon-pack fallback resolves in detection and persists like any other
  cover.
- `s_last_art`/`s_rs_art` were 256 bytes while `games_cache` stores 512-byte
  cover/mapping values. Long `mp:` paths could be truncated into an invalid
  asset. All art buffers on that path are 512 bytes now.
- The committed fingerprint tuple was the plan's stale example
  (`1.0.9206`/`385412`). `--fetch` now reads the stable x64 installer
  redirect for the version and `discord.com/app` for the web
  `BUILD_NUMBER`; the first live run recorded `1.0.9266`/`655747`. The
  weekly workflow opens a PR on a change; the release job fails when the
  committed tuple is not current. No runtime fetch runs on the console.
- Fingerprint tests: 17 (validation, header generation, parsing, stale
  docs). Host suite still green.

## Iteration 10 — 2026-10-29 — desktop logout was self-inflicted token rotation

Reported symptom: after orbisRPC ran for about an hour, Discord logged the
desktop client out; no security email; a fresh token was needed every time.

Root cause: 1.1 added `capabilities: 16381` to IDENTIFY. That mask includes
bit 8, `AUTH_TOKEN_REFRESH`. Discord's gateway documentation for the Ready
event says `auth_token` is "the refreshed auth token for this user; if
present, the client should discard the current auth token and use this in
subsequent requests", and the field requires the `AUTH_TOKEN_REFRESH`
capability. orbisRPC never read `auth_token`. Discord could therefore issue
a replacement token to the daemon, invalidating the copy the desktop client
held, while the daemon kept using the old value. The desktop showed the
login screen; the daemon eventually got close 4004.

This also explains why the failure looked like server-side "different
device" enforcement: the auth session was rotated by our own opt-in, not
by a device check. The build number and UA were inconsistent, but they do
not rotate tokens; they are a separate fidelity issue.

Fix: the capability mask is now `16125` (16381 without bit 8), with a
`_Static_assert` in `discord.c` and a host test that drives
`discord_connect()` through scripted HELLO/READY frames, parses the actual
IDENTIFY JSON, and asserts `capabilities & (1 << 8) == 0`. Tests:
`make -C tests test` (replay 14 scenarios, 0 failed) and the full host
suite pass. Real-Discord confirmation still needs a console run; the
compile-time test cannot prove server behaviour.

## Iteration 11 — 2026-11-01 — post-list audit: updater staging, socket close, art trust

The D-list was closed; this iteration audited the paths the fixes touch.
Five concrete defects, each with a host regression:

- **Updater stack overwrite.** Releases attach both `orbisrpc_sdk.elf` and
  `orbisrpc.bin`, and both names map to `/data/payloads/orbisrpc.bin`. The
  staging code used `pend[1]` with `pend_n < 2`, so the second matching
  asset wrote past the array. Removed the array: stage the first accepted
  asset and free later ones. The two-phase commit (download, verify,
  activate, rollback) is unchanged.
- **WebSocket close leak.** `discord_tick()` clears `connected` when it
  detects a peer drop; a later `ws_close()` returned early and left the
  TLS context and fd open. One socket per heartbeat timeout or dropped
  peer. Close is now unconditional about freeing resources.
- **Untrusted artwork URLs.** Cover URLs reach the external-assets upload
  path. `art_url_ok()` now rejects anything that is not `http(s)://`,
  is 512 bytes or longer, or contains a quote, backslash, or control byte.
  Unsafe URLs are dropped with a log line instead of being escaped.
- **Dangling asset key.** When `art_base_url` was configured but the cover
  did not resolve, the builder sent a generated key that Discord does not
  know; a dangling key drops the entire activity. Art is now omitted in
  that case. The bare-key fallback remains only when no cover source is
  configured at all.
- **Title-rule art override.** `title_rules.art` was read after metadata
  art had already won. It is now forced into `s_last_art` before the
  resolve/cache step, so operator overrides win and still go through the
  same cache as TMDB/pkg-zone art. The presence builder stays cache-only,
  so op 3 cannot block on TLS.

Host gates: `make -C tests test`, `make -C tests asan`,
`ORBISRPC_STRICT_SECRETS=1 python3 tests/e2e_consumer.py` (ALL PASS),
28 script tests, `git diff --check` clean, SDK ELF builds (2,256,128
bytes). No console run; all rows remain L1/L2.

## Iteration 12 — 2026-11-02 — second audit pass: parsers, cache, lifecycle

The second pass targeted code adjacent to the D-list fixes. Eight defects,
each fixed at the shared path:

- **`pkgzone.c` stack overwrite.** `blen` was `int` but passed as
  `(size_t *)&blen`; `upd_parse_response()` writes a `size_t`, so a
  64-bit write landed in a four-byte stack object. Declared it `size_t`.
  The SDK build was the only thing exercising this path.
- **`retro.c` unbounded search.** The streaming window is a fixed byte
  buffer, not a C string; `strstr()` could read past `len` and match
  adjacent memory. Added `find_bytes()` with explicit bounds, and reserved
  one byte when reading headers so the header `strstr()` always sees a
  terminator. Regression feeds an exact-size, non-NUL buffer under ASAN.
- **`tmdb.c` parsed before termination.** `https_get_tmdb()` handed its
  static response buffer to `upd_parse_response()`, which starts with
  `strstr()`. The buffer is now NUL-terminated at `bl` first.
- **Control bytes in `games_cache.json`.** Retro names decode `\n`/`\t`;
  serialization wrote them raw, so the next boot parsed invalid JSON and
  silently lost every cached title. All four string fields now replace
  bytes below `0x20` with spaces. Regression round-trips
  `Line\nBreak\tTool`.
- **Detection cached against stale state.** The cache gate used
  `detect_last_ok()`, which still described the previous title until the
  current resolve finished; a first-run miss could suppress caching the
  next successful resolve. The gate now uses the current `named` flag.
  Title-rule art also resolves when the name did not, so an operator
  override is not lost to a naming miss.
- **Shutdown lifecycle.** Normal exit never marked the boot clean, so a
  deliberate stop counted toward safe mode. The config-disabled and fatal
  token exits also returned with the pid lock held. Normal shutdown now
  calls `health_mark_clean()`; both early exits call `lock_release()`.
- **Config temp permissions.** `cfg_save()` opened the temp file with
  `fopen()` and only then `fchmod()`ed it, leaving a window where the
  token was readable under a permissive umask. The temp is now created
  with `open(..., 0600)` and wrapped with `fdopen`; `fchmod` remains as a
  guard.
- **Release asset alias.** The manifest lists only `orbisrpc.bin`, but the
  release publishes the same bytes as `orbisrpc_sdk.elf` too; the updater
  refused the second name. `manifest_check_alias()` verifies the exact
  name when listed, otherwise the canonical `orbisrpc.bin`. The updater
  now stages the first accepted asset for the shared target instead of
  holding a fixed pair.

Host gates: `make -C tests test`, `make -C tests asan`,
`ORBISRPC_STRICT_SECRETS=1 python3 tests/e2e_consumer.py` (ALL PASS),
28 script tests, `git diff --check` clean, SDK ELF builds (2,256,200
bytes). No console run; all rows remain L0–L2.

## Iteration 13 — 2026-11-02 — idle clear, JSON UTF-8, TMDB trust

Three behaviour defects found by continuing the same audit. Each has a
host regression at the shared predicate or parser, and no console run.

- **`show_idle=0` still held the closed game.** The idle-suppression
  branch ran after the post-close hold branch, so a game that closed to
  the home screen kept its tile for `presence_hold_s` even though the docs
  promise the presence is cleared. Added the pure predicate
  `sched_clear_idle_now(gate_idle, active, scan_unknown)` and moved the
  branch ahead of the hold. A failed or uncertain scan still wins, because
  a game may be live underneath; only a confident home reading clears.
  Regression: `test_sched` asserts all four truth-table cases, and the
  daemon branch is L0 evidence.
- **`\uXXXX` surrogate pairs produced invalid UTF-8.** The JSON string
  decoder emitted each surrogate half as its own three-byte sequence, so
  an emoji in a game name became six bytes of invalid UTF-8 and Discord
  rejected the presence. The decoder now reads the low half, combines the
  pair into one four-byte code point, and maps a lone surrogate to U+FFFD.
  Regression: `test_json` asserts `\ud83c\udfae` decodes to `🎮` and a
  lone `\ud83c` becomes U+FFFD.
- **TMDB accepted an `http://` icon it could not upgrade.** `tmdb_parse()`
  upgraded short http URLs to https but then accepted a long http URL
  unchanged when the built buffer was too small. Discord silently ignores
  a non-https external asset, so that produced a dangling image. The
  parser now accepts only `https://` after the upgrade step; a too-long
  http URL is dropped and the name still resolves. Regression: `test_tmdb`
  feeds a 300-character http path and asserts the icon is empty while the
  name is kept.

Host gates re-ran green for this iteration: `make -C tests test`,
`make -C tests asan`, `ORBISRPC_STRICT_SECRETS=1 python3
tests/e2e_consumer.py` (ALL PASS), 28 script tests, `git diff --check`
clean, SDK ELF builds (2,256,200 bytes). No console run; all rows remain
L0–L2.

## Iteration 14 — 2026-11-02 — installer safety, gateway input, idle home

The next pass covered the installer and the gateway parser. Ten defects
were fixed; the first was the highest-impact one.

- **Reinstall wiped the saved config.** `step_files()` copied
  `/app0/assets/config.json` over `/data/orbisRPC/config.json`
  unconditionally, replacing the user's token, learned titles and settings
  with the packaged defaults; `step_token()` then asked for the token
  again. The copy now happens only when the destination does not exist, so
  a PKG update preserves the live config. Docs: `docs/INSTALLER.md`.
- **Installer and daemon validated tokens differently.** `icfg.c` kept its
  own 40–150-char, "two dots anywhere" check: it refused 151–200-char
  tokens the daemon accepts and accepted empty segments (`abc..def`). It
  now wraps the shared `orbisrpc/token.h`, so both binaries use one rule.
- **Installer temp config was world-readable.** `icfg_write()` used
  `fopen()` (0666 & umask) for a file containing the token and skipped
  `fclose()` when the write failed. It now creates the temp with
  `open(..., 0600)`, wraps it with `fdopen`, and always closes it.
- **Installer could install a truncated payload.** `copy_file()` hashed
  only the bytes `fread()` returned; a short source read hashed cleanly
  against the partial destination. Both the copy and read-back loops now
  check `ferror()` and remove the partial destination on failure.
- **Installer claimed a save that failed.** `step_token()` ignored
  `icfg_token_save()`'s read-back result and always said "Config saved".
  It now reports the FTP fallback when the save fails and logs the rc.
- **Evict failure stage was misattributed.** The report reused `g_stage`
  after the daemon copy had overwritten it. The evict stage is captured
  immediately after that copy.
- **Gateway decimal parsing could overflow.** `gw_op()` and `gw_seq()`
  accumulated arbitrary digit runs into `int`; a malformed `"op"`/`"s"`
  was signed-overflow UB. Ops are now capped before the multiply and an
  implausible sequence leaves the last valid value.
- **HELLO heartbeat interval cast was unchecked.** A double outside
  `int64_t` range (including `inf` from `1e400`) was cast directly. The
  value is now clamped to 5–300 s and junk falls back to 45 s.
- **`show_idle=0` still posted a home presence from a fresh boot.** The
  clear branch only ran when a game had been active. `sched_home_due()`
  now gates the home post on the flag, matching `docs/CONFIG.md`.
- **HTTP status and Content-Length used `atoi`/`atol`.** Both are
  undefined on a value they cannot represent; the response comes from the
  network. The updater parser, TMDB status parse and retro status log now
  use `strtol`. Regression feeds an unrepresentable Content-Length and
  status line.

The ASAN gate now runs with `UBSAN_OPTIONS=halt_on_error=1`, so undefined
behaviour fails the suite instead of only printing. Three new replay
scenarios (huge `op`, huge `s`, huge heartbeat) exercise the parser
bounds; the gateway fixture count is 17.

Host gates: `make -C tests test`, `make -C tests asan` (UBSan fatal), e2e
ALL PASS, 28 script tests, `git diff --check` clean, SDK ELF builds
(2,256,200 bytes after this pass). No console run; installer rows stay
L0, gateway rows L1/L2.

## Iteration 15 — 2026-11-02 — notification spam, clear retry, drop visibility

The user-visible defect this pass is the "Connected to Discord" toast:
testers reported it returning every few minutes, which is consistent with
the reconnect loop re-firing the 300 s throttle. The toast is now once per
boot; the reconnect itself stays visible in the log.

- **"Connected to Discord" popped on every reconnect.** All three READY /
  RESUMED paths called `notify_throttled("connected", ..., 300)`. A link
  that dropped more often than every five minutes therefore showed the
  toast every time; the throttle only capped the worst case. They now use
  `notify_once_boot("connected", ...)`, the same pattern as the token and
  DNS notices. The notice tables moved out of the payload-only block so
  the behaviour is host-tested: `test_notify` proves three calls show one
  notification, another key still fires, and `notify_throttled` still
  rate-limits. E2E source contract `notification/connected-once` pins the
  call sites.
- **A failed presence clear was treated as success.** Three branches in
  `daemon.c` cleared local state unconditionally: a gated title, the
  `show_idle=0` home path, and hold expiry. `discord_clear_presence()`
  returns -1 when the socket is down or the send fails, so the daemon
  forgot the tile while Discord still displayed it; with `show_idle=0` a
  closed game could stay on the profile until reboot. Each branch now
  commits only on rc 0, leaving the clear armed for the next tick, and
  the gated-title post is skipped while the suppress-clear is pending.
- **A dropped gateway read logged nothing.** `discord_tick()` returned -1
  for `nr < 0` without a line, so a normal peer close and a protocol
  violation looked identical in the log (the latter logged inside
  `ws.c`). It now logs `gateway read failed (rc=N); reconnecting`; close
  frames already log their code. D-02's "log every disconnect" is now
  covered for both paths.
- **A retro index fetch ran on a blocking socket.** `retro_fetch_scan()`
  created a default blocking socket, set only `SO_SNDTIMEO`, and left
  `tls_read()` with no receive deadline. `tls.c` documents a non-blocking
  transport, and TMDB/art/updater switch the fd before the handshake;
  retro was the exception. On a network that keeps the connection but
  stops answering, the daemon's only thread could sit in `recv()`
  indefinitely, stopping heartbeats, detection and the STOP check. The fd
  is now non-blocking before the handshake, both the header and body loops
  pump with `RETRO_DEADLINE_S`, and one 25 s budget covers all three
  platform indexes instead of 75 s. A miss retries on the normal
  five-minute resolve window. E2E contract `retro/nonblocking-bounded`
  pins the three properties; the host build stubs the network path, so
  the socket behaviour itself is console-only evidence.

D-10 (nanoDNS exceptions for GTA) stays documentation-only: no nanoDNS
component is built, bundled or changed by this repository.

Host gates: `make -C tests test`, `make -C tests asan` (UBSan fatal), e2e
ALL PASS, 28 script tests, `git diff --check` clean, SDK ELF builds
(2,256,200 bytes after this pass), installer objects and eboot link.

## Iteration 16 — 2026-11-02 — bounded connect on every network path

The residual hang class after iteration 15 was `connect()`. Every module set
`SO_SNDTIMEO` and treated it as a connect timeout; a host experiment on a
blackholed IPv4 route shows that is false (2 s send timeout, 75 s `connect`).
On the daemon's only thread that is 75 s without heartbeats, detection or
the STOP check, and it is exactly the shape of a payload that looks alive
but has stopped updating presence.

- **`connect()` was unbounded on six paths.** `ws.c` (gateway), `tmdb.c`
  (both HTTP and HTTPS), `art.c`, `pkgzone.c`, `retro.c` and `updater.c`
  all waited for the kernel's SYN budget. A new `netconnect.c` resolves,
  opens a non-blocking socket, and waits in `select()` for a caller-set
  deadline, then checks `SO_ERROR` before returning the fd. The fd is left
  non-blocking, which is what every TLS pump already assumed. All six call
  sites use it; the app-world Orbis branch keeps its own transport.
- **`pkgzone.c` also read a third-party page without a deadline.** Its
  `for(;;) tls_read()` loop had no receive timeout and treated a non-blocking
  0 as EOF, so a stalled pkg-zone connection could park resolution forever.
  It now pumps to `PKGZONE_DEADLINE_S` and only a negative read ends the
  loop early. This is the same class as the retro fix in iteration 15, on
  the homebrew-title path.
- **Log rotation could dead-end after a failed reopen.** If `rename()` to
  `.1` succeeded but creating the replacement failed, the live path stayed
  missing: every later rotation hit `ENOENT` and returned, so the renamed
  file grew unbounded again. The rename is now rolled back when the new
  file cannot be opened, leaving the next over-cap line able to retry.

Host coverage: new `tests/test_net.c` (connected non-blocking fd, refused
port, invalid arguments) runs in both `make test` and `make asan`; e2e
contracts `net/bounded-connect` and `net/all-paths-bounded` pin the helper
and its use by all six modules. The blackholed-route timeout itself is not
host-tested (it needs a dropped SYN); the select/non-blocking path it uses
is what the local tests exercise.

Host gates: `make -C tests test`, `make -C tests asan` (UBSan fatal), e2e
ALL PASS, 28 script tests, `git diff --check` clean, SDK ELF builds
(2,256,360 bytes after this pass). The OpenOrbis app variant compiles every
object; its final link is blocked on this host by an unrelated lld/LLVM
dylib mismatch, and the installer `eboot.bin` still links.

## Iteration 18 — 2026-11-02 — GPL-3.0-or-later

Files: `LICENSE`, `NOTICE`, `README.md`, `AGENTS.md`, `docs/`, every `.c`,
`.h`, `.py`, `.sh`, and Makefile under `orbisrpc/`, `installer/`, `tests/`,
`scripts/`, `tools/`, and the release workflow.

Change:

1. **Replaced the source-available PFAL-1.0 with GPL-3.0-or-later.** The
   old license forbade private forks, which the owners did not want. The
   GPL keeps private and internal forks legal while requiring complete
   corresponding source for anything distributed, so public forks must stay
   open.
2. **Added the attribution term as a GPLv3 section 7(b) additional term**
   in `NOTICE`: redistributors must credit SirHumza and CyberMask367, link
   the original repository in their primary README, and not present
   modified versions as official releases. Section 7(b) is the GPL's own
   mechanism for this, so it does not conflict with the copyleft.
3. **Added SPDX headers to all 103 source files** and taught the three
   generators (`gen_fingerprint_header.py`, `gen_icon_header.py`,
   `make_ca_bundle.py`) to emit the same header, so a regeneration does not
   strip it.
4. **Audited third-party compatibility.** mbedTLS is used under its
   GPL-2.0-or-later option, SQLite is public domain, BearSSL is MIT and only
   feeds the legacy build harness, and the CA bundle is certificate data.
   Nothing in the shipped payload is GPL-incompatible.
5. **Packaging:** release assets now include `LICENSE` and `NOTICE`, and
   the README, release notes, and roadmap state the license prominently.

Residual risk: relicensing a joint work needs both copyright holders'
agreement. The tree now credits SirHumza and CyberMask367; their consent to
supersede PFAL-1.0 is an offline matter, recorded here so it is not lost.

## Current gate status (2026-11-02, host)

```
$ make -C tests test
ws tests passed
replay: 2 fixtures, 17 scenarios, 0 failed
utility tests passed

$ make -C tests asan
ws tests passed
replay: 2 fixtures, 17 scenarios, 0 failed
utility tests passed

$ ORBISRPC_STRICT_SECRETS=1 python3 tests/e2e_consumer.py
PASS linkage/no-app-libs - libSceSystemService.sprx,libkernel_web.sprx,libSceLibcInternal.sprx,libSceNet.sprx
E2E host simulation: ALL PASS

$ python3 -m unittest discover -s scripts/tests -p 'test_*.py'
Ran 28 tests
OK

$ sh scripts/build_sdk.sh
-rwxr-xr-x  1 mac  staff  2256200 build-sdk/orbisrpc_sdk.elf
```

The SDK was built from git into a scratch prefix, and the ELF linkage gate
was read with `llvm-readelf -d`: only the four daemon-world libraries above
are NEEDED, and `libkernel.so` is absent.

## Limits of this evidence

- No console run happened for any change in this log. Suspend/resume,
  real gateway behaviour, `4004` against Discord, Settings/browser probes,
  and long-run rotation all still need a 9.00 console session. The console
  was unreachable on 2026-10-29 (FTP `2121` and klog `3232` closed; the
  Remote Play host did not answer), so the checklist could not start.
- 13.x is compiled by CI only; it is not runtime-verified.
- The Discord fingerprint endpoints were fetched from the dev Mac on
  2026-10-29 (`1.0.9266`/`655747`), but the weekly workflow itself has not
  been observed in GitHub Actions yet.

## Next action

Release steps: run the three gates from clean, bump `version.h` (done,
1.1.0), confirm the fingerprint is current (done locally; CI weekly job
still unobserved), and run the 9.00 console checklist. The SDK build and
linkage gate are now verified locally as well. The console-only items
remain unverified: suspend/resume, a real reconnect, `4004` against
Discord, Settings/browser probes, crossing the 512 KB log cap, and the
second-launch lock check. 13.x stays CI-built and explicitly unverified.
The detection spike is deferred to 1.2 with hardware.

<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- Copyright (C) 2026 SirHumza and CyberMask367 - see NOTICE for attribution terms. -->
