# Verification and release checklist

## Current branch evidence

- [x] Branch is `1.1`, based on commit `e5dd5b8`.
- [x] `make -C tests test` passed after the current code edits, including
  installer config tests on every invocation and gateway/auth safety tests.
- [x] `make -C tests asan` passed with the new safety tests.
- [x] `ORBISRPC_STRICT_SECRETS=1 python3 tests/e2e_consumer.py` passed with the
  built ELF linkage gate enabled.
- [x] Token scanner finds no real credentials, private webhook URLs, or signing
  secrets in tracked or staged files.
- [x] `git diff --check` passed after the last source/documentation updates;
  generated test artifacts were cleaned before staging.

## Build and package

- [x] SDK source revision `573b4a0` and 13.50/13.52 firmware gates are recorded.
- [x] `make all` produced daemon ELF and setup PKG from the current working tree.
- [x] ELF dependencies contain only the four supported daemon-world libraries.
- [x] PKG manifest lists all required assets, fonts, and the newly built daemon
  ELF; `create-gp4` and PkgTool completed successfully.
- [ ] Installer install, update, settings save, cancellation, failure recovery,
  and uninstall are checked against a console.

## Discord and PS4 evidence

- [x] Offline tests cover READY-only promotion, auth/QoS extraction, nested
  decoys, and IDENTIFY QoS presence/absence. Full exact wire acceptance remains
  unverified against Discord.
- [x] Frame-header tests reject reserved opcodes, invalid RSV/masking bits,
  fragmented or oversized control frames, and invalid fragment ordering.
- [x] Verify PING payload forwarding and masked PONG frame encoding offline.
- [x] Dispatcher tests verify immediate empty-CLOSE handling and exactly one
  PONG for an empty PING, including a subsequent no-data poll.
- [x] CLOSE payload validation rejects one-byte payloads, reserved/out-of-range
  codes, malformed UTF-8 reasons, and payloads over the control-frame limit;
  empty CLOSE and Discord 4004 remain valid cases.
- [x] Offline decision-helper test covers retry while disk retains the origin,
  repeated rotations, and manual edits taking precedence. Actual filesystem
  write-failure injection remains unverified.
- [x] Tests and docs make no claim that undocumented gateway fields are
  accepted or that token rotation prevents account challenges.
- [ ] Gateway reconnect, heartbeat timeout, presence republish, and timer
  continuity are checked on a console.
- [ ] Firmware and GoldHEN versions are recorded for every hardware result.
- [ ] All known issues and PDF requirements have a linked code/test/evidence
  record or an explicit unresolved explanation in `plan.md`.

## GitHub handoff

- [x] Review the complete staged diff and confirm no user-owned changes were
  discarded.
- [x] Commit the validated changes on `1.1`.
- [x] Push `1.1` to `origin` and verify the remote commit SHA.
- [x] Report implemented work, test/build results, unverified behavior, and
  remaining risks without overstating release readiness.
