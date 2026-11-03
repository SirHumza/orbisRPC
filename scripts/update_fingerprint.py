#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 SirHumza and CyberMask367 - see NOTICE for attribution terms.
"""update_fingerprint.py - review gate and updater for the client fingerprint.

The daemon reports one desktop-client identity tuple everywhere: the gateway
WebSocket handshake, IDENTIFY properties, and REST X-Super-Properties. Those
values live in `scripts/fingerprint.json`, are generated into
`orbisrpc/fingerprint_gen.h`, and are used as config defaults.

Where each field comes from:

  client_version      Discord's stable Windows installer redirect
  client_build_number `discord.com/app` BUILD_NUMBER
  chrome_version      the official Electron release feed, for the reviewed
                      electron_version (Electron's release JSON publishes the
                      exact Chromium build it bundles)
  user_agent          rendered from the fields above, so it cannot drift
  everything else     reviewed constants (OS build, locale, timezone)

Discord does not publish which Electron build its installer ships, so
`electron_version` is a reviewed field; `--electron X.Y.Z` changes it and
re-derives Chromium from the feed. A runtime fetch remains out of scope: the
console keeps the compiled, reviewed tuple.

Usage:
  python3 scripts/update_fingerprint.py --check
  python3 scripts/update_fingerprint.py --fetch
  python3 scripts/update_fingerprint.py --fetch --electron 42.11.18
  python3 scripts/update_fingerprint.py --set 1.0.9059 657850 --source manual
"""
import argparse
import datetime as dt
import json
import os
import re
import sys
import urllib.error
import urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

import gen_fingerprint_header as gen  # noqa: E402

JSON_PATH = gen.JSON_PATH
MAX_AGE_DAYS = 45

# Stable Windows installer. The endpoint answers with a redirect whose path
# carries the client version; following it would download ~150 MB. Discord
# currently serves the x86 installer even for arch=x64, so both are accepted.
VERSION_URL = ("https://discord.com/api/downloads/distributions/app/"
               "installers/latest?arch=x64&channel=stable&platform=win")
# The desktop client reports the web bundle's BUILD_NUMBER as
# client_build_number. It is an official Discord page, not a community feed.
APP_URL = "https://discord.com/app"
# Electron's release metadata: version -> exact bundled Chromium.
ELECTRON_URL = "https://releases.electronjs.org/releases.json"
USER_AGENT = "orbisRPC-fingerprint/1.0"


def parse_stable_version(location):
    """Extract 1.0.NNNN from the installer redirect URL."""
    m = re.search(r"/stable/win/(?:x64|x86)/(\d+\.\d+\.\d+)/", location or "")
    return m.group(1) if m else None


def parse_build_number(html):
    """Extract the web bundle BUILD_NUMBER from discord.com/app."""
    m = re.search(r'"BUILD_NUMBER"\s*:\s*"(\d+)"', html or "")
    return int(m.group(1)) if m else None


def parse_electron_chrome(doc, version):
    """Return the Chromium version bundled by a stable Electron release."""
    for row in doc:
        if not isinstance(row, dict):
            continue
        if row.get("version") == version and isinstance(row.get("chrome"), str):
            return row["chrome"]
    return None


class _NoRedirect(urllib.request.HTTPRedirectHandler):
    """Surface the 302 so the version can be read without the installer."""

    def redirect_request(self, req, fp, code, msg, headers, newurl):
        return None


def _get(url, timeout):
    req = urllib.request.Request(url, headers={"User-Agent": USER_AGENT})
    with urllib.request.urlopen(req, timeout=timeout) as r:
        return r.read()


def fetch_stable_version(timeout=20):
    opener = urllib.request.build_opener(_NoRedirect)
    req = urllib.request.Request(VERSION_URL,
                                 headers={"User-Agent": USER_AGENT})
    try:
        with opener.open(req, timeout=timeout) as r:
            location = r.geturl()
    except urllib.error.HTTPError as e:
        location = e.headers.get("Location", "")
        e.close()
    version = parse_stable_version(location)
    if not version:
        raise RuntimeError("stable download did not redirect to a version")
    return version


def fetch_build_number(timeout=20):
    html = _get(APP_URL, timeout)[:2 * 1024 * 1024].decode("utf-8", "replace")
    build = parse_build_number(html)
    if build is None:
        raise RuntimeError("discord.com/app did not publish BUILD_NUMBER")
    return build


def fetch_electron_chrome(version, timeout=30):
    rows = json.loads(_get(ELECTRON_URL, timeout).decode("utf-8"))
    chrome = parse_electron_chrome(rows, version)
    if not chrome:
        raise RuntimeError("Electron %s is not in the release feed" % version)
    return chrome


def render_user_agent(doc):
    """The desktop UA embeds the three versions; derive it, never type it."""
    return ("Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
            "(KHTML, like Gecko) discord/%s Chrome/%s Electron/%s Safari/537.36"
            % (doc["client_version"], doc["chrome_version"],
               doc["electron_version"]))


def fetch_doc(today=None, electron=None, base=None):
    """Build a candidate document from official Discord/Electron endpoints."""
    today = today or dt.datetime.now(dt.timezone.utc).date().isoformat()
    doc = dict(base or gen.load())
    doc["schema"] = 2
    doc["client_version"] = fetch_stable_version()
    doc["client_build_number"] = fetch_build_number()
    if electron:
        doc["electron_version"] = electron
    doc["chrome_version"] = fetch_electron_chrome(doc["electron_version"])
    doc["browser_version"] = doc["chrome_version"]
    doc["user_agent"] = render_user_agent(doc)
    doc["fetched_at"] = today
    doc["source"] = "discord-download+electron-releases"
    return doc


def non_regression(old, new):
    """Refuse to move the build number backwards."""
    errors = []
    try:
        if new["client_build_number"] < old["client_build_number"]:
            errors.append("client_build_number would go backwards (%d -> %d)"
                          % (old["client_build_number"],
                             new["client_build_number"]))
    except (KeyError, TypeError):
        pass
    return errors


def age_days(doc, today=None):
    today = today or dt.date.today()
    fetched = dt.date.fromisoformat(doc["fetched_at"])
    return (today - fetched).days


def write_json(doc, path=JSON_PATH):
    tmp = path + ".new"
    with open(tmp, "w", encoding="utf-8", newline="\n") as f:
        json.dump(doc, f, indent=2)
        f.write("\n")
    os.replace(tmp, path)


def main(argv=None):
    ap = argparse.ArgumentParser()
    ap.add_argument("--check", action="store_true",
                    help="validate the committed tuple and its age")
    ap.add_argument("--fetch", action="store_true",
                    help="fetch the current stable tuple from Discord")
    ap.add_argument("--electron", default=None,
                    help="reviewed Electron version to use with --fetch")
    ap.add_argument("--set", nargs=2, metavar=("VERSION", "BUILD"),
                    help="write a reviewed version/build pair")
    ap.add_argument("--source", default="manual",
                    choices=list(gen.SOURCES))
    ap.add_argument("--date", default=None,
                    help="fetched_at for --set (default: today UTC)")
    args = ap.parse_args(argv)

    if args.fetch and args.set:
        print("fingerprint: --fetch and --set are mutually exclusive",
              file=sys.stderr)
        return 1

    if args.fetch:
        try:
            doc = fetch_doc(electron=args.electron)
        except Exception as e:  # network, DNS, TLS, malformed response
            print("fingerprint: fetch failed: %s" % e, file=sys.stderr)
            return 1
        old = gen.load()
        errors = gen.validate(doc) + non_regression(old, doc)
        if errors:
            print("fingerprint: refusing fetched tuple: " + "; ".join(errors),
                  file=sys.stderr)
            return 1
        if doc == old:
            if not gen.header_in_sync(old):
                gen.write_header(old)
            print("fingerprint: %s build %d unchanged"
                  % (doc["client_version"], doc["client_build_number"]))
            return 0
        write_json(doc)
        gen.write_header(doc)
        print("fingerprint: %s build %d (electron %s, chromium %s, %s)"
              % (doc["client_version"], doc["client_build_number"],
                 doc["electron_version"], doc["chrome_version"],
                 doc["fetched_at"]))
        return 0

    if args.set:
        version, build = args.set
        try:
            build_no = int(build)
        except ValueError:
            print("fingerprint: build must be an integer", file=sys.stderr)
            return 1
        fetched = args.date or dt.datetime.now(dt.timezone.utc).date().isoformat()
        doc = dict(gen.load())
        doc.update({
            "schema": 2,
            "client_version": version,
            "client_build_number": build_no,
            "fetched_at": fetched,
            "source": args.source,
        })
        doc["user_agent"] = render_user_agent(doc)
        errors = gen.validate(doc)
        old = gen.load()
        errors += non_regression(old, doc)
        if errors:
            print("fingerprint: refusing update: " + "; ".join(errors),
                  file=sys.stderr)
            return 1
        write_json(doc)
        gen.write_header(doc)
        print("fingerprint: %s build %d (%s)" % (version, build_no, fetched))
        return 0

    if not args.check:
        ap.print_help()
        return 2

    doc = gen.load()
    errors = gen.validate(doc)
    if errors:
        print("fingerprint.json invalid: " + "; ".join(errors), file=sys.stderr)
        return 1
    if not gen.header_in_sync(doc):
        print("fingerprint_gen.h is out of sync with fingerprint.json",
              file=sys.stderr)
        return 1
    age = age_days(doc)
    print("fingerprint: client %s build %d electron %s chromium %s, %d days old "
          "(limit %d)"
          % (doc["client_version"], doc["client_build_number"],
             doc["electron_version"], doc["chrome_version"], age,
             MAX_AGE_DAYS))
    if age > MAX_AGE_DAYS:
        if os.environ.get("ALLOW_STALE_FINGERPRINT") == "1":
            print("fingerprint: stale but ALLOW_STALE_FINGERPRINT=1 is set",
                  file=sys.stderr)
            return 0
        print("fingerprint: stale; refresh scripts/fingerprint.json",
              file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
