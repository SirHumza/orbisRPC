#!/bin/sh
# ps4-check.sh - one command to answer "what is the console actually running?".
# Pulls the evidence files over FTP, lists staged PKGs, and diffs local
# artifacts against what the console has, so support questions resolve
# without a multi-command FTP dance.
#
# Usage: ./scripts/ps4-check.sh [tail_lines]        (default 25)
# Env:   PS4_HOST (default 192.168.1.136)  PS4_PORT (2121)
set -eu
cd "$(dirname "$0")/.."

HOST="${PS4_HOST:-192.168.1.136}"
PORT="${PS4_PORT:-2121}"
TAIL="${1:-25}"
BASE="ftp://$HOST:$PORT"
FTP="curl -sS --connect-timeout 5 --max-time 20"

echo "=== console $HOST:$PORT ==="
# Probe with a file we know exists; fall back to a directory listing, since
# some FTP builds refuse listings but serve files fine.
if $FTP "$BASE/user/data/orbisRPC/install.log" >/dev/null 2>&1 \
   || $FTP "$BASE/user/data/orbisRPC/log.txt" >/dev/null 2>&1 \
   || $FTP "$BASE/user/data/pkg/" >/dev/null 2>&1; then
    :
else
    echo "UNREACHABLE (jailbreak must be active + console awake)"
    exit 1
fi

# --- evidence: newest run of the installer -------------------------
echo
echo "--- install.log (last $TAIL) ---"
if $FTP "$BASE/user/data/orbisRPC/install.log" 2>/dev/null | tail -n "$TAIL"; then :; else
    echo "(missing)"
fi

# --- evidence: daemon log ------------------------------------------
echo
echo "--- log.txt (last $TAIL) ---"
$FTP "$BASE/user/data/orbisRPC/log.txt" 2>/dev/null | tail -n "$TAIL" || echo "(missing)"

# --- liveness / diagnostics (remaster v1.1.0+) ---------------------
for f in status.json diag.json daemon.gen; do
    echo
    echo "--- $f ---"
    $FTP "$BASE/user/data/orbisRPC/$f" 2>/dev/null || echo "(missing = pre-remaster build)"
done

# --- staged PKGs: which one is the current test build? -------------
echo
echo "--- /user/data/pkg/ ---"
$FTP "$BASE/user/data/pkg/" 2>/dev/null | sed -n '1,40p' || echo "(missing)"

# --- local vs remote payload: is the console running OUR build? ---
echo
echo "--- payload: local vs console ---"
LOCAL=""
for c in build/orbisrpc.elf build-sdk/orbisrpc_sdk.elf; do
    [ -f "$c" ] && { LOCAL="$c"; break; }
done
if [ -z "$LOCAL" ]; then
    echo "no local build yet (run ./scripts/build.sh elf)"
else
    lsz=$(wc -c <"$LOCAL" | tr -d ' ')
    rsz=$($FTP "$BASE/user/data/payloads/orbisrpc.bin" 2>/dev/null | wc -c | tr -d ' ')
    echo "local  $LOCAL $lsz"
    echo "remote /data/payloads/orbisrpc.bin $rsz"
    [ "$lsz" = "$rsz" ] && echo "MATCH (console runs the current payload)" \
                         || echo "DIFFERS (payload staged but not launched, or console is older)"
fi
echo
echo "done."
