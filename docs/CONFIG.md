# Config reference (`/data/orbisRPC/config.json`)

Only `token` is required. Everything else ships with working defaults
(see `cfg_defaults()` in `orbisrpc/cfg.c`). Junk values are clamped
(`poll_interval_s` is forced into 5–300); a `SET_ME` token logs a
reminder and the daemon waits instead of exiting.

## Auth

| Key | Default | Meaning |
|---|---|---|
| `token` | `"SET_ME"` | Discord user session token (**required**). Full account access — never share it, never commit it. Dies on password change / logout-all (log shows close `4004`); the daemon parks and re-reads this file every 10 s, so a fresh token takes effect without a reboot or reinstall. Surrounding whitespace and one extra pair of matching quotes are trimmed, so a token pasted from a browser works. An unquoted bare value is repaired in place instead of resetting the rest of the file. A rejected token always logs the exact reason (placeholder, length, empty segment, or the illegal character and its position). |
| `application_id` | `"1536977374795538532"` | Only needed for custom uploaded-asset images. |
| `enabled` | `1` | Master switch. |

## Presence text

Discord shows three lines: name, details, state (+ timer). Details says
what you're doing; state carries the firmware.

| Key | Default | Meaning |
|---|---|---|
| `presence_state` | `"On PS4"` | State line for games and home. |
| `presence_details_game` | `"Playing on PlayStation 4"` | Details line while a game is open. |
| `presence_details_home` | `"Idling on Home Menu"` | Details line on home. |
| `presence_details_media` | `"Watching on PlayStation 4"` | Details line for media apps (Netflix, YouTube, …). |
| `presence_settings_text` | `"In Settings"` | Activity name while Settings is open (replaces the whole presence; the game timer underneath is kept). |

## Visibility flags (all default `true`)

A `false` flag still detects the title — it just stops telling Discord
about it, so a game underneath keeps showing. Exception: with
`show_idle` off, a closed game clears (nothing runs underneath home).

| Key | Hides |
|---|---|
| `show_firmware` | The `Firmware X.YY` line. |
| `show_logo` | The PlayStation idle logo and small PlayStation badge on game art. Defaults to `true`. |
| `show_idle` | Home screen, Settings, browser presence. |
| `show_media` | Media apps (Netflix, YouTube, Plex…) as `Watching`. |
| `show_homebrew` | Homebrew titles. Retro classics (PS1/PS2/PSP) are **not** affected. |
| `status` | `online` (default), `idle`, `dnd`, or `invisible`; applies to gateway IDENTIFY and presence updates. |
| `timestamp_mode` | `elapsed` (default) shows session elapsed time; `none` omits timestamps. |

## Sources & art

| Key | Default | Meaning |
|---|---|---|
| `poll_interval_s` | `12` | Detection cadence (clamped 5–300). Detection probes are the expensive part; heartbeat/tick stays at ~1 s. |
| `presence_reconcile_s` | `900` | Re-post the current presence even when nothing changed (clamped 60–3600). Heals a tile that silently desynced. |
| `presence_hold_s` | `300` | Keep the last game tile for this long after the game closes (clamped 0–1800). The session and timer end immediately, so a relaunch starts at 0:00; this only stops a quick menu hop from blanking the tile. `0` clears at once. |
| `pkgzone_enabled` | `true` | Ask pkg-zone.com for **homebrew-only** names. Sends the title id to a third party — one boolean away from off. Retail titles never reach it. |
| `retro_enabled` | `true` | PS1/PS2/PSP names + covers. `/data/orbisRPC/ps1.json`, `ps2.json`, `psp.json` are scanned first; the remote retro index is only a fallback. |
| `art_base_url` | project icon pack | Per-title art source, resolved to an `mp:` mapping during detection and persisted in `games_cache.json`; a failed mapping is retried at most every 15 minutes. |
| `large_art` / `small_art` / `browser_art` | project logos | Fallback artwork (large falls back to `home_art`, small to large). Raw `https` only renders via the `mp:` proxy — a typo here blanks the tile, nothing else. Static art is resolved once at boot into an 8-entry cache. |
| `asset_idle` / `asset_playing` | empty | Uploaded Discord asset keys. A key needs no network; empty means the URLs above get used. |
| `home_art` | project logo | Legacy idle-tile fallback (predates `large_art`). |
| `auto_update` | `1` | Signed self-updates with boot rollback. |
| `debug` | `false` | Verbose per-poll logging. |

## Recovery & lifecycle

These control the daemon's own timers. Defaults match the built-in 1.1
behaviour, so leave them out unless you need a different cadence. Every
value is clamped; the daemon logs the clamp when it changes one.

| Key | Default | Meaning |
|---|---|---|
| `lock_heartbeat_s` | `60` | How often the daemon touches `daemon.lock` to prove it is still making progress (clamped 30–600). |
| `lock_stale_s` | `600` | Reclaim a lock that has not been touched for this long (clamped 120–86400, and forced to at least 2× `lock_heartbeat_s`). Lower values recover a wedged daemon faster; too low risks reclaiming one that is blocked in a slow network call. |
| `reconnect_max_s` | `600` | Slow-cadence ceiling after 10 consecutive gateway failures (clamped 60–3600). The first 10 retries stay at 60 s or less; this is the quiet-persistence delay after that. |
| `timesync_interval_s` | `3600` | NTP refresh cadence (clamped 300–86400). One host per tick; the boot sweep still tries all three. |
| `log_max_kb` | `512` | Rotate `log.txt` at this size in KiB (clamped 64–8192). Raise it for long-run captures; the previous file is kept as `log.old.txt`. |

## Per-title rules

`titles` maps title IDs to display names. `title_rules` can add custom details,
art, suppression, or an honest end time when a title's duration is known:

```json
"titles": {"CUSA11995": "Example Game"},
"title_rules": {
  "CUSA11995": {
    "details": "Playing",
    "art": "https://example.org/cover.png",
    "duration_min": 90
  }
}
```

`duration_min` adds `timestamps.end` only when `timestamp_mode` is `elapsed`.
Omit it when the game's duration is not known.

## Client fingerprint (`fp_*`)

These are the desktop-client properties sent in IDENTIFY and the REST
headers. Defaults are generated from `scripts/fingerprint.json` into
`orbisrpc/fingerprint_gen.h`; config values override them without a rebuild.
Refresh the committed tuple with `python3 scripts/update_fingerprint.py
--fetch` (see `docs/DAEMON.md`).

| Key | Default | Meaning |
|---|---|---|
| `fp_client_version` | generated | Desktop version, e.g. `1.0.9266`. |
| `fp_client_build` | generated | Web bundle build number, e.g. `655747`. Must match the version. |
| `fp_os` / `fp_os_version` | `Windows` / `10.0.22631` | Reported OS. |
| `fp_arch` | `x64` | Reported architecture. |
| `fp_locale` / `fp_timezone` | `en-US` / `Europe/London` | Locale and timezone in REST headers. |
| `fp_user_agent` | generated | Full desktop user agent; its `discord/<version>` prefix follows `fp_client_version`. |
| `fp_installation_id` | generated once | Stable per-console UUID; leave it alone unless you deliberately want a new identity. |

## Self-learned state (daemon-written, don't hand-edit)

- `games_cache.json` — resolved title id → name, raw cover URL, and the
  Discord `mp:` mapping. Fresh entries skip every network resolver; stale
  entries are still used as an offline fallback. Capped at 256 entries and
  512 KB; least-recently-refreshed is evicted when full.
- `playtime.log` — one JSON object per finished session. Read it on a host
  with `python3 scripts/playtime_report.py` (also accepts the old
  space-separated format). No session state is restored across a restart:
  `started` is process-local in 1.1.
- `diagnostic.txt` — written only when you create `diagnose.request`; safe
  to share, the token is redacted. See `docs/TROUBLESHOOTING.md`.

## File handling

The file is chmodded to `0600` on load and save — it holds a full-access
token. Saves are atomic (tmp + fsync + rename) and start from the document
already on disk, so keys this build does not know (a newer version's, or
your own notes) survive a rewrite instead of being deleted. A key this
build does know is replaced in place, never duplicated.

Before every rewrite the current file is copied to `config.json.bak`. A
config written by 1.0 (no `schema_version`, or `1`) also gets a one-time
`config.v1.bak` that later saves never touch. Both backups are `0600`.
`schema_version` is `2` after the first save; a file from a newer schema
is loaded but never rewritten by this build.

The daemon logs the path it is writing (`log: /data/orbisRPC/log.txt`) on
every start. To stop it, create an empty `/data/orbisRPC/stop`; the
process exits within a second. The file stays until the next launch so a
second instance also sees it. No lock deletion or reboot is needed.

Optional offline metadata: `ps1.json`, `ps2.json`, `psp.json` in
`/data/orbisRPC/` are read before any network lookup. Release assets carry
the current copies; they are small enough to FTP once.

<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- Copyright (C) 2026 SirHumza and CyberMask367 - see NOTICE for attribution terms. -->
