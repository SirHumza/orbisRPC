#!/usr/bin/env bash
#
# Convenience wrapper around `make` that guarantees the toolchain environment is
# present. Interactive shells already get it from ~/.bashrc; this is for
# non-interactive runs (CI, `wsl.exe -e bash`, fresh terminals).
#
#   ./scripts/build.sh          build the .pkg
#   ./scripts/build.sh clean    clean instead
#   ./scripts/build.sh <args>   forward args straight to make

set -euo pipefail

cd "$(dirname "$0")/.."

: "${OO_PS4_TOOLCHAIN:=$HOME/OpenOrbis-PS4-Toolchain}"
export OO_PS4_TOOLCHAIN

if [ ! -d "$OO_PS4_TOOLCHAIN" ]; then
	echo "error: OO_PS4_TOOLCHAIN does not point at a toolchain: $OO_PS4_TOOLCHAIN" >&2
	echo "       edit ~/.bashrc or pass OO_PS4_TOOLCHAIN=/path/to/OpenOrbis-PS4-Toolchain" >&2
	exit 1
fi

exec make "$@"
