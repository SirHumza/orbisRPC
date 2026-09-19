# Cover art pipeline

Two supported paths. The URL pack is the primary one: no uploads,
no per-game setup, no application needed for images.

## Path A: icon pack + external URLs (recommended)

The daemon sends `assets.large_image` as
`<art_base_url><lowercase titleId>.png` (Discord accepts external image
URLs in asset fields). Host the pack once, e.g. in this repo:

```
config/icons/cusa00740.png   (512x512 PNG, pulled from the console)
```

`scripts/sync_icons.sh` pulls every `/user/appmeta/<TITLEID>/icon0.png`
off the PS4 over FTP (read-only) into `config/icons/`. Set
`art_base_url` in config to the hosted prefix, e.g.
`https://raw.githubusercontent.com/<you>/orbisRPC/main/config/icons/`.
Every install then shows art with zero setup. Missing files degrade to
no image. Pack hosting is a maintainer decision (game art is not ours).

## Path B: shared Discord application (fallback)

Create an application, upload one PNG per game under Art Assets named
as the lowercase title ID, set `application_id` in config. The daemon
sends the asset key instead. Capped at 300 assets per app.

## Runtime behavior

URL pack wins when `art_base_url` is set, uploaded keys when only
`application_id` is set, no artwork otherwise (one log notice). Missing
art never crashes anything.
