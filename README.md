# megatouch-port

Run a **Megatouch ION (2014)** bar-top cabinet on a PC: the cabinet's own factory software,
unmodified, from its original installer discs, on Linux or Windows (WSL2). You get the whole
cabinet: attract loop, game menus, every game, Operator Setup, coins and credits, operator and
player keys, high scores and books, 31 languages, plus MegaNet and TournaMAXX on community
servers.

Nothing is emulated at the CPU level: the cabinet's x86 Linux programs run directly. What the
project provides is the cabinet around them. The I/O board, touchscreen, security key, sound card,
network port and monitor are simulated, and small compatibility fixes cover the 13 years between
the cabinet's Linux and today's.

**No cabinet software is included.** You need your own copy of the cabinet's software:

| Source | You get |
| --- | --- |
| **The ION 2014 installer discs** (two ISOs, `V40_02T130830_1115`), recommended | the factory software and a factory-fresh cabinet |
| The `Megatouch ION 2014 HDD Keyless` disk image (60 GB) | the same software with a patched loader, and a previous owner's settings and books |

Everything is read from them.

## Quick start

On Windows, install Ubuntu under WSL2 first (`wsl --install -d Ubuntu` in an administrator
PowerShell, then reboot). Then, in Ubuntu (or on any Ubuntu box), one command installs
everything:

```bash
bash <(curl -fsSL https://raw.githubusercontent.com/Merit-Megatouch/megatouch-port/main/scripts/install.sh) \
  --disc "/mnt/e/Images/ION 2014 Disc 1 or 2 V40_02T130830_1115_1OF2.iso" \
  --disc "/mnt/e/Images/ION 2014 Disc 2 of 2 V40_02T130830_1115_2OF2.iso"
```

(Windows drives are under `/mnt`: `E:\Images` is `/mnt/e/Images`.) Without `--disc` it asks for
the disk image instead. Add `--kiosk` for a dedicated touchscreen box: it then boots into the
cabinet, fullscreen, and restarts it if it crashes or hangs.

By hand, put `DISC1="…1OF2.iso"` and `DISC2="…2OF2.iso"` (or `IMG="….img"`) in
`cabinet.local.conf`, then:

```bash
sudo apt install git gcc g++ make python3 python3-venv curl e2fsprogs binutils bubblewrap
git clone https://github.com/Merit-Megatouch/megatouch-port ~/megatouch-port
cd ~/megatouch-port
nano cabinet.local.conf   # DISC1= / DISC2= (or IMG=), see above
make setup            # once: 32-bit runtime and compiler support
make loader-setup     # once: unpack the cabinet's software (10-20 min in total)
make loader-run       # start the cabinet
```

A cabinet installed from the discs starts like a new one: touchscreen calibration (touch *EXIT*
on each screen), then the Connection Wizard (*Next*, *Skip*, then ✕). After that, touch (click)
to get the menu. **F1** = Operator Setup, **F5–F8** = coins, **F9** = operator key,
**F10** = player key, **F11** = fullscreen. Close the window to stop.

**New operators: read the [Operator guide](docs/guides/operator-guide.md).** It covers
installing, the controls, first-time setup, coins and keys, adding games to the menu, MegaNet
and TournaMAXX, languages and region, backups and troubleshooting.

## What works

| | |
| --- | --- |
| Install from the original ION 2014 installer discs: factory software, unmodified | yes |
| Boot, attract loop, menus, widescreen sidebar | yes |
| The games (classic, Hi-Res widescreen, Unity) | yes, with sound; not every one has been tried, so report any that misbehave |
| Operator Setup, Diagnostics / I/O test, calibration | yes |
| Coins (4 channels), credits, free play, books | yes |
| Operator key + PIN, My Merit player keys | yes |
| Languages and region | 31 languages (Arabic, Japanese, Russian, Chinese …), any country the software knows (`KEY_LANGUAGES`, `KEY_COUNTRY`) |
| Network, MegaNet updates, TournaMAXX tournaments | yes, with a community MegaNet server |
| Resizable / fullscreen window | yes |
| Dedicated touchscreen box: auto-run, fullscreen, restart on crash or hang, nightly updates with rollback | yes (kiosk mode) |
| Settings kept between runs, automatic backups | yes |
| Joystick accessory | yes, on the arrow keys (`MEGAIO_JOYSTICK=1`) |
| Light-show kit, books printer | yes: the cabinet's light sequences and printouts, passed on to real lights and files |
| Real hardware and services | coin acceptors, buttons, GPIO, a real iButton reader, relays for meters and lockout, MQTT / Home Assistant, webhooks, WLED lights ([connectors](docs/guides/connectors.md)) |
| Linked cabinets (MegaLink) | yes on one PC (a full linked game played); PCs joined over a LAN or the internet share a network and find each other (experimental) ([guide](docs/guides/operator-guide.md#17-linked-cabinets-megalink-experimental)) |
| Other years' installer discs (ION 2008.5–2013, Force, Maxx) | same disc format, not tried yet |
| Credit card reader, TouchTunes / Rowe jukebox links, dial-up modem | no (the services behind them are gone) |

## Documentation

| | |
| --- | --- |
| [Operator guide](docs/guides/operator-guide.md) | **Start here.** Install, run, set up and look after the cabinet |
| [Quick start](docs/guides/quick-start.md) | The install steps in detail, from a bare Windows machine |
| [Troubleshooting](docs/guides/troubleshooting.md) | Problems and fixes |
| [Cabinet loader](docs/guides/cabinet-loader.md) | How the simulation works: every stand-in, the installer discs, network, licence key, settings |
| [Connectors](docs/guides/connectors.md) | Real lights, coin acceptors, buttons, iButton reader, meters, MQTT, webhooks |
| [I/O board](docs/reference/io-board.md) | The USB I/O board, the security key (its full layout) and operator keys |
| [Cabinet software](docs/reference/cabinet-software.md) | How the cabinet's software works inside: games, hardware, light show, printer, NVRAM, MegaNet, MegaLink, debug flags |
| [Cabinet state](docs/reference/cabinet-state.md), [MegaNet](docs/reference/meganet.md), [MegaLink](docs/reference/megalink.md) | Where the cabinet keeps its state; the server exchange; the link protocol |
| [Commands](docs/reference/commands.md) | Every command, script and setting |
| [All docs](docs/README.md) | Index, including reference and history |

## Everyday commands

```
make loader-run                   start the cabinet
make loader-backup                save a backup of its settings (every start also saves one)
make loader-restore BACKUP=<file> restore one (cabinet stopped)
make loader-reset                 back to the original settings: factory (discs) or the image's (backed up first)
scripts/loader-option.sh --list   the cabinet's 120 game options (TournaMAXX, free play, …)
scripts/loader-key.sh --show      the licence: country, languages, options
make update                       update (settings backed up; rolls back if the new version fails)
make kiosk / make kiosk-stop      kiosk mode: fullscreen, restarted on crash or hang
scripts/cabinet.sh autostart on   start the kiosk at login
```

Everything else: [docs/reference/commands.md](docs/reference/commands.md).

## How it works, in short

`make loader-setup` unpacks the cabinet's partitions: from the installer discs (their partimage
images, read with `scripts/partimage.py`) or from the disk image, with `debugfs` either way (no
mounting, no root). `make loader-run` starts the cabinet's own startup sequence in a rootless
sandbox (bubblewrap) on a modern 32-bit runtime, with stand-ins for the hardware:

| Cabinet | Stand-in |
| --- | --- |
| USB I/O board (coins, buttons, DIP switches, key reader, light show, books printer) | a fake board, as the cabinet's `libusb` |
| Touchscreen controller | the mouse |
| Security key (the licence) | a key image the factory loader accepts, served by the fake board |
| Sound card | PulseAudio |
| Ethernet port | a private virtual network with internet access, or a shared one for linked cabinets |
| Monitor (changing resolutions) | a nested X server, shown scaled in a normal window |

Details, and the compatibility fixes: [docs/guides/cabinet-loader.md](docs/guides/cabinet-loader.md).

## Repository layout

| Path | In git | What |
| --- | --- | --- |
| `Makefile`, `cabinet.conf` | yes | Commands; where the cabinet's software comes from (your disc or image paths go in `cabinet.local.conf`, not in git) |
| `scripts/` | yes | `install.sh`, `cabinet.sh` (kiosk), `loader*.sh` (run, setup, backup, options, key, identity), `partimage.py` (installer discs), `hwbridge.py` (connectors), helpers |
| `src/fakeio/` | yes | The cabinet's simulated hardware and compatibility fixes |
| `docs/` | yes | `guides/`, `reference/`, `history/`, `roadmap.md` |
| `build/loader/` | no | The cabinet's software from your discs or image, its settings (`var/`) and backups |
| `shared/`, `toolchain/` | no | Generated by `make setup` / `make loader-setup` |
| `src/backend`, `src/host`, `src/legacy`, `src/unity`, `src/gendef`, `games/`, `tools/` | yes | Standalone game ports (below) |

## Standalone game ports (developers)

Before the cabinet's own software ran, the project ran single games without the cabinet, on a
reimplemented engine layer (`make run GAME=g_trix`); a handful are playable. Players and
operators don't need this: the cabinet runs every game. It remains for development; see
[porting a game](docs/guides/porting-a-game.md) and the [contributing guide](docs/guides/contributing.md)
(the games are git submodules: clone with `--recurse-submodules` if you want them).
