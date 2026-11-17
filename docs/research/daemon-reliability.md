# Daemon reliability pass, 2026-11-09

## Comparisons

- [Discord's C++ RPC client](https://github.com/discord/discord-rpc/blob/master/src/discord_rpc.cpp)
  re-arms queued presence on connection and resets reconnect backoff after READY.
  OrbisRPC already resets backoff; Settings/browser presence now also re-arms.
- [PS3 Presence gateway](https://github.com/Nyxnix/ps3-presence/blob/master/discord/gateway.c)
  handles requested heartbeats separately from its outstanding ACK deadline.
  OrbisRPC now answers requests immediately without refreshing an existing ACK window.
- [Discord Gateway documentation](https://github.com/discord/discord-api-docs/blob/main/developers/events/gateway.mdx)
  specifies immediate replies to opcode 1. Both authentication and live-session
  paths now handle this request through the same heartbeat sender.

These are behavioral comparisons. No third-party source was copied. The C++
RPC library uses local IPC; that transport does not replace console Gateway auth.

## Fixes and evidence

- Requested heartbeats work before READY and during live sessions; scripted tests
  cover both, including a HELLO with an out-of-range heartbeat interval.
- Reconnect and supervisor retries reset Settings/browser presence state.
- Session restoration validates finite, integral, bounded timestamps and exact
  uppercase alphanumeric title IDs before any integer conversion.
- Failed WebSocket connects release descriptor zero. Oversized HTTP upgrade
  requests fail before sending; loopback tests cover both paths.
- Full host and ASan/UBSan suites pass, including the new transport regression.
  The SDK payload builds and links using the existing firmware gates.

Live console reconnect behavior and sustained Discord account sessions remain
unverified. These checks do not establish that account challenges are eliminated.
