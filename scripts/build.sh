#!/bin/sh
# Compatibility entry point. There is one supported payload build path.
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
echo "build.sh is deprecated; using the SDK payload build from build_sdk.sh" >&2
exec "$ROOT/scripts/build_sdk.sh"
