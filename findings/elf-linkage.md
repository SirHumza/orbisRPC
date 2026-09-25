# ELF linkage: why OpenOrbis binaries die in spawned processes

## Observed

- elfldr.elf (ps4-payload-sdk build): runs, serves, no faults.
- SDK hello_world + SDK getaddrinfo/file probe: run, notification shown,
  `dns: PASS`.
- Every OpenOrbis-linked binary (hello 90KB, v0.4.0, 1.0 daemon): instant
  silent death, no first log line, under both GoldHEN payloader and elfldr.

## Structural comparison (llvm-readelf, verified locally)

| | working (elfldr/sdk) | dying (ours) |
|---|---|---|
| NEEDED | libkernel_web.sprx, libSceLibcInternal.sprx, libSceNet.sprx | libkernel.so + app-world `.so` set |
| Segments | merged RWE LOADs, no RELRO | split R-X/RW, GNU_RELRO, GNU_STACK |
| Hash | GNU_HASH + SYSV HASH (both have both) | same |
| Relocations | GLOB_DAT / JUMP_SLOT / RELATIVE only | same families |

elfldr spawns via `rfork_thread(RFPROC|RFCFDG|RFMEM)` then resolves each
import and SIGKILLs on the first unresolvable one — silent by design.
Our 89 undefined imports (vs ~26 working) include `libkernel.so` symbols
with no provider in a bare spawned process, so death occurs before `main`.

## Consequence

The daemon must be rebuilt against daemon-world linkage
(ps4-payload-sdk: `libkernel_web` + internal libc + `libSceNet`, BSD
sockets instead of Sony `orbis/Net.h`). Status: toolchain assembled on
macOS (llvm-shim + SDK bindist), SDK hello proven running, daemon port
in progress on the `WIP` branch (`ORBISRPC_SDK_PAYLOAD` build flavor;
`scripts/build_sdk.sh`). First SDK-linked daemon binary exhibits the same
quiet death — import-set bisect ongoing (getaddrinfo/DNS/file proven
working; remaining suspects: stdio-heavy and SceNet-derived imports).

## Ruled out

- PIE vs EXEC (both crash), SELF vs ELF (page wants ELF), size (90KB dies),
  hash style, relocation families, segment permissions.
