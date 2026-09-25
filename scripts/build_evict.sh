#!/bin/sh
# build_evict.sh - evict.elf via ps4-payload-sdk (daemon-world linkage only).
# Usage: PS4_PAYLOAD_SDK=/path ./scripts/build_evict.sh
set -e
SDK="${PS4_PAYLOAD_SDK:-$HOME/ps4-payload-sdk/ps4-payload-sdk}"
CC="$SDK/bin/orbis-clang"
export PS4_PAYLOAD_SDK="$SDK"
export PATH="$HOME/llvmshim:$PATH"
OUT="build-sdk"
mkdir -p "$OUT"
echo "=== evict (SDK) ==="
"$CC" -O2 -Wall -DORBISRPC_SDK_PAYLOAD -o "$OUT/evict.elf" tools/evict.c || { echo "FAIL: evict"; exit 1; }
ls -la "$OUT/evict.elf"
