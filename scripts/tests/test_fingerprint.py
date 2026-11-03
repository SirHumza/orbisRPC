#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 SirHumza and CyberMask367 - see NOTICE for attribution terms.
"""Host tests for the fingerprint pipeline (no network, no C build)."""
import datetime as dt
import json
import os
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(HERE))

import gen_fingerprint_header as gen  # noqa: E402
import update_fingerprint as upd  # noqa: E402


def valid_doc(**over):
    doc = {
        "schema": 2,
        "client_version": "1.0.9059",
        "client_build_number": 657850,
        "electron_version": "42.11.18",
        "chrome_version": "148.0.7778.280",
        "browser_version": "148.0.7778.280",
        "os": "Windows",
        "os_version": "10.0.22631",
        "os_sdk_version": "10",
        "os_arch": "x64",
        "locale": "en-US",
        "timezone": "Europe/London",
        "user_agent": ("Mozilla/5.0 (Windows NT 10.0; Win64; x64) "
                       "AppleWebKit/537.36 (KHTML, like Gecko) "
                       "discord/1.0.9059 Chrome/148.0.7778.280 "
                       "Electron/42.11.18 Safari/537.36"),
        "fetched_at": "2026-10-30",
        "source": "manual",
    }
    doc.update(over)
    return doc


class TestFetchParsing(unittest.TestCase):
    def test_parse_stable_version(self):
        for arch in ("x64", "x86"):
            url = ("https://stable.dl2.discordapp.net/distro/app/stable/win/%s/"
                   "1.0.9059/DiscordSetup.exe" % arch)
            self.assertEqual(upd.parse_stable_version(url), "1.0.9059")

    def test_rejects_wrong_platform_or_missing_version(self):
        for bad in (
            "https://stable.dl2.discordapp.net/distro/app/stable/osx/"
            "universal/0.0.420/Discord.dmg",
            "https://stable.dl2.discordapp.net/distro/app/stable/win/x64/latest/DiscordSetup.exe",
            "",
        ):
            self.assertIsNone(upd.parse_stable_version(bad), bad)

    def test_parse_build_number(self):
        html = 'window.__BOOT__={"BUILD_NUMBER":"657850","RELEASE_CHANNEL":"stable"};'
        self.assertEqual(upd.parse_build_number(html), 657850)

    def test_rejects_missing_or_malformed_build(self):
        for bad in ('"BUILD_NUMBER":"abc"', '{"BUILD_NUMBER":657850}', ""):
            self.assertIsNone(upd.parse_build_number(bad), bad)

    def test_electron_chrome_mapping(self):
        feed = [
            {"version": "42.11.18", "chrome": "148.0.7778.280"},
            {"version": "42.11.19-nightly.20261029", "chrome": "149.0.0.1"},
        ]
        self.assertEqual(upd.parse_electron_chrome(feed, "42.11.18"),
                         "148.0.7778.280")
        self.assertIsNone(upd.parse_electron_chrome(feed, "41.0.0"))

    def test_rendered_user_agent_matches_fields(self):
        doc = valid_doc()
        self.assertEqual(upd.render_user_agent(doc), doc["user_agent"])

    def test_fetched_doc_validates(self):
        candidate = valid_doc(source="discord-download+electron-releases")
        self.assertEqual(gen.validate(candidate), [])


class TestValidation(unittest.TestCase):
    def test_accepts_reviewed_tuple(self):
        self.assertEqual(gen.validate(valid_doc()), [])

    def test_rejects_malformed_version(self):
        for bad in ("1.0", "1.0.9266-beta", "v1.0.9266", ""):
            self.assertTrue(gen.validate(valid_doc(client_version=bad)), bad)

    def test_rejects_huge_or_tiny_build(self):
        self.assertTrue(gen.validate(valid_doc(client_build_number=99999999)))
        self.assertTrue(gen.validate(valid_doc(client_build_number=42)))

    def test_rejects_bool_build(self):
        self.assertTrue(gen.validate(valid_doc(client_build_number=True)))

    def test_rejects_missing_and_bad_date(self):
        for bad in ("2026-10-29T00:00:00Z", "29-10-2026", ""):
            self.assertTrue(gen.validate(valid_doc(fetched_at=bad)), bad)

    def test_rejects_unknown_source(self):
        self.assertTrue(gen.validate(valid_doc(source="scraped")))

    def test_browser_version_must_match_chrome(self):
        bad = valid_doc(browser_version="37.2.6")
        self.assertTrue(gen.validate(bad))

    def test_user_agent_must_embed_every_version(self):
        for old, new in (("1.0.9059", "1.0.9000"),
                         ("148.0.7778.280", "149.0.0.0"),
                         ("42.11.18", "42.0.0")):
            doc = valid_doc()
            doc["user_agent"] = doc["user_agent"].replace(old, new)
            self.assertTrue(gen.validate(doc), old)

    def test_rejects_old_schema(self):
        self.assertTrue(gen.validate(valid_doc(schema=1)))


class TestHeader(unittest.TestCase):
    def test_render_is_deterministic(self):
        doc = valid_doc()
        self.assertEqual(gen.render(doc), gen.render(json.loads(json.dumps(doc))))
        self.assertIn('#define ORBISRPC_FP_VERSION "1.0.9059"', gen.render(doc))
        self.assertIn("#define ORBISRPC_FP_BUILD 657850", gen.render(doc))
        self.assertIn('#define ORBISRPC_FP_ELECTRON "42.11.18"', gen.render(doc))
        self.assertIn('discord/1.0.9059 Chrome/148.0.7778.280', gen.render(doc))

    def test_write_is_idempotent(self):
        doc = valid_doc()
        with tempfile.TemporaryDirectory() as d:
            path = os.path.join(d, "fingerprint_gen.h")
            self.assertTrue(gen.write_header(doc, path))
            with open(path, encoding="utf-8") as f:
                first = f.read()
            self.assertFalse(gen.write_header(doc, path))  # unchanged
            with open(path, encoding="utf-8") as f:
                self.assertEqual(f.read(), first)
            self.assertTrue(gen.header_in_sync(doc, path))
            self.assertFalse(gen.header_in_sync(valid_doc(client_build_number=1),
                                                path))

    def test_invalid_doc_never_writes(self):
        with tempfile.TemporaryDirectory() as d:
            path = os.path.join(d, "fingerprint_gen.h")
            with self.assertRaises(SystemExit):
                gen.write_header(valid_doc(client_version="nope"), path)
            self.assertFalse(os.path.exists(path))


class TestUpdate(unittest.TestCase):
    def test_staleness_window(self):
        doc = valid_doc(fetched_at="2026-01-01")
        self.assertGreater(upd.age_days(doc, today=dt.date(2026, 10, 30)), 45)

    def test_non_regression_blocks_backwards_build(self):
        old = valid_doc()
        new = valid_doc(client_build_number=657849)
        self.assertTrue(upd.non_regression(old, new))
        self.assertEqual(upd.non_regression(old, valid_doc(client_build_number=657851)), [])

    def test_committed_file_is_valid_and_fresh(self):
        doc = gen.load()
        self.assertEqual(gen.validate(doc), [])
        self.assertTrue(gen.header_in_sync(doc))
        self.assertEqual(upd.render_user_agent(doc), doc["user_agent"])


if __name__ == "__main__":
    unittest.main()
