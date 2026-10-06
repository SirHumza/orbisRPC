# orbisRPC docs

Start at the repo [README](../README.md) (install, support, config basics).
This folder is the deep end: design, building, and hardware research.

## For testers

- [TROUBLESHOOTING.md](TROUBLESHOOTING.md) — every known failure, symptom
  first: no presence, `?` tile, token rejected, payload won't start, DNS.
- [CONFIG.md](CONFIG.md) — every `config.json` key, defaults, what the
  `show_*` flags actually hide.

## For developers

- [DAEMON.md](DAEMON.md) — loop, states, detection, names, art, sessions.
- [BUILDING.md](BUILDING.md) — toolchain, SDK-from-git rule, scripts, CI
  jobs, host tests.
- [INSTALLER.md](INSTALLER.md) — Setup PKG design (ships with the official
  release, not with test builds).
- [injecting.md](injecting.md) — getting payloads onto the console,
  loaders, the one-shot rule.
- [PRODUCTION.md](PRODUCTION.md) — production-readiness report: what was
  verified on hardware, what is still open.

## Hardware research archive

[research/](research/) — findings earned on a real console. Every claim was
observed unless marked THEORY.

- [research/loader.md](research/loader.md) — the three loaders, behaviors,
  crashes, ports.
- [research/elf-linkage.md](research/elf-linkage.md) — why app-world-linked
  binaries die in spawned processes; the daemon-world rule.
- [research/tls-ime.md](research/tls-ime.md) — TLS 1.2 ceiling (PSA entropy
  failure), presence-display rules, IME status.
- [research/capture.md](research/capture.md) — observing the console: klog,
  FTP paths, proof markers, rules learned the hard way.
- [research/sdk-13.52.md](research/sdk-13.52.md) — the SDK CRT firmware gate
  and why the SDK must be built from git.
- [research/proc-table-offsets.md](research/proc-table-offsets.md) — from
  offset heuristics to asking the OS; why detection uses BigApp + TitleId.
- [research/retired-tables/](research/retired-tables/) — retired per-title
  art-table tooling, kept for history.
