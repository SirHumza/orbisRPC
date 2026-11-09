# THE BIGGEST PLAN EVER — OrbisRPC living master plan

**Goal:** Ship a reliable PS4 daemon and setup app with recoverable authentication, accurate presence, safe updates, useful diagnostics, and demonstrated console stability.

**Architecture:** Keep the daemon in C with bounded state and explicit ownership of sockets, files, buffers, and persisted records. Keep the installer responsible for setup and recovery. Share small parsing/validation rules where practical; avoid speculative frameworks.

**Stack:** ps4-payload-sdk built from pinned git source, OpenOrbis installer, vendored mbedTLS and SQLite, existing host tests and GitHub Actions.

**Specifications:** The portable PDF requirement matrix below, the user’s daemon/PKG/QR/auto-update requests, and `AGENTS.md`. Native work is being done on branch `1.1`; code, tests, docs, and build evidence must all match the commit that is pushed.

Native implementation is authorized and active. Existing edits are candidates, not proof of completion. Execute tasks in order; keep the full scope open until its evidence exists.

## Non-negotiable constraints

- Preserve user config, token, overrides, and playtime during upgrades.
- Never log or commit user tokens, webhook URLs, QR session secrets, or signing keys.
- Never link app-world libraries into the daemon; inspect ELF NEEDED entries.
- Use a git-built SDK with firmware gates; never the release ZIP.
- Do not recommend Payload Guest or autorun for unverified test builds.
- Add no GPL code or LICENSE until the owners choose a license.
- Authentication failures must distinguish local syntax, transport, and explicit gateway rejection. No promise that unsupported user-token authentication avoids account restrictions.
- QR authentication requires a technically valid, reviewed flow. A documentation QR must be labeled as instructions, never presented as login.
- No release, push, or hardware success claim without matching evidence.

## Evidence and task workflow

For each task: inspect current code and requirements; reproduce a concrete failure; add a meaningful regression test; implement; run relevant checks; inspect regressions; update docs and status. Record exact commands, exit codes, build identity, and hardware observations. Checkboxes mean the full acceptance gate passed, not merely that code exists.

Review especially: corrupted persistence after power loss; stale state after suspend/reconnect; network responses fragmented at every boundary; config edits while disconnected; installer failure before graphics initialization.

## 1. Establish a trustworthy baseline

Files: `AGENTS.md`, `Makefile`, `scripts/build_sdk.sh`, `tests/Makefile`, `todo.md`, `checkbox.md`.

- [ ] Inventory current branch, merge base, dirty files, artifacts, existing plans, GitHub issues, and PDF requirements without discarding prior work.
- [ ] Map every requirement to source, a regression test, a documentation location, and hardware evidence where needed.
- [ ] Inventory dependencies, licenses, firmware assumptions, SDK revision, and build provenance.
- [ ] Identify which existing tests are behavioral and which only search source text; replace weak evidence for critical behavior.
- [ ] Capture baseline host results before interpreting any new failures.

Acceptance: reproducible baseline and an explicit unresolved-defect list; no inherited change silently counted as complete.

## 2. Authentication and secret lifecycle — D-12, D-14

Files: `orbisrpc/cfg.c`, `cfg.h`, `discord.c`, `daemon.c`, `log.c`, `installer/icfg.c`, `settings.cpp`, `tests/test_utils.c`, `tests/test_icfg.c`, `docs/TOKEN_SETUP.md`, `SECURITY.md`.

- [ ] Trace token input, normalization, validation, memory copies, persistence, reload, REST usage, gateway identify/resume, rejection, and shutdown.
- [ ] Normalize pasted whitespace/outer quotes at the input boundary; preserve strict JSON parsing for the config document.
- [ ] Return actionable syntax reasons without printing token fragments; test empty, placeholder, oversized, malformed, and valid-shaped inputs.
- [ ] Preserve last-good config on malformed reload and failed save; test backup recovery, permissions, symlinks, partial writes, and allocation failures.
- [ ] Stop repeated authentication after explicit rejection; recover only on a valid credential change. Transport failures retain bounded retry behavior.
- [ ] Bind persisted gateway sessions to the credential hash; reject corrupt, stale, or mismatched records.
- [ ] Verify redaction across logs, status, diagnostics, exception paths, and support exports.
- [ ] Research supported console-compatible authorization options against current primary documentation; document capability limits and legitimate refresh behavior.

Acceptance: fault tests prove no credential loss/leak, no rejection loop, and successful recovery after configuration changes. Real Discord behavior requires a separately recorded console test.

## 3. Gateway and network resilience — D-02, D-15

Files: `orbisrpc/ws.c`, `tls.c`, `discord.c`, `daemon.c`, `timesync.c`, related headers/tests.

- [ ] Audit WebSocket upgrade validation, masking, fragmentation, continuation, control frames, close codes, payload limits, and malformed UTF-8/JSON handling.
- [ ] Test heartbeat scheduling and missing ACK recovery with a controlled clock and fake transport.
- [ ] Test reconnect/resume/identify transitions, invalid sessions, gateway-requested reconnect, rate limits, and repeated rejection.
- [ ] Bound DNS/connect/TLS/read/write operations; ensure background metadata cannot starve heartbeats.
- [ ] Add startup DNS diagnostics with an accurate loopback/nanoDNS hint only when observed evidence supports it.
- [ ] Verify certificate and hostname checking, RTC loss recovery, clock adjustments, redirects, and failure cleanup.
- [ ] Apply capped backoff with jitter and reset only after proven health; coalesce notifications and logs.

Acceptance: recover from dropped Wi-Fi, silent peers, DNS failures, TLS failures, and gateway disconnects without restart, floods, busy loops, or timer resets.

## 4. Lifecycle, detection, and timers — D-01, D-03, D-04, D-05, D-17, D-18

Files: `orbisrpc/main.c`, `daemon.c`, `detect.c`, `bigapp.c`, `procwalk.c`, `sess.c`, `lock.c`, `supervise.c`, `fw.c`.

- [ ] Define title/session transitions for game exit, relaunch, quick switching, media, home, Settings, browser, unknown app, and process disappearance.
- [ ] Validate persisted timestamps, checkpoint age, future dates, reboot, wall-clock changes, and resume windows.
- [ ] Investigate supported suspend/resume events and combine reliable lifecycle evidence with clock-gap recovery; test actual rest mode.
- [ ] Test PID reuse, stale lock ownership, concurrent launches, lock I/O failure, and release after every exit path.
- [ ] Verify graceful stop via file/signal during connection, authentication wait, active play, and backoff; clear presence where possible and checkpoint safely.
- [ ] Log build identity and usable launch/path diagnostics without requiring a particular loader filename.
- [ ] Report firmware/GoldHEN compatibility using detected facts; do not invent a version requirement.

Acceptance: correct timer and foreground presence after each transition, no duplicate daemon, clean stop, and recovery after rest mode on supported firmware.

## 5. Metadata and artwork — D-08, D-09, D-10

Files: `orbisrpc/appdb.c`, `detect.c`, `gamecache.c`, `art.c`, `tmdb.c`, `custom.c`, `retro.c`, `config/custom.json`, `installer/nanodns.c`.

- [ ] Make authoritative local metadata the first useful path, then bounded cached/remote fallbacks.
- [ ] Add ELYS00001 and test unknown homebrew, retail regional variants, duplicate IDs, Unicode names, and corrupted SFO/database records.
- [ ] Preserve useful names and generic artwork when every network source fails.
- [ ] Bound cache size, response size, retries, negative entries, and stale expiry; test offline restart and art URL changes.
- [ ] Avoid cross-token or cross-application reuse of Discord external-asset mappings where invalid.
- [ ] Verify nanoDNS changes are minimal, reversible, and idempotent; research actual GTA/PSN endpoints before adding exceptions.
- [ ] Validate GTA Online with nanoDNS on hardware; do not mark complete based on an INI edit alone.

Acceptance: offline launch produces useful presence, fallback sources cannot wedge the daemon, and DNS changes preserve existing user rules.

## 6. Installer and genuine setup recovery — D-06, D-07, D-13, D-16

Files: `installer/main.cpp`, `installer.c`, `dbglog.cpp`, `settings.cpp`, `settingsview.cpp`, `uibridge.cpp`, `updatecheck.cpp`, `docs/INSTALLER.md`.

- [ ] Capture early persistent logs before graphics/module setup; record stage-specific failures and return safely.
- [ ] Reproduce CE-34878-0 on affected firmware; audit module ordering, app/payload boundaries, graphics lifetimes, stack usage, and cleanup.
- [ ] Verify typed token IME/controller input, cancel behavior, validation, save failure, and preservation of unknown config keys.
- [ ] Investigate QR login feasibility; implement only a verified credential flow with expiry, confirmation, cancellation, and secret cleanup. Otherwise provide clearly labeled setup/documentation QR plus working manual entry.
- [ ] Make install/update/uninstall transactional: verify payload, preserve config, provide recovery after interruption, and avoid contradictory status messages.
- [ ] Keep autorun/nanoDNS edits explicit and reversible; report exact changed paths.
- [ ] Add a useful diagnostics page and clear log retrieval instructions; test text clipping, navigation, missing fonts/assets, and all dialog cancellations.
- [ ] Keep any unvalidated Discord-client PKG out of the stable release surface.

Acceptance: complete setup on console, upgrade preserves user state, cancellation never damages an install, and failures produce retrievable logs.

## 7. Signed auto-update and distribution — D-19

Files: `orbisrpc/updater.c`, `updater_util.c`, `updater_http.c`, `release_pubkey.h`, `cfg.c`, installer settings, `scripts/make_manifest.py`, `.github/workflows/release.yml`.

- [ ] Review current updater edits before enabling them; add host-testable staging and verification boundaries.
- [ ] Verify signed stable manifest with pinned P-256 key, exact platform/asset, bounded integer size, strict version, SHA-256, and structurally valid payload ELF.
- [ ] Test invalid/truncated signatures, wrong keys, tampered payload, downgrade/replay, wrong platform, oversized/chunked/truncated responses, redirects, and missing release assets.
- [ ] Stage atomically; preserve prior executable before replacement; test disk full, write/fsync/rename failure, symlinks, interrupted updates, and restart recovery.
- [ ] Detect installed versus staged state accurately; never tell users a staged file will activate automatically.
- [ ] Prevent repeatedly overwriting rollback with an unproven update; define confirmation of healthy new-version boots and rollback policy.
- [ ] Keep opt-out and safe-mode behavior explicit; avoid holding up initial presence for slow update checks.
- [ ] Verify CI signing key matches the pinned public key, key custody/recovery is documented, and signing material never enters artifacts.
- [ ] Publish ELF, signed manifest, signature, checksums, version/build identity, and accurate install instructions through the release workflow when publication is authorized.

Acceptance: every failure leaves a usable prior installation; only a verified newer compatible release can replace it. Verify the full path using a test release/channel before stable enablement.

## 8. Feature expansion: evaluate and implement useful additions

Every item requires a measurable user benefit, bounded resource cost, documented behavior, and meaningful tests. Reject or defer additions that weaken the preceding reliability gates; record why.

- [ ] 1. Installer diagnostics dashboard: connection state, detected title, build, firmware, last failure.
- [ ] 2. Secret-free support bundle with explicit preview of exported files.
- [ ] 3. Config validation command with precise field errors.
- [ ] 4. Effective-config preview with secrets redacted.
- [ ] 5. Config backup/restore and migration recovery.
- [ ] 6. Installer auto-update toggle with clear activation state.
- [ ] 7. Update progress, staged-version display, and manual retry.
- [ ] 8. Verified rollback/recovery action.
- [ ] 9. Working token input and clearly labeled setup QR.
- [ ] 10. Local presence preview using the actual activity builder.
- [ ] 11. Named minimal/detailed/privacy presets.
- [ ] 12. Per-title privacy exclusions without leaking title IDs/art.
- [ ] 13. Custom title names and artwork overrides.
- [ ] 14. Offline metadata import/update with schema and size validation.
- [ ] 15. Cache inspection and targeted repair instead of deleting all data.
- [ ] 16. Session timer reset independent of reinstall.
- [ ] 17. Local playtime history with bounded retention and export.
- [ ] 18. Graceful stop/restart controls with truthful status.
- [ ] 19. Network/DNS/TLS diagnostic actions with bounded deadlines.
- [ ] 20. Notification verbosity and reconnect-spam controls.
- [ ] 21. Media/homebrew/idle visibility controls that preserve game state.
- [ ] 22. Safe presence templates with validated placeholders and length limits.
- [ ] 23. Rate-limited presence coalescing and duplicate suppression.
- [ ] 24. Resource/health counters with periodic compact summaries.
- [ ] 25. Installer accessibility: readable focus, contrast, controller hints, error text.
- [ ] 26. First-run connectivity and configuration checklist in the installer.
- [ ] 27. Disk-space preflight and interruption recovery for install/update.
- [ ] 28. Build provenance in diagnostics and release assets.
- [ ] 29. Crash-loop safe mode with an actionable recovery screen.
- [ ] 30. Documented portable user-data backup that excludes credentials by default.

## 9. Resource safety and fault injection

- [ ] Audit every allocation, buffer length, arithmetic conversion, file handle, socket, SQLite statement, TLS context, and error return in changed paths.
- [ ] Test allocation failure, interrupted syscalls, partial reads/writes, malformed persistence, exhausted disk, invalid configuration, and unavailable services.
- [ ] Stress repeated title changes, config reloads, reconnections, installer entry/exit, and update failures.
- [ ] Measure idle CPU, memory, network usage, cache growth, log growth, and reconnect resource usage; establish documented budgets from measured hardware baseline.
- [ ] Require no monotonic resource growth over 1,000 simulated reconnect/title-switch cycles; investigate every sanitizer finding.

## 10. Verification and release gates

- [ ] `make -C tests test` passes.
- [ ] `make -C tests asan` passes with no address/undefined-behavior findings.
- [ ] `ORBISRPC_STRICT_SECRETS=1 python3 tests/e2e_consumer.py` passes.
- [ ] Build payload using the pinned git SDK; inspect NEEDED entries and firmware gate output.
- [ ] Build PKG and inspect packaged payload/config/assets; prove packaged ELF matches the intended build.
- [ ] Exercise clean install, upgrade, uninstall, cancellation, bad config, invalid token, network outage, and update rollback on console.
- [ ] Test supported firmware matrix using documented tester/device evidence; label any untested firmware explicitly.
- [ ] Run at least 24 hours of continuous presence and a 72-hour soak including game switches, Wi-Fi outages, daemon restarts, and repeated rest-mode cycles.
- [ ] Verify no secret appears in logs, diagnostic exports, screenshots, artifacts, release notes, or repository history introduced by this work.
- [ ] Review full diff for correctness, scope, licensing, maintainability, and undocumented behavior.
- [ ] Synchronize README, CONFIG, TROUBLESHOOTING, INSTALLER, BUILDING, SECURITY, todo, and completion checklist.

## Additional bug-hunting backlog

These are failure cases to investigate, not claims that each bug currently exists. Each confirmed defect needs a reproduction, a regression test, a root-cause fix, and a recorded result. Do not add production complexity for a hypothetical failure that the architecture already prevents.

### Authentication and configuration

- [x] B01 Reject embedded NULs in credential/config strings rather than validating only a prefix. Daemon config loading rejects decoded NULs transactionally; the installer JSON parser rejects `\u0000` because its string values are NUL-terminated. Host tests cover both boundaries.
- [ ] B02 Reject overlong credentials without silently truncating them into different credentials.
- [ ] B03 Handle CRLF, UTF-8 BOM, pasted whitespace, and outer quote normalization consistently at input boundaries.
- [ ] B04 Reject ambiguous duplicate security-sensitive JSON keys or define one consistent interpretation across daemon and installer.
- [ ] B05 Reject wrong JSON root types and non-finite/out-of-range numbers before conversion to integers.
- [ ] B06 Verify boolean handling agrees across the two JSON implementations and the settings UI.
- [ ] B07 Preserve unknown config keys and nested settings when the installer saves one field.
- [ ] B08 Prevent installer saves from overwriting concurrent daemon-learned title mappings or newer token edits.
- [ ] B09 Ensure failed config reload leaves every last-good field unchanged, including secret redaction state.
- [ ] B10 Verify config backup fallback cannot recursively load itself or replace valid data with defaults.
- [ ] B11 Validate config path lengths and refuse truncated temporary/backup filenames.
- [ ] B12 Prevent token/session secrets remaining in temporary files after cancellation or failed writes.
- [ ] B13 Verify credential changes invalidate all credential-bound gateway and external-art caches.
- [ ] B14 Ensure a malformed new token does not cause repeated authentication or destroy a resumable old session unexpectedly.
- [ ] B15 Verify logs redact a newly edited token before any error path can emit it.

### WebSocket, TLS, and reconnects

- [ ] B16 Test upgrade headers split at every byte boundary, repeated headers, case variations, and oversized headers.
- [ ] B17 Test fragmented text messages with interleaved ping/pong frames and multiple messages in one read.
- [x] B18 Reject invalid RSV bits, opcodes, continuation ordering, control-frame lengths, and length overflows. Offline tests cover the shared validator and fragment assembler; arbitrary socket chunk-boundary integration remains in B16/B17.
- [ ] B19 Handle short writes, TLS WANT_READ/WANT_WRITE, EINTR, peer EOF, and close during a write without spinning.
- [ ] B20 Ensure a peer that sends partial data forever cannot extend a deadline indefinitely.
- [x] B21 Validate CLOSE payload lengths, status codes, and UTF-8 reasons; offline tests cover empty, one-byte, reserved/invalid codes, malformed UTF-8, oversized payloads, and Discord 4004. Console behavior remains unverified.
- [x] B22 Only Gateway opcode 11 refreshes the heartbeat ACK timestamp; an offline test sends unrelated dispatch traffic and verifies the session still expires at the ACK deadline.
- [x] B23 `discord_connect()` clears the session object before each fresh IDENTIFY and `ws_connect()` clears transport/parser state; an offline scripted HELLO/READY test verifies stale sequence, heartbeat, and presence-queue state does not carry across reconnects. Compression is not enabled in this client.
- [ ] B24 Ensure resume failure transitions to identify only when allowed and does not form a tight retry loop.
- [ ] B25 Validate gateway-provided URLs and session resume endpoints before use.
- [ ] B26 Bound retry-after values and avoid integer overflow or negative sleep intervals.
- [ ] B27 Verify retry jitter does not synchronize multiple consoles or wrap counters into invalid delays.
- [ ] B28 Re-resolve DNS after network changes rather than retaining a dead address indefinitely.
- [ ] B29 Test failed first address with a reachable alternative address and document IPv4/IPv6 support.
- [ ] B30 Ensure certificate verification remains enabled on every connection and redirect path.
- [ ] B31 Detect wall-clock corrections without invalidating monotonic network deadlines.
- [ ] B32 Prevent metadata, artwork, logging, or update operations from starving heartbeat processing.

### Detection, lifecycle, and presence

- [ ] B33 Test PID reuse and process disappearance between enumeration and metadata lookup.
- [ ] B34 Distinguish foreground title changes from transient detection failures; avoid false game exits.
- [ ] B35 Verify two processes with the same title ID do not create duplicate sessions.
- [ ] B36 Test rapid A→B→A switching, same-title relaunch, and game exit while Settings is foreground.
- [ ] B37 Handle inaccessible/corrupt app databases without retaining an unrelated stale title.
- [ ] B38 Validate title-ID character sets and lengths before constructing filesystem paths or URLs.
- [ ] B39 Test multilingual names, invalid UTF-8, combining characters, and truncation at character boundaries.
- [ ] B40 Ensure private titles cannot leak through state, details, timestamps, artwork URLs, diagnostics exports, or cached activity.
- [ ] B41 Verify every visibility flag clears or updates previously visible presence immediately.
- [ ] B42 Keep activity argument ordering correct; assert actual state/name/details/asset values.
- [ ] B43 Test timestamp overflow, future starts, RTC reset, timezone changes, and prolonged suspension.
- [ ] B44 Ensure graceful shutdown checkpoints once and never counts suspended/offline time as active play unintentionally.
- [ ] B45 Verify restored sessions cannot resurrect a title that is no longer running.
- [ ] B46 Verify stop requests are observed in token waits, reconnect delays, metadata operations, and update checks.
- [ ] B47 Audit supervisor retries for stale static state, duplicate notifications, leaked locks, and duplicated ledger entries.
- [ ] B48 Ensure presence rate limits cover keepalives, reconnect posts, manual refresh, and rapid title switches together.

### Metadata, artwork, storage, and updates

- [ ] B49 Detect corrupt/truncated cache records and recover without deleting unrelated user configuration.
- [ ] B50 Verify cache keys include all inputs that affect validity: title, application, credential context, source, and relevant URL.
- [ ] B51 Bound negative-cache lifetime so temporary outages do not permanently suppress names or covers.
- [ ] B52 Test artwork responses with wrong content type, HTML error pages, oversized data, redirects, and missing assets.
- [ ] B53 Prevent path traversal through imported metadata, title overrides, archive names, or release filenames.
- [ ] B54 Test SQLite busy/locked/corrupt databases and finalize every statement on each return path.
- [ ] B55 Ensure cache/log/ledger rotation works at size limits and after power loss during rotation.
- [ ] B56 Verify atomic writes are on the same filesystem and directory durability is handled where supported.
- [ ] B57 Protect update downloads from truncated headers/body, extra trailing bytes, signature-size mismatch, and ambiguous framing.
- [ ] B58 Validate ELF program-header bounds and daemon compatibility beyond the first ELF header bytes.
- [ ] B59 Avoid repeated downloads of an already staged version on each daemon restart.
- [ ] B60 Preserve the last proven healthy rollback generation until a new version is confirmed healthy.
- [ ] B61 Ensure staged-versus-installed update notifications accurately describe the next action.
- [ ] B62 Test release API rate limits, no releases, prereleases, deleted assets, tag changes, and unavailable signing secrets.
- [ ] B63 Reject signing-key mismatch before publishing any release assets.
- [ ] B64 Ensure package version, daemon version, manifest version, and displayed installed version cannot diverge silently.

### Installer, UI, build, and support

- [ ] B65 Test IME cancellation, maximum input, disconnected controller, and suspend during token entry.
- [ ] B66 Initialize every settings field before first render; avoid reading uninitialized availability/state flags.
- [ ] B67 Test repeated opening/closing of settings and dialogs for surface, font, module, and heap leaks.
- [ ] B68 Prevent double-submit from installing, updating, or uninstalling twice concurrently.
- [ ] B69 Verify all install stages report failure accurately instead of continuing to a success screen.
- [ ] B70 Test read-back integrity when console stat/fstat sizes are unreliable inside the app sandbox.
- [ ] B71 Preserve a recoverable install through power loss at each copy/config/version-marker step.
- [ ] B72 Confirm uninstall removes only owned files/rules and preserves unrelated payloads and nanoDNS entries.
- [ ] B73 Ensure failed graphics initialization still leaves a useful persistent diagnostic record.
- [ ] B74 Verify focus/navigation and text bounds after adding settings rows; test the longest supported error strings.
- [ ] B75 Prevent stale object files or stale packaged assets from entering a new build.
- [ ] B76 Make build failures fatal before packaging and reject missing/duplicate embedded payloads.
- [ ] B77 Verify reproducibility inputs and record SDK/toolchain revisions alongside build identity.
- [ ] B78 Confirm support instructions match actual log paths, stop behavior, loading methods, and current package availability.
- [ ] B79 Audit dependency updates for ABI, firmware, licensing, and security regressions before adopting them.
- [ ] B80 Test diagnostic redaction using synthetic credential canaries across every export and logging path.

## More feature suggestions

These extend the earlier 30 suggestions. Evaluate each against reliability, privacy, console resources, and maintenance cost before committing to implementation. Internet-facing control servers and credential-sharing services require a separate justified design.

- [ ] 31. Detect an already running daemon and display its actual status before offering start/restart.
- [ ] 32. Show detected title and metadata source so incorrect naming is diagnosable.
- [ ] 33. Preview privacy exclusions before saving them.
- [ ] 34. Separate clear-cache actions for names, artwork mappings, and failed lookups.
- [ ] 35. Export/import title overrides with schema, size, and path validation.
- [ ] 36. Offer per-category notification preferences while retaining actionable failures.
- [ ] 37. Show next reconnect attempt and last successful gateway connection.
- [ ] 38. Show whether credentials are missing, malformed, rejected, or awaiting a network connection.
- [ ] 39. Add an explicit pause-sharing control with visible paused status.
- [ ] 40. Offer a temporary privacy session that restores normal sharing after an explicit resume.
- [ ] 41. Add a first-run presence preview before contacting Discord.
- [ ] 42. Explain each config setting in the installer with its actual daemon default.
- [ ] 43. Show storage used by logs, cache, metadata, playtime, and staged updates.
- [ ] 44. Offer cleanup previews listing exactly what will be removed.
- [ ] 45. Display update verification results and the installed/staged version separately.
- [ ] 46. Display the build commit and a QR link to its release/support instructions.
- [ ] 47. Provide an offline recovery guide accessible inside the installer.
- [ ] 48. Offer a safe config reset that preserves credentials only with an explicit user choice.
- [ ] 49. Add an installer preflight report for writable paths, disk space, payload integrity, and required modules.
- [ ] 50. Add a read-only firmware compatibility report with tested versus unknown status.
- [ ] 51. Provide local session summaries without collecting telemetry.
- [ ] 52. Export playtime as CSV/JSON with explicit privacy controls.
- [ ] 53. Add bounded log verbosity that automatically returns to normal after a diagnostic window.
- [ ] 54. Display recent sanitized errors with timestamps and suggested recovery actions.
- [ ] 55. Offer a manual re-detect/re-publish action that respects the same rate limiter as automatic updates.
- [ ] 56. Validate artwork overrides before applying them and retain the last working fallback.
- [ ] 57. Provide a dry-run nanoDNS edit preview and restore the exact previous rules on undo.
- [ ] 58. Support offline installation/update from a locally supplied, signature-verified release bundle.
- [ ] 59. Add controller-friendly confirmation screens for destructive cleanup/uninstall actions.
- [ ] 60. Generate a concise release readiness report from test/build/hardware evidence without embedding secrets.

## Priority and scope discipline

Fix confirmed crashes, credential loss/leaks, broken reconnection, incorrect timers, and update corruption before cosmetic additions. Complete offline metadata and working setup/recovery before optional analytics or customization. Track each suggestion as accepted, implemented, deferred with reason, or rejected with reason; unchecked suggestions must never silently become release promises.

## Completion rule

All PDF defects must be resolved or explicitly identified as unsupported with an approved alternative. Every promised feature must have implementation and evidence; a suggestion is not a delivered feature. Host tests cannot establish PS4 stability or Discord acceptance. Production readiness remains unproven until hardware, soak, package, update, and release gates all pass.


## Master-plan operating rules

Current instruction: execute natively on branch `1.1`, without subagents. The user has authorized the branch commit and push after review. Unrelated external changes remain outside scope. A living backlog can continue growing while individual releases have finite acceptance gates.

On each future execution cycle:

1. Inspect changes since the previous recorded baseline, current issues/PRs, CI failures, dependency/API changes, and tester evidence. Perform a full subsystem audit at milestone boundaries; do not repeatedly reread unchanged files without a reason.
2. Reconcile new evidence with this plan; deduplicate ideas and preserve unresolved requirements.
3. Rank by account/data safety, console crashes, recovery, correctness, then user benefit divided by maintenance/resource cost.
4. Select one coherent increment with explicit acceptance criteria; implement only when execution is authorized.
5. Record source changes, tests, console evidence, documentation, and remaining uncertainty.
6. Add newly discovered improvements when they have a concrete benefit. Avoid empty stubs and invented bug claims.

### Explicit exclusions

- No built-in web dashboard served by the PS4. Status belongs in the PKG UI, CLI, and local files.
- No Discord activity join/spectate buttons.
- No PS5 Remote Play presence feature.

The installer diagnostics dashboard already listed means a native PKG screen, not a web server.

## Release sequence and priority queue

| Milestone | Scope | Exit evidence |
|---|---|---|
| R0: establish reality | Audit inherited dirty changes, current issues, PDF mapping, build provenance | Reconciled defect ledger and reproducible baseline |
| R1: protect the account and console | Auth rejection recovery, redaction, config preservation, installer crashes, locks/stop | Failure tests plus affected-console reproduction/results |
| R2: survive the session | Heartbeats, reconnects, rest mode, title transitions, timers | Controlled fault tests and console lifecycle matrix |
| R3: safe installation and updates | Transactional PKG operations, signature chain, staging, healthy-version confirmation, rollback | Interrupted-install/update tests and a verified test-release upgrade |
| R4: useful without perfect internet | Local metadata, art fallback, DNS diagnostics, cache repair | Offline cold boot, unknown titles, unavailable sources |
| R5: richer everyday use | Profiles, templates, history, native setup improvements, localization | Behavioral/UI tests and resource budgets |
| R6: sustained engineering | Soak automation, performance gates, dependency maintenance, release evidence | Repeatable CI and documented hardware results |

Do not assume an auto-updater exists on this branch. Earlier experimental updater edits were discarded with the old workspace. Inspect the actual branch before implementing update transport, signature verification, staging, and recovery.

## Advanced feature tracks and feasibility gates

### A. Multiple profiles without authentication abuse

- [ ] Separate named profiles for presence preferences, privacy settings, templates, and artwork.
- [ ] Investigate explicitly selected account profiles with one active account per daemon; isolate credentials and session caches.
- [ ] Make account switching stop/clear the previous session before connecting the selected account.
- [ ] Test rename/delete/import and crashes during profile selection without credential crossover.
- [ ] Do not automatically cycle tokens after rejection or account security challenges. Surface the failure and wait for user correction.
- [ ] Accept an account-profile feature only after defining storage permissions, migration, redaction, and installer interaction.

Acceptance: profile changes cannot publish one account's state or reuse its credentials in another profile.

### B. Templates, statistics, and media

- [ ] Define an allowlist of placeholders with bounded expansion and deterministic missing-value fallbacks.
- [ ] Provide game, home, media, and privacy template previews using the production activity builder.
- [ ] Add per-profile template validation and restore-default actions.
- [ ] Aggregate bounded daily/title playtime locally; define active, suspended, disconnected, and idle time explicitly.
- [ ] Preserve ledger integrity across duplicate checkpoints, crash recovery, clock jumps, and record migration.
- [ ] Research media metadata exposed through legitimate console interfaces; show richer detail only when actually available.
- [ ] Never fabricate episode names, emulator ROM identities, achievements, or playback position.

Acceptance: preview matches published fields; exported statistics reconcile with deterministic session fixtures.

### C. Homebrew and emulator integrations

- [ ] Inventory maintained homebrew/emulator projects, licenses, supported versions, and documented integration interfaces.
- [ ] Add title mappings independently from deeper integrations so names work immediately offline.
- [ ] Investigate opt-in emulator metadata through documented local files or interfaces with bounded parsing.
- [ ] Prototype one integration at a time; require provenance, stale-data detection, and a generic fallback.
- [ ] Investigate GoldHEN plugin interoperability using published interfaces and version checks.
- [ ] Clarify the intended ETA integration before promising an interface or functionality.

Acceptance: an absent/outdated third-party component never prevents core presence or causes speculative memory access.

### D. Firmware compatibility and detection recovery

- [ ] Build an evidence matrix from 5.05 through the newest SDK-supported firmware, distinguishing builds, boots, detection, TLS, installer, and rest-mode tests.
- [ ] Prefer stable system interfaces and source-verified SDK offsets over custom memory probing.
- [ ] Maintain capability detection and explicit unsupported states rather than assuming all firmware behaves identically.
- [ ] Consider signed, versioned compatibility data only after defining validation and rollback.
- [ ] Treat automatic offset discovery as a research proposal: require bounds, read-only methodology, independent validation, and safe failure before adoption.
- [ ] Never auto-patch unknown kernel memory to make a compatibility check pass.

Acceptance: unknown firmware degrades safely with actionable diagnostics; every claimed supported firmware has recorded device evidence.

### E. Supervision and observability

- [ ] Distinguish recoverable network failures from corrupt process state; do not restart blindly on every failure.
- [ ] Add bounded restart budgets and crash-loop containment with persistent recovery guidance.
- [ ] Expose sanitized health through the native installer and local status file.
- [ ] Record compact counters for reconnect causes, metadata misses, dropped/coalesced updates, and resource failures.
- [ ] Design support export with field-by-field redaction and user preview.
- [ ] Keep remote log shipping/telemetry off by default; require a separate endpoint, consent, retention, revocation, and secret-redaction design before implementation.
- [ ] Prefer manual sanitized report export before introducing any collection service.

Acceptance: diagnostics can explain a failure without revealing credentials or creating a new unauthenticated network service.

### F. Update efficiency and supply chain

- [ ] Add full-download caching and avoid redownloading an identical verified staged version.
- [ ] Verify installed/staged/healthy-version state survives interruption and supports a deliberate rollback.
- [ ] Define signing-key backup, rotation, revocation, and recovery before relying on unattended releases.
- [ ] Gate releases on version consistency, test results, linkage, package integrity, and manifest verification.
- [ ] Investigate delta updates only after measuring payload size and bandwidth benefit; require signed base/output hashes, bounded patching, and full-download fallback.
- [ ] Reject delta support if patch complexity or peak memory exceeds its measured benefit.
- [ ] Generate dependency/provenance records and retain checksums with release assets.

Acceptance: a compromised transport cannot substitute code; failed optimizations preserve the prior executable and a full-download recovery route.

### G. Native installer quality and localization

- [ ] Organize settings into coherent native pages with consistent controller navigation and visible save state.
- [ ] Add confirmation and recovery copy for account changes, reset, rollback, and uninstall.
- [ ] Centralize user-facing strings before translation; keep log identifiers stable.
- [ ] Test UTF-8/font coverage, long translations, fallback language, and text clipping.
- [ ] Make language selection independent of config migration and credentials.
- [ ] Verify every toggle changes real daemon behavior and every displayed status comes from observed state.

Acceptance: a new user can configure, diagnose, update, and recover using the controller without consulting source code.

### H. Performance and developer experience

- [ ] Measure release ELF/PKG size, idle CPU, resident memory, polling cost, TLS peak allocations, and persistent storage growth.
- [ ] Establish baseline-relative CI budgets; fail unexplained regressions rather than inventing an unattainable near-zero target.
- [ ] Add reproducible fake-clock/fake-transport scenarios for reconnection, suspension, rate limits, and outages.
- [ ] Add deterministic malformed-input corpora and bounded fuzz targets for JSON, SFO, WebSocket, HTTP, and manifest parsing.
- [ ] Run sanitizer/fuzz jobs on supported host platforms and keep console ABI checks separate.
- [ ] Add packaging/version/linkage checks to CI and retain their evidence as artifacts.
- [ ] Document a single supported development workflow, dependency pins, troubleshooting, and targeted test commands.
- [ ] Schedule dependency/API reviews and record compatibility decisions rather than blindly importing code from other projects.

Acceptance: performance changes include before/after measurements; maintenance checks catch realistic regressions without hiding hardware gaps.

## New suggestions: 61–80

- [ ] 61. Profile duplication with credentials excluded by default.
- [ ] 62. Per-profile visibility/template presets with a local preview.
- [ ] 63. Last-known-good config comparison before restoring a backup.
- [ ] 64. Native screen showing which configuration layer supplied each effective value.
- [ ] 65. Session-history repair report that previews discarded corrupt entries.
- [ ] 66. Local retention controls for history and diagnostics.
- [ ] 67. Metadata source provenance and last successful lookup timestamp.
- [ ] 68. Firmware capability report with tested/unknown/unsupported labels per subsystem.
- [ ] 69. Explicit update health-confirmation status and rollback availability.
- [ ] 70. Offline signed bundle import with verification details.
- [ ] 71. Exportable compatibility report containing build/SDK/firmware facts.
- [ ] 72. Guided recovery after repeated early installer failures.
- [ ] 73. Distinct indicators for detected game, queued presence, and acknowledged connection.
- [ ] 74. Preview the effect of disabling media, idle, or homebrew sharing.
- [ ] 75. Dependency/license inventory generated alongside release artifacts.
- [ ] 76. Binary-size trend report tied to source commits.
- [ ] 77. Reproducible outage scenario catalog for maintainers and testers.
- [ ] 78. Translation validation for missing keys and unsupported glyphs.
- [ ] 79. Documentation consistency checks for paths, config names, and version references.
- [ ] 80. A release evidence index linking each claim to tests or recorded console results.

## Decision log

- Planning remains the only authorized action for this request. The 1.1 branch contains committed gateway work and this plan; discarded workspace experiments are not deliverables.
- The master backlog remains open across releases; each shippable increment has a finite evidence gate.
- Excluded web dashboard, join/spectate buttons, and PS5 Remote Play presence are not in scope.
- Multiple account profiles require deliberate switching; rejected credentials must not trigger token cycling.
- Firmware support is established by evidence, not a numerical range in marketing copy.
- Delta updates, telemetry, and automatic offset discovery are research tracks until justified and validated.
- No feature is counted as delivered because it has a checkbox, proposal, or empty stub.


## Portable requirements matrix: all 19 PDF defects

This matrix preserves the actionable requirements from the PDF for cloud workers. Reported symptoms are evidence to investigate, not proof of the proposed cause.

| ID | Reported symptom | Required result and acceptance evidence |
|---|---|---|
| D-01 | Timer survives exits/reboots incorrectly | Define session boundaries; invalidate stale/future continuation; offer reset without reinstall; test exit/relaunch/reboot/switch |
| D-02 | Presence disappears after hours | Missing ACK/silent-peer recovery reconnects automatically; log disconnect cause/code; sustained console test |
| D-03 | Rest mode stops updates and includes sleep time | Re-establish network/presence and apply documented timer policy after wake; repeated console suspend cycles |
| D-04 | Stale lock prevents next launch | Dead ownership cannot permanently block boot; concurrent live launches remain excluded; stale/reused PID tests |
| D-05 | No stop mechanism | Bounded graceful stop during each daemon state; persist session and release resources; document recovery |
| D-06 | Installer CE-34878-0/no logs | Reproduce affected firmware, diagnose early failures, produce persistent logs, verify corrected PKG on device |
| D-07 | Experimental Discord-client PKG crashes | Exclude unvalidated client build from stable distribution; label any experimental artifact accurately |
| D-08 | Raw IDs/no artwork when remote metadata fails | Local metadata first, persistent cache, bounded remote fallback, generic art; offline cold-start test |
| D-09 | ELYS00001 unknown | Add authoritative mapping and generic fallback; regression fixture |
| D-10 | nanoDNS breaks GTA Online | Identify necessary exceptions from evidence; reversible minimal edits; GTA Online console test |
| D-11 | Presence lingers after shutdown | Explain server-observed expiry; clear gracefully where possible; expose only locally controllable timing, never promise control of Discord expiry |
| D-12 | Previously usable token rejected without reason | Specific local validation errors, distinct gateway rejection/transport states, recover on correction; no credential disclosure |
| D-13 | Installer token field ignores input | Working IME/controller flow with cancel/save/validation; true QR login only after feasibility proof |
| D-14 | Quoting/input formatting confusion | Normalize pasted token at entry; explain JSON syntax; never accept a malformed whole document silently |
| D-15 | DNS loopback without nanoDNS causes endless retries | Diagnose observed resolver failure and provide accurate nanoDNS hint; bounded retries and clear state |
| D-16 | Users cannot retrieve logs | Always attempt early capped/rotated logging; report writable log path and one-step retrieval guidance |
| D-17 | Loading path confusion | Supported loader instructions and observable startup diagnostics; do not falsely require a path for an injected payload |
| D-18 | GoldHEN loading differences | Detect capabilities where possible, publish tested combinations, explain unsupported loading routes without inventing a minimum version |
| D-19 | Expiring/untrusted download surface | Authorized versioned release with checksums/signatures and build identity; retain stable provenance |

## Executor contract: discover missing necessities and act on them

The engineer executing this plan is responsible for finding omitted requirements while investigating. A checklist is a starting inventory, not a limit on competent engineering.

- [ ] When implementation reveals a necessary option, recovery path, validation rule, migration, diagnostic, or test, add a concrete task and implement it in the same increment when tightly related.
- [ ] Record why it is needed, who benefits, its default, compatibility impact, resource/privacy cost, and how it is verified.
- [ ] Prefer a clear default over adding a switch for every internal detail. Expose controls when users have a real choice: privacy, network sources, sharing, notifications, updates, retention, or appearance.
- [ ] Fix closely related confirmed bugs rather than leaving a known broken dependency behind. Defer unrelated work with a reason and priority.
- [ ] Research uncertain platform/API behavior before building a feature around it; record supported capabilities and failure behavior.
- [ ] Never invent proof, silently narrow a requirement, mark a suggestion implemented, or add empty production stubs to satisfy a checkbox.

### User-control completeness audit

For every user-visible feature, explicitly answer these questions in its task record:

1. Should the user be able to enable/disable it? What is the safe, useful default?
2. Is it global, per profile, per title, or per session? Which scopes are actually needed?
3. Where is it configured: PKG UI, config JSON, CLI, or a temporary runtime control?
4. Does a change apply immediately, after reconnect, or after restart? Is that shown accurately?
5. What happens to already published presence, open connections, cached data, and pending jobs when disabled?
6. Does the setting survive upgrade and round-trip through the installer without dropping unknown fields?
7. What are invalid-value, missing-key, older-schema, and conflicting-override behaviors?
8. Does disabling it stop the relevant requests, writes, notifications, or sharing in reality?
9. Is the effective value visible in diagnostics with its source and any override explained?
10. Are docs, shipped defaults, daemon parsing, installer persistence, and behavioral tests consistent?

Required audit candidates include auto-update, each external metadata/art source, presence categories, elapsed timers, firmware display, privacy exclusions, notification level, debug logging, history retention, cache limits, profile selection, and any future optional collection/export feature.

### Configuration design decisions

- Keep old valid config files working; migrate only when necessary and preserve a recoverable original.
- Define one precedence order after auditing current behavior; document defaults → file → explicit runtime overrides, with nested/legacy key conflicts resolved consistently.
- Use typed validation and bounds; avoid silent truncation, arbitrary sleeps, unsupported fields, and hidden settings that the UI overwrites.
- Apply reload as a validated snapshot; failed edits keep the last-good state and produce an actionable sanitized error.
- Add no credential-bearing command-line option or diagnostic dump.
- Every enable/disable control needs a behavioral test that observes its effect, including transition from enabled to disabled.

## Implementation task packet template

Before coding each increment, fill this short packet in the progress record:

- Requirement IDs and observed failure/user benefit.
- Current source evidence, exact files and interfaces to change, and dependencies.
- Intended behavior, defaults, limits, persistence, cancellation, and recovery.
- Regression scenario and assertions; hardware evidence needed separately.
- Commands/gates to run and their expected outcomes.
- Documentation/config/UI changes required together.
- Result: commit, commands/exit status, console evidence, unresolved limitations, next priority.

The first execution packet must audit commit `3e692a0` and establish actual branch state. A prior local host/strict-scan pass exists in conversation history, but ASan was interrupted while compiling SQLite; neither is a substitute for fresh cloud results on the selected commit. Then prioritize reproduced account/data safety and runtime failures before feature expansion.

## Plan quality gate

- [ ] Every PDF ID appears in the matrix and has a concrete owning task.
- [ ] Every mandatory feature has defaults, user controls where useful, failure behavior, and acceptance evidence.
- [ ] Every optional research track has a feasibility gate and can be rejected with an explicit reason.
- [ ] No task depends on an inaccessible laptop path, unstaged file, discarded experiment, or undocumented secret.
- [ ] Hardware claims are separated from host simulation, source inspection, and successful builds.
- [ ] Exclusions remain enforced across new suggestions.
- [ ] The next executable increment is specific enough for another engineer to start without reconstructing the conversation.


## Accepted user features: controller-friendly customization

These 13 features are requested scope, not speculative suggestions. Reuse existing presets/privacy/template/history code where it is correct; each feature needs an actual PKG control, documented config representation, tested behavior, and migration compatibility.

| Feature | Required experience and acceptance |
|---|---|
| Per-game custom presence | Edit display name, message, and supported cover override by title ID; preview, reset, and persist; stale edits cannot overwrite other settings |
| Quick privacy toggle | Pause sharing while detection continues; clear prior presence, suppress queued publications, show paused state, and resume current title intentionally |
| Favourite presets | Built-in Minimal/Detailed plus named user styles; save/duplicate/delete/preview; exclude credentials from style exports |
| Custom idle message | Set home/idle text independently from game text; apply visibility and privacy policies consistently |
| Playtime page | Today, most-played titles, and recent sessions from bounded local history; disclose suspended/idle-time policy and allow history deletion |
| Session goals | Optional duration goal with one notification per threshold/session; no duplicate alerts after reconnect; disable/cancel from PKG |
| Game launch messages | Per-title saved text such as “Going for platinum”; publish only user-provided text without inferring achievements or game progress |
| Custom artwork picker | Browse/import supported local image files with size/format validation; explain that a console file path is not a Discord image URL. Resolve a supported delivery mechanism before promising that imported artwork appears remotely; retain default covers on failure |
| Presence preview | Show actual computed fields/assets/timer mode before saving; disclose approximate rendering and do not claim the server accepted a preview |
| Temporary status | Expire after a chosen duration or session end; define precedence over saved messages, clear on cancellation, and avoid resurrection after reboot |
| Hide selected games | Select titles in the PKG; exclude their identity/art/timer from Discord; separate this from explicit local history deletion preferences |
| Timer choices | Current game session, since PS4 startup, since daemon launch, or hidden; manual reset where meaningful, with clear labels and tests |
| Quiet mode | Suppress routine connection/success notifications; keep failures discoverable in native status/logs and separately configure actionable failure alerts |

### Timer contract: “do not reset until the PS4 restarts”

- [ ] Add an optional `console_session` timer mode: retain one PS4-session anchor across title switches, game closes, reconnects, and daemon reinjection within the same boot.
- [ ] Keep `game_session` as the backward-compatible default; offer `daemon_session` and `hidden` separately with accurate descriptions.
- [ ] Research a reliable boot identity/start-time source on supported firmware. Persisted wall time alone cannot distinguish reboot from daemon restart.
- [ ] If actual boot start cannot be determined, label the fallback “Since OrbisRPC started tracking this PS4 session”; never fabricate a boot timestamp.
- [ ] Reset console-session anchor on verified full reboot; never reuse an anchor from a previous boot or a future/corrupt timestamp.
- [ ] Offer an explicit rest-mode policy: include suspended time or exclude suspended time. Default to excluding it for activity duration; explain the choice in the UI.
- [ ] Separate display-timer anchors from per-game history accounting: a shared console timer must not count previous games toward the current game's statistics.
- [ ] Define manual reset as resetting the selected display anchor, with a confirmation that explains it no longer represents actual startup time; alternatively switch to a clearly labeled custom session anchor.
- [ ] Apply mode changes without losing history or resetting unrelated anchors; reconnects must preserve the selected anchor.
- [ ] Test A→B→home→A, close/relaunch, reconnect, reinjection, full reboot, rest-mode cycles, RTC correction, invalid persistence, privacy pause, and mode switching.

## GitHub issue audit: open and closed

Snapshot: 2026-11-09. Enumerated all issue pages with `state=all`, excluding pull requests: **40 issues**. Retrieved paginated issue comments, including the title-request discussion. Linked attachments, external research documents, Discord conversations, videos, and hardware logs need their own inspection before their contents support a fix claim. Refresh this inventory when execution resumes.

Closed state is not proof of repair. Distinguish closed-as-fixed, closed-as-documented, redirected support, triage cleanup, duplicates, and unreproduced reports. Match claimed fixes to current source, tests, release artifacts, and console evidence.

| Issue | Snapshot state | Subject |
|---|---|---|
| [#1](https://github.com/SirHumza/orbisRPC/issues/1) | closed | Crashes on launch |
| [#2](https://github.com/SirHumza/orbisRPC/issues/2) | closed | Daemon crashes on launch on all firmwares (9.00, 12.50, 13.52) |
| [#3](https://github.com/SirHumza/orbisRPC/issues/3) | closed | "Text input failed. Skipping." — token dialog fails, can't enter Discord token |
| [#4](https://github.com/SirHumza/orbisRPC/issues/4) | closed | Payload Guest crashes when launching orbisrpc on 12.50+ — use GoldHEN Settings instead |
| [#5](https://github.com/SirHumza/orbisRPC/issues/5) | closed | Installer auto-runs in a loop after completing — should hand off to daemon |
| [#6](https://github.com/SirHumza/orbisRPC/issues/6) | closed | README: add Discord token security warning + manual token entry instructions |
| [#8](https://github.com/SirHumza/orbisRPC/issues/8) | closed | Packaging mismatch vs Apollo: CATEGORY=gd, ATTRIBUTE=0, missing --authinfo — likely causes launch crash |
| [#9](https://github.com/SirHumza/orbisRPC/issues/9) | closed | Updater validates only ELF magic bytes (64 bytes) — malicious binary can pass health checks |
| [#10](https://github.com/SirHumza/orbisRPC/issues/10) | closed | Update mechanism has no TLS cert pinning and accepts relative redirects — MITM surface |
| [#11](https://github.com/SirHumza/orbisRPC/issues/11) | closed | docs/injecting.md references broken elfldr/payloader that segfaults |
| [#12](https://github.com/SirHumza/orbisRPC/issues/12) | closed | docs/TROUBLESHOOTING.md and README use inconsistent GoldHEN menu names |
| [#13](https://github.com/SirHumza/orbisRPC/issues/13) | closed | kinfo_proc layout assumptions (offset 447, recsz>=479) are fragile across firmware versions |
| [#14](https://github.com/SirHumza/orbisRPC/issues/14) | closed | build.sh standalone daemon missing -DORBISRPC_SDK_PAYLOAD, uses wrong detection path |
| [#15](https://github.com/SirHumza/orbisRPC/issues/15) | closed | Firmware-support bug: payload build skips ShellCoreUtil, relies only on fragile sysctl scanning |
| [#16](https://github.com/SirHumza/orbisRPC/issues/16) | closed | No runtime capability probing — daemon assumes one fixed system-module set on every firmware from 9.00 to 13.52 |
| [#17](https://github.com/SirHumza/orbisRPC/issues/17) | closed | No watchdog — a single daemon crash ends presence silently with no auto-restart or user feedback |
| [#18](https://github.com/SirHumza/orbisRPC/issues/18) | closed | Release process — fixes on main never reach the published Setup PKG |
| [#19](https://github.com/SirHumza/orbisRPC/issues/19) | closed | Two divergent build paths with no parity check (scripts/build.sh vs scripts/build_sdk.sh) |
| [#20](https://github.com/SirHumza/orbisRPC/issues/20) | closed | Discord token stored plaintext in /data/orbisRPC/config.json — readable via FTP from any device |
| [#21](https://github.com/SirHumza/orbisRPC/issues/21) | closed | Presence runs but never appears in-game — game detection false-negative with no diagnostics |
| [#22](https://github.com/SirHumza/orbisRPC/issues/22) | closed | No support path — crashing users cannot share log.txt, klog, or firmware info |
| [#23](https://github.com/SirHumza/orbisRPC/issues/23) | closed | Any transient network or sysctl error aborts the daemon — no retry or backoff |
| [#24](https://github.com/SirHumza/orbisRPC/issues/24) | closed | Focus detection via klog AppFocusChanged events (community proposal) |
| [#25](https://github.com/SirHumza/orbisRPC/issues/25) | closed | 13.52 |
| [#26](https://github.com/SirHumza/orbisRPC/issues/26) | closed | CE-34878-0 |
| [#27](https://github.com/SirHumza/orbisRPC/issues/27) | closed | 13.52 (CE-34878-0) |
| [#37](https://github.com/SirHumza/orbisRPC/issues/37) | open | 🎮 Missing game/homebrew title? Post it here (no PR needed) |
| [#39](https://github.com/SirHumza/orbisRPC/issues/39) | closed | Discord rich presence status not displaying |
| [#40](https://github.com/SirHumza/orbisRPC/issues/40) | closed | NanoDNS setup/load order confusion |
| [#41](https://github.com/SirHumza/orbisRPC/issues/41) | closed | DNS/clock or internal-check failure suspicion |
| [#42](https://github.com/SirHumza/orbisRPC/issues/42) | closed | Discord account security emails (ToS concern) |
| [#43](https://github.com/SirHumza/orbisRPC/issues/43) | closed | Payload beta stability concern blocking auto-run |
| [#44](https://github.com/SirHumza/orbisRPC/issues/44) | closed | Settings header broken |
| [#45](https://github.com/SirHumza/orbisRPC/issues/45) | closed | Crash on launch (FW 13.52, klog attached) |
| [#46](https://github.com/SirHumza/orbisRPC/issues/46) | closed | Error on 13.02 (new-version implementation pending) |
| [#49](https://github.com/SirHumza/orbisRPC/issues/49) | open | Bug |
| [#53](https://github.com/SirHumza/orbisRPC/issues/53) | closed | Super Mario wonders |
| [#54](https://github.com/SirHumza/orbisRPC/issues/54) | closed | Skate 3 \| XeMoisties |
| [#55](https://github.com/SirHumza/orbisRPC/issues/55) | open | Discord logs me out on a device I took the token from after a while |
| [#56](https://github.com/SirHumza/orbisRPC/issues/56) | open | Investigations... |

### Findings that must affect execution

- [ ] **#1/#2/#4/#25–27/#45/#46:** preserve device-specific launch/crash regressions, including 9.00, 11.00, 13.02, 13.04, and 13.52 reports. Support redirection or cleanup closures do not establish a console fix.
- [ ] **#3/#6:** test IME input and manual recovery; token safety copy must accurately describe every network recipient and storage limit.
- [ ] **#5:** prove installer and daemon identities/paths cannot be confused in autorun or handoff; do not introduce an installer relaunch loop.
- [ ] **#8:** verify packaging field semantics from authoritative sources and actual builds; do not copy another app's authority/flags blindly.
- [ ] **#9:** validate full executable bounds and reject malformed/overflowed manifest asset lists; a magic-byte check alone is insufficient.
- [ ] **#10:** correct the threat model: an intercepted connection does not forge a valid signature without the signing key. Verify independent signing, TLS hostname checking, and redirect policy; evaluate pinning with an operational rotation plan rather than assuming it is required.
- [ ] **#11/#12:** old loader instructions conflict with current contributor constraints. Follow verified current loading routes and preserve the explicit prohibition on recommending Payload Guest.
- [ ] **#13–16/#19/#21/#23/#24:** validate detection APIs, build-context parity, runtime capability fallback, transient-error recovery, and focus transitions. Klog focus parsing remains a measured research option, not an assumed replacement.
- [ ] **#17:** distinguish an internal retry loop from a watchdog capable of restarting a dead process; require console proof for either claim.
- [ ] **#18:** build published artifacts from their exact tag; verify the PKG embeds the same payload/config represented by release notes.
- [ ] **#20/#22:** private file permissions are not a guarantee against privileged anonymous FTP; document limits and provide secret-free support exports.
- [ ] **#37/#53/#54:** reconcile every submitted title, including submissions without art. A missing cover must not prevent adding a verified display name. Track later corrections such as CUSA52463 → Deltarune; never guess names from unknown-title auto-reports.
- [ ] **#37:** prefer durable art sources over expiring attachment URLs; preserve provenance without copying reporter account identities into shipped metadata.
- [ ] **#39–43:** reproduce connected-but-invisible presence separately from DNS failure, clock failure, and account challenges. Documentation closures do not replace runtime diagnosis; stable startup guidance must reflect proven loader order.
- [ ] **#44:** add a native Settings header/layout/navigation regression check; cleanup closure contains no proof of a repaired UI.
- [ ] **#49:** add detection fixtures and console checks for the named no-trophy-directory apps (Cyber TV, RetroArch, pFBN, EmuSuNi, Vue, Netflix, SMB/FTP/WebDAV clients, Remote PKG installer, Cheats Manager, ezRemote, Itemzflow, PS4PKGInstaller). Do not assume trophy directories exist; do not recommend Payload Guest as a loader because it appears in this app list.
- [ ] **#55/#56:** reconcile contradictory token-refresh claims using protocol evidence and actual branch behavior. Issue #56 is static research with explicit open questions, not live proof that a fingerprint, capability value, native login, or rotation policy prevents security challenges. Validate legitimate credential lifecycle/recovery without promising account safety or implementing enforcement evasion.

Each item gets an issue-to-task mapping, current source evidence, regression test, and console verification requirement where applicable. Record why any proposed fix is rejected or replaced; retain the original user symptom in the regression ledger.
