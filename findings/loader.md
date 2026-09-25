# Loader behavior (observed on PS4 9.00, GoldHEN 2.4)

## The three loaders

| Loader | Port | Behavior observed |
|---|---|---|
| GoldHEN settings-page payloader | 9090 (transient) | Accepts bytes, prints "payload launched successfully", then its own `GoldHENLoader` thread SIGSEGVs (null read). Payload never executes. Reproduced with 90KB hello-world, v0.4.0, 1.0 daemon. |
| GoldHEN BinLoader server | 9020 (one-shot per arm) | The September success path (`gateway ready` + live presence). Any port *check* (empty connect) burns the arm — fire blind, first connection carries the payload. |
| elfldr (ps4-payload-dev v0.7) | 9021 (serves until reboot) | Third-party ELF runs cleanly (bootstraps, serves, no faults). Our OpenOrbis-linked binaries die silently inside it. |

## Proven facts

- The 9090 segfault is in GoldHEN's loader thread, not the payload:
  `signal 11 (SIGSEGV), thread GoldHENLoader, fault address 0`.
- ELF support in the settings-page loader needs GoldHEN ≥ v2.4b18.5;
  older builds cannot load ELFs at all.
- 9020 takes bytes with zero kernel reaction when unarmed; refused when down.
- FTP (2121) and klog (3232) are the observation channels; see capture.md.

## Open

- Whether a newer GoldHEN (v2.4b18+) fixes the 9090 segfault.
- The exact GoldHEN build on the test console (About screen).
