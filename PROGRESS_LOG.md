# OrbisRPC Migration Progress Log

**Started:** 2026-09-29
**Repo:** `C:\Users\user\Documents\GitHub\orbisRPC`
**SDK Reference:** `ps4-payload-sdk-reference` (local checkout, not committed)
**Old Project:** `PS4-Rich-Presence-for-Discord/goldhen-rich-presence-bin` (local, not committed)

---

## Session 1: Understanding the Codebase (2026-09-29)

### What we reviewed
- **orbisRPC** — complete self-contained Discord Rich Presence daemon running on PS4 via GoldHEN
- **Architecture:** Payload ELF → direct TLS WebSocket → Discord gateway (no PC client needed)
- **Key modules:** daemon.c (loop), detect.c (sandbox mount + eboot count), discord.c/ws.c/tls.c (gateway), art.c/tmdb.c (artwork), updater.c (self-update), cfg.c (config + self-learning titles)
- **Installer:** One-tap PKG that stages payload + evict.elf + config, prompts for Discord token

### Key differences from old project (goldhen-rich-presence-bin)
| Aspect | Old | orbisRPC |
|--------|-----|----------|
| Transport | Payload → TCP:8000 → Python PC → Discord IPC | Payload → TLS WebSocket → Discord Gateway |
| Detection | kern.msgbuf polling | /mnt/sandbox/TITLEID_000 + eboot count |
| Game names | Title ID only | Multi-source: config → app.db → SFO → TMDB |
| Artwork | None | mp: proxy via external-assets (Sony CDN) |
| Timer | PC-side | Console-side (SNTP), survives reconnects/restarts |
| Updates | Manual | Self-updating from GitHub with rollback |
| Installer | Manual ncat send | PKG (OrbisRPC-Setup.pkg) |

### Known issues (from HANDOFF.md)
1. Cover art shows "?" — mp: proxy works but asset propagation needs work
2. Stale title on fresh launches — cloud sync bulk-touches save mtimes
3. Game crashes (CE-34878-0) on some titles with daemon running
4. IME keyboard never opens (token pre-seeded via FTP)
5. Rest Mode wake/resume untested
6. Updater drill untested
7. Plugin path (PRX) untested — must ship in PKG

---

## Session 2: Removed Plugin/PRX Support (2026-09-29)

**Decision:** Keep payload-only, remove all PRX plugin code.

### Changes made
| File | Change |
|------|--------|
| `plugin/` | **Deleted entirely** |
| `orbisrpc/daemon.h` | Removed `fixed_game_name` param from `daemon_run()` |
| `orbisrpc/daemon.c` | Removed plugin path from health check targets; removed plugin-mode logic |
| `orbisrpc/main.c` | `daemon_run(NULL)` → `daemon_run()` |
| `orbisrpc/detect.h/c` | Removed "(plugin mode)" comments |
| `orbisrpc/tls.h` | Removed "and plugin processes" |
| `orbisrpc/ws.c` | Removed "(plugin_unload joins this thread)" |
| `orbisrpc/updater.h/c` | Removed plugin asset download/stage logic |
| `orbisrpc/updater_util.c` | Removed `updater_self_ok()` (SELF check for PRXs) |
| `orbisrpc/compat.c/log.c/sfo.c` | Removed "plugin" references |

---

## Session 3: Flat Binary Build (2026-09-29)

**Request:** Convert build to produce flat `.bin` for BinLoader one-shot injection (like old project).

### Change to `scripts/build_sdk.sh`
Added `objcopy -O binary` after linking:
```bash
"$CC" -o "$OUT/orbisrpc_sdk.elf" "$OUT"/*.o
"$SDK/bin/orbis-objcopy" -O binary "$OUT/orbisrpc_sdk.elf" "$OUT/orbisrpc_sdk.bin"
```

### Build command (run in WSL)
```bash
PS4_PAYLOAD_SDK=/mnt/c/Tools/ps4-payload-sdk-reference ./scripts/build_sdk.sh
```

### Fixed
- Line endings (CRLF → LF) on `scripts/build_sdk.sh` and `scripts/build_evict.sh`

### Caveats to test
1. **`.bss` handling** — flat binary drops `.bss`; large static buffers will crash
2. **SDK libc** — global constructors may not run in flat binary
3. **Relocations** — works if position-independent (`-fPIC`/`-fpie`)

---

## Correction (2026-09-29, after review)

Sessions 3 and 4 above went in the wrong direction. Fixed:

- **Wrong SDK.** `ps4-payload-sdk-reference` is Scene-Collective libPS4 (no libc, runtime-resolved functions), the SDK of the old goldhen-rich-presence-bin project. orbisRPC needs libc, BSD sockets, mbedTLS and sqlite, so it builds with the ps4-payload-dev SDK instead (`ps4-payload-sdk.zip`, see `.github/workflows/ci.yml`, job `sdk-payload`). The OpenOrbis toolchain is for PKG apps and does not provide the `orbis-clang` that `build_sdk.sh` calls.
- **Flat `.bin` reverted.** The daemon ELF is dynamically linked (CI gate requires NEEDED `libkernel_web.sprx`, `libSceLibcInternal.sprx`, `libSceNet.sprx`). `objcopy -O binary` discards the dynamic section and relocations, and BinLoader has no dynamic linker, so the flat file would most likely not run (inference, untested). The `objcopy` lines were removed from `scripts/build_sdk.sh`; it outputs `build-sdk/orbisrpc_sdk.elf` again. Line endings verified LF, `sh -n` passes.
- The ELF is meant for elfldr (port 9021 per the repo docs) or Payload Guest.

### Correct SDK install (WSL)
```bash
sudo apt update && sudo apt install -y clang lld llvm unzip curl
mkdir -p ~/ps4-payload-sdk
curl -L -o /tmp/sdk.zip https://github.com/ps4-payload-dev/sdk/releases/latest/download/ps4-payload-sdk.zip
unzip -o /tmp/sdk.zip -d ~/ps4-payload-sdk
export PS4_PAYLOAD_SDK=$HOME/ps4-payload-sdk/ps4-payload-sdk
ls $PS4_PAYLOAD_SDK/bin
cd /mnt/c/Users/user/Documents/GitHub/orbisRPC
./scripts/build_sdk.sh
```

### Open question
How to move features from the old kern.msgbuf payload (AppFocusChanged / Kill App close detection, Home/Settings screen states) into orbisRPC. To be decided after the ELF builds and runs via elfldr.

---

## Session 4: Toolchain Setup (2026-09-29)

**Goal:** Install OpenOrbis LLVM toolchain (`orbis-clang`, `orbis-objcopy`) in WSL for building the flat `.bin`.

### Attempted approaches
1. **Prebuilt release URL (404)** — `llvm-15.0.7` release not found on GitHub
2. **Build from source** — `OpenOrbis-PS4-Toolchain` repo only contains headers/stubs; `build-toolchain.sh` doesn't exist
3. **System clang + lld** — Ubuntu's clang 18 lacks FreeBSD/PS4 targets
4. **Prebuilt v0.5.4 release** — `toolchain-llvm-18.tar.gz` (LLVM 18 with FreeBSD targets) — **correct path**

### Commands to run in WSL
```bash
cd /opt
sudo wget https://github.com/OpenOrbis/OpenOrbis-PS4-Toolchain/releases/download/v0.5.4/toolchain-llvm-18.tar.gz
sudo tar -xf toolchain-llvm-18.tar.gz
sudo ln -sf /opt/toolchain-llvm-18/bin/clang /usr/local/bin/orbis-clang
sudo ln -sf /opt/toolchain-llvm-18/bin/llvm-objcopy /usr/local/bin/orbis-objcopy
sudo ln -sf /opt/toolchain-llvm-18/bin/ld.lld /usr/local/bin/orbis-ld
sudo ln -sf /opt/toolchain-llvm-18/bin/llvm-ar /usr/local/bin/orbis-ar
orbis-clang --version
```

### Environment setup
```bash
export OO_PS4_TOOLCHAIN=/opt/OpenOrbis-PS4-Toolchain
export PATH="$OO_PS4_TOOLCHAIN/bin/linux:$PATH"
```

### Then build
```bash
cd /mnt/c/Tools/ps4-payload-sdk-reference
make -C libPS4
cd /mnt/c/Users/user/Documents/GitHub/orbisRPC
PS4_PAYLOAD_SDK=/mnt/c/Tools/ps4-payload-sdk-reference ./scripts/build_sdk.sh
```

---

## Session 5: ELF vs flat bin on high firmware (2026-09-29)

- User reports ELF payloads do not work on high PS4 firmwares like 13.52 (cause not investigated). The repo docs (`docs/injecting.md`, `findings/loader.md`) say BinLoader 9020 and elfldr 9021 take the ELF, but that was only observed on 9.00 / GoldHEN 2.4.
- Context from web search: PS4 13.52 got a public WebKit jailbreak on 2026-09-19 and GoldHEN 2.4b18.11 support on 2026-09-20 (beta; other 13.xx builds planned later).
- Conclusion: a flat `.bin` (libPS4 style, like the old kern.msgbuf payload) is needed for this console.
- Extra risk: the daemon has only ever run on 9.00. `orbisrpc/detect.c` reads the process table with hard-coded offsets (name at 447, record size 479, pid at 72); these may differ on 13.52, so detection needs verifying even after a flat port.
- Approach agreed in principle: do NOT port the whole daemon at once. Grow the old flat payload into orbisRPC step by step, in a new folder, leaving the old payload and the repo daemon untouched.

### Step plan
1. Network probe: resolve `gateway.discord.gg`, plain TCP connect to :443 from a flat payload.
2. TLS handshake with mbedTLS (libc shims, /dev/urandom entropy, stack/heap size). Biggest risk.
3. WebSocket + Discord gateway (HELLO, IDENTIFY, heartbeats).
4. Presence from the old payload's detection, copied as-is (kern.msgbuf AppFocusChanged / Kill App logic, `closed_latch`, shared mmap buffer). DECIDED by user 2026-09-29: orbisRPC's `detect.c` (process-table offsets, save/atime scans) is NOT ported.
5. Names and art (SFO / pronunciation first, then TMDB over TLS, mp: proxy).
6. Persistence: config, session, timer.

Other flat-image constraints to watch: no relocations in a flat image, so static pointer tables (host allowlist, media table, protos) need rewriting or a small self-relocator; sqlite/appdb can be dropped (`appdb_title` is already commented out in `detect.c`).

### Decisions
- Port scope: connectivity (DNS/TCP/TLS), WebSocket + gateway, clock/timer first; names (config map, SFO, pronunciation) and art (mp: proxy, TMDB) second. Skip updater, manifest, health, installer, evict, lock, sqlite/appdb.
- Detection: user's kern.msgbuf method from goldhen-rich-presence-bin (see step 4). This also removes the 9.00-specific process-table risk for the port.
- Firmware detection: user's logic from goldhen-rich-presence-bin is carried over as-is (firmware string parsed from `libc.sprx` in the sandbox path, then `/system/common/lib/libc.sprx`, with the system API as fallback). Runs at startup, is logged, and is available for any firmware-specific branches. Added 2026-09-29 at user's request.
- Open concern raised: orbisRPC needs a raw Discord user token on the console and identifies as a desktop client (against Discord ToS, ban risk); suggested a throwaway account, or keeping the PC relay. Steps 1-2 need no token.

### Pending
- User go-ahead for step 1 and where the new folder goes (inside orbisRPC repo or next to the old project).
- Current state of repo: `scripts/build_sdk.sh` is ELF-only again; plugin/PRX code removed; nothing else changed.

---

## Session 6: Port step 1, network probe (2026-09-29)

User chose to build the port inside the existing project directory (`goldhen-rich-presence-bin`), not a new folder. Existing files (`Makefile`, `source/main.c`, `icon_embedded.h`) are untouched; the probe is additive.

New files:
- `source/probe_net.c`: flat-BIN probe using libPS4. Reuses the firmware detection from `main.c` unchanged. Tests (1) TCP to literal `1.1.1.1:443`, (2) DNS for `gateway.discord.gg` via libSceNet resolver (`sceNetPoolCreate` + `sceNetResolverCreate` + `sceNetResolverStartNtoa`; libPS4 has no DNS of its own), (3) TCP to the resolved IP on 443. Non-blocking connect polled up to 15 s. Output: klog, `/user/data/rpc_probe.log` (FTP :2121), and a final notification. No TLS, no token.
- `Makefile.probe`: separate build target so the working payload's Makefile is not modified. Output `Discord_RPC_Probe.bin` / `Discord_RPC_Probe.map`. Tabs and LF line endings verified.

Not compiled by Claude (user prefers to run builds). Update: user built it in WSL, build OK (no errors), now testing on console. If `-Werror` trips on a redeclared `sceNet*` symbol, paste the error.

Build and run (WSL):
```bash
cd /mnt/c/Tools/PS4-Rich-Presence-for-Discord/goldhen-rich-presence-bin
export PS4SDK=/mnt/c/Tools/ps4-payload-sdk-reference
make -f Makefile.probe clean && make -f Makefile.probe
ncat PS4_IP 9090 --send-only < Discord_RPC_Probe.bin
```
Then read `/user/data/rpc_probe.log` over FTP (:2121) or the klog.

What the result decides: if DNS fails, try other resolver setup (skip `sceNetInit`, different pool size); if TCP by IP fails, the payload's network context is the problem; if all pass, go to step 2 (mbedTLS handshake).

---

## Session 6 result: step 1 passed on firmware 13.52 (2026-09-29)

Probe output from the console:
- literal-ip: TCP 1.1.1.1:443 OK after ~100 ms
- dns: pool create rc=0x00000bc2, resolver create rc=0x0000001b (both non-zero, but the lookup worked anyway)
- dns: gateway.discord.gg -> 162.159.133.234
- gateway: TCP 162.159.133.234:443 OK after ~200 ms

Conclusion: DNS, sockets and outbound TCP all work from a flat libPS4 payload on 13.52. Step 1 done, no token involved.

---

## Session 7: step 2a, self-relocating flat BIN (2026-09-29)

Why: mbedTLS is full of static pointer tables (vtables, cipher/md info structs, function-pointer tables). A flat image has no dynamic relocations, so those point at link-time addresses and break when the loader places the image anywhere except address 0. Before compiling mbedTLS in, prove the fix on the console.

New files in `goldhen-rich-presence-bin` (additive; `Makefile`, `Makefile.probe`, `source/main.c`, `source/probe_net.c` untouched):
- `crt0_reloc.s`: entry point that walks a table appended at `__file_end` and adds the runtime load base to each listed 64-bit word, then jumps to `_main`. Only r8-r10 and rax are touched, so rdi (thread arg) is preserved.
- `linker_reloc.x`: like libPS4's `linker.x` but links at address 0, keeps `.got`, broadens `.rodata`/`.data` patterns for `-fdata-sections`, and defines `__file_end`.
- `tools/mkreloc.py`: reads the ELF (`--emit-relocs`), collects R_X86_64_64 words in loaded sections plus non-zero `.got` words, appends `u32 count + u32 offsets` to the `.bin`. Fails loudly on unsupported relocation types or a relocation inside `.bss`.
- `source/reloc_test.c`: console test with a const table of strings and function pointers plus a writable pointer to a global. Logs to `/user/data/rpc_reloc.log`, shows one notification, prints PASS or FAIL.
- `Makefile.reloc`: separate target, output `Discord_RPC_RelocTest.bin`.

Tested off-console only: a toy program at a randomized non-zero load address relocated correctly, and a negative control with the table emptied failed as expected. Not yet built with the real SDK or run on the console.

Build and send (WSL):
```bash
cd /mnt/c/Tools/PS4-Rich-Presence-for-Discord/goldhen-rich-presence-bin
export PS4SDK=/mnt/c/Tools/ps4-payload-sdk-reference
make -f Makefile.reloc clean && make -f Makefile.reloc
ncat PS4_IP 9090 --send-only < Discord_RPC_RelocTest.bin
```
Then read `/user/data/rpc_reloc.log` over FTP (:2121). Expect `relocation test PASS` and a non-zero entry count. If the build fails, paste the error. Note `mkreloc.py` needs python3 in WSL.

What the result decides: PASS means pointer tables work in a flat BIN, so go on to step 2b (mbedTLS handshake). FAIL means look at the logged base/entry count first.

### Build error and fix (2026-09-29)

First WSL build failed in `mkreloc.py`: `unsupported relocation type 11 at 0x7f in .text` (R_X86_64_32S).

Cause: `reloc_test.o` and `libPS4.a` are compiled -fPIC, so they load extern symbols with `mov reg, [rip+sym@GOTPCREL]` (R_X86_64_REX_GOTPCRELX). In a static non-PIE link the linker relaxes those into `mov reg, imm32` with an absolute 32-bit address. A 32-bit immediate can't take a 64-bit base added to it, so the self-relocator can't fix it.

Fix: add `-Wl,--no-relax` to LFLAGS in `Makefile.reloc`, so the GOT loads stay and the addresses live in `.got` as 64-bit words, which the relocator does handle. Checked in a scratch link with the same `reloc_test.o` and `libPS4.a`: 138 R_X86_64_32S relocations before, 0 after; `mkreloc.py` then reports 131 relocation words (.data: 5, .got: 126), 7,856 bytes total. Not run on the console yet.

This matters for step 2b too: mbedTLS code will hit the same thing, and the same flag covers it.

### Two more relocator bugs found in code review (2026-09-29)

Found by linking a toy program and running it at random load addresses in a scratch harness (not on the console):

1. **Table overlapped `.bss`.** `.bss` starts at `__file_end`, which is exactly where `mkreloc.py` appends the table, so zero-initialised globals started out holding table bytes (for example `rpc_log_started` began as the entry count, so the log was appended to instead of truncated). Fix: `crt0_reloc.s` now zeroes the table after applying it.
2. **Zero-valued GOT slot skipped.** With `--no-relax`, `(unsigned long)_start` is loaded through a GOT slot whose link-time value is 0, and `mkreloc.py` only relocated non-zero GOT words, so `base` read as 0 and `reloc_test` would have reported a false FAIL. Fix: `mkreloc.py` now also finds GOT slots from the GOTPCREL relocations in the code.

Scratch results: before the fixes the toy returned failure bits at every base; after, 5 of 5 random bases pass, and a negative control with an empty table fails. The real `reloc_test.o` + `libPS4.a` link now gives 132 relocation words (was 131). Files changed: `crt0_reloc.s`, `tools/mkreloc.py`. Rebuild with the same commands; expect the entry count in `rpc_reloc.log` to be 132.

---

## Next Steps (Pending User Direction)

1. **Test the flat binary** on PS4 via BinLoader (port 9020)
2. **If crashes:** Check `.bss` size in map, move large statics to runtime `mmap`
3. **Migrate features incrementally** from old project as needed:
   - kern.msgbuf fallback detection?
   - Different Discord auth (OAuth/Social SDK)?
   - Other?

---

## Reference: Old Project Status (goldhen-rich-presence-bin)

Last session (2026-09-29) applied patches:
- `.bss` → runtime `mmap` for kern.msgbuf buffer
- False `closed` events fixed (return uncertain when log wraps)
- kern.msgbuf > 128 KB handled (dynamic size query)
- Title stays after closing app fixed (closed_latch)

Next steps noted: rebuild in WSL, send to GoldHEN, test on console, capture klog.