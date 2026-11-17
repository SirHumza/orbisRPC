#!/bin/sh
# build_sdk_from_source.sh - build the ps4-payload-dev SDK from git and install
# it to a DESTDIR, instead of downloading the release ZIP.
#
# WHY THIS EXISTS
#
# The SDK's crt/patch.c holds a per-firmware table, and __patch_init() returns
# an error for any firmware not listed in it:
#
#     default:
#       klog_printf("Unsupported firmware %x\n", fw);
#
# crt/crt.c propagates that error out of _start():
#
#     if((err=payload_init())) {
#       return payload_terminate(err);   // never reaches main(), no log line
#     }
#
# crt/kernel.c has the same shape: unlisted firmware falls through to a
# kexec_find_* signature scan for the kernel image base, copyin/copyout and
# targetid, and rtld.c then depends on addresses __kernel_init() derived.
#
# The released SDK (v0.9, 2026-05-12) lists 13.50 (0x1350) but NOT 13.52
# (0x1352), so a payload linked against the release ZIP cannot boot on 13.52 --
# it dies before main() with no diagnostics. 13.52 offsets landed on the SDK's
# master branch in 959e4ed (2026-06-25) and have never been in a release.
#
# This script therefore builds from git. Recipe taken from drakmor/nanoDNS's
# .github/workflows/ps4.yml, which targets the same elfldr path and ships a
# working 13.52 payload.
#
# Usage:
#   PS4_SDK_REF=573b4a0 ./scripts/build_sdk_from_source.sh
#   PS4_SDK_REF=573b4a0 PS4_SDK_DEST=~/ps4-payload-sdk ./scripts/build_sdk_from_source.sh
#
# Then build the daemon against it:
#   PS4_PAYLOAD_SDK=~/ps4-payload-sdk ./scripts/build_sdk.sh
set -e

# Pinned to the SDK commit that carries 13.52 and 14.00 offsets. Bump when the
# SDK lands a release that includes them, then this script can be retired.
REF="${PS4_SDK_REF:-573b4a0}"
DEST="${PS4_SDK_DEST:-$HOME/ps4-payload-sdk}"
SRC="${PS4_SDK_SRC:-$HOME/ps4-payload-sdk-src}"

command -v git >/dev/null 2>&1 || { echo "FAIL: git not found"; exit 1; }
command -v llvm-config-18 >/dev/null 2>&1 || command -v llvm-config >/dev/null 2>&1 || {
  echo "FAIL: llvm-config not found. Install clang-18 + lld-18."; exit 1; }

mkdir -p "$SRC"
if [ -d "$SRC/.git" ]; then
  echo "=== fetching SDK $REF ==="
  git -C "$SRC" fetch -q origin
else
  echo "=== cloning SDK ==="
  git clone -q https://github.com/ps4-payload-dev/sdk.git "$SRC"
fi
git -C "$SRC" checkout -q "$REF"
echo "SDK at $(git -C "$SRC" rev-parse --short HEAD) ($(git -C "$SRC" log -1 --format=%s))"

# Fail loudly and early rather than shipping a payload that cannot boot. This
# is a source-level check on purpose: the installed SDK is a bare tree with no
# crt/ sources, and byte-scanning the linked ELF for a firmware offset is not
# reliable (a 4-byte pattern occurs by chance in a 2 MB binary).
echo "=== verifying firmware support in SDK source ==="
missing=""
for fw in 1350 1352; do
  if grep -q "case 0x${fw}:" "$SRC/crt/patch.c" 2>/dev/null; then
    echo "  crt/patch.c  has case 0x${fw}"
  else
    echo "  crt/patch.c  MISSING case 0x${fw}"
    missing="$missing patch:0x$fw"
  fi
done
if [ -n "$missing" ]; then
  echo "FAIL: SDK $REF lacks 13.52 offsets ($missing)."
  echo "      Payloads built against it die before main() with no log."
  echo "      Bump PS4_SDK_REF to a newer SDK commit."
  exit 1
fi

echo "=== building SDK -> $DEST ==="
make -C "$SRC" DESTDIR="$DEST" clean >/dev/null 2>&1 || true
make -C "$SRC" DESTDIR="$DEST" install >/dev/null

for f in bin/orbis-clang target/lib/crt1.o toolchain/orbis.mk; do
  [ -e "$DEST/$f" ] || { echo "FAIL: $DEST/$f missing after install"; exit 1; }
done

echo
echo "SDK installed at $DEST"
echo "Next:"
echo "  PS4_PAYLOAD_SDK=$DEST ./scripts/build_sdk.sh"
echo "  PS4_SDK_SRC=$SRC ./scripts/build_sdk.sh   # enables the firmware gate"
