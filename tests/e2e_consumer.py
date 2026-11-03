#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 SirHumza and CyberMask367 - see NOTICE for attribution terms.
"""e2e_consumer.py - host verification of the consumer flow for the SDK line.
Checks source contracts + runs the compiled unit tests + gates the payload
linkage (daemon-world libs only). Exit nonzero on any failure.
Honest about what is host-verified (logic/contracts) vs hardware-only
(presence display, loaders, Rest Mode)."""
import json
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
                   capture_output=True, text=True, errors="replace")
check("host/unit-tests", r.returncode == 0 and "utility tests passed" in r.stdout + r.stderr)
# 2. config template ships no real token, sane defaults, art pack default
cfg = src("config/config.json")
check("install/template-has-no-token", "SET_ME" in cfg)
check("install/art-pack-default", "orbisrpc-host" in cfg)
check("install/presence-hold-default",
      json.loads(cfg).get("presence_hold_s") == 300 and
      json.loads(src("installer/config.json")).get("presence_hold_s") == 300)
rel = src(".github/workflows/release.yml")
check("release/retro-index-shipped",
      "config/ps1.json" in rel and "config/ps2.json" in rel and "config/psp.json" in rel)
check("release/checksums", "SHA256SUMS" in rel)
check("release/no-pkg", "installer/" not in rel and ".pkg" not in rel)
# 2b. no Discord-session-shaped token anywhere the PKG or repo ships.
# Fatal when ORBISRPC_STRICT_SECRETS=1 (CI); a local working copy may hold
# a dev token and gets a warning instead.
tok_re = re.compile(rb"MT[A-Za-z0-9_-]{20,}\.[A-Za-z0-9_-]{5,}\.[A-Za-z0-9_-]{20,}")
leaks = []
for rel in ("installer/installer.c", "installer/config.json",
            "installer/assets/config.json", "config/config.json"):
    try:
        with open(os.path.join(ROOT, rel), "rb") as fh:
            if tok_re.search(fh.read()):
                leaks.append(rel)
    except OSError:
        pass
if leaks:
    _msg = "token-shaped string in " + ", ".join(leaks)
    if os.environ.get("ORBISRPC_STRICT_SECRETS") == "1":
        check("secrets/no-token-in-installer", False, _msg)
    else:
        print("WARN secrets/no-token-in-installer (set ORBISRPC_STRICT_SECRETS=1 "
              "to fail) - " + _msg)
else:
    check("secrets/no-token-in-installer", True)
# 3. single-instance lock with recycled-PID guard
lock = src("orbisrpc/lock.c")
check("runtime/single-lock", "lock_acquire" in src("orbisrpc/daemon.c"))
check("runtime/lock-name-check", "Payload" in lock)
# 4. session lifecycle: D-01 removed the continuation cache entirely. A
# relaunch/reboot/Rest Mode must never resurrect an elapsed timer.
d = src("orbisrpc/daemon.c")
check("session/no-continuation-cache", "session.json" not in d and "SESSION_RESUMED" not in d)
check("session/no-stale-restore", "sched_session_fresh" not in d)
check("session/fresh-timer", "started = time_fixed();" in d)
check("session/ledger", "playtime.log" in d)
check("session/presence-hold", "presence_hold_s" in d and "presence_hold_s" in src("orbisrpc/cfg.c"))
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
check("shutdown/stop-file", "STOP_PATH" in d and "stop_file_present" in d)
check("resume/suspend-drift", "sched_suspend_drift" in d and "sched_rebase_start" in d)
check("token/reason-logged", "usable_token" in d and "no usable token" in d)
check("dns/startup-probe", "dns_resolves" in d and "nanoDNS" in d)
check("log/path-printed", "log_path()" in d)
check("log/path-accessor", "log_path" in src("orbisrpc/log.c"))
check("build/hash-logged", "ORBISRPC_BUILD_HASH" in d)
check("reconnect/jitter-attempts", "reconnect_delay" in d and "attempt %d" in d)
check("reconnect/never-exits", "600" in d and "quiet persistence" in d)
check("notification/connected-once",
      'notify_once_boot("connected"' in disc and
      'notify_throttled("connected"' not in disc)
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
                             text=True, errors="replace").stdout
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
# NOTE (e35c47b base): eboot fast-switch debounce and atime-identity signals
# are not implemented in this tree, so there is nothing to contract-check.
# Re-add these gates if the features land.
check("detect/no-baked-table", "nametable" not in src("orbisrpc/detect.c").lower())
check("detect/builtin-homebrew", "builtin_title_name" in src("orbisrpc/detect.c"))
check("metadata/local-retro", "retro_local_scan" in src("orbisrpc/retro.c"))
_retro = src("orbisrpc/retro.c")
check("retro/nonblocking-bounded",
      "net_tcp_connect" in _retro and "body timeout" in _retro and
      "header timeout" in _retro)
_net = src("orbisrpc/netconnect.c")
check("net/bounded-connect",
      "O_NONBLOCK" in _net and "select(" in _net and "EINPROGRESS" in _net)
check("net/all-paths-bounded",
      all("net_tcp_connect" in src("orbisrpc/" + f)
          for f in ("ws.c", "tmdb.c", "art.c", "pkgzone.c", "retro.c",
                    "updater.c")))
check("goldhen/version-probe", "detect_goldhen_version" in src("orbisrpc/detect.c"))
check("lock/boot-stale", "lock_entry_stale" in src("orbisrpc/lock.c"))
# 11. iteration 17: presence frames are all-or-nothing; a stalled main loop
# re-arms presence; a wedged lock holder is reclaimable by a new injection.
check("presence/atomic-envelope",
      "presence_frame" in disc and "presence_send" in disc and
      'return presence_send(d, "presence"' in disc)
check("presence/send-failure-reconnects", disc.count("d->connected = 0") >= 2)
check("sched/loop-stall",
      "sched_loop_stall" in d and "sched_loop_stall" in src("orbisrpc/sched.h"))
check("lock/heartbeat-recovery",
      "LOCK_HEARTBEAT_S" in src("orbisrpc/lock.h") and
      "lock_refresh" in d and "lock_owned" in d)

# 12. license: GPL-3.0-or-later, no PFAL remnant, SPDX on every source file.
_lic = src("LICENSE")
check("license/gpl3",
      "GNU GENERAL PUBLIC LICENSE" in _lic and
      "Version 3, 29 June 2007" in _lic)
check("license/no-pfal",
      not any("PFAL" in src(p) for p in
              ("LICENSE", "README.md", "AGENTS.md", "NOTICE",
               "docs/ROADMAP.md", "docs/RELEASE_NOTES_1.1.md")))
_srcs = []
for _root, _dirs, _files in os.walk(ROOT):
    _dirs[:] = [x for x in _dirs if x not in
                ("third_party", "build", "build-sdk", ".git", "graphify-out",
                 ".superpowers", "plugin")]
    for _f in _files:
        if (_f.endswith((".c", ".h", ".py", ".sh", ".md", ".yml", ".yaml"))
                or _f in ("Makefile", ".gitignore", ".gitattributes")):
            _srcs.append(os.path.relpath(os.path.join(_root, _f), ROOT))
_missing = sorted(p for p in _srcs
                  if "SPDX-License-Identifier: GPL-3.0-or-later"
                  not in src(p))
check("license/spdx-headers", not _missing, ",".join(_missing[:5]))
check("license/attribution-notice",
      "GPLv3 section" in src("NOTICE") and
      "SirHumza and CyberMask367" in src("NOTICE"))
_release = src(".github/workflows/release.yml")
check("license/release-assets",
      "LICENSE" in _release and "NOTICE" in _release)

elf = os.path.join(ROOT, "build-sdk", "orbisrpc_sdk.elf")
if not os.path.exists(elf):
    # Host job has no SDK-built payload (sdk-payload job gates linkage
    # separately); skipping here instead of failing keeps CI honest.
    print("SKIP linkage/payload-built - no build-sdk elf in this job")
print(f"{len(fails)} failures" if fails else "E2E host simulation: ALL PASS")
sys.exit(1 if fails else 0)
