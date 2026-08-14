#!/bin/bash
# build.sh - orbisRPC: compile payload ELF + fself for PS4.
# Usage: ./scripts/build.sh [elf|fself|all]   default: all
set -euo pipefail
SDK="${OO_PS4_TOOLCHAIN:-/Users/mac/PS4Toolchain/OpenOrbis/PS4Toolchain}"
LLVM_BIN="${LLVM_HOME:-/usr/local/opt/llvm}/bin"
export PATH="$LLVM_BIN:$SDK/bin/macos:$PATH"
CC="$LLVM_BIN/clang"
LD="${LLD:-/Users/mac/lldbuild/build/bin/ld.lld}"
TARGET="x86_64-pc-freebsd12-elf"
CFLAGS="--target=$TARGET -fPIC -std=gnu11 -Wall -Wno-unused \
        -Wno-int-conversion -Wno-incompatible-pointer-types \
        -isystem $SDK/include"
LIBS="-lc -lkernel -lSceNet -lSceNetCtl -lSceLibreSSL -lSceSsl -lSceHttp -lSceSysmodule \
      -lSceUserService -lSceAppInstUtil -lSceAppContent"
LDFLAGS="-m elf_x86_64 -pie --eh-frame-hdr -L$SDK/lib $LIBS $SDK/lib/crt1.o --script $SDK/link.x"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
export OO_PS4_TOOLCHAIN="$SDK"
OUT="$ROOT/build"; mkdir -p "$OUT"
echo "=== compiling ==="
for f in log cfg jsonlite b64 http ws detect discord main; do
  "$CC" $CFLAGS -c -o "$OUT/$f.o" "orbisrpc/$f.c" || { echo "compile $f FAILED"; exit 1; }
done
echo "=== linking ($LD) ==="
"$LD" $OUT/*.o -o "$OUT/orbisrpc.elf" $LDFLAGS || { echo "link FAILED"; exit 1; }
echo "ELF -> $OUT/orbisrpc.elf"
MODE="${1:-all}"
if [ "$MODE" = "fself" ] || [ "$MODE" = "all" ]; then
  "$SDK/bin/macos/create-fself-macos" -in="$OUT/orbisrpc.elf" \
      -out="$OUT/orbisrpc.fself" --eboot "$OUT/orbisrpc-eboot.bin" --paid 0x3800000000000011
  echo "FSELF -> $OUT/orbisrpc.fself"
  echo "  (GoldHEN payload to deploy is $OUT/orbisrpc.elf -> /data/GoldHEN/payloads/orbisrpc.bin)"
fi
echo "done."