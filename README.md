# megatouch-port

Run a **Megatouch ION (2014)** bar-top cabinet on a PC: the cabinet's own software, unmodified,
from its disk image, on Linux or Windows (WSL2). You get the whole cabinet: attract loop, game
menus, every game, Operator Setup, coins and credits, operator and player keys, high scores and
books, plus MegaNet and TournaMAXX on community servers.

Nothing is emulated at the CPU level: the cabinet's x86 Linux programs run directly. What the
project provides is the cabinet around them. The I/O board, touchscreen, security key, sound card,
network port and monitor are simulated, and small compatibility fixes cover the 13 years between
the cabinet's Linux and today's.

**No cabinet software is included.** You need your own copy of the disk image; everything is
read from it.

## Quick start

On Windows, install Ubuntu under WSL2 first (`wsl --install -d Ubuntu` in an administrator
PowerShell, then reboot). Then, in Ubuntu (or on any Ubuntu box), one command installs
everything and asks for the image:

```bash
bash <(curl -fsSL https://raw.githubusercontent.com/Merit-Megatouch/megatouch-port/main/scripts/install.sh)
```

Add `--kiosk` for a dedicated touchscreen box: it then boots into the cabinet, fullscreen, and
restarts it if it crashes or hangs. Or by hand:

```bash
sudo apt install git gcc g++ make python3 python3-venv curl e2fsprogs binutils bubblewrap
git clone https://github.com/Merit-Megatouch/megatouch-port ~/megatouch-port
cd ~/megatouch-port
echo 'IMG="/mnt/e/path/to/Megatouch ION 2014 HDD Keyless.img"' > cabinet.local.conf
make setup            # once: 32-bit runtime and compiler support
make loader-setup     # once: copy the cabinet's software out of the image (10-20 min in total)
make loader-run       # start the cabinet
```

Touch (click) to get the menu. **F1** = Operator Setup, **F5–F8** = coins, **F9** = operator key,
**F10** = player key, **F11** = fullscreen. Close the window to stop.

**New operators: read the [Operator guide](docs/guides/operator-guide.md).** It covers
installing, the controls, first-time setup, coins and keys, adding games to the menu, MegaNet
and TournaMAXX, backups and troubleshooting.

## What works

| | |
| --- | --- |
| Boot, attract loop, menus, widescreen sidebar | yes |
| The games (classic, Hi-Res widescreen, Unity) | yes, with sound; not every one has been tried, so report any that misbehave |
| Operator Setup, Diagnostics / I/O test, calibration | yes |
| Coins (4 channels), credits, free play, books | yes |
| Operator key + PIN, My Merit player keys | yes |
| Network, MegaNet updates, TournaMAXX tournaments | yes, with a community MegaNet server |
| Resizable / fullscreen window | yes |
| Dedicated touchscreen box: auto-run, fullscreen, restart on crash or hang, nightly updates with rollback | yes (kiosk mode) |
| Settings kept between runs, automatic backups | yes |
| Joystick accessory | yes, on the arrow keys (`MEGAIO_JOYSTICK=1`) |
| Linked cabinets (MegaLink) | experimental: cabinets on one PC, or on PCs joined over a LAN or the internet, share a network and find each other ([guide](docs/guides/operator-guide.md#17-linked-cabinets-megalink-experimental)) |
| Credit card reader, TouchTunes jukebox | no |

## Documentation

| | |
| --- | --- |
| [Operator guide](docs/guides/operator-guide.md) | **Start here.** Install, run, set up and look after the cabinet |
| [Quick start](docs/guides/quick-start.md) | The install steps in detail, from a bare Windows machine |
| [Troubleshooting](docs/guides/troubleshooting.md) | Problems and fixes |
| [Cabinet loader](docs/guides/cabinet-loader.md) | How the simulation works: every stand-in, network, licence key, settings |
| [I/O board](docs/reference/io-board.md) | The USB I/O board, security key and operator key, as read from the image |
| [Commands](docs/reference/commands.md) | Every command, script and setting |
| [All docs](docs/README.md) | Index, including reference and history |

## Everyday commands

```
make loader-run                   start the cabinet
make loader-backup                save a backup of its settings (every start also saves one)
make loader-restore BACKUP=<file> restore one (cabinet stopped)
make loader-reset                 back to the image's original settings (backed up first)
scripts/loader-option.sh --list   the cabinet's 120 game options (TournaMAXX, free play, …)
make update                       update (settings backed up; rolls back if the new version fails)
make kiosk / make kiosk-stop      kiosk mode: fullscreen, restarted on crash or hang
scripts/cabinet.sh autostart on   start the kiosk at login
```

Everything else: [docs/reference/commands.md](docs/reference/commands.md).

## How it works, in short

`make loader-setup` copies the cabinet's partitions out of the image with `debugfs` (no
mounting, no root). `make loader-run` starts the cabinet's own startup sequence in a rootless
sandbox (bubblewrap) on a modern 32-bit runtime, with stand-ins for the hardware:

| Cabinet | Stand-in |
| --- | --- |
| USB I/O board (coins, buttons, DIP switches, key reader) | a fake board, as the cabinet's `libusb` |
| Touchscreen controller | the mouse |
| Security key (the licence) | a key image of the cabinet's own licence |
| Sound card | PulseAudio |
| Ethernet port | a private virtual network with internet access |
| Monitor (changing resolutions) | a nested X server, shown scaled in a normal window |

Details, and the compatibility fixes: [docs/guides/cabinet-loader.md](docs/guides/cabinet-loader.md).

## Repository layout

| Path | In git | What |
| --- | --- | --- |
| `Makefile`, `cabinet.conf` | yes | Commands; image partition offsets (your image path goes in `cabinet.local.conf`, not in git) |
| `scripts/` | yes | `install.sh`, `cabinet.sh` (kiosk), `loader*.sh` (run, setup, backup, options, key), `setup.sh`, helpers |
| `src/fakeio/` | yes | The cabinet's simulated hardware and compatibility fixes |
| `docs/` | yes | `guides/`, `reference/`, `history/`, `roadmap.md` |
| `build/loader/` | no | The cabinet's software from your image, its settings (`var/`) and backups |
| `shared/`, `toolchain/` | no | Generated by `make setup` / `make loader-setup` |
| `src/backend`, `src/host`, `src/legacy`, `src/unity`, `src/gendef`, `games/`, `tools/` | yes | Standalone game ports (below) |

## Standalone game ports (developers)

Before the cabinet's own software ran, the project ran single games without the cabinet, on a
reimplemented engine layer (`make run GAME=g_trix`); a handful are playable. Players and
operators don't need this: the cabinet runs every game. It remains for development; see
[porting a game](docs/guides/porting-a-game.md) and the [contributing guide](docs/guides/contributing.md)
(the games are git submodules: clone with `--recurse-submodules` if you want them).
