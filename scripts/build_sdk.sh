#!/bin/sh
# build_sdk.sh - OrbisRPC daemon payload via ps4-payload-sdk (daemon-world
# linkage: no libkernel.so, BSD sockets, SDK libc). Test vehicle: elfldr.
# Usage: PS4_PAYLOAD_SDK=/path ./scripts/build_sdk.sh
set -e
SDK="${PS4_PAYLOAD_SDK:-$HOME/ps4-payload-sdk/ps4-payload-sdk}"
[ -x "$SDK/toolchain/../../ps4-payload-sdk/bin/orbis-clang" ] || {
  # resolve relative to SDK root regardless of layout
  true
}
CC="$SDK/bin/orbis-clang"
export PS4_PAYLOAD_SDK="$SDK"
export PATH="$HOME/llvmshim:$PATH"
OUT="build-sdk"
mkdir -p "$OUT"
CFLAGS="-O2 -Wall -DORBISRPC_SDK_PAYLOAD -Iorbisrpc -Ithird_party/mbedtls/include -Ithird_party/sqlite/sqlite-amalgamation-3510100"
SQLITE_DIR="third_party/sqlite/sqlite-amalgamation-3510100"
echo "=== daemon sources (SDK) ==="
for f in log cfg jsonlite b64 sfo tmdb_crypto tmdb updater updater_http updater_util tls ws detect discord daemon compat lock timesync art health manifest appdb main; do
  # clock has no .c (header-only helper lives in compat.c); skip if missing
  [ -f "orbisrpc/$f.c" ] || continue
  "$CC" $CFLAGS -c -o "$OUT/$f.o" "orbisrpc/$f.c" || { echo "FAIL: $f"; exit 1; }
done
echo "=== sqlite (SDK, readonly, no threads/extensions) ==="
"$CC" $CFLAGS -Os -DSQLITE_THREADSAFE=0 -DSQLITE_OMIT_LOAD_EXTENSION -DSQLITE_OMIT_DEPRECATED -c -o "$OUT/sqlite3.o" "$SQLITE_DIR/sqlite3.c" || { echo "FAIL: sqlite"; exit 1; }
echo "=== mbedtls (SDK, skip net_sockets/timing/entropy_poll) ==="
for f in $(find third_party/mbedtls/library -name '*.c' ! -name 'net_sockets.c' ! -name 'timing.c' ! -name 'entropy_poll.c' | sort); do
  o="$OUT/mbed_$(echo "$f" | sed 's|third_party/mbedtls/library/||; s|\.c$||').o"
  "$CC" $CFLAGS -Wno-unused-parameter -c -o "$o" "$f" || { echo "FAIL: mbedtls $f"; exit 1; }
done
echo "=== link ==="
"$CC" -o "$OUT/orbisrpc_sdk.elf" "$OUT"/*.o || { echo "FAIL: link"; exit 1; }
ls -la "$OUT/orbisrpc_sdk.elf"
