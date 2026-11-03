#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 SirHumza and CyberMask367 - see NOTICE for attribution terms.
"""gen_fingerprint_header.py - turn the reviewed fingerprint into a C header.

`scripts/fingerprint.json` is the single source of truth for every identity
field the daemon reports: the gateway handshake User-Agent, the IDENTIFY
properties, and the REST X-Super-Properties header. The header is committed
so the PS4 build never depends on Python; `--check` verifies that the two
agree.

Usage:
  python3 scripts/gen_fingerprint_header.py            # rewrite if changed
  python3 scripts/gen_fingerprint_header.py --check    # exit 1 if out of sync
"""
import argparse
import datetime as dt
import json
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
JSON_PATH = os.path.join(HERE, "fingerprint.json")
HEADER_PATH = os.path.join(HERE, "..", "orbisrpc", "fingerprint_gen.h")

SEMVER_RE = re.compile(r"^\d+\.\d+\.\d+$")
CHROME_RE = re.compile(r"^\d+\.\d+\.\d+\.\d+$")
LOCALE_RE = re.compile(r"^[a-z]{2}-[A-Z]{2}$")
BUILD_MIN = 100000
BUILD_MAX = 9999999
SOURCES = ("manual", "discord-download", "discord-download+electron-releases",
           "community")


def load(path=JSON_PATH):
    with open(path, "r", encoding="utf-8") as f:
        return json.load(f)


def _str_field(doc, key, errors, maxlen):
    value = doc.get(key)
    if not isinstance(value, str) or not value or len(value) > maxlen:
        errors.append("%s must be a non-empty string of at most %d chars"
                      % (key, maxlen))
        return ""
    return value


def validate(doc):
    """Return a list of human-readable problems; empty means usable."""
    errors = []
    if not isinstance(doc, dict):
        return ["document is not an object"]
    if doc.get("schema") != 2:
        errors.append("schema must be 2")
    version = doc.get("client_version")
    if not isinstance(version, str) or not SEMVER_RE.match(version):
        errors.append("client_version must look like 1.0.9059")
    build = doc.get("client_build_number")
    if isinstance(build, bool) or not isinstance(build, int):
        errors.append("client_build_number must be an integer")
    elif not BUILD_MIN <= build <= BUILD_MAX:
        errors.append("client_build_number outside %d..%d" % (BUILD_MIN, BUILD_MAX))

    electron = _str_field(doc, "electron_version", errors, 24)
    chrome = _str_field(doc, "chrome_version", errors, 32)
    browser = _str_field(doc, "browser_version", errors, 32)
    if electron and not SEMVER_RE.match(electron):
        errors.append("electron_version must look like 42.11.18")
    if chrome and not CHROME_RE.match(chrome):
        errors.append("chrome_version must look like 148.0.7778.280")
    if browser and not CHROME_RE.match(browser):
        errors.append("browser_version must look like 148.0.7778.280")
    if chrome and browser and chrome != browser:
        # Desktop reports the Chromium version in both fields. A mismatch is
        # the kind of detail that makes the tuple look synthetic.
        errors.append("browser_version must equal chrome_version")

    _str_field(doc, "os", errors, 15)
    _str_field(doc, "os_version", errors, 31)
    _str_field(doc, "os_sdk_version", errors, 15)
    _str_field(doc, "os_arch", errors, 15)
    locale = _str_field(doc, "locale", errors, 15)
    if locale and not LOCALE_RE.match(locale):
        errors.append("locale must look like en-US")
    _str_field(doc, "timezone", errors, 47)
    ua = _str_field(doc, "user_agent", errors, 255)
    if ua:
        for needle in ("discord/", "Chrome/", "Electron/"):
            if needle not in ua:
                errors.append("user_agent must contain " + needle)
        if version and ("discord/%s" % version) not in ua:
            errors.append("user_agent discord/<client_version> is out of sync")
        if chrome and ("Chrome/%s" % chrome) not in ua:
            errors.append("user_agent Chrome/<chrome_version> is out of sync")
        if electron and ("Electron/%s" % electron) not in ua:
            errors.append("user_agent Electron/<electron_version> is out of sync")

    fetched = doc.get("fetched_at")
    if not isinstance(fetched, str) or len(fetched) != 10:
        errors.append("fetched_at must be YYYY-MM-DD")
    else:
        try:
            dt.date.fromisoformat(fetched)
        except ValueError:
            errors.append("fetched_at must be a real YYYY-MM-DD date")
    source = doc.get("source")
    if source not in SOURCES:
        errors.append("source must be one of " + ", ".join(SOURCES))
    return errors


def c_string(value):
    """Escape a value for a C string literal (only \\ and " can appear)."""
    return value.replace("\\", "\\\\").replace('"', '\\"')


def render(doc):
    """Deterministic header text for a validated document."""
    fields = (
        ("VERSION", doc["client_version"]),
        ("ELECTRON", doc["electron_version"]),
        ("CHROME", doc["chrome_version"]),
        ("BROWSER_VERSION", doc["browser_version"]),
        ("OS", doc["os"]),
        ("OS_VERSION", doc["os_version"]),
        ("OS_SDK_VERSION", doc["os_sdk_version"]),
        ("ARCH", doc["os_arch"]),
        ("LOCALE", doc["locale"]),
        ("TIMEZONE", doc["timezone"]),
        ("USER_AGENT", doc["user_agent"]),
        ("FETCHED_AT", doc["fetched_at"]),
    )
    lines = [
        "/* SPDX-License-Identifier: GPL-3.0-or-later",
        " * Copyright (C) 2026 SirHumza and CyberMask367 - see NOTICE for attribution terms.",
        " */",
        "/* Generated by scripts/gen_fingerprint_header.py from",
        " * scripts/fingerprint.json. Do not edit by hand; run the generator",
        " * after a reviewed fingerprint update. */",
        "#ifndef ORBISRPC_FINGERPRINT_GEN_H",
        "#define ORBISRPC_FINGERPRINT_GEN_H",
    ]
    for name, value in fields:
        lines.append('#define ORBISRPC_FP_%s "%s"' % (name, c_string(value)))
    lines.append("#define ORBISRPC_FP_BUILD %d" % doc["client_build_number"])
    lines.append("#endif")
    lines.append("")
    return "\n".join(lines)


def write_header(doc, path=HEADER_PATH):
    errors = validate(doc)
    if errors:
        raise SystemExit("fingerprint: refusing to generate: " + "; ".join(errors))
    text = render(doc)
    try:
        with open(path, "r", encoding="utf-8") as f:
            if f.read() == text:
                return False
    except FileNotFoundError:
        pass
    tmp = path + ".new"
    with open(tmp, "w", encoding="utf-8", newline="\n") as f:
        f.write(text)
    os.replace(tmp, path)
    return True


def header_in_sync(doc, path=HEADER_PATH):
    try:
        with open(path, "r", encoding="utf-8") as f:
            return f.read() == render(doc)
    except FileNotFoundError:
        return False


def main(argv=None):
    ap = argparse.ArgumentParser()
    ap.add_argument("--check", action="store_true",
                    help="exit 1 when the committed header is out of sync")
    args = ap.parse_args(argv)
    doc = load()
    errors = validate(doc)
    if errors:
        print("fingerprint.json invalid: " + "; ".join(errors), file=sys.stderr)
        return 1
    if args.check:
        if not header_in_sync(doc):
            print("fingerprint_gen.h is stale; run "
                  "python3 scripts/gen_fingerprint_header.py", file=sys.stderr)
            return 1
        return 0
    changed = write_header(doc)
    print("fingerprint header %s" % ("updated" if changed else "unchanged"))
    return 0


if __name__ == "__main__":
    sys.exit(main())
