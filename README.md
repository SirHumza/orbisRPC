<p align="center">
  <img src="config/images/icons/logo.png" width="420" alt="orbisRPC">
</p>

# orbisRPC — Discord Rich Presence for PS4 (GoldHEN RPC)

<p align="center">
  <a href="https://github.com/SirHumza/orbisRPC/releases"><img src="https://img.shields.io/badge/version-beta-ffd800?style=flat-square" alt="version"></a>
  <a href="https://github.com/SirHumza/orbisRPC/actions/workflows/ci.yml"><img src="https://github.com/SirHumza/orbisRPC/actions/workflows/ci.yml/badge.svg" alt="CI"></a>
  <img src="https://img.shields.io/badge/PS4-GoldHEN-003791?style=flat-square" alt="PS4 GoldHEN">
  <img src="https://img.shields.io/badge/Discord-Rich%20Presence-5865F2?style=flat-square" alt="Discord">
  <a href="https://discord.gg/BWEyfcT7ZQ"><img src="https://img.shields.io/badge/Discord-Join%20the%20server-5865F2?style=flat-square&logo=discord&logoColor=white" alt="Join the Discord"></a>
  <img src="https://img.shields.io/badge/tables-none-brightgreen?style=flat-square" alt="no tables">
</p>

<p align="center">by <b>SirHumza & CyberMask367</b> · <a href="https://discord.gg/BWEyfcT7ZQ">discord.gg/BWEyfcT7ZQ</a></p>

<p align="center"><b>Discord Rich Presence for the jailbroken PS4 — every game, zero setup.</b><br>
A background daemon that lives entirely on your console and posts what you're
playing to Discord: name, cover art, timer. No PC at runtime.</p>

---

## Contents

- [What you get](#what-you-get)
- [How it works](#how-it-works)
- [Requirements](#requirements)
- [Install (5 minutes)](#install-5-minutes)
- [What you'll see](#what-youll-see)
- [Config](#config)
- [Support / Community](#support--community)
- [Troubleshooting](#troubleshooting)
- [Building](#building)
- [Developing](#developing)
- [Safety](#safety)
- [Credits](#credits)
- [License](#license)

---

## What you get

| | |
|---|---|
| 🎮 **Any games** | Names resolve on your console via Sony's TMDB — CUSA, zero per-game setup. |
| 🕹️ **Homebrew + retro** | Homebrew resolves via pkg-zone; PS1/PS2/PSP classics via a custom list. |
| 🖼️ **Real cover art** | Game art served per title, PlayStation logo when idle. |
| ⏱️ **True timers** | Survive reconnects and restarts, resume across quick game switches. |
| 🧠 **Self-learning** | First-seen titles are remembered, so later boots resolve instantly. |
| 🔔 **Update notices** | The daemon spots newer releases and tells you — you install the new build yourself. |
| 📦 **Simple install** | Payload (`.elf`) install → token → presence. |
| 👀 **Your call what shows** | `show_firmware`, `show_idle`, `show_media`, `show_homebrew` — all on by default, each toggled separately. |

## How it works

The PS4 runs one foreground app at a time, so a normal homebrew app gets
suspended the moment you launch a game. orbisRPC avoids that by running as
a **background payload daemon** while games run in the foreground.

```
PS4 (GoldHEN)                              Discord
┌─────────────────────────┐      ┌──────────────────┐
│ orbisRPC daemon         │ TLS  │  your profile    │
│  BigApp+TitleId → game  │ ◄──► │  Playing Game    │
│  metadata/TMDB → name   │      │  [cover] [timer] │
└─────────────────────────┘      └──────────────────┘
```

1. The daemon asks the OS what's in front (`BigApp` → `TitleId` — one
   call, so identity and state can't disagree).
2. Looks up its title + cover (config → app.db → local files → Sony
   TMDB → pkg-zone for homebrew → raw ID fallback).
3. Connects to the Discord gateway over TLS 1.2.
4. Sets your activity with an elapsed timer; clears it when the game exits.

Details: [`docs/DAEMON.md`](docs/DAEMON.md) · full index at [`docs/`](docs/)

## Requirements

- Jailbroken PS4 on firmware **from 5.05 upto 13.52**, GoldHEN running.
- A way to send the payload (elfldr on port 9021, or GoldHEN BinLoader
  on 9090).
- A Discord account + your user token (see Install).
- Internet on the PS4 that can reach Discord (see Troubleshooting if not).

## Install (5 minutes)

> ✅ **Released as 1.0.0.** Grab `orbisRPC-1.0.0.pkg` from
> [releases](https://github.com/SirHumza/orbisRPC/releases/latest).

1. Install the PKG on the PS4.
2. Open **orbisRPC Setup** from the home screen.
3. Pick **INSTALL / UPDATE**. It copies the payload into
   `/data/payloads/`, writes your config, and offers to add orbisRPC to the
   GoldHEN auto-run queue so it starts on every boot. It also offers to add a
   Sony TMDB exception to [nanoDNS](https://github.com/peterrsr/nanoDNS) if
   you run it — skip either prompt if you don't want it.
4. Enter your Discord token when asked. Don't know where the token comes
   from? Follow [docs/TOKEN_SETUP.md](docs/TOKEN_SETUP.md) — wrong tokens are
   the #1 install failure.
5. Restart the console (or re-jailbreak). Launch a game and watch Discord.

After that the daemon starts on boot. The app's **UNINSTALL** entry removes
the payload and your configs again.

<details>
<summary>Installing the payload by hand instead</summary>

You can skip the PKG and run the daemon yourself:

1. Delete the `/data/orbisRPC` folder from your PS4 if you have one from an
   earlier build.
2. Put `orbisRPC.elf` on the console at `/data/payloads/` (FTP it over, or
   USB) — that's where GoldHEN looks for payloads.
3. Launch it from the payload list. First run creates the config file, then
   says there's no token yet.
4. Open `/data/orbisRPC/config.json` and put your account token in the
   `"token": "SET_ME"` slot.
5. Re-run the payload from `/data/payloads`. Launch a game and watch Discord.

The installer is the better path — it does the token, the auto-run entry and
the nanoDNS exception for you — but the manual route works, and it's what you
want if you'd rather not install an app.

To start on boot by hand, add `orbisRPC.elf` to the auto-run queue in GoldHEN
settings.

</details>

✅ Firmware: confirmed working on 9.00 through 13.52 but older fimwares should work.

### What's new in 1.0.0

- **orbisRPC Setup** — a real installer app with install/update, settings and
  uninstall. Handles the token, GoldHEN auto-run, and the nanoDNS TMDB
  exception.
- **In-app update check** — both the app and the daemon notice a newer
  release and tell you. Neither replaces the payload on its own; the installer
  is what does that.
- **Community title list** — [`config/custom.json`](config/custom.json)
  resolves apps and homebrew pkg-zone doesn't carry. Anyone can contribute by
  opening a pull request.
- Dropped the payload `.bin` validation path.

## What you'll see

| Where you are | Discord shows |
|---|---|
| In a game | `Playing <Game>` + cover + elapsed timer (+ firmware line) |
| Home screen | `PlayStation 4` + logo (`Idling on Home Menu`) |
| Settings | `PlayStation 4` + `In Settings` (game timer underneath is kept) |
| Browser | `PlayStation 4` + `Using Web Browser` (game timer underneath is kept) |
| Netflix & co. | `Watching <App>` |
| Nothing to show | Presence clears |

## Config

Only `token` (your Discord user session token) is required —
`/data/orbisRPC/config.json`. Everything else ships working. Full
reference: [`docs/CONFIG.md`](docs/CONFIG.md).

| Key | Default | Meaning |
|---|---|---|
| `token` | — | Discord user session token (**required**) |
| `presence_state` | `"On PS4"` | Activity state line for games and the home screen |
| `presence_settings_text` | `"In Settings"` | Activity name while Settings is open |
| `poll_interval_s` | `12` | Detection cadence |
| `home_art` | project logo | Idle tile artwork (URL or uploaded asset key) |
| `pkgzone_enabled` | `true` | Ask pkg-zone.com for homebrew names — sends the title id to a third party; set `false` to disable |
| `retro_enabled` | `true` | Ask the retro-games indexes for PS1/PS2/PSP names and covers |
| `show_firmware` | `true` | Show the `Firmware X.YY` line |
| `show_idle` | `true` | Post a presence on the home screen, Settings and the browser |
| `show_media` | `true` | Post media apps (Netflix, YouTube, Plex…) as `Watching` |
| `show_homebrew` | `true` | Post homebrew titles. Retro classics are **not** affected |
| `debug` | `false` | Verbose per-poll logging |

A `false` visibility flag still detects the title — it just stops telling
Discord about it, so a game underneath keeps showing. The exception is the home
screen: with `show_idle` off, a game that closes is cleared rather than left up,
since nothing is running underneath there.

Plus the self-learned `titles` map, which fills itself in as titles resolve.

## Support / Community

Questions, test-build feedback, bug reports: join the Discord —
**[discord.gg/BWEyfcT7ZQ](https://discord.gg/BWEyfcT7ZQ)** — and post in
`testers-chat`. No need to hunt for the link; this is it.

**Found an issue?** Come to the Discord server and report it to us —
we'll take care of it.

⚠️ **This is a beta.** Test builds are handed out on the Discord — join the
server to get beta access.

## Troubleshooting

Short version here; the full symptom-first guide is
[`docs/TROUBLESHOOTING.md`](docs/TROUBLESHOOTING.md).

**Game cover not showing on Discord?** It's your DNS blocker. Disable it, or use
[nanoDNS](https://github.com/drakmor/nanoDNS) with an exception.

To add the exception, open the nanoDNS config at
`/data/nanodns/nanodns.ini` and find the exceptions list:

```ini
[exceptions]
feature.api.playstation.com
.stun.playstation.net
stun..playstation.net
ena.net.playstation.net
post.net.playstation.net
gst.prod.dl.playstation.net
```

Add `tmdb.np.dl.playstation.net` so it looks like this:

```ini
[exceptions]
tmdb.np.dl.playstation.net
feature.api.playstation.com
.stun.playstation.net
stun..playstation.net
ena.net.playstation.net
post.net.playstation.net
gst.prod.dl.playstation.net
```

Once you're done, save and restart your console.

Also set your PS4's DNS to `127.0.0.1` so traffic actually goes through
nanoDNS — then reboot. (This is the step most people miss.)

**Still "can't connect to Discord" while the PS4 is online?**

1. Open the PS4 web browser and go to `discord.com` — if that doesn't load,
   it's your connection, not the payload.
2. Delete `/data/orbisRPC/log.txt`, restart the console, run the payload
   again.
3. If it still fails, send the new `log.txt` in `testers-chat`.

**Known issues (test build)**

- Some homebrew (Apollo Save Tool, Cheats Manager, Homebrew Store) isn't
  detected yet — under investigation.

## Building

Two toolchains are involved. Point them at yours once per shell:

```bash
export OO_PS4_TOOLCHAIN="$HOME/OpenOrbis-PS4-Toolchain"  # installer app
export PS4_PAYLOAD_SDK="$HOME/ps4-payload-sdk"            # daemon payload
export PS4_SDK_SRC="$HOME/ps4-payload-sdk-src"            # 13.52 firmware gate
```

Build the PS4 SDK from git once, before anything else:

```bash
./scripts/build_sdk_from_source.sh
```

Then, from the repo root:

| Command | Builds | Output |
|---|---|---|
| `make all` | payload → stage into the app → package | `output/orbisRPC-1.0.0.elf` + `output/orbisRPC-1.0.0.pkg` |
| `make payload` | daemon payload only | `output/orbisRPC-1.0.0.elf` |
| `make installer` | Setup PKG only | `output/orbisRPC-1.0.0.pkg` |
| `make elf` | compile + link the app, no packaging | — |
| `make gfx` | artwork only (PNG → BMP) | — |
| `make clean` | remove build output | — |

`make all` stages the freshly built payload into `installer/assets/` before
packaging, so the PKG never ships a stale ELF.

Host-side checks, no PS4 toolchain needed:

```bash
make -C tests test                # unit tests
make -C tests asan                # ASan/UBSan
python3 tests/e2e_consumer.py     # contracts + linkage gate
```

⚠️ **Build the SDK from git, not the release ZIP.** The SDK's CRT refuses to
start on firmware it does not list, and terminates the payload before `main()`
with no log line. The released SDK (v0.9) lists 13.50 but not 13.52, so a
ZIP-built payload cannot boot on 13.52. `PS4_SDK_SRC` is what enables the
build-time check for this; without it the build still succeeds and dies at
runtime. See [`docs/research/sdk-13.52.md`](docs/research/sdk-13.52.md).

Installer app internals: [`docs/INSTALLER.md`](docs/INSTALLER.md).
Build/test/CI details: [`docs/BUILDING.md`](docs/BUILDING.md).

### Adding a title to the community list

Names for apps pkg-zone doesn't carry live in
[`config/custom.json`](config/custom.json). Add one with:

```bash
python3 scripts/add_custom_title.py     # asks for TITLEID, name, cover URL
python3 scripts/add_custom_title.py --list
python3 scripts/add_custom_title.py --remove ORPC00042
```

Then open a pull request. **The file must stay compact** — `"id":"ORPC00042"`,
no space after the colon. The payload scans for that exact byte run, so
pretty-printing the file makes every lookup miss silently. The script writes it
correctly for you.

## Developing

- CI runs host tests, ASan, contract checks, the SDK payload build with a
  linkage gate, and uploads the `.elf` as an artifact. Tagging `v*`
  attaches it to a GitHub Release.
- Contributor notes: [`AGENTS.md`](AGENTS.md). Security notes:
  [`SECURITY.md`](SECURITY.md).
- PRs need the host suite green with outputs pasted (see the PR template).
- Daemon changes need on-console proof: log lines + firmware tested.

## Safety

- A user session token grants full account access — never share the config,
  never commit a real token. CI scans for token-shaped strings and fails.
- Token-based presence is against Discord's ToS (standard practice for
  headless presence tools; risk is yours).

## Credits

- **SirHumza** — project founder, daemon core.
- **CyberMask367** — firmware coverage to 13.52, homebrew and retro games resolving, presence flags, tester wrangling.
- Testers in `testers-chat`  on DIscord — every log file that made a fix possible.
- [OpenOrbis](https://github.com/OpenOrbis/OpenOrbis-PS4-Toolchain) - SDK/Toolchain used to make setup PKG
- [*PKG-ZONE*](https://pkg-zone.com) — used for resolving metadata on hombrew apps
- [*PSX DATA CENTER*](https://psxdatacenter.com) — source used to make custom json list  in [`config`](config/) for resolving retro games

## License

To be finalized (MIT vs GPL). No GPL source is copied into this tree.
