#!/bin/bash
# build.sh - orbisRPC: compile payload ELF + fself for PS4.
# Usage: ./scripts/build.sh [elf|fself|all]   default: all
# Zero-guess: validates toolchain up front with actionable errors.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

SDK="${OO_PS4_TOOLCHAIN:-/Users/mac/PS4Toolchain/OpenOrbis/PS4Toolchain}"
# Auto-detect Homebrew LLVM on macOS if LLVM_HOME not set
if [ -z "${LLVM_HOME:-}" ]; then
  for cand in /opt/homebrew/opt/llvm /usr/local/opt/llvm /usr/local/llvm; do
    if [ -x "$cand/bin/clang" ]; then LLVM_HOME="$cand"; break; fi
  done
fi
LLVM_BIN="${LLVM_HOME:-/usr/local/opt/llvm}/bin"
# Auto-detect lld
if [ -z "${LLD:-}" ]; then
  if [ -x "/Users/mac/lldbuild/build/bin/ld.lld" ]; then LLD="/Users/mac/lldbuild/build/bin/ld.lld"
  elif command -v ld.lld >/dev/null 2>&1; then LLD="$(command -v ld.lld)"
  else LLD="$LLVM_BIN/ld.lld"
  fi
fi
export PATH="$LLVM_BIN:$SDK/bin/macos:$PATH"
CC="$LLVM_BIN/clang"
LD="$LLD"

fail() { echo "BUILD FAIL: $1" >&2; exit 1; }
[ -x "$CC" ] || fail "clang not found at $CC. Install: brew install llvm  OR  LLVM_HOME=/path ./scripts/build.sh"
[ -x "$LD" ] || fail "ld.lld not found at $LD. Set LLD=/path/to/ld.lld"
[ -d "$SDK" ] || fail "OpenOrbis SDK not found at $SDK. Set OO_PS4_TOOLCHAIN=~/PS4Toolchain/OpenOrbis/PS4Toolchain"
[ -f "$SDK/lib/crt1.o" ] || fail "missing $SDK/lib/crt1.o — corrupt toolchain download, re-extract v0.5.4"
[ -f "$SDK/link.x" ] || fail "missing $SDK/link.x — corrupt toolchain download"
[ -f "$SDK/include/orbis/Net.h" ] || fail "SDK headers missing orkNet.h — wrong SDK version?"
[ -d "third_party/mbedtls/include" ] || fail "third_party/mbedtls missing — git submodule update --init?"
[ -f "third_party/mbedtls/include/mbedtls/ssl.h" ] || fail "mbedtls headers missing"
command -v "$SDK/bin/macos/create-fself-macos" >/dev/null 2>&1 || [ -x "$SDK/bin/macos/create-fself-macos" ] || fail "create-fself-macos missing in SDK/bin/macos"

TARGET="x86_64-pc-freebsd12-elf"
CFLAGS="--target=$TARGET -fPIC -std=gnu11 -Wall -Wno-unused \
        -Wno-int-conversion -Wno-incompatible-pointer-types \
        -DMBEDTLS_NO_PLATFORM_ENTROPY \
        -isystem $SDK/include -Ithird_party/mbedtls/include"
LIBS="-lc -lkernel -lSceNet -lSceNetCtl -lSceSysmodule \
      -lSceUserService -lSceAppInstUtil -lSceAppContent"
LDFLAGS="-m elf_x86_64 -pie --eh-frame-hdr -L$SDK/lib $LIBS $SDK/lib/crt1.o --script $SDK/link.x"
export OO_PS4_TOOLCHAIN="$SDK"
OUT="$ROOT/build"; mkdir -p "$OUT"
echo "=== compiling (CC=$CC LD=$LD SDK=$SDK) ==="
for f in log cfg jsonlite b64 tls ws detect discord daemon compat main; do
  "$CC" $CFLAGS -c -o "$OUT/$f.o" "orbisrpc/$f.c" || fail "compile $f"
done
echo "=== mbedtls (skip net_sockets/timing: POSIX-only) ==="
for f in $(find third_party/mbedtls/library -name '*.c' ! -name 'net_sockets.c' ! -name 'timing.c' ! -name 'entropy_poll.c' | sort); do
  o="$OUT/mbed_$(echo "$f" | sed 's|third_party/mbedtls/library/||; s|\.c$||').o"
  "$CC" $CFLAGS -c -o "$o" "$f" || fail "mbedtls $f"
done
echo "=== linking ($LD) ==="
"$LD" $OUT/*.o -o "$OUT/orbisrpc.elf" $LDFLAGS || fail "link — see duplicate-symbol or missing-lib errors above"
echo "ELF -> $OUT/orbisrpc.elf ($(stat -f%z "$OUT/orbisrpc.elf" 2>/dev/null || stat -c%s "$OUT/orbisrpc.elf") bytes)"
MODE="${1:-all}"
if [ "$MODE" = "fself" ] || [ "$MODE" = "all" ]; then
  "$SDK/bin/macos/create-fself-macos" -in="$OUT/orbisrpc.elf" \
      -out="$OUT/orbisrpc.fself" --eboot "$OUT/orbisrpc-eboot.bin" --paid 0x3800000000000011 || fail "fself creation"
  echo "FSELF -> $OUT/orbisrpc.fself"
  echo "  (GoldHEN payload to deploy is $OUT/orbisrpc.elf -> /data/GoldHEN/payloads/orbisrpc.bin)"
fi
echo "done."
