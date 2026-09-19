# Cover art pipeline

Discord serves Rich Presence images only from assets uploaded to a
Discord application. There is no URL mode and no automatic official art
for custom presences. One shared app covers every install.

## One-time maintainer setup

1. Create an application at https://discord.com/developers/applications.
2. Rich Presence, Art Assets, upload one PNG per game. Asset name must be
   the lowercase title ID, e.g. `cusa00740` for Terraria. 512x512 PNG.
3. Copy the Application ID into the repo default (`config/config.json`
   `application_id`) so every install inherits it.

## Where icons live on the PS4

`/user/appmeta/<TITLEID>/icon0.png` (around 50KB). Pull over FTP:

```
get /user/appmeta/CUSA00740/icon0.png -> cusa00740.png
```

Upload as asset `cusa00740`.

## Runtime behavior

The daemon sends `assets: { large_image: "<lower titleId>",
large_text: "<game name>" }` only when `application_id` is set. Without
it, presence posts with no artwork and logs one notice. Missing assets
for a title also degrade to no image. Nothing crashes on absent art.
