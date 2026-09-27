#!/bin/sh
# release.sh - one-command release: SDK payload -> staged assets -> Setup PKG.
# Usage: ./scripts/release.sh   (needs OO_PS4_TOOLCHAIN + PS4_PAYLOAD_SDK)
# Output: OrbisRPC-Setup-<version>.pkg at repo root.
set -eu
cd "$(dirname "$0")/.."
VER="$(sed -n 's/^#define ORBISRPC_VERSION "\(.*\)"/\1/p' orbisrpc/version.h)"
[ -n "$VER" ] || { echo "FAIL: version.h unreadable"; exit 1; }
echo "=== release $VER ==="
./scripts/build_sdk.sh || { echo "FAIL: sdk payload"; exit 1; }
make -f installer/Makefile || { echo "FAIL: pkg"; exit 1; }
PKG="IV0000-ORPC00001_00-ORBISRPCSETUP000.pkg"
[ -f "$PKG" ] || { echo "FAIL: $PKG missing"; exit 1; }
OUT="OrbisRPC-Setup-$VER.pkg"
mv "$PKG" "$OUT"
ls -la "$OUT"
echo "done: $OUT"
