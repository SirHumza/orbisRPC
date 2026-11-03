#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 SirHumza and CyberMask367 - see NOTICE for attribution terms.
"""Read the orbisRPC playtime ledger.

The daemon appends one JSON object per finished session:
    {"title_id":"CUSA...","name":"...","started":...,"ended":...,"seconds":...}
Older builds wrote "<title_id> <name> <seconds>" lines; both are accepted so
an upgrade does not lose history. Pure stdlib, no network.
"""
import argparse
import datetime as dt
import json
import sys
from collections import defaultdict

DEFAULT_LEDGER = "/data/orbisRPC/playtime.log"


def parse_line(line):
    """Return (title_id, name, started, seconds) or None for junk."""
    line = line.strip()
    if not line:
        return None
    if line.startswith("{"):
        try:
            obj = json.loads(line)
        except ValueError:
            return None
        tid = str(obj.get("title_id") or "").strip()
        name = str(obj.get("name") or tid).strip()
        try:
            secs = int(obj.get("seconds") or 0)
        except (TypeError, ValueError):
            return None
        try:
            started = int(obj.get("started") or 0)
        except (TypeError, ValueError):
            started = 0
        if not tid or secs <= 0:
            return None
        return tid, name, started, secs
    parts = line.split()
    if len(parts) < 3:
        return None
    try:
        secs = int(parts[-1])
    except ValueError:
        return None
    if secs <= 0:
        return None
    return parts[0], " ".join(parts[1:-1]) or parts[0], 0, secs


def load(path):
    rows = []
    with open(path, "r", encoding="utf-8", errors="replace") as f:
        for line in f:
            row = parse_line(line)
            if row:
                rows.append(row)
    return rows


def summarize(rows, now=None):
    now = now or dt.datetime.now()
    today = now.date()
    week_start = today - dt.timedelta(days=today.weekday())
    total = 0
    by_title = defaultdict(int)
    today_s = 0
    week_s = 0
    for tid, name, started, secs in rows:
        total += secs
        by_title[(tid, name)] += secs
        # Legacy rows have no date; count them in the total only, never in a
        # day bucket where they would silently inflate today's number.
        if started > 0:
            day = dt.datetime.fromtimestamp(started).date()
            if day == today:
                today_s += secs
            if day >= week_start:
                week_s += secs
    return {
        "sessions": len(rows),
        "total_seconds": total,
        "today_seconds": today_s,
        "week_seconds": week_s,
        "by_title": [
            {"title_id": tid, "name": name, "seconds": secs}
            for (tid, name), secs in sorted(by_title.items(),
                                            key=lambda kv: -kv[1])
        ],
    }


def hms(secs):
    h, rem = divmod(int(secs), 3600)
    m, s = divmod(rem, 60)
    return f"{h}h {m:02d}m {s:02d}s" if h else f"{m}m {s:02d}s"


def main(argv=None):
    ap = argparse.ArgumentParser(description="orbisRPC playtime report")
    ap.add_argument("ledger", nargs="?", default=DEFAULT_LEDGER)
    ap.add_argument("--json", action="store_true", help="machine-readable output")
    args = ap.parse_args(argv)
    try:
        rows = load(args.ledger)
    except OSError as e:
        print(f"cannot read {args.ledger}: {e}", file=sys.stderr)
        return 2
    out = summarize(rows)
    if args.json:
        print(json.dumps(out, indent=2))
        return 0
    print(f"sessions: {out['sessions']}")
    print(f"total:    {hms(out['total_seconds'])}")
    print(f"today:    {hms(out['today_seconds'])}")
    print(f"this week:{hms(out['week_seconds'])}")
    if out["by_title"]:
        print("\nper title:")
        for row in out["by_title"]:
            print(f"  {hms(row['seconds']):>12}  {row['name']} ({row['title_id']})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
