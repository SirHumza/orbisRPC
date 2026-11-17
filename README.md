<p align="center">
  <img src="config/images/icons/logo.png" width="320" alt="orbisRPC">
</p>

<h1 align="center">orbisRPC</h1>

<p align="center"><strong>Show your PS4 game, cover art, and play timer on Discord.</strong><br>
Runs on a jailbroken PS4 with GoldHEN. No PC or companion script needed.</p>

<p align="center">
  <a href="https://github.com/SirHumza/orbisRPC/releases"><img src="https://img.shields.io/badge/version-1.1-ffd800?style=flat-square" alt="Version 1.1"></a>
  <a href="https://github.com/SirHumza/orbisRPC/actions/workflows/ci.yml"><img src="https://github.com/SirHumza/orbisRPC/actions/workflows/ci.yml/badge.svg" alt="CI status"></a>
  <a href="https://discord.gg/BWEyfcT7ZQ"><img src="https://img.shields.io/badge/Discord-join%20the%20community-5865F2?style=flat-square&logo=discord&logoColor=white" alt="Join the orbisRPC Discord"></a>
</p>

<p align="center">By SirHumza and CyberMask367 · <a href="https://sirhumza.github.io/orbisRPC/">Project site</a></p>

[![Watch the orbisRPC demo on YouTube](https://img.youtube.com/vi/J3kjfGJ-HYk/hqdefault.jpg)](https://www.youtube.com/watch?v=J3kjfGJ-HYk)

## Install

Download the current release from [GitHub Releases](https://github.com/SirHumza/orbisRPC/releases/latest). The standalone `.elf` is the recommended option: it installs only the daemon and keeps setup simple. The Setup PKG remains available for users who prefer its graphical installer and settings app.

### Recommended: standalone payload

1. Download `orbisRPC.elf` from the release assets.
2. Copy it to `/data/payloads/orbisRPC.elf` on the PS4.
3. Launch it from your GoldHEN payload loader. The first run creates `/data/orbisRPC/config.json`.
4. Follow [Token setup](docs/TOKEN_SETUP.md) to add your Discord token, then launch the payload again.

To update, replace the `.elf` and keep your existing `/data/orbisRPC/config.json`. Do not share that file; it contains your Discord token.

### Optional: Setup PKG

Download `orbisRPC-setup-1.1.pkg` from the release assets, install it, and open **orbisRPC Setup**. Choose **INSTALL / UPDATE** to install the payload and configure it from the console. The app also provides settings and uninstall options. It can offer to add a GoldHEN auto-run entry and a nanoDNS exception; both are optional.

## What it does

- Shows the foreground game's title, cover art, and elapsed play time on your Discord profile.
- Runs as a background payload on the PS4, so Discord activity continues while a game is in front.
- Resolves retail, homebrew, and supported retro titles from local and online metadata.
- Reconnects after gateway or network interruptions and preserves session timing across daemon restarts.
- Lets you choose which idle, media, homebrew, and firmware activity appears.

The supported firmware range is **5.05–13.52**. Published hardware testing covers **9.00–13.52**.

## Important token note

orbisRPC uses a Discord user session token to set Rich Presence. Discord's Terms of Service prohibit automating normal user accounts this way, and Discord may invalidate the token or challenge the account. Use a separate account if you choose to run it. Never post your token or attach `config.json` to an issue or chat.

## Help and documentation

- [Setup guide](https://sirhumza.github.io/orbisRPC/guide.html)
- [Troubleshooting](docs/TROUBLESHOOTING.md)
- [Installer details](docs/INSTALLER.md)
- [Configuration reference](docs/CONFIG.md)
- [Discord community](https://discord.gg/BWEyfcT7ZQ)
- [Report a bug](https://github.com/SirHumza/orbisRPC/issues)

## Build and verify

See [Building](docs/BUILDING.md) for the toolchains and firmware SDK setup. `make all` builds the payload and optional Setup PKG.

```sh
make -C tests test
make -C tests asan
ORBISRPC_STRICT_SECRETS=1 python3 tests/e2e_consumer.py
```

The host checks do not require a PS4. Do not distribute a build until its package and payload have been checked on target hardware.

## Contributing

Add title metadata with `python3 scripts/add_custom_title.py`. Include firmware details and relevant redacted logs in bug reports. Never include account tokens or other private data.

## Credits and license

SirHumza and CyberMask367 maintain orbisRPC. Thanks to the testers who supplied logs and title fixes, and to the OpenOrbis, pkg-zone, and PSX Data Center projects that support its development.

The repository has no project license file. Check with the maintainers before redistributing modified builds; third-party components retain their own terms.

<sub>orbisRPC is a PlayStation 4 homebrew project. It is unrelated to Orbis Protocol, Orbis Corporation, and other projects that share the name.</sub>
