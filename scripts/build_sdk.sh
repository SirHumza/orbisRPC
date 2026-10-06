#!/bin/sh
# build_sdk.sh - OrbisRPC daemon payload via ps4-payload-sdk (daemon-world
# linkage: no libkernel.so, BSD sockets, SDK libc). Test vehicle: elfldr.
#
# Usage:
#   PS4_PAYLOAD_SDK=/path ./scripts/build_sdk.sh
#
# The SDK must be built from git, not downloaded from the release ZIP: see
# scripts/build_sdk_from_source.sh for why. Set PS4_SDK_SRC to that checkout to
# have the firmware gate below verify the SDK actually supports 13.52 instead of
# trusting it.
set -e
SDK="${PS4_PAYLOAD_SDK:-$HOME/ps4-payload-sdk/ps4-payload-sdk}"
CC="$SDK/bin/orbis-clang"

if [ ! -x "$CC" ]; then
  echo "FAIL: $CC not found."
  echo "      Build the SDK from git first: ./scripts/build_sdk_from_source.sh"
  echo "      (the release ZIP is v0.9, which predates the 13.52 offsets)"
  exit 1
fi

# Gate: the SDK's crt/patch.c lists supported firmware and __patch_init() errors
# out for anything absent, which makes _start() call payload_terminate() before
# main() -- silent death, no log line. v0.9 lists 13.50 but not 13.52.
# Checked against source, not the linked ELF: a 4-byte offset pattern occurs by
# chance in a 2 MB binary, so a byte scan would prove nothing.
if [ -n "${PS4_SDK_SRC:-}" ] && [ -f "$PS4_SDK_SRC/crt/patch.c" ]; then
  echo "=== SDK firmware gate ==="
  ok=1
  for fw in 1350 1352; do
    if grep -q "case 0x${fw}:" "$PS4_SDK_SRC/crt/patch.c"; then
      echo "  ok   crt/patch.c case 0x${fw}"
    else
      echo "  FAIL crt/patch.c missing case 0x${fw}"
      ok=0
    fi
  done
  [ "$ok" -eq 1 ] || {
    echo "FAIL: SDK at $PS4_SDK_SRC cannot boot 13.52; payload would die before main()."
    exit 1
  }
else
  echo "note: PS4_SDK_SRC not set, skipping the 13.52 SDK firmware gate."
  echo "      If the payload is silent on 13.52, the SDK is the first suspect."
fi
export PS4_PAYLOAD_SDK="$SDK"
export PATH="$HOME/llvmshim:$PATH"
OUT="build-sdk"
mkdir -p "$OUT"
CFLAGS="-O2 -Wall -DORBISRPC_SDK_PAYLOAD -Iorbisrpc -Ithird_party/mbedtls/include -Ithird_party/sqlite/sqlite-amalgamation-3510100"
SQLITE_DIR="third_party/sqlite/sqlite-amalgamation-3510100"
echo "=== daemon sources (SDK) ==="
for f in log cfg jsonlite b64 sfo procwalk bigapp fw pkgzone gamecache notify retro tmdb_crypto tmdb updater updater_http updater_util tls ws detect discord daemon compat lock timesync art health manifest appdb main; do
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
# libSceSystemService supplies sceSystemServiceGetAppIdOfBigApp and
# sceLncUtilGetAppTitleId, which are the daemon's entire foreground signal.
# Dropping it makes the payload fail at load time, which is why the CI gate
# asserts it is present rather than trusting this line.
"$CC" -o "$OUT/orbisrpc_sdk.elf" "$OUT"/*.o -lSceSystemService || { echo "FAIL: link"; exit 1; }
ls -la "$OUT/orbisrpc_sdk.elf"
