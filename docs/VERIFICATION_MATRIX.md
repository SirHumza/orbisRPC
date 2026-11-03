# 1.1 verification matrix

Source of truth for defect status from `orbisRPC-1.1-fixes.md` (D-01…D-20)
and the feature requests in `orbisRPC-issues.json`. Read the evidence
column before believing a row: no 1.1 item has a console run.

## Evidence levels

| Level | Meaning |
|---|---|
| L0 | Code read / compiles for the payload SDK |
| L1 | Host unit test, named in the row |
| L2 | Host e2e/static contract in `tests/e2e_consumer.py` |
| L3 | Host gateway replay through the real connect/tick loop |
| L4 | Observed on a 9.00 console |
| L5 | Release/field verified by users |

Hardware baseline: 0.4 on 9.00 (`9d9c5b7`). 1.1 has **no L4/L5 rows**.
`13.x` is CI-built only.

## Defects

| ID | Status | What changed | Evidence | Missing |
|---|---|---|---|---|
| D-01 | Fixed (host) | Continuation window and `session.json` removed; `started` is process-local; new game/relaunch always starts at 0:00 | L2 `session/no-continuation-cache`, `session/no-stale-restore`, `session/fresh-timer`; L0 `daemon.c` | L4 close/relaunch/reboot |
| D-02 | Fixed (host) | Heartbeat ACK loss and close codes force reconnect; RESUME with fresh-IDENTIFY fallback; every close logged | L3 replay (heartbeat ACK loss, close 1006/4009/4004, op 7/9) | L4 five-hour run |
| D-03 | Fixed (host, different mechanism) | Wall-vs-monotonic drift detects suspend, rebases `started`, re-posts presence. Notification API was not used | L1 `test_sched` (`sched_suspend_drift`, `sched_rebase_start`); L2 `resume/suspend-drift`; L0 daemon wiring | L4 Rest Mode wake |
| D-04 | Fixed (host) | Lock stores pid + instance token; boot-stale mtime or dead pid reclaims; release removes only our token | L1 `test_lock_stale`; L2 `lock/boot-stale` | L4 stale-lock boot |
| D-05 | Fixed (host) | `/data/orbisRPC/stop` checked every second; left in place for other instances, cleared on next start; SIGTERM path kept | L2 `shutdown/stop-file`; L0 `daemon.c` | L4 stop-file exit |
| D-06 | Not fixed, mitigated | PKG excluded from release; installer writes `/data/orbisRPC/log.txt` (fallback `/data/log.txt`) | L0 installer; release workflow has no PKG | Crash not reproduced; needs console |
| D-07 | Not fixed, distribution removed | No PKG is hosted beside the daemon release | L0 release workflow | Kernel panic not reproduced |
| D-08 | Partially fixed | Local PS1/PS2/PSP index scanned before network; built-in homebrew name; learned cache remains. Full offline Sony TMDB database is **not** shipped | L1 `test_retro` (local scan), `test_builtin_titles`; L2 `metadata/local-retro` | First-seen retail titles still need TMDB/nanoDNS; L4 offline boot |
| D-09 | Fixed (host) | `ELYS00001` → `Elysium`; unresolved ids fall back to title ID, never "not recognized" | L1 `test_builtin_titles`; L2 `detect/builtin-homebrew` | L4 |
| D-10 | Documented, unverified | Troubleshooting now explains adding GTA/PSN exceptions instead of removing nanoDNS | L0 docs only | No hardware test; do not call fixed |
| D-11 | Fixed (host) | `presence_hold_s` default 300, clamp 0–1800; tile holds, session/timer end at close | L1 `test_cfg_clamp`; L2 `session/presence-hold` | L4 hold + relaunch |
| D-12 | Fixed (host) | `token_reason()` reports placeholder/length/empty-segment/illegal-character position; logged at startup and in the retry loop | L1 `test_token_shape`; L2 `token/reason-logged` | L4 real token |
| D-13 | Partially fixed | IME input kept; failure/cancel now prints explicit FTP fallback path | L0 installer | Input still depends on the console IME; L4 |
| D-14 | Fixed (host) | Whitespace/matching quotes trimmed; bare unquoted token repaired in place without resetting other keys | L1 `test_cfg_token_parsing` | L4 |
| D-15 | Fixed (host) | Startup `discord.com` getaddrinfo probe; non-fatal warning with nanoDNS hint | L2 `dns/startup-probe`; L0 `daemon.c` | L4 nanoDNS stopped |
| D-16 | Fixed (host) | `log_path()` printed at startup; installer uses the same `log.txt`; rotation unchanged | L2 `log/path-printed`, `log/path-accessor`; L1 `test_log_rotation` | L4 FTP fetch |
| D-17 | Fixed (host) | argv[0] logged; `/data/payloads` absence hint | L2 `log/path-printed`; L0 `main.c` | L4 wrong path |
| D-18 | Fixed (host) | GoldHEN banner parsed from `kern.msgbuf`; logs found version and `2.4b18.10+` requirement | L1 `test_goldhen`; L2 `goldhen/version-probe` | L4 old GoldHEN |
| D-19 | Fixed (host/release config) | Build hash (with `-dirty`) logged; release workflow attaches SHA-256 sums and versioned assets | L2 `build/hash-logged`; workflow YAML parsed locally | GitHub Actions run; L4 log line |
| D-20 | Fixed (docs) | README links the GitHub Releases page and names the Discord beta channel | L0 README | Release page must exist before launch |

## Feature requests

| # | Request | Disposition |
|---|---|---|
| 1 | Show presence only in-game + idle/browser/media options | Already covered by `show_idle`, `show_media`, `show_homebrew`; no new code |
| 2 | Custom presence layouts/variants | Declined; image hosting cost |
| 3 | Remove firmware/logo from presence | `show_firmware` and `show_logo` are configurable; status (`online`/`idle`/`dnd`/`invisible`) and elapsed/none timestamps are configurable; title rules can add a known end time. Host assertions cover config round-trip and activity JSON |
| 4 | GitHub webhook commit feed | Out of project scope |
| 5 | Use game icon/imgur instead of hosted art | Declined; imgur upload cap |
| 6 | Save-data export | Separate project (`ps4-savedrive`) |
| 7 | "Playing on PlayStation" platform label | Declined; bot RPC/ban risk |
| 8 | Other DNS/homebrew provider integration | Community follow-up, no daemon work |
| 9 | Custom emojis/stickers | Not in this daemon |
| 10 | Offline Sony TMDB copy | Partial: local retro index + learned cache only; full DB not shipped (see D-08) |
| 11 | Explain nanoDNS/PSN DNS equivalency | Added to `TROUBLESHOOTING.md` |
| 12 | Low-RAM Discord client | Separate client project |
| 13 | Presence in other ecosystems | Community follow-up; Novacore already uses it |

## Follow-up audit fixes (2026-11-01, host only)

These came from the post-matrix audit, not from the D-list. They have L1/L2
coverage and no console run.

| Finding | Status | What changed | Evidence |
|---|---|---|---|
| Updater dual-asset staging wrote past `pend[1]` | Fixed | Both `orbisrpc_sdk.elf` and `orbisrpc.bin` map to the same target; the first accepted asset is staged, later matches are freed. No fixed-size pair remains | L0 `updater.c`; `ORX-UPDATE-003` path still stages atomically |
| `ws_close()` leaked TLS/fd after a peer drop | Fixed | Close now always frees the TLS context and closes the fd even when `discord_tick()` already cleared `connected` | L1 `test_close_after_peer_drop` |
| Artwork URL accepted quotes/controls/non-http | Fixed | `art_url_ok()` accepts only `http(s)://`, <512 bytes, no quote/backslash/control byte; resolution refuses malformed input | L1 `test_art_url_ok` |
| Configured-but-unresolved cover sent a dangling key | Fixed | Presence omits art when a cover URL was configured but did not resolve; the generated key is only used when no cover source exists | L1 `test_activity_assets`, `test_discord_builder` |
| `title_rules.art` lost to metadata art | Fixed | The rule is forced into `s_last_art` before detection's resolve/cache path, so operator art wins and is cached like metadata art | L0 `detect.c`; rule resolution path shared with metadata |

## Second audit pass (2026-11-02, host only)

| Finding | Status | What changed | Evidence |
|---|---|---|---|
| `pkgzone.c` passed `&blen` as `size_t *` after declaring `int blen` | Fixed | `blen` is `size_t`; the parser writes eight bytes instead of four over a four-byte stack object | L0 `pkgzone.c` (SDK build path); parser output type asserted by `test_updater_http` |
| `retro_extract()` used `strstr()` on a non-NUL-terminated stream window | Fixed | Added bounded `find_bytes()`; header read reserves a byte for the terminator | L1 `test_retro` exact-size non-NUL buffer under ASAN |
| TMDB HTTPS response was parsed before NUL termination | Fixed | `raw[bl] = 0` before `upd_parse_response()`, whose contract is a C string | L0 `tmdb.c`; `test_updater_http` covers the parser contract |
| Cached control bytes could invalidate `games_cache.json` | Fixed | Every serialized field maps bytes `< 0x20` to spaces | L1 `test_gamecache` (`Line\nBreak\tTool`) |
| Detection cached the previous title's resolve state | Fixed | Cache gate uses the current `named` flag; title-rule art resolves even when the name did not | L0 `detect.c`; no host harness for detection |
| Clean shutdown was counted as an unclean boot; two exits skipped `lock_release()` | Fixed | Normal exit calls `health_mark_clean()`; config-disabled and fatal-token exits release the lock | L0 `daemon.c`; L1 `test_health_safe_mode` covers the health primitive |
| Config temp file was world-readable between create and `fchmod` | Fixed | Temp is created with `open(..., 0600)` then `fdopen`; `fchmod` is a guard, not the first restriction | L1 `test_cfg_save` asserts 0600 on the saved file |
| Release manifest lists only `orbisrpc.bin`; updater rejected `orbisrpc_sdk.elf` | Fixed | `manifest_check_alias()` verifies the exact name when listed, else `orbisrpc.bin`; updater stages one asset for the shared target | L1 `test_manifest` alias assertions; L0 `updater.c` |

## Third audit pass (2026-11-02, host only)

| Finding | Status | What changed | Evidence |
|---|---|---|---|
| `show_idle=0` kept a closed game's tile for `presence_hold_s` | Fixed | `sched_clear_idle_now()` plus branch reorder: a confident home reading clears immediately; an uncertain scan still holds | L1 `test_sched` (four truth-table cases); L0 `daemon.c` |
| JSON surrogate pairs decoded to invalid UTF-8 | Fixed | `\uD800`–`\uDBFF` reads the following low half and emits one four-byte code point; lone surrogates become U+FFFD | L1 `test_json` (pair and lone-half cases) |
| TMDB accepted a too-long `http://` icon | Fixed | After the https upgrade step, only `https://` is accepted; a dropped icon leaves the name intact | L1 `test_tmdb` (300-char http path) |

## Fourth audit pass (2026-11-02, host only)

| Finding | Status | What changed | Evidence |
|---|---|---|---|
| Reinstall overwrote the saved config and token | Fixed | Packaged `config.json` is copied only when `/data/orbisRPC/config.json` does not exist | L0 `installer.c`; `docs/INSTALLER.md` |
| Installer token rules diverged from the daemon | Fixed | `icfg.c` wraps shared `token.h`: 40–200 chars, three non-empty base64url segments | L1 `test_installer_cfg` (180-char token, empty segments) |
| Installer temp config was created with `fopen()` | Fixed | `open(..., 0600)` + `fdopen`; `fclose` always runs, including after a write error | L1 `test_installer_cfg` asserts 0600 |
| Installer copy could accept a truncated payload | Fixed | `ferror()` checked on copy and read-back; partial destination removed | L0 `installer.c` |
| Installer claimed a failed token save succeeded | Fixed | Read-back result is checked; failure shows the FTP path and logs the rc | L0 `installer.c` |
| Evict failure stage was overwritten in the report | Fixed | Stage captured immediately after the evict copy | L0 `installer.c` |
| Gateway `op`/`seq` parsing could signed-overflow | Fixed | Op accumulator capped before multiply; implausible sequence leaves the last valid value | L1 replay `tick-huge-op-ignored`, `tick-huge-seq-ignored` under UBSan-fatal |
| HELLO heartbeat interval cast was unbounded | Fixed | Clamped to 5–300 s; junk/inf falls back to 45 s | L1 replay `hello-huge-heartbeat-clamped` (`hb_ms`) |
| `show_idle=0` posted a home presence from a fresh boot | Fixed | `sched_home_due()` gates the home branch on the flag | L1 `test_sched` (five cases); L0 `daemon.c` |
| HTTP status/Content-Length parsed with `atoi`/`atol` | Fixed | Network fields use `strtol`; an unrepresentable value neither overflows nor reads as 200/complete | L1 `test_updater_http` (huge Content-Length and status) |

## Fifth audit pass (2026-11-02, host only)

| Finding | Status | What changed | Evidence |
|---|---|---|---|
| "Connected to Discord" re-popped on every reconnect | Fixed | All READY/RESUMED paths use `notify_once_boot("connected", ...)`; the dedupe tables are host-visible and reset for tests | L1 `test_notify` (one toast for three calls, key independence, throttle still works); L0 `discord.c`; e2e `notification/connected-once` |
| A failed presence clear was committed as success | Fixed | Gated-title, `show_idle=0` home, and hold-expiry branches commit state only when `discord_clear_presence()` returns 0; the clear stays armed to retry, and the gated title is not posted while pending | L0 `daemon.c` (no host seam for the state machine) |
| Gateway read drop left no cause in the log | Fixed | `discord_tick()` logs `gateway read failed (rc=N); reconnecting`; close frames still log their code | L0 `discord.c`; replay fixtures cover frame parsing, not this call site |
| Retro index fetch used a blocking socket with no receive deadline | Fixed | `O_NONBLOCK` before the handshake; header/body loops pump to `RETRO_DEADLINE_S`; one 25 s budget across the three indexes | L0 `retro.c`; e2e `retro/nonblocking-bounded`; host network path is stubbed, so socket behaviour is console-only |

## Sixth audit pass (2026-11-02, host only)

| Finding | Status | What changed | Evidence |
|---|---|---|---|
| `SO_SNDTIMEO` does not bound `connect()`; a dead route froze the only thread | Fixed | New `netconnect.c`: resolve, non-blocking connect, `select()` deadline, `SO_ERROR` check; returns a non-blocking fd. Used by `ws.c`, `tmdb.c` (HTTP + HTTPS), `art.c`, `pkgzone.c`, `retro.c`, `updater.c` | L1 `test_net` (connected non-blocking fd, refused port, bad args); L0 call sites; e2e `net/bounded-connect`, `net/all-paths-bounded`. Host experiment measured a 75 s blocking connect despite a 2 s `SO_SNDTIMEO` |
| pkg-zone lookup read without a deadline and treated 0 as EOF | Fixed | `PKGZONE_DEADLINE_S`; `tls_read()==0` pumps instead of ending the loop; only a negative read is EOF | L0 `pkgzone.c` (network half is SDK-only); e2e `retro/nonblocking-bounded` covers the shared rule |
| Log rotation dead-ended when the replacement file could not be opened | Fixed | After a successful rename to `.1`, a failed `fopen()` now renames the old file back so later lines can retry; previously every later rotation hit `ENOENT` and the renamed file grew unbounded | L0 `log.c`; existing `test_log_rotation` still covers the normal roll |

## Gate record (latest run)

Run serially on the host, 2026-11-02:

- `make -C tests test` → `netconnect tests passed`, `ws tests passed`,
  `replay: 2 fixtures, 17 scenarios, 0 failed`, `utility tests passed`,
  rc 0.
- `make -C tests asan` → same, rc 0; the run sets
  `UBSAN_OPTIONS=halt_on_error=1`, so undefined behaviour aborts the gate.
- `ORBISRPC_STRICT_SECRETS=1 python3 tests/e2e_consumer.py` →
  `E2E host simulation: ALL PASS`, rc 0, including `net/bounded-connect`
  and `net/all-paths-bounded`.
- `python3 -m unittest discover -s scripts/tests` → `Ran 28 tests`, `OK`,
  rc 0.
- `./scripts/build_sdk.sh` → `build-sdk/orbisrpc_sdk.elf`, 2,256,360
  bytes, rc 0; NEEDED = `libSceSystemService.sprx`, `libkernel_web.sprx`,
  `libSceLibcInternal.sprx`, `libSceNet.sprx`; no `libkernel.so`.
- `OO_PS4_TOOLCHAIN=... ./scripts/build.sh elf` → every object compiles;
  the final link is blocked on this host by an unrelated
  `ld.lld`/`libLLVM.23.dylib` mismatch (symbol
  `llvm::format_object_base::home`), not by a source change. The SDK build
  above is the shipped artifact and links.
- `make -f installer/Makefile installer/eboot.bin` with
  `OO_PS4_TOOLCHAIN` exported → objects compile, `eboot.bin` links, rc 0.
  Without the variable, `create-fself-macos` exits 255 after the link. The
  PKG itself is not built or run.
- `git diff --check` → clean.

The ELF is built but **not loaded on a console**. Any claim above L3 is
pending.

<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- Copyright (C) 2026 SirHumza and CyberMask367 - see NOTICE for attribution terms. -->
