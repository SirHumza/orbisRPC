# Native execution queue

The acceptance criteria and full issue/feature backlog live in [`plan.md`](plan.md).
This file tracks the next concrete work; completion is based on evidence in
[`checkbox.md`](checkbox.md), not on code being present.

## Current pass

- [x] Inspect the current `1.1` branch and preserve its dirty work.
- [x] Add bounded custom-title extraction and cover-isolation regression cases.
- [x] Fix pending READY token rotation lineage across failed disk writes.
- [x] Stop reconnecting with an unchanged credential after explicit 4004.
- [x] Clear gateway token captures and live credential copies when a session ends.
- [x] Add offline READY/IDENTIFY and token-lineage tests.
- [x] Reject reserved WebSocket control opcodes through the shared tested frame
  validator; RFC-level frame tests cover sequencing and bounds.
- [x] Process empty CLOSE/PING control frames before the no-data sentinel and
  reset frame outputs per poll to prevent stale-opcode loops.
- [x] Echo WebSocket PING payloads in masked PONG replies and reconnect if the
  reply cannot be sent.
- [x] Validate WebSocket CLOSE status codes and UTF-8 reasons before applying
  Discord's invalid-token handling.
- [x] Keep heartbeat timeout independent of unrelated Gateway traffic.
- [x] Verify fresh IDENTIFY resets prior Discord session and parser state.
- [x] Reject escaped NULs at daemon and installer config parsing boundaries.
- [x] Reject ambiguous duplicate token and QoS config keys; explicit token edits repair duplicates and invalidate old QoS state.
- [x] Reject credentials that cannot fit destination buffers; never truncate token-loader output.
- [x] Normalize pasted token boundaries without changing internal bytes; accept a leading config BOM in both readers.
- [x] Declare C++11/POSIX installer build requirements and use the POSIX page-size query.
- [x] Reject invalid JSON number syntax and overflow before installer integer conversions; preserve the daemon's last-good config on invalid numeric input.
- [x] Align installer toggle loading with daemon boolean-only config semantics.
- [x] Verify installer read-modify-write preserves unknown nested config and newer token state.
- [x] Make malformed optional title overrides non-fatal again.
- [x] Rewrite README claims to match the current source and installer flow.
- [x] Fix macOS package tool selection and gate the setup app's font assets.
- [x] Review the changed implementation and documentation; remove generated test artifacts.
- [x] Run host suite, ASan/UBSan suite, and strict secret/e2e checks on this tree.
- [x] Build the SDK payload and setup PKG; inspect ELF imports and packaged files.
- [x] Run final checks after the last source/documentation updates.
- [x] Commit validated work and push branch `1.1` to GitHub.

## Remaining project work

- [ ] Complete token persistence fault tests, account rejection recovery behavior,
  secret redaction review, and record the limits of console user-token auth.
- [x] Add bounded memory assembly for normal WebSocket text fragmentation; the
  pure assembler test passes. Full socket framing and interleaved control-frame
  coverage remains outstanding.
- [ ] Test invalid UTF-8/JSON, close codes, and arbitrary socket chunk boundaries.
- [ ] Finish lifecycle/timer coverage for game transitions, reboot, suspend,
  resume, RTC changes, daemon restart, and privacy controls.
- [ ] Audit metadata and artwork sources, all open/closed issue reports, and the
  PDF matrix; verify mappings and detection on hardware.
- [ ] Finish installer recovery, config-preservation, asset/package gates,
  IME behavior, update rollback, and uninstall tests.
- [ ] Review update signing, manifest bounds, TLS/redirect behavior, and key
  rotation before calling updates safe.
- [ ] Implement accepted user-facing settings only after their scope and storage
  model are clear: custom presence, privacy pause, presets, idle text, playtime,
  session goals, launch text, artwork selection, preview, temporary status,
  hidden titles, timer modes, and quiet mode.
- [ ] Investigate official QR authorization feasibility; do not label a setup
  or support QR as a Discord login flow.
- [ ] Record exact hardware results by firmware and release build. Do not claim
  production stability until these gates pass.
