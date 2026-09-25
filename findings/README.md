# Findings archive

Hardware-grounded research from bringing OrbisRPC up on a real PS4
(9.00, GoldHEN 2.4). Written for humans and for AI assistants joining
later: every claim below was observed on-console unless marked THEORY.

- [loader.md](loader.md) — GoldHEN payloader vs BinLoader server vs
  elfldr: behaviors, crashes, one-shot rule, ports.
- [elf-linkage.md](elf-linkage.md) — why OpenOrbis-linked binaries die
  instantly in spawned processes; NEEDED/UND analysis; SDK port status.
- [tls-ime.md](tls-ime.md) — mbedTLS 3.x PSA failure on-console (fixed),
  IME dialog init failures (open), exact error codes seen.
- [capture.md](capture.md) — how to observe: klog, FTP paths, ports,
  screenshot locations, what each log proves.
