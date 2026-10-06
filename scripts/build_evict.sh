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
if [ ! -x "$CC" ]; then
  echo "FAIL: $CC not found. Build the SDK from git first:"
  echo "      ./scripts/build_sdk_from_source.sh"
  exit 1
fi
echo "=== evict (SDK) ==="
# evict.c shares orbisrpc/procwalk.c with the daemon so the kinfo_proc record
# walk stays offset-free in one place. A private second copy in evict.c is
# exactly how the 9.00-only +447/+72 offsets survived there in the first place.
"$CC" -O2 -Wall -DORBISRPC_SDK_PAYLOAD -Iorbisrpc -c -o "$OUT/procwalk_evict.o" orbisrpc/procwalk.c \
  || { echo "FAIL: procwalk"; exit 1; }
"$CC" -O2 -Wall -DORBISRPC_SDK_PAYLOAD -Iorbisrpc -o "$OUT/evict.elf" \
  tools/evict.c "$OUT/procwalk_evict.o" || { echo "FAIL: evict"; exit 1; }
ls -la "$OUT/evict.elf"
