#!/usr/bin/env bash
#
# PkgTool.Core -- the LibOrbisPkg tool the OpenOrbis Makefile uses to build
# .pkg files -- is a self-contained .NET Core 3.0.3 application. Its crypto
# native library probes for OpenSSL 1.x (libssl.so.1.1, 1.0.2, 1.0.0, 10).
# Ubuntu 24.04 ships OpenSSL 3 only, so `pkg_build` dies with:
#
#     No usable version of libssl was found
#
# OpenSSL 1.1 was dropped after Ubuntu 20.04, so there is no apt package for
# it. This script downloads the focal .deb and unpacks it into a private
# prefix -- nothing is installed system-wide, and OpenSSL 3 is left untouched.
# The Makefile puts that prefix on LD_LIBRARY_PATH, so only the packaging step
# ever sees the old library.
#
# Safe to re-run: it does nothing if the library is already in place.

set -euo pipefail

PREFIX="${OPENSSL11_PREFIX:-$HOME/.local/opt/libssl1.1}"
CACHE="$HOME/.cache/libssl1.1"
DEB="libssl1.1_1.1.1f-1ubuntu2.24_amd64.deb"
URL="https://security.ubuntu.com/ubuntu/pool/main/o/openssl/$DEB"

# dpkg-deb unpacks into usr/lib/x86_64-linux-gnu on Debian/Ubuntu.
LIBDIR="$PREFIX/usr/lib/x86_64-linux-gnu"

if [ -e "$LIBDIR/libssl.so.1.1" ]; then
	echo "==> OpenSSL 1.1 already present in $LIBDIR"
	exit 0
fi

for tool in curl dpkg-deb; do
	command -v "$tool" >/dev/null 2>&1 || {
		echo "error: $tool is required but not installed" >&2
		exit 1
	}
done

mkdir -p "$CACHE" "$PREFIX"

echo "==> Downloading $DEB"
curl -fsSL --retry 3 -o "$CACHE/$DEB" "$URL"

echo "==> Unpacking into $PREFIX"
dpkg-deb -x "$CACHE/$DEB" "$PREFIX"

if [ ! -e "$LIBDIR/libssl.so.1.1" ]; then
	echo "error: libssl.so.1.1 not found under $PREFIX after unpacking" >&2
	echo "       unexpected layout; check the .deb contents" >&2
	exit 1
fi

echo "==> Done. Library lives in $LIBDIR"
echo
echo "Verify with:"
echo "  LD_LIBRARY_PATH=$LIBDIR \$OO_PS4_TOOLCHAIN/bin/linux/PkgTool.Core help"
