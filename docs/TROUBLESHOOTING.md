# Troubleshooting

Start with the symptom and read the matching log before reinstalling. The daemon
log is `/data/orbisRPC/log.txt`; the setup app writes `/data/orbisRPC/install.log`
and `/data/orbisRPC/menu.log` (or `/data/menu.log` if its directory is not yet
available). Remove tokens and personal data before sharing logs.

## No activity appears in Discord

1. Confirm that GoldHEN is running and the payload is installed at
   `/data/payloads/orbisRPC.elf`.
2. Check `/data/orbisRPC/config.json`. It must be valid JSON and its `token`
   value must not be `SET_ME`.
3. Check the daemon log for config errors, TLS failures, gateway close codes,
   and title detection messages.
4. If the setup app offered an auto-run entry and you accepted it, verify the
   GoldHEN auto-run queue contains `/user/data/payloads/orbisRPC.elf`.

The setup app's **INSTALL / UPDATE** option installs the payload and config.
For a manual install, place the ELF at `/data/payloads/orbisRPC.elf` and use
the setup app or edit the config over FTP. A reboot clears the jailbreak and
payload; re-jailbreak and relaunch it.

## Discord reports an invalid login (close 4004)

The gateway rejected the configured credential. Check the account in Discord's
official client and follow any security or password-reset prompt there. Then
enter a replacement token through **SETTINGS** in orbisRPC Setup or update the
`token` field in the config. The daemon waits for a changed token after an
explicit 4004 rejection; it does not keep identifying with the same rejected
value. A network outage is a different failure and uses reconnect backoff.

A rotated token received in a READY event is saved to config. If the save
fails, the daemon keeps the latest accepted value in memory and retries later.
This cannot guarantee Discord will accept a user token or prevent account
security checks. A token has full account access; do not share it or the config.

## TLS verification fails

Check the PS4 date and time and the daemon log. A wrong clock can make valid
certificates appear expired or not yet valid. Restore the date in PS4 Settings
and retry. If the log reports a DNS or connection failure instead, check the
console's network and DNS configuration.

## The game name is a title ID

Names are resolved from config overrides and local metadata, then remote title
services. A blocked or unavailable service can leave a new title unresolved.
Add a verified name with `python3 scripts/add_custom_title.py`; see the
[contributor guide](../README.md#contributing). Some homebrew apps do not expose
the same title metadata as retail games and still need console-specific
investigation.

## Name appears, but cover art does not

Artwork may be absent from the metadata source, its URL may no longer work, or
the PS4 may not be able to reach the host. Check the log for art lookup errors
and test the network path. If using nanoDNS, the setup app can offer to add
`tmdb.np.dl.playstation.net` to its exceptions. The title can still be shown
without artwork.

## The setup app cannot enter text or save settings

Confirm the controller works in the app and check the setup log. The token row
validates the input shape before saving; that check cannot verify the token with
Discord. Settings are written to the existing config while preserving its other
fields. If the config is malformed or unreadable, fix or restore it before
saving settings so the app does not replace it with a partial file.

## Setup install or update fails

Read `/data/orbisRPC/install.log` and `/data/orbisRPC/net.log`. Confirm the
console can reach GitHub when downloading an update and has enough free storage.
Do not delete `/data/orbisRPC/config.json` as a first troubleshooting step; it
contains the user's configuration and credential. Back it up privately before
any manual recovery.

## Report a bug

Include the PS4 firmware, GoldHEN version, orbisRPC build, the exact step or
symptom, and relevant redacted log lines. Do not attach `config.json`, tokens,
webhook URLs, or account-identifying data. See the
[GitHub issue tracker](https://github.com/SirHumza/orbisRPC/issues).
