# RPC implementation comparison — 2026-11-08

Scope: planning for clean branch 1.1, console-only PS4 daemon. No product code imported. Repository availability and source patterns do not establish long-session account safety.

## Primary sources inspected

- [Discord RPC README](https://github.com/discord/discord-rpc): C++ library communicating with a locally running desktop Discord client; not a direct Gateway authentication replacement. MIT licensed.
- [Discord RPC implementation](https://github.com/discord/discord-rpc/blob/master/src/discord_rpc.cpp): queues the latest presence, requeues failed writes, reposts on connection, and resets exponential backoff on success. Adapt behavior only where absent from orbisRPC; no need to port desktop IPC or its thread model.
- [Cordless README](https://github.com/Brendonm17/Cordless): MIT C homebrew client for PS Vita, direct REST/Gateway via vendored mbedTLS and SceNet. Closest platform comparison found.
- [Cordless Gateway implementation](https://github.com/Brendonm17/Cordless/blob/main/src/gateway/gateway.c): stores session ID and resume URL, sends opcode 6 RESUME, stops retries after 4004, handles server heartbeat requests, and jitters the initial heartbeat. The inspected file search found no auth_token handling; this is not proof about every file or release. Its behavior is reference material, not a complete verified solution.
- [Abaddon](https://github.com/uowuo/abaddon): C++ direct Discord client. GPL-3.0; do not copy code under current AGENTS.md instructions. Its README explicitly does not promise perfect client fidelity or freedom from flags.
- [Investigation issue 56](https://github.com/SirHumza/orbisRPC/issues/56): reports static evidence that official READY handling persists auth_token. The issue explicitly says no live account testing occurred. Rotation/revocation causality remains to be demonstrated for the reported failure.

## Plan implications

1. Treat approximate elapsed time as an observation, not a fixed expiry.
2. Normal desktop Discord running concurrently is a required test scenario.
3. Reproduce credential replacement through the actual Gateway message path, including large READY frames, then test safe persistence and subsequent authentication.
4. Compare resumption, heartbeat, retry, and presence queue behavior with existing implementations before adding abstractions or dependencies.
5. Test correctness separately from account intervention: no claim that heartbeat fixes, headers, or copied fingerprints prevent password-reset challenges.
6. Before importing any permissively licensed code, pin the source revision and retain required notices. Do not add GPL code or a project LICENSE.

## Limits

This is a source comparison, not a live soak result. Source links track branches and must be pinned before reuse. No accounts, credentials, or console sessions were used. No product changes or tests were performed for this note.
