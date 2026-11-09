# orbisRPC

orbisRPC is a Discord Rich Presence daemon for jailbroken PS4 consoles running
GoldHEN. The daemon runs on the console and reports the foreground title,
cover art when available, and elapsed play time.

The repository contains the payload, the **orbisRPC Setup** PKG, config and
title metadata, host tests, and build scripts. The supported firmware range is
5.05–13.52; hardware confirmation is documented separately from build support.

## Install

Get the current PKG from [GitHub Releases](https://github.com/SirHumza/orbisRPC/releases).
Install it on the PS4, open **orbisRPC Setup**, and choose **INSTALL / UPDATE**.
The setup app installs the payload and configuration and offers to add a
GoldHEN auto-run entry. You can decline auto-run and launch the payload
manually. The app also offers a nanoDNS exception for Sony's title metadata
service when nanoDNS is installed.

The daemon needs a Discord user session token. Enter it in the setup app or
follow [Token setup](docs/TOKEN_SETUP.md). A user token grants access to the
account; keep it private and never attach `config.json` to a public report.
Discord may invalidate a token or require account recovery. orbisRPC cannot
prevent those server-side actions. If Discord challenges the account, resolve
that through Discord's official client before replacing the token.

For symptoms and log locations, see [Troubleshooting](docs/TROUBLESHOOTING.md)
and [the installer guide](docs/INSTALLER.md). Remove the installation through
the setup app's **UNINSTALL** option.

## What it does

- Detects the foreground PS4 application and title ID through system APIs.
- Resolves title names from local metadata, Sony TMDB, pkg-zone for homebrew,
  and the community list in [`config/custom.json`](config/custom.json).
- Sends game, home-screen, browser, and media activity according to the
  visibility settings in `/data/orbisRPC/config.json`.
- Keeps session timing and cached title metadata across daemon restarts.
- Reconnects to the Discord gateway after network or gateway failures and
  reports daemon health in its logs.

The setup app currently provides token editing and the supported visibility
and presence settings. Options in the master plan are not necessarily shipped;
check the config example and release notes for the behavior in a given build.

## Build and verify

See [Building](docs/BUILDING.md) for the PS4 toolchains and firmware SDK setup.
The normal build is:

```sh
make all
```

Host checks do not require a PS4:

```sh
make -C tests test
make -C tests asan
ORBISRPC_STRICT_SECRETS=1 python3 tests/e2e_consumer.py
```

`make all` builds the payload, stages that payload into the setup app, and
packages the PKG. Do not distribute a test build as a release until its PKG
has also been checked on the target console and firmware.

## Contributing

Add title metadata with `python3 scripts/add_custom_title.py`; the script
validates and writes the compact JSON format. Run the host checks above before
opening a pull request. Include the firmware and relevant redacted log lines
for console-only bugs. Never include a Discord token or other account data.

Project documentation lives in [`docs/`](docs/), including the
[daemon overview](docs/DAEMON.md), [installer details](docs/INSTALLER.md),
[token setup](docs/TOKEN_SETUP.md), and [troubleshooting](docs/TROUBLESHOOTING.md).

## License and attribution

The repository currently does not declare a project license. Check with the
maintainers before redistributing modified builds. Third-party components and
their licenses are recorded alongside their source where available.
