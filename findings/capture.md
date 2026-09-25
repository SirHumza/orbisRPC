# Observing the console (for assistants)

## Channels

| Channel | Address | Use |
|---|---|---|
| FTP | `192.168.1.136:2121` (anonymous) | files up/down |
| Kernel log | `192.168.1.136:3232` (TCP stream) | loader events, faults, crashes |
| BinLoader server | `192.168.1.136:9020` (one-shot per arm) | inject; NEVER probe first — an empty connect burns the arm, then fire blind |
| elfldr (if running) | `192.168.1.136:9021` | third-party ELF test harness |

## Key paths (FTP)

- `/data/orbisRPC/log.txt` — daemon log. Absent = payload never ran.
- `/data/orbisRPC/app.log` — setup-app + transport logs.
- `/data/orbisRPC/screen.log` — every dialog as text + answers.
- `/data/orbisRPC/diag.txt` — sanitized report (token structurally excluded).
- `/data/GoldHEN/payloads/` — autoloaded at jailbreak.
- `/data/payloads/` — Payload Guest scan dir.
- `/data/pkg/` — PKG staging.
- `/user/av_contents/photo/NPXS20001/<TITLE>/*/*/*.jpg` — screenshots.
- `/user/data/sce_coredumps/` — crash dumps.

## Proof markers

- Daemon alive: `log.txt` exists with `daemon start` line.
- Network proven: `tls: established (TLSv1.2)` in app.log.
- September success marker: `gateway ready` + `presence:` lines.
- Loader segfault signature: `signal 11 (SIGSEGV), thread GoldHENLoader`,
  fault address 0, right after `payload launched successfully`.

## Rules learned the hard way

1. Never port-check 9020/9090 before sending — fire blind on tap.
2. Klog is a live firehose: capture across the send window, not after.
3. `log.txt` absent always means "never executed", never "crashed later".
4. Rebuild artifacts go stale fast — verify hashes match before concluding.
5. **Never FTP-upload `.prx`/SELF files**: the console-side path
   deterministically converts SELF containers to raw ELF on write
   (verified: 1,476,032-byte PRX repeatedly lands as a 1,719,072-byte
   ELF, different hash; random bytes and PKGs round-trip perfectly).
   Deliver plugins via PKG install only.
