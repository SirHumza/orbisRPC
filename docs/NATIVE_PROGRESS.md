# Native branch 1.1 work

## 2026-11-09

Base commit: `e5dd5b8` on branch `1.1`.

### Changes in progress

- Added 22 community title mappings from reported PS4 homebrew and game IDs.
  The list now has 34 unique IDs. Names are local data; remote artwork has not
  been checked on console.
- Fixed custom-title extraction to parse bounded JSON objects. A title without
  a cover no longer borrows the next title's cover. Added bounded-buffer,
  incomplete-object, whitespace, and field-order regressions.
- Hardened config loading so malformed files preserve the last good config;
  oversized known strings and out-of-range numeric fields fail safely. Invalid
  optional title overrides continue to be ignored.
- Kept a failed token rotation's original disk-token lineage across repeated
  READY rotations. A later QoS save can persist the newest token and matching
  QoS token together without restoring an obsolete credential.
- Factored pending-rotation reconciliation into the daemon's tested decision
  helper: a failed-save poll retries while disk still has the original token,
  and a manual disk edit discards the pending server value. The test does not
  inject real filesystem write failures.
- After an explicit gateway 4004 rejection, the daemon waits for the configured
  token to change instead of repeatedly identifying with the rejected value.
  Credential copies and gateway capture buffers are cleared when a session
  ends; the live copy remains only while it is needed for presence requests.
- Tightened gateway envelope validation, integer parsing, oversized READY
  handling, and first-heartbeat ACK timing. READY is the only event that can
  promote captured auth and QoS values. IDENTIFY includes QoS only when one is
  present; no undocumented client fingerprint fields were added.
- Rejected all undefined WebSocket opcodes, including reserved control opcodes
  `0xB` through `0xF`, with offline checks for RSV bits, masking, fragmentation,
  control-frame limits, and 64-bit length overflow. Compared this path against
  [RFC 6455](https://www.rfc-editor.org/rfc/rfc6455) and the current
  [discord.py gateway implementation](https://github.com/Rapptz/discord.py/blob/master/discord/gateway.py).
  Discord's documented Resume flow uses application Gateway session metadata;
  it is not assumed to support this console user-token connection.
- Fixed control-frame dispatch for zero-length frames: empty CLOSE now tears
  down immediately, empty PING gets one PONG, and a no-data poll cannot reuse
  the previous opcode and send PONG repeatedly. PONG write failure now forces
  reconnect. PONG replies now echo the PING payload and use an offline-tested
  masked control-frame encoder; auth-time PONG write failures also close the
  session cleanly.
- Validated CLOSE payload lengths, status-code ranges/reserved values, and
  UTF-8 reasons in both authentication and active-session dispatch. Empty
  CLOSE remains valid and disconnects without a status; code 4004 is preserved
  for invalid-token handling.
- Added offline gateway, token-lineage, and installer settings regression
  programs to the host test Makefile. The test target now runs the installer
  config test every time, even when its binary is already built.
- Improved setup diagnostics, startup failure exits, token edit behavior, and
  font face cache cleanup. Rewrote the README to remove inaccurate claims and
  explain the actual install path and token risk.

### Verification

- `make -C tests test`: passed after correcting optional title override
  handling. Includes existing utility tests and the new gateway, token lineage,
  persistence-decision, frame-header, and installer safety tests.
- `make -C tests asan`: passed with ASan/UBSan, including the new standalone
  fragment, gateway, token-lineage, and installer safety tests.
- `ORBISRPC_STRICT_SECRETS=1 python3 tests/e2e_consumer.py`: passed, including
  the built ELF linkage gate and forbidden app-world library check.
- SDK `573b4a0` built from source and passed its 13.50/13.52 source gate.
  `make all` produced the daemon ELF and a 10 MiB setup PKG, including the
  pending-rotation follow-up. The generated GP4
  manifest includes `orbisRPC.elf`, config, and all three FreeType fonts.
- `git diff --check`: passed.
- No console test was run. Build success does not prove the PKG launches or that
  Discord accepts the account token on PS4.
- The gateway comparison is source-level only. Issue #56's claim that
  IDENTIFY capabilities are missing is stale for this branch's current payload
  shape; the issue does not establish a production fix for Discord account
  challenges. No impersonation fields were copied from another client.

### Remaining release gates

- Exercise repeated READY credential rotations with an actual gateway account;
  offline tests cover capture and token lineage but cannot prove Discord's
  server behavior or prevent account challenges.
- Build the SDK payload and installer PKG, inspect linkage and packaged assets,
- Test install, update, settings save, daemon reconnect, and uninstall on
  hardware. Firmware claims require recorded console evidence.
- Exercise normal and oversized fragmented gateway frames on a scripted
  transport; current tests cover the pure bounded assembler and frame-header
  validator, not socket framing or chunk-boundary integration.
- Work through the remaining accepted items and issue matrix in `plan.md`;
  none should be described as shipped based only on the plan.
