# Cover art pipeline

## Path A: Sony TMDB icon URLs (primary, fully automatic)

Every title lookup also queries Sony's TMDB service, which returns an
official 512x512 icon URL on Sony's CDN. The daemon sends it as
`assets.large_image` (Discord accepts external image URLs there). No
uploads, no hosting, no per-game work, works on any console. If the
service lacks the title or the network fails, the next path is tried.

## Path B: icon pack + external URLs (fallback, maintainer-hosted)

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

Current default: the public PS4-Rich-Presence-for-Discord application
(zorua98741/bshar1865, ID 858345055966461973), which already hosts
per-title art. Borrowed backend, credited here: if it ever goes away,
art degrades to nothing and everything else keeps working. Replace the
default with your own app ID to own the whole chain.

## Runtime behavior

URL pack wins when `art_base_url` is set, uploaded keys when only
`application_id` is set, no artwork otherwise (one log notice). Missing
art never crashes anything.
