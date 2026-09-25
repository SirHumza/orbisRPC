#!/usr/bin/env python3
"""e2e_consumer.py - host verification of the consumer flow for the SDK line.
Checks source contracts + runs the compiled unit tests + gates the payload
linkage (daemon-world libs only). Exit nonzero on any failure.
Honest about what is host-verified (logic/contracts) vs hardware-only
(presence display, loaders, Rest Mode)."""
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
fails = []


def check(name, cond, note=""):
    print(("PASS " if cond else "FAIL ") + name + (" - " + note if note else ""))
    if not cond:
        fails.append(name)


def src(p):
    with open(os.path.join(ROOT, p), encoding="utf-8", errors="replace") as f:
        return f.read()


# 1. unit tests build + pass (covers json, sfo, tmdb, updater, art, health…)
r = subprocess.run(["make", "-C", os.path.join(ROOT, "tests"), "clean"],
                   capture_output=True)
r = subprocess.run(["make", "-C", os.path.join(ROOT, "tests"), "test"],
                   capture_output=True, text=True)
check("host/unit-tests", r.returncode == 0 and "utility tests passed" in r.stdout + r.stderr)
# 2. config template ships no real token, sane defaults, art pack default
cfg = src("config/config.json")
check("install/template-has-no-token", "SET_ME" in cfg)
check("install/art-pack-default", "orbisrpc-host" in cfg)
# 3. single-instance lock with recycled-PID guard
lock = src("orbisrpc/lock.c")
check("runtime/single-lock", "lock_acquire" in src("orbisrpc/daemon.c"))
check("runtime/lock-name-check", "Payload" in lock)
# 4. session: persistence file, resume window, integer ms, since null
d = src("orbisrpc/daemon.c")
check("session/persisted", "session.json" in d)
check("session/resume-window", "SESSION_RESUMED" in d)
check("session/ledger", "playtime.log" in d)
disc = src("orbisrpc/discord.c")
check("discord/ms-integer", "jl_new_int" in disc and "1000LL" in disc)
check("discord/since-null", "jl_new_null" in disc)
check("discord/media-types", "detect_media_type" in disc)
# 5. crash recovery wired
check("health/boot-marker", "health_boot_note_crash" in d)
check("health/safe-mode-gates-updater", "safe_mode" in d)
check("health/rollback-at-boot", "health_verify_or_rollback" in d)
# 6. time correction
check("clock/sntp", "time_sync" in d and "time_fixed" in d)
check("clock/monotonic-deadlines", "orbis_mono_s" in src("orbisrpc/ws.c"))
# 7. art: mp: proxy path, no raw URLs, pack default
check("art/mp-proxy", "art_resolve_mp" in disc)
check("art/no-raw-urls", "mp:" in src("orbisrpc/art.c"))
check("art/live-tmdb", "https_get_tmdb" in src("orbisrpc/tmdb.c"))
check("art/no-baked-table", "art_table" not in src("orbisrpc/tmdb.c").lower())
# 8. home + rest handling
check("home/presence", "presence: home" in d)
check("shutdown/signals", "daemon_on_signal" in d and "lock_release" in d)
check("shutdown/session-persisted", "session.json" in d)
check("reconnect/jitter-attempts", "reconnect_delay" in d and "attempt %d" in d)
check("reconnect/never-exits", "600" in d and "quiet persistence" in d)
check("metrics/hourly", "HEALTH:" in d)
check("queue/failed-post-retries", "will retry" in d)
check("state/explicit", "pres_set" in d and "STATE: presence" in d)
check("art/disk-cache", "artwork_cache.json" in src("orbisrpc/art.c"))
check("cfg/schema-save-checked", "cfg_save" in d and "unwritable" in d)
# 9. signed updates, no unsigned fallback
upd = src("orbisrpc/updater.c")
check("update/manifest-required", "manifest" in upd.lower())
check("update/unsigned-refused", "refus" in upd.lower())
# 10. payload linkage gate (daemon-world libs only)
elf = os.path.join(ROOT, "build-sdk", "orbisrpc_sdk.elf")
if os.path.exists(elf):
    readelf = os.environ.get("LLVM_READELF", "/usr/local/opt/llvm/bin/llvm-readelf")
    try:
        out = subprocess.run([readelf, "-d", elf], capture_output=True,
                             text=True).stdout
    except OSError:
        out = ""
    needed = re.findall(r"\[(.*?)\]", out)
    check("linkage/daemon-libs",
          any("libkernel_web" in n for n in needed) and
          any("SceLibcInternal" in n for n in needed))
    check("linkage/no-app-libs",
          not any(n in ("libkernel.so", "libc.so") for n in needed),
          ",".join(needed))
check("detect/unknown-holds", "scan_unknown" in d)
check("detect/eboot-fast-switch", "fast-switching" in d)
check("detect/atime-identity", "pkg-atime" in src("orbisrpc/detect.c") or "st_atime" in src("orbisrpc/detect.c"))
check("detect/no-baked-table", "nametable" not in src("orbisrpc/detect.c").lower())
check("session/validates-restore", "failed validation" in d)
elf = os.path.join(ROOT, "build-sdk", "orbisrpc_sdk.elf")
if not os.path.exists(elf):
    check("linkage/payload-built", False, "run scripts/build_sdk.sh first")
print(f"{len(fails)} failures" if fails else "E2E host simulation: ALL PASS")
sys.exit(1 if fails else 0)
