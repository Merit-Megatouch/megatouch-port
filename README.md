# megatouch-port

Run a **Megatouch ION (2014)** bar-top cabinet on a PC: the cabinet's own factory software,
unmodified, from its original installer discs, on Linux or Windows (WSL2). You get the whole
cabinet: attract loop, game menus, every game, Operator Setup, coins and credits, operator and
player keys, high scores and books, 31 languages, plus MegaNet and TournaMAXX on community
servers. The **Megatouch Force** (the cabinet before the ION) runs too, from its 2011 installer
disc ([below](#the-force-2011)), and so does the **Megatouch Maxx** (the one before that), from
its 2005 Crown discs ([below](#the-maxx-2005-crown)).

Nothing is emulated at the CPU level: the cabinet's x86 Linux programs run directly. What the
project provides is the cabinet around them. The I/O board, touchscreen, security key, sound card,
network port and monitor are simulated, and small compatibility fixes cover the 13 years between
the cabinet's Linux and today's.

**No cabinet software is included.** You need your own copy of the cabinet's software:

| Source | You get |
| --- | --- |
| **The ION 2014 installer discs** (two ISOs, `V40_02T130830_1115`), recommended | the factory software and a factory-fresh cabinet |
| The `Megatouch ION 2014 HDD Keyless` disk image (60 GB) | the same software with a patched loader, and a previous owner's settings and books |
| Optional: **the Force 2011 installer disc** (one ISO, `V28_40T101109_1217`) | a Force cabinet next to the ION one |
| Optional: **the Maxx Crown installer discs** (three ISOs, `PG3002-01 V16.10`) | a Maxx cabinet next to the ION one |

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
sudo apt install git gcc g++ make python3 python3-venv curl e2fsprogs binutils bubblewrap \
  xvfb xserver-xephyr slirp4netns libslirp-dev libsdl2-2.0-0
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

## The Force (2011)

The Force was the Megatouch before the ION: 640×480 (800×600 for Hi-Res games), no sidebar, its
own key, touchscreen and network setup. Its 2011 software runs from its installer disc, next to
the ION cabinet and with its own settings, books and licence:

```bash
# cabinet.local.conf
FORCE_DISC1="/mnt/e/Images/Force 2011 Disk 1 of 1 V28_40T101109_1217_1OF2.iso"

make loader-setup                 # also builds build/force (a few minutes)
make loader-run CABINET=force     # the window is titled "Megatouch Force"
```

First start: two calibration screens (press *EXIT* on each), then the menu. The controls are the
ION's. For MegaNet/TournaMAXX: **F1** → *Network* → *LAN* → *Ethernet Setup* → *AUTOMATIC* →
*Setup*: server `us.oerinet.net`, DNS `1.1.1.1` and `8.8.8.8` (public DNS servers are needed:
the Force tests its Internet connection through them), then *YES* to the TournaMAXX connection.

Tried so far: from the disc to the menu, Operator Setup, the licence, DHCP and TournaMAXX
registration with a community server. Not yet: playing through games, coins, operator keys,
MegaLink. Only the 2011 disc is supported (the 2007.5–2010.5 discs have the same layout but
weren't tried; 2004–2006.5 use an older one). Details: [operator guide §20](docs/guides/operator-guide.md#20-a-force-cabinet-2011),
[how it works](docs/guides/cabinet-loader.md#a-force-cabinet), [the Force's software](docs/reference/force.md).

## The Maxx (2005 Crown)

The Maxx was the Megatouch before the Force: 640×480, an older USB I/O board and the older
DS1991 security key. Its last Linux release, Crown (`PG3002-01 V16.10`, 2005), runs from its three
installer discs, next to the ION cabinet and with its own settings, books and licence:

```bash
# cabinet.local.conf (all three discs, in order)
MAXX_DISC1="/mnt/e/Images/maxx_crown_v16-10_reva_d1.iso"
MAXX_DISC2="/mnt/e/Images/maxx_crown_v16-10_reva_d2.iso"
MAXX_DISC3="/mnt/e/Images/maxx_crown_v16-10_reva_d3.iso"

make loader-setup                 # also builds build/maxx (a few minutes)
make loader-run CABINET=maxx      # the window is titled "Megatouch Maxx"
```

First start: one calibration screen (press *EXIT*, bottom right), then the menu ("Welcome to
Merit NTOUCH"), in English with seven categories. The controls are the ION's. The licence key is
made automatically on the first start, with every game of the release enabled.

TournaMAXX on the Maxx only dials in, so the cabinet gets an emulated modem: it dials, "connects",
and its traffic goes out over the PC's network. The community server doesn't speak the Maxx's
2005 protocol (port 17751), so calls end with "SERVER NOT RUNNING". The protocol is worked out
and a minimal server is included: `scripts/tmaxx-server.py --tournament` on this PC, and the
cabinet started with `MEGA_TMAXX_SERVER=<this PC's IP address>:17751`, gets a test tournament
(F1 → *TournaMAXX* → *Update From Server*).

Tried so far: from the discs to the menu, the licence, colours and sound, Operator Setup, the
TournaMAXX call and a tournament from the included server, coins (F5–F8: 25¢, 50¢, $1, $5; one
credit = 25¢). Not tried yet: playing through games, operator keys. Only Crown has been tried: Jade 2 (V15.10) has the same disc layout, Sapphire and
Ruby use older layouts, Diamond, Emerald and earlier are DOS. Details: [operator guide
§21](docs/guides/operator-guide.md#21-a-maxx-cabinet-2005-crown), [the Maxx's software](docs/reference/maxx.md).

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
| Force 2011 (the cabinet before the ION), from its installer disc | yes: menus, Operator Setup, licence, network and MegaNet/TournaMAXX registration with a community server; games not yet played through ([above](#the-force-2011)) |
| Maxx Crown (2005, the cabinet before the Force), from its three installer discs | partly: menus in English with every game licensed, colours, sound, Operator Setup; coins and credits; TournaMAXX dials in through an emulated modem and works with the included server, not with the community one; games not yet played through ([above](#the-maxx-2005-crown)) |
| Other years' installer discs (ION 2008.5–2013, Force 2004–2010.5, other Maxx releases) | ION, Force and Maxx Jade 2: same disc format, not tried yet; older Maxx releases: other formats ([every disc](docs/reference/cabinet.md#every-installer-disc-as-seen-2026-10-11)) |
| Credit card reader, TouchTunes / Rowe jukebox links | no (the services behind them are gone) |
| Dial-up modem | emulated for the Maxx's TournaMAXX call only |

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
| [Cabinet state](docs/reference/cabinet-state.md), [MegaNet](docs/reference/meganet.md), [MegaLink](docs/reference/megalink.md) | Where the cabinet keeps its state (and the game menu); the server exchange; the link protocol |
| [Force](docs/reference/force.md) | The Force's software: system, files, touchscreen protocol, network, what breaks on today's systems |
| [Maxx](docs/reference/maxx.md) | The Maxx Crown software: the older I/O board and DS1991 key, colours, the dial-up chain, the TournaMAXX protocol |
| [Commands](docs/reference/commands.md) | Every command, script and setting |
| [All docs](docs/README.md) | Index, including reference and history |

## Everyday commands

```
make loader-run                   start the cabinet
make loader-run CABINET=force     start the Force cabinet (FORCE_DISC1 in cabinet.local.conf)
make loader-run CABINET=maxx      start the Maxx cabinet (MAXX_DISC1-3 in cabinet.local.conf)
make loader-backup                save a backup of its settings (every start also saves one)
make loader-restore BACKUP=<file> restore one (cabinet stopped)
make loader-reset                 back to the original settings: factory (discs) or the image's (backed up first)
make loader-reset CABINET=force   the Force back to factory settings (CABINET=maxx: the Maxx)
scripts/loader-menu.sh --missing  licensed games on no menu page (each start adds them; --list shows the menu)
scripts/loader-option.sh --list   the cabinet's 120 game options (TournaMAXX, free play, …)
scripts/loader-key.sh --show      the licence: country, languages, options
make update                       update (settings backed up; rolls back if the new version fails)
make report                       one file with the logs and system details, to attach to a problem report
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

A Force needs a few more (its own I/O board access, a MicroTouch controller, old network and
clock code); they are picked automatically.

Details, and the compatibility fixes: [docs/guides/cabinet-loader.md](docs/guides/cabinet-loader.md).

## Repository layout

| Path | In git | What |
| --- | --- | --- |
| `Makefile`, `cabinet.conf` | yes | Commands; where the cabinet's software comes from (your disc or image paths go in `cabinet.local.conf`, not in git) |
| `scripts/` | yes | `install.sh`, `cabinet.sh` (kiosk), `loader*.sh` (run, setup, backup, options, key, identity, menu), `partimage.py` (installer discs), `hwbridge.py` (connectors), helpers |
| `src/fakeio/` | yes | The cabinet's simulated hardware and compatibility fixes |
| `docs/` | yes | `guides/`, `reference/`, `history/`, `roadmap.md` |
| `build/loader/` | no | The cabinet's software from your discs or image, its settings (`var/`) and backups |
| `build/force/` | no | The Force cabinet from its disc, and its settings (`var/`) |
| `shared/`, `toolchain/` | no | Generated by `make setup` / `make loader-setup` |
| `src/backend`, `src/host`, `src/legacy`, `src/unity`, `src/gendef`, `games/`, `tools/` | yes | Standalone game ports (below) |

## Standalone game ports (developers)

Before the cabinet's own software ran, the project ran single games without the cabinet, on a
reimplemented engine layer (`make run GAME=g_trix`); a handful are playable. Players and
operators don't need this: the cabinet runs every game. It remains for development; see
[porting a game](docs/guides/porting-a-game.md) and the [contributing guide](docs/guides/contributing.md)
(the games are git submodules: clone with `--recurse-submodules` if you want them).
