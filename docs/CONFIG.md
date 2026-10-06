# Config reference (`/data/orbisRPC/config.json`)

Only `token` is required. Everything else ships with working defaults
(see `cfg_defaults()` in `orbisrpc/cfg.c`). Junk values are clamped
(`poll_interval_s` is forced into 5–300); a `SET_ME` token logs a
reminder and the daemon waits instead of exiting.

## Auth

| Key | Default | Meaning |
|---|---|---|
| `token` | `"SET_ME"` | Discord user session token (**required**). Full account access — never share it, never commit it. Dies on password change / logout-all (log shows close `4004`); just paste a fresh one, no reinstall needed. |
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
| `presence_settings_text` | `"In Settings"` | Activity name while Settings is open (replaces the whole presence; the game timer underneath is kept). |

## Visibility flags (all default `true`)

A `false` flag still detects the title — it just stops telling Discord
about it, so a game underneath keeps showing. Exception: with
`show_idle` off, a closed game clears (nothing runs underneath home).

| Key | Hides |
|---|---|
| `show_firmware` | The `Firmware X.YY` line. |
| `show_idle` | Home screen, Settings, browser presence. |
| `show_media` | Media apps (Netflix, YouTube, Plex…) as `Watching`. |
| `show_homebrew` | Homebrew titles. Retro classics (PS1/PS2/PSP) are **not** affected. |

## Sources & art

| Key | Default | Meaning |
|---|---|---|
| `poll_interval_s` | `12` | Detection cadence (clamped 5–300). |
| `pkgzone_enabled` | `true` | Ask pkg-zone.com for **homebrew-only** names. Sends the title id to a third party — one boolean away from off. Retail titles never reach it. |
| `retro_enabled` | `true` | PS1/PS2/PSP names + covers from the retro indexes. |
| `art_base_url` | project icon pack | Per-title art source, resolved through Discord's `mp:` proxy at post time. |
| `large_art` / `small_art` / `browser_art` | project logos | Fallback artwork (large falls back to `home_art`, small to large). Raw `https` only renders via the `mp:` proxy — a typo here blanks the tile, nothing else. |
| `asset_idle` / `asset_playing` | empty | Uploaded Discord asset keys. A key needs no network; empty means the URLs above get used. |
| `home_art` | project logo | Legacy idle-tile fallback (predates `large_art`). |
| `auto_update` | `1` | Signed self-updates with boot rollback. |
| `debug` | `false` | Verbose per-poll logging. |

## Self-learned state (daemon-written, don't hand-edit)

- `titles` map — every resolved title is remembered, so later boots
  resolve instantly. Also the manual-override slot: one entry fixes a
  title forever.
- `session.json` / `playtime.log` — resume window (10 min, cross-restart)
  and the playtime ledger.
