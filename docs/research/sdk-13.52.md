# SDK firmware gate: why the payload was silent on 13.52

## Observed

- Daemon ELF does nothing on 13.52. No `/data/orbisRPC/log.txt`, no klog line,
  nothing. The 9.00 console runs the same daemon.
- 13.50 is unaffected — which is the clue. 13.50 works, 13.52 does not, and the
  only difference is which firmware the SDK lists.

## Cause

Not the loader, not the linker script, not `libkernel_web`. The SDK's own CRT
gates startup on a per-firmware table.

`crt/patch.c`, `__patch_init()`:

```c
  case 0x1350:
    ...
  default:
    klog_printf("Unsupported firmware %x\n", fw);
```

`crt/crt.c`, `_start()`:

```c
  if((err=payload_init())) {
    return payload_terminate(err);   // returns before main() is ever called
  }
```

`payload_init()` runs `__klog_init()`, `__kernel_init()`, `__rtld_init()`,
`__patch_init()`, and returns the first non-zero. Any failure ends the payload
before `main()`, which is why there is no log line: the daemon's own first
`log_msg` never executes.

`crt/kernel.c`, `__kernel_init()` has the same shape. Unlisted firmware skips
the hard-coded offset table and falls into a `kexec_find_*` signature scan for
the kernel image base, copyin/copyout and targetid; `crt/rtld.c` then depends on
`KERNEL_ADDRESS_ROOTVNODE` / `KERNEL_ADDRESS_PRISON0` that the scan derived.

The released SDK is **v0.9, published 2026-05-12**. Its `crt/patch.c` lists
`0x1300`, `0x1302`, `0x1304`, `0x1350` — and *not* `0x1352`. 13.50 is why that
firmware works. 13.52 offsets landed on the SDK's master branch in `959e4ed`
("Add 13.52 offsets", 2026-06-25) and have **never appeared in a release**:

```diff
+ case 0x1352:
+   patch1 = 0x003B35F0;
+   patch2 = 0x003B3610;
+   patch3 = 0x001FC561;
```

`orbis-clang` links `target/lib/crt1.o` unconditionally, and `crt/Makefile`
builds `crt1.o` from `crt.o klog.o kernel.o rtld.o patch.o mdbg.o nid.o` — so
those offsets are in every payload built against the release ZIP.

## Fix

Build the SDK from git instead of downloading the release ZIP.

- `scripts/build_sdk_from_source.sh` — clones `ps4-payload-dev/sdk`, pins a ref,
  refuses to proceed unless `crt/patch.c` has the required firmware cases, then
  `make DESTDIR=... clean install`.
- `scripts/build_sdk.sh` — gates on `$PS4_SDK_SRC/crt/patch.c` so a stale SDK
  fails the build instead of producing a payload that dies silently.
- `.github/workflows/ci.yml` — replaced the `releases/latest` ZIP download with
  a git checkout plus source build, and added the firmware gate.

Recipe taken from `drakmor/nanoDNS`'s `.github/workflows/ps4.yml`, which
targets the same elfldr path (port 9021) and ships a working 13.52 payload. Its
PS4 job checks out `ps4-payload-dev/sdk` and runs `make clean install` with
clang-18/lld-18; it never downloads the release ZIP.

## Ruled out

- **Linker script.** `ldscripts/elf_x86_64.x` is byte-identical between the
  release and master.
- **The `NEEDED` set.** Unchanged by the SDK update: still `libkernel_web.sprx`,
  `libSceLibcInternal.sprx`, `libSceNet.sprx`, and 112 undefined symbols
  resolved at load time. Same count before and after.
- **Binary offset scanning as a check.** An earlier idea — grep the linked ELF
  for 13.52's patch offsets — is not sound evidence: a 4-byte pattern occurs by
  chance in a 2 MB binary. The gate checks SDK *source* instead. The sound
  evidence is the `v0.9` source listing above plus `crt1.o`'s md5 matching a
  fresh master build (`c9011d1254f7104998f663d9bbd25416`).
- **`EchoStretch/ps4-payload-sdk`.** A mirror of Scene-Collective's libPS4 — it
  has `libPS4/` and `crt0.s`, no `crt/`, no firmware table. It is the flat-`.bin`
  SDK, not the ELF one.

## Not yet proven

The daemon still has not been run on 13.52 with a fixed-SDK build. If it is
still silent after this, the next suspect is the loader/spawn side, not the SDK:
`elf-linkage.md` records that elfldr "SIGKILLs on the first
unresolvable import — silent by design", which would look identical from the
outside.

For reference, Scene-Collective's flat-bin SDK took the same shape of update
(`2847f1f` Add 13.50 support, `46efae9` Add 13.52 support, `b732641` Add 14.00
support), and a flat libPS4 payload already passes DNS + TCP on 13.52.