# THE BIGGEST PLAN EVER — OrbisRPC living master plan

**Goal:** Ship a reliable PS4 daemon and setup app with recoverable authentication, accurate presence, safe updates, useful diagnostics, and demonstrated console stability.

**Architecture:** Keep the daemon in C with bounded state and explicit ownership of sockets, files, buffers, and persisted records. Keep the installer responsible for setup and recovery. Share small parsing/validation rules where practical; avoid speculative frameworks.

**Stack:** ps4-payload-sdk built from pinned git source, OpenOrbis installer, vendored mbedTLS and SQLite, existing host tests and GitHub Actions.

**Specifications:** All 19 defects in `/Users/mac/Desktop/PS4/orbisRPC-1.1-fixes.pdf`, the user's daemon/PKG/QR/auto-update requests, and `AGENTS.md`.

Implementation is paused for plan review. Existing edits are candidates, not proof of completion. Execute tasks in order; keep the full scope open until its evidence exists.

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

- [ ] B01 Reject embedded NULs in token/config strings rather than validating only a prefix.
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
- [ ] B18 Reject invalid RSV bits, opcodes, continuation ordering, control-frame lengths, and length overflows.
- [ ] B19 Handle short writes, TLS WANT_READ/WANT_WRITE, EINTR, peer EOF, and close during a write without spinning.
- [ ] B20 Ensure a peer that sends partial data forever cannot extend a deadline indefinitely.
- [ ] B21 Verify close-code parsing with empty, one-byte, malformed, and oversized close payloads.
- [ ] B22 Treat missing heartbeat ACKs independently of unrelated incoming traffic.
- [ ] B23 Reset connection-specific heartbeat, sequence, compression, and parser state on each new socket.
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

Current instruction: **planning only**. Implementation, builds, releases, and external writes remain paused until execution is requested. A living backlog can continue growing while individual releases have finite acceptance gates.

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

Previously written updater code has not passed the full acceptance gate. Its staging notification, rollback-generation handling, startup blocking, and ELF validation require review before enablement.

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

- Planning remains the only authorized action for this request; earlier implementation edits are preserved.
- The master backlog remains open across releases; each shippable increment has a finite evidence gate.
- Excluded web dashboard, join/spectate buttons, and PS5 Remote Play presence are not in scope.
- Multiple account profiles require deliberate switching; rejected credentials must not trigger token cycling.
- Firmware support is established by evidence, not a numerical range in marketing copy.
- Delta updates, telemetry, and automatic offset discovery are research tracks until justified and validated.
- No feature is counted as delivered because it has a checkbox, proposal, or empty stub.
