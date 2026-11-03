#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 SirHumza and CyberMask367 - see NOTICE for attribution terms.
"""Host tests for scripts/playtime_report.py (no console, no network)."""
import datetime as dt
import os
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(HERE))

import playtime_report as pr  # noqa: E402


class PlaytimeReportTest(unittest.TestCase):
    def test_parse_jsonl(self):
        row = pr.parse_line(
            '{"title_id":"CUSA12345","name":"Game \\"One\\"",'
            '"started":1700000000,"ended":1700003600,"seconds":3600}')
        self.assertEqual(row, ("CUSA12345", 'Game "One"', 1700000000, 3600))

    def test_parse_legacy(self):
        self.assertEqual(pr.parse_line("CUSA12345 Some Game 120"),
                         ("CUSA12345", "Some Game", 0, 120))

    def test_junk_ignored(self):
        for line in ("", "   ", "not a row", '{"title_id":"X","seconds":0}',
                     '{"broken"'):
            self.assertIsNone(pr.parse_line(line), line)

    def test_summary_buckets(self):
        now = dt.datetime(2026, 10, 30, 12, 0, 0)  # Friday
        today = int(now.timestamp())
        yesterday = today - 86400
        old = today - 365 * 86400
        rows = [
            ("CUSA1", "A", today, 600),
            ("CUSA1", "A", yesterday, 300),
            ("CUSA2", "B", old, 60),
            ("LEGACY", "L", 0, 30),
        ]
        out = pr.summarize(rows, now=now)
        self.assertEqual(out["sessions"], 4)
        self.assertEqual(out["total_seconds"], 990)
        self.assertEqual(out["today_seconds"], 600)
        # Monday-start week includes yesterday (Thursday) and today.
        self.assertEqual(out["week_seconds"], 900)
        self.assertEqual(out["by_title"][0]["title_id"], "CUSA1")
        self.assertEqual(out["by_title"][0]["seconds"], 900)

    def test_load_and_cli(self):
        with tempfile.TemporaryDirectory() as d:
            path = os.path.join(d, "playtime.log")
            with open(path, "w", encoding="utf-8") as f:
                f.write('{"title_id":"CUSA1","name":"A","started":1,'
                        '"ended":61,"seconds":60}\n')
                f.write("CUSA2 Legacy 120\n")
                f.write("garbage\n")
            self.assertEqual(len(pr.load(path)), 2)
            self.assertEqual(pr.main([path]), 0)
            self.assertEqual(pr.main([path, "--json"]), 0)

    def test_missing_file(self):
        self.assertEqual(pr.main(["/nonexistent/playtime.log"]), 2)


if __name__ == "__main__":
    unittest.main()
