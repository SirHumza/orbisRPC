# Cover art pipeline

How a game gets its tile. Order matters — first hit wins.

## Path 1: Sony TMDB live (automatic)

Every title lookup queries Sony's TMDB service, which returns an
official icon URL on Sony's CDN. Requires the console to reach
`tmdb.np.dl.playstation.net` — without the nanoDNS exception this
path is dead (see Troubleshooting). No uploads, no hosting, no
per-game work.

## Path 2: icon pack (fallback, maintainer-hosted)

The daemon sends `<art_base_url><lowercase titleId>.png`. Default is
the project pack (`orbisrpc-host`); host your own by setting
`art_base_url`. `scripts/sync_icons.sh` pulls every
`/user/appmeta/<TITLEID>/icon0.png` off the PS4 over FTP (read-only)
into `config/icons/`. Missing files degrade to no image.

## Path 3: uploaded Discord asset keys

Set `asset_idle` / `asset_playing` to an uploaded key to prefer that
instead — keys need no network at all. Current default
`application_id` is `1536977374795538532`. Capped at 300 assets per app.

## Hard rule (verified on hardware)

Raw external URLs and dangling keys **drop the entire activity**
silently — name, timer, everything. So every URL above is resolved
through Discord's `mp:` external-assets proxy at post time, and
dangling keys are dropped, never sent. Missing art never crashes
anything; worst case is a tile with no image.
