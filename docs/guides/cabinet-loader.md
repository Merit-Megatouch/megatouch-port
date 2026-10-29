# Running the cabinet's own loader

The Megatouch ION 2014 loader, the factory one from the original installer discs (or the
"keyless" build from a disk image), runs unmodified on Linux / WSL2, with its menus, attract
mode, Operator Setup, coins, operator and player keys, and every game it launches. The
missing cabinet hardware is replaced by stand-ins: a fake USB I/O board, a fake touchscreen
controller, a security-key image, an OSS sound device that plays through PulseAudio, a virtual
wired network, and a nested X server that changes resolution the way the cabinet does, shown
scaled in a normal window.

This is the technical guide. **Operators**: the [operator guide](operator-guide.md) covers
installing, running and setting up the cabinet step by step.

```bash
make setup            # once: the 32-bit runtime the cabinet's programs run on
make loader-setup     # once: unpack the partitions from the discs or the image (~11 GB), download Xephyr, Xvfb, SDL2, slirp4netns
make loader-run       # builds the stand-ins and starts the cabinet in its own window
```

The loader takes about 20 seconds to start (24 s on the real cabinet), then plays its attract
loop. Touch the screen (click) to get the menu. The 128-pixel strip beside the game is the
widescreen sidebar (rotating ads); the two buttons under it move it to the other side (the
orange one is the one that does something).

## Controls

| Cabinet | Here |
|---|---|
| Touchscreen | Mouse (left button) in the window |
| SETUP button inside the cabinet (Operator Setup) | **F1** |
| CALIBRATE button | **F2** |
| Plug in the books printer (prints on the attract screen) | **F4** |
| Coin into channel 1 / 2 / 3 / 4 | **F5 / F6 / F7 / F8** |
| Operator key on the reader (held while the key is down) | **F9** |
| Player key on the reader | **F10** |
| Joystick accessory (only with `MEGAIO_JOYSTICK=1 make loader-run`) | arrow keys; **Space** left button, **Enter** right button |

The window can be resized and maximised; **F11** (or **Alt+Enter**) switches to fullscreen,
**Ctrl+Alt+S** between keeping the cabinet's shape (black bars) and stretching. The picture
follows the cabinet's resolution changes (640×480 menus, 768×480 widescreen, 1280×800 games), and
touches land where you click at any size. Closing the window stops the loader.

The keys are read from the loader's display, so they work while the window has focus. The
same inputs, and a few more, from a terminal while the loader runs:

```bash
build/loader/bin/megaio coin 0 4               # 4 coins into channel 1
build/loader/bin/megaio setup                  # press SETUP
build/loader/bin/megaio dip 3 on               # DIP switch 3 of bank DS1
build/loader/bin/megaio fob 024d454741100130 operator   # touch a key (family 02 ROM ID) …
build/loader/bin/megaio fob off                         # … and take it away
build/loader/bin/megaio print                  # plug in the books printer, wait for the printout
build/loader/bin/megaio status                 # polls, lockout flags, meters, status bytes
```

Real devices and services (coin acceptors, buttons, a real iButton reader, relays for meters,
MQTT, webhooks, WLED lights) connect through the hardware bridge: see
[connectors](connectors.md).

### Setting up the operator key

Neither the disk image nor a factory-fresh cabinet has operator keys. To use **F9** as the
operator key:

1. **F1** → Operator Setup → *System* → *Setup Operator Keys* → *Set Key* (slot 1).
2. Hold **F9** while it says "Touch new key to machine…", release when asked.
3. Enter a PIN on the six stars, *Set New PIN*. The key shows as `3001104147454d02`.
4. Leave setup. From now on **F9** at the menu asks for that PIN and opens Operator Setup.

**F9** is the ROM ID `024d454741100130`, **F10** `02504c4159455205` (a player key, which opens
the My Merit *Player Key Setup*). Other IDs: `MEGAIO_OPERATOR_KEY` / `MEGAIO_PLAYER_KEY` (16 hex
digits, family `02`, with a valid Dallas CRC in the last byte).

To charge for games: Operator Setup → *Credits/Pricing* → *Options* → untick *Freeplay Enabled*.

## Commands

| Command | |
|---|---|
| `make loader-run` | start the loader (window "Megatouch ION (cabinet loader)") |
| `make loader-reset` | put the loader's `/var` (settings, NVRAM, books, high scores, logs) back as it was after setup: the factory defaults (discs) or the image's state (a backup is taken first) |
| `make loader-backup` / `make loader-restore BACKUP=<file>` | snapshot / restore the loader's state (see *Settings and backups*) |
| `scripts/loader-option.sh --list` | the 120 game options: value and what the key allows (see *Game options and the security key*) |
| `scripts/loader.sh shell` | a shell inside the cabinet userland |
| `scripts/loader.sh run <cmd>` | run a command inside it; joins the running loader's display |
| `scripts/loader.sh run /opt/fakeio/shots.sh /var/merit/shot` | screenshot every window to `build/loader/var/merit/shot-*.png` |
| `scripts/loader.sh run /opt/fakeio/xtouch click X Y` | a scripted touch (also `drag`, `key NAME[+NAME]`) |

Volume: Operator Setup → *System* → *Volume Control* (and the loader's own volume changes) run
`ossmix`, which here sets the PulseAudio volume of the loader's and the games' sound streams.

The joystick is off by default because a present joystick changes the attract loop and some
games. With it on, *CALIBRATE* (F2) continues into the joystick calibration after the
touchscreen one.

Settings (environment): `MEGA_LOADER_NET=host` shares the desktop's network instead of giving the
loader its own (the cabinet then shows no network), `MEGA_LOADER_VIEW=xephyr` uses Xephyr's own window (always the cabinet's exact resolution;
`MEGAVIEW_STRETCH=1`, `MEGAVIEW_FULLSCREEN=1`, `MEGAVIEW_SCALE=N` set megaview's start-up
state), `MEGA_LOADER_KEY=none` runs without a
security-key image, `MEGA_LOADER_VAR=<dir>` uses another directory as `/var` (a second,
throwaway session: copy `build/loader/var.orig`, pick another `MEGA_LOADER_DISPLAY`), `MEGA_LOADER_X=host` draws on the desktop's X server instead of
Xephyr (no resolution changes), `MEGA_LOADER_DISPLAY` (default 55), `MEGAIO_TRACE=1` logs every
board command, `OSSFAKE_TRACE=1` every sound ioctl, `MEGA_EXTRA_ENV="A=1 B=2"` passes variables
in, `MEGAIO_SERIAL` sets the cabinet serial number of a new board EEPROM, `MEGAIO_LIGHTSHOW=1`
fits the light-show kit, `HW_*` settings switch on the hardware bridge ([connectors](connectors.md)).
`MEGAIO_*` and `HW_*` settings can also go in `cabinet.local.conf`; the environment wins.

Console: the terminal shows the cabinet's output minus lines known to be harmless (hardware
probes, the language table, the stand-ins' start-up messages, the card reader's expected crash
at shutdown: `scripts/console-filter.awk`); `build/loader/console.log` has all of it (previous run:
`console.log.1`), and `MEGA_LOADER_VERBOSE=1` shows all of it in the terminal.

Logs: the loader's own log is appended to `build/loader/var/merit/logging/logs/*.running.log`
(fields separated by `|`, records by `\x03`); crash reports land in
`build/loader/var/merit/logging/crashes/`; the stand-ins print to the terminal (`[fakeio]`,
`[twfake]`, `[ossfake]`, `[nx]`, `[crash]`).

## Settings and backups

Everything the cabinet keeps (settings, NVRAM, books, high scores, MegaNet registration, the
TournaMAXX databases, the fake board's EEPROM and key) lives in `build/loader/var/merit`. Every
`make loader-run` first snapshots it to `build/loader/backups/<date>-auto.tar.gz` (the newest 20
are kept); `make loader-backup` takes a named one, `make loader-reset` takes one before resetting.

```bash
make loader-backup                                    # build/loader/backups/<date>-manual.tar.gz
scripts/loader-backup.sh before-tournament            # … with a label
scripts/loader-backup.sh --list
make loader-restore BACKUP=20261007-203741-before-meganet.tar.gz   # loader stopped; the current
                                                      # state is kept as var/merit.before-restore-<date>
```

Logs and the swap file are left out of snapshots.

## Network and MegaNet

The loader gets its own network: a virtual wired `eth0` (slirp4netns: NAT through the PC, DHCP
gives 10.0.2.15, DNS 10.0.2.3). The cabinet's own network manager and DHCP client configure it
as on a real machine, so its Network menu, Connection Wizard and MegaNet updates work.

The disk image's settings are for wireless (a factory-fresh cabinet runs the Connection Wizard
on its first start). To connect once: **F1** → *Network* → *Connection Wizard*
→ *Next* → *Skip* → *Skip* → *Wired Ethernet* → *Wired Ethernet Network* → *Accept Settings and
Connect* → *Save Settings*. To use a community MegaNet server: *Network Options* → *MegaNet
Server* → *Set* (e.g. `us.oerinet.net`) → *Enter*, then *Connect to MegaNet/Update from Server*.

A MegaNet connection sends the server the machine's books, logs and crash dumps (as the real
cabinet did) and can change settings, menus and tournaments. `/var/merit/fakeio/net.txt` shows
the network as the cabinet sees it 25 s after start.

## Game options and the security key

The cabinet's behaviour is driven by 120 game options in NVRAM (free play, TournaMAXX, Six Stars,
languages, attract sound, …). On a real cabinet the security key (an iButton inside, read through
the I/O board) decides, per option, whether it is locked off, locked on, or the operator's choice;
Operator Setup only shows the operator's choices. Without a key everything reads as locked off:
key-gated menus disappear (no TournaMAXX, no MegaNet setup page) and Hi-Res-only games are left
out of the menu.

So the fake board serves a key image, `var/merit/fakeio/key.bin`, made by `scripts/loader-key.sh`
the first time the loader starts with this `/var`. Its contents:

- **Identity:** the cabinet's own, from NVRAM: `SA362801 R00` (a different part number would
  make the loader wipe NVRAM). A factory-fresh `/var` has no NVRAM yet, so the ION 2014 key's
  identity and factory option defaults are used.
- **Options:** every one is left to the operator, defaulting to its current value. The
  exceptions are options for hardware we don't have (MindSpark mode, TouchTunes, credit card,
  the Rowe download selector), which stay locked off.
- **Factory key data:** the cabinet code, a default price for every game, US factory pricing,
  Champion Edition values, and the language set.
- **Country and languages:** set with `KEY_COUNTRY` and `KEY_LANGUAGES` (the factory loader
  reads all of these; [io-board](../reference/io-board.md#security-key-internal-licence) has the
  layout).

After changing any of it, remake the key (`scripts/loader-key.sh --force`, cabinet stopped).

Change options in Operator Setup (*System → Options*, *Games → Options*, *Credits/Pricing*, …).
From a terminal, with the loader stopped:

```bash
scripts/loader-option.sh --list                       # index, name, value, key licence
scripts/loader-option.sh TOURNAMAXX_ENABLED           # one option
scripts/loader-option.sh ENABLE_SOUND_IN_IDLE_MODE 1  # set it (backup taken first)
scripts/loader-key.sh --show                          # decode key.bin
scripts/loader-key.sh --force                         # remake it from the current NVRAM
```

**Games that are installed but not on the menu** (Trix, for one): *Games → Game Setup →* the
category (Trix is a *Cards* game) → tap a slot in the upper list, then the game in the lower list
(games already placed are shown in red) → *Done* → *Yes*. Hi-Res-only games such as Trix are only
offered with a key image (see above).

**TournaMAXX**: with *System → Options → Tournament Mode: ON-LINE* (option `TOURNAMAXX_ENABLED`),
a MegaNet connection, free play off (or `TMAXX_OK_IN_FREEPLAY`), the start screen gets a
*Competition!* (MegaNet / TournaMAXX) button listing the server's running tournaments.

## From the installer discs

Megatouch shipped software as bootable installer CDs. The ION 2014 set
(`V40_02T130830_1115`, two discs) holds the same software as the keyless disk image, as
partimage images of each partition. With `DISC1` and `DISC2` in `cabinet.local.conf`,
`make loader-setup` builds the cabinet from them:

| Partition | From | What it is |
| --- | --- | --- |
| root | `lib/images/part5.gz.000–005` (disc 1) | the system and the factory loader |
| home | `part2` (disc 1) | `/home/maxx`, `/home/firefox` |
| ion_only | `part6.gz.000` (disc 1) and `.001–.008` (disc 2) | games and content |
| var | root's `/factorybackup/var.tar.gz` | the factory defaults, as the cabinet's first boot restores them onto its empty /var |

`scripts/partimage.py` reads the discs: `info` shows their version and images, `extract N OUT.img
DISCS…` writes one partition as a raw ext3 image (CRC-checked), which `debugfs` unpacks. The
format is described in the script.

**Compared with the keyless disk image:** the root, home and ion_only partitions are the same,
apart from leftovers of use (browser caches, kernel modules installed at first boot), except for
one file: the image's `/usr/local/bin/start` has 1,114 bytes patched in five security-key
functions ([io-board → the keyless build](../reference/io-board.md#security-key-internal-licence)).
The factory loader reads the security key through the I/O board. The fake board serves
`key.bin`, which `scripts/loader-key.sh` writes with everything that loader checks:

- the cabinet code;
- a price for every game (0 hides a game). A genuine 2014 key prices about 30 installed games 0,
  and the factory menu leaves those out; ours offers them, and `scripts/loader-menu.sh` (run at
  every start) puts them on the menu ([cabinet state → the game menu](../reference/cabinet-state.md#the-game-menu));
- the factory language set, pricing and Champion Edition values;
- the game options.

`KEY_COUNTRY`, `KEY_LANGUAGES` and `KEY_PLAYER_LANGUAGES` change the country and the languages it
offers (`scripts/loader-key.sh --countries`, `--languages`). The key only *offers* languages: which ones players can pick
is the operator's choice in Operator Setup's language screen (stored in
`settings/never/language.xml`, at most eight). Languages the key stops offering are dropped from
that list and aren't added back by themselves. Translations exist for all 31: Arabic, for one,
covers the menus and 109 games.

**First boot of a fresh install:** touchscreen calibration at every resolution, then the
Connection Wizard. `scripts/loader.sh` writes the `hardware.xml` that the cabinet's hardware
detection would (ION 945GC, FX1 board, widescreen CML-190W monitor; without it the cabinet runs
4:3). The other installer discs (ION 2008.5–2013, Force, Maxx) use the same format; of those,
Force 2011 and Maxx Crown run (next sections).

## A Force cabinet

The Force (2004–2011 software, before the ION) runs from its installer disc next to the ION
cabinet, in `build/force`. Tried with Force 2011 (`V28_40T101109_1217`); the Force discs from
2007.5 on have the same layout (`part1`, `part2`, `part5`). Older ones (2004–2006.5) have other
images and aren't supported by setup yet.

```bash
# cabinet.local.conf
FORCE_DISC1="/path/to/Force 2011 Disk 1 of 1 V28_40T101109_1217_1OF2.iso"

make loader-setup                 # also builds build/force (root, home, var from the factory backup)
make loader-run CABINET=force     # window title "Megatouch Force"
make loader-reset CABINET=force   # back to the factory /var
```

Setup checks what the discs hold: ION discs set as `FORCE_DISC1` (or the reverse) stop with a
message. The cabinet is recognised as a Force by its software (no layout daemon), so
`MEGA_LOADER_CABINET=build/force scripts/loader.sh` works too.

**First boot:** touchscreen calibration at 640×480 and 800×600 (the fake controller passes the
targets; press EXIT at each *MOVE CURSOR* check), then the menu.
**Operator Setup:** F1, as on ION.

**Network and server:** in Operator Setup → *Network*:

1. *Internet Connection*: **LAN**.
2. *Ethernet Setup* → **AUTOMATIC** (DHCP gives 10.0.2.15).
3. *Setup*:
   - *Server Name*: a community MegaNet server, e.g. `us.oerinet.net`;
   - *DNS*: **1.1.1.1** and **8.8.8.8**.
4. Answer **YES** to *Initial TournaMAXX Connection*.

The Force tests for broadband by opening a TCP connection to port 53 of its DNS servers. Under
WSL the sandbox's own DNS (10.0.2.3) passes TCP on to Windows' resolver, which refuses it, so
use public DNS servers. The first connection registers the cabinet with the server, which answers
with its registration details, tournaments and catalog.

**What a Force needs besides the ION's stand-ins** (all picked automatically for a Force):

| The Force had | Stand-in | Source |
|---|---|---|
| Its own usbfs code for the I/O board (`/proc/bus/usb`, `USBDEVFS_CONTROL`) instead of libusb | the board's entry in `/proc/bus/usb/devices` and its device file; control transfers go to the fake board | `src/fakeio/usbfsfake.c` |
| An Intel 810 motherboard with a CMI8738 sound card (platform detection) | a `/proc/bus/pci/devices` listing them | `src/fakeio/pci-devices.force810` |
| XFree86 with XF86VidMode/DGA, full-screen mode switches | windowed Allegro (`--videomode W`); the loader's unchecked VidMode calls answered; the nested screen follows the window's size with RandR (640×480, 800×600) | `src/fakeio/forcefix.c`, `xinit-force.sh` |
| MicroTouch serial controller behind the TouchWare driver (`/dev/twscreen*`) | a fake controller: OK to every command, parameter block 1 (`GP1`/`SP1`, where the loader keeps its calibration) in `fakeio/microtouch-block1.bin`, `CX` calibration answered at once | `src/fakeio/forcefix.c` |
| `/dev/rtc` (written after a network time sync; an unchecked open, and the loader's debug `close` aborts on -1) | `/dev/null` with the clock ioctls answered | `src/fakeio/forcefix.c` |
| A 2.4 kernel: `dhcpcd` 1.3.22 adds the default route with a 32-bit `SIOCADDRT` on its packet socket | today's 64-bit kernels refuse that on a packet socket (no gateway, no Internet); retried on an IPv4 socket | `src/fakeio/forcefix.c` |
| Loudmouth 1.0 (the MegaNet messenger's XMPP) expects a failed connect to poll as just "error"; Linux also reports writable/hang-up, and Loudmouth aborts the loader | for sockets with an error, only the error is reported | `src/fakeio/forcefix.c` |
| 2005 libogg/libvorbis (RPATH `/usr/local/lib`), LinuxThreads `pthread_create` exported by the loader | the runtime's Ogg/Vorbis preloaded; PulseAudio loaded with `RTLD_DEEPBIND` | `scripts/loader.sh`, `src/fakeio/ossfake.c` |
| Its security key: Force cabinet code `90322bbaa4370cfe`, data in block 0 XORed with `id[1..6] ^ "NVI6=9"` (no footer) | `loader-key.sh` writes it for a Force (`MEGA_KEY_PLATFORM=force`); the key's country and languages come from the same `KEY_*` settings | `scripts/loader-key.sh` |
| The 2005 software (Maxx Crown) dials in for TournaMAXX: a Hayes modem on `/dev/modem`, WvDial, then `pppd` (and WSL's kernel has no PPP) | `modemfake.so`: `/dev/modem` (a `/dev/null` node, for the `stat` check) opens as a pseudo-terminal answered by a fake modem (OK to every AT command, CONNECT on `ATD`, carrier detect while "connected"); `pppd-fake.sh` is mounted over `/usr/sbin/pppd` (writes slirp's DNS server, stays up until hung up); the slirp interface is named `ppp0` and configured by slirp. `MEGA_MODEM_TRACE=1` logs the AT conversation and every IPv4 `connect` | `src/fakeio/modemfake.c`, `src/fakeio/pppd-fake.sh` |

Five failed key reads in a row make the Force stop for good at its tamper screen; the count is
`read_attempts` in `var/config/settings/never/key_settings.xml`.

`make loader-backup` covers `build/loader` only; copy `build/force/var` to keep the Force's
state. Internals (files, obfuscated strings, platform detection, the touchscreen protocol, the
network and its Internet test): [force](../reference/force.md).

Verified (2026-10-11): Force 2011 from its disc to the menu through `make`; first-boot
calibration; Operator Setup; the security key; DHCP with a default route; TournaMAXX
registration with `us.oerinet.net` (tournaments and catalog received); still running after the
clock sync and the messenger's connection attempt. Not yet tried: games, coins and credits,
operator keys, MegaLink, sound in a game.

## A Maxx cabinet

The Maxx's last Linux release, Crown (`PG3002-01 V16.10`, 2005, three discs), runs next to the
ION cabinet in `build/maxx`. It runs as a Force-family cabinet with extra stand-ins (the rows
for the 2005 software in the table above, and [maxx](../reference/maxx.md)).

```bash
# cabinet.local.conf: all three discs (part5, the root, spans them)
MAXX_DISC1="/path/to/maxx_crown_v16-10_reva_d1.iso"
MAXX_DISC2="/path/to/maxx_crown_v16-10_reva_d2.iso"
MAXX_DISC3="/path/to/maxx_crown_v16-10_reva_d3.iso"

make loader-setup                 # also builds build/maxx (root, home, var from the factory backup)
make loader-run CABINET=maxx      # window title "Megatouch Maxx"
make loader-reset CABINET=maxx    # back to the factory /var
```

Setup recognises the Maxx software by its `/usr/local/share/forceonly` mount point and stops
when the discs are set under the wrong name. It also makes the directories the discs keep
unreadable readable. On every start `loader.sh` adds what this release needs: Crown's own SQLite
and text libraries, a 16-bit X screen (its Allegro is built for BGR), the Maxx menu background
under the Force's name, the DS1991 key image (made on the first start, every game licensed), the
fake modem, and the network interface as `ppp0`.

**First boot:** one calibration screen (press *EXIT*), then the main menu in English.

**TournaMAXX** dials in: modemfake answers the AT commands, `pppd-fake.sh` reports the link up, and
the call goes out over slirp to `us0.accessmerit.com`, port 17751. The community server there
refuses that port, so the call ends with "SERVER NOT RUNNING". `scripts/tmaxx-server.py
[--tournament]` is a minimal server for the protocol; start the cabinet with
`MEGA_TMAXX_SERVER=<this PC's address>:17751` to use it (slirp blocks the host's loopback address).

Verified (2026-10-11): Crown from its discs to the menu through `make loader-setup` and
`make loader-run CABINET=maxx`; the licence (every game); colours; sound; Operator Setup; the
TournaMAXX call; a tournament from the included server shown to players; F1 opens Operator
Setup; coins (F5–F8 = 25¢, 50¢, $1, $5: the key's coin channels give 1, 2, 4, 20 credits). Not yet
tried: games, operator keys.

## How it works

`make loader-setup` (`scripts/loader-setup.sh`) unpacks the cabinet's partitions with `debugfs`
(a Force's into `build/force` and a Maxx's into `build/maxx`, the same way, without `ion`):
root → `build/loader/root`, /home → `home`, ion_only → `ion` (including the `content/` packs
some games need), /var → `var` (plus `var.orig` for resets).

- **From the installer discs:** each partition is first converted from its partimage volumes
  (see *From the installer discs* above), and /var comes from the factory defaults.
- **From the disk image:** its side-B partitions are read directly.

A partition directory that already exists is never extracted again, so `/var` keeps its state.
`scripts/loader.sh` turns them into a rootless sandbox (`bwrap`, user namespaces) and runs what
`/home/maxx/.xinitrc` and `layout_start` ran: `db_state`, `layout_daemon`, the start, sidebar
and loading layouts, then `/usr/local/bin/start -name merit-start --videomode F`.

`start` is either the factory loader (installed from the discs) or the 2021 "Keyless" build
(from the disk image), which reads a copy of a key built into it instead of the security key
(see `docs/reference/io-board.md`). Everything below is ours; no cabinet file is modified.

| Cabinet had | Stand-in | Source |
|---|---|---|
| 2013 glibc, `/lib/ld-linux.so.2` | the modern i386 runtime (`shared/runtime`); its `ld-linux.so.2` is mounted over the cabinet's, so every cabinet program runs on it | `scripts/loader.sh` |
| glibc entered constructors and `main` with `%ebp = 0` (the loader's crash handler walks frame pointers to 0) | `startfix.so` wraps `__libc_start_main` and calls both through a trampoline that clears `%ebp` and keeps the stack 16-byte aligned (the code uses `fxsave` on stack buffers) | `src/fakeio/startfix.c` |
| `iopl`/`ioperm` for the pre-ION ISA board (ports 0x228–0x22A) | granted; the faulting `in`/`out` instructions are emulated (reads 0xFF) | `startfix.c`, `crashlog.c` |
| 32-bit programs could execute heap memory (Allegro 4 compiles blitters into `malloc`'d buffers) | an instruction fetch from a non-executable page makes that page RWX and retries; the loader's own SIGSEGV handler is kept behind ours | `src/fakeio/crashlog.c` |
| USB I/O board (Cypress FX1 `04b4:6473`) | `libusb-1.0.so.0` that is the board: firmware download, RAM mailbox, the 0xA1 command set, coins/buttons/DIP switches, EEPROM with the cabinet's serial, both iButton readers | `src/fakeio/fakeio.c`, `megaio.c` |
| 3M MicroTouch controller + TouchWare daemon (`libTwDrvFifo.so`) | a client library that reports one USB controller and answers reset/status/calibration; touches arrive as X mouse events, as on the cabinet | `src/fakeio/twdrvfifo.c` |
| OSS v4 `/dev/dsp`, `/dev/mixer` | `ossfake.so`: `/dev/dsp` is a socket pair fed to PulseAudio (150 ms buffer), `/dev/mixer` a stub; free space (`GETOSPACE`) counts only the audio bytes the pump hasn't taken yet (the socket's own count includes kernel overhead and made programs underfill, which clicked); the pump hands PulseAudio whole sample frames only (a stream socket can split a write anywhere, and one refused odd-sized chunk shifted every later sample: loud noise); `GETOSPACE` reports at most one free fragment per query, since Allegro mixes all free space at once and the Ogg music's 2 × 1024-frame ring was mixed past its refilled half (music slow and repeating, issue #2; `OSSFAKE_PACE=0` turns it off); a closed `/dev/dsp` that is still draining stops answering to its fd number, which a reopen gets at once (the new stream's format went to the old one: 8 kHz noise); without a PulseAudio socket the sound is dropped, with one message; FMOD (Unity games) is switched to its PulseAudio output. `OSSFAKE_TRACE=1` logs the format and, each second, the bytes forwarded and PulseAudio's lowest latency | `src/fakeio/ossfake.c` |
| OSS v4's `ossmix` / `savemixer` (volume) | `ossmix` sets the PulseAudio volume of the cabinet's streams; PulseAudio remembers it per application | `src/fakeio/ossmix.c` |
| Touchscreen calibration | the fake controller reports every calibration target as touched; CALIBRATE runs through all resolutions | `src/fakeio/twdrvfifo.c` |
| Joystick accessory | optional, on the arrow keys, raw values matching the loader's default calibration | `src/fakeio/fakeio.c` |
| ION 945GC motherboard (platform detection) | a `/proc/bus/pci/devices` listing that board's chipset | `src/fakeio/pci-devices.ion945gc` |
| The cabinet's monitor | Xephyr runs on an invisible display (Xvfb); `megaview`, an SDL2 window, shows its screen scaled (on the GPU through WSLg when the 64-bit SDL2 from `loader-setup` is there; only changed frames are redrawn) and sends mouse and keys back with XTest. `MEGA_LOADER_VIEW=xephyr` shows Xephyr's own fixed-size window instead | `src/fakeio/megaview.c`, `scripts/loader.sh` |
| X server with RandR (640×480 menu, 768×480 widescreen, 800×600, 960×600 and 1280×800 games) | Xephyr from Ubuntu's package, its RandR size table patched to include 768×480, 960×600 and 1280×800 (`scripts/lib/xephyr-sizes.py`; installs from before 2026-10-13 get 960×600 at their next start). Without a size the game draws in the top-left corner of a bigger screen | `scripts/loader-setup.sh`, `scripts/loader.sh` |
| `/sys` (Unity's graphics-card probe crashes without it) | the host's, read-only | `scripts/loader.sh` |
| glibc's charset converters (SDL 1.2 needs them to set window titles; the layout manager finds the sidebar and switcher by title) | `GCONV_PATH` → the runtime's `gconv/` | `scripts/loader.sh` |
| zlib 1.2.3 (the loader's sprite reader relies on its `gzread`/`gzseek`/`gztell` behaviour; on the modern zlib Super Boxxi and others crash) | `gz*` calls from cabinet code go to a private copy of the cabinet's zlib (renamed `libz-cabinet.so`), runtime libraries keep the modern one; `MEGA_ZLIB=modern` turns it off | `src/fakeio/zlibcompat.c` |
| Ethernet port, DHCP | its own network namespace with a virtual `eth0` from slirp4netns (user-mode NAT); the cabinet's `network_manager` and `dhclient` configure it. Network admin rights exist only inside that namespace | `scripts/loader.sh`, `src/fakeio/xinit.sh` |
| Cabinets in one venue on one Ethernet switch (MegaLink: UDP broadcasts on port 4700 to find partners, then TCP) | `MEGA_LOADER_NET=lan`: `megalan`, a userspace Ethernet switch with a libslirp router (DHCP, DNS, NAT) built in; each cabinet's `lantap` is its `eth0` on it. The MegaLink ID is the last number of the address. Switches on different PCs join over TCP (a hub and members, HMAC-SHA256 challenge with a shared password); each PC keeps its own router and gets its own block of 16 addresses, and router traffic never crosses between PCs | `src/fakeio/megalan.c`, `src/fakeio/lantap.c`, `scripts/new-cabinet.sh` |
| Programs started with an empty environment (`dhclient` runs its script that way) found the 2013 libc | the runtime is mounted at `/lib32` (the modern loader's first default directory) and the cabinet's `/etc/ld.so.cache` is hidden | `scripts/loader.sh` |
| A cabinet's own serial number and MegaNet ID | a per-install identity (`build/loader/var.identity`): the serial goes into the fake board's EEPROM, the MegaNet ID into the cabinet's encrypted network settings through its own network library (`netcfg`) | `scripts/loader-identity.sh`, `src/fakeio/netcfg.cpp` |
| Security key (licence iButton) | a key image of this cabinet's own key, made from NVRAM; served block by block by the fake board | `scripts/loader-key.sh`, `src/fakeio/fakeio.c` |
| The 2008 allocator, which left freed memory intact (the sound manager's `StopSound` erases a map entry and keeps walking from it; on the modern runtime Trix froze after the first card, its last sound repeating) | `soundfix.so`: every sound backend's `ImplementationStopSound` still stops the voice but reports "not stopped", so the entry is not erased mid-walk (known bug 15, as in the SDL2 backend) | `src/fakeio/soundfix.c` |
| The ION's Intel graphics driver, which drew `glDrawPixels` pictures whatever 3D state was on and showed front-buffer drawing at once (on Mesa the "quit this game?" box over a black-and-white copy of the game came out black in Trivia Whiz 2 and Rack 'Em, as did Monkey Bash's loading bar) | `glfix.so`: `glDrawPixels` with depth test, texturing, alpha test, fog and stencil off; front-buffer drawing and reading done on the back buffer over the last frame shown, swapped in at the next flush. `MEGA_GLFIX=0` turns it off | `src/fakeio/glfix.c`, `scripts/loader.sh` |
| ION light-show kit (LED lighting on the board's PSoC) | optional (`MEGAIO_LIGHTSHOW=1`): the fake board reports PSoC version 2 and a detected kit, answers the loader's light packets and logs them as events | `src/fakeio/fakeio.c` |
| Books printer ("Minidrucker", read through the board) | **F4** / `megaio print` plugs one in; the attract loop prints the books through board command 5, saved as `fakeio/printouts/books-*.txt` | `src/fakeio/fakeio.c`, `megaio.c` |
| Meters, lockout, coins, buttons, keys, lights as signals for other equipment | every hardware event goes to `fakeio/events.jsonl`; `gameevents.so` adds games starting and ending (it wraps `Logger::SetGameID`, which `ExecuteGameID` calls before and after every game); `scripts/hwbridge.py` passes events on (commands, MQTT, webhook, WLED) and feeds real inputs (input devices, GPIO, 1-Wire iButton reader) into the board | `src/fakeio/gameevents.c`, `scripts/hwbridge.py` |
| `layout` logging (crashes in the cabinet's `liblogging` on the modern runtime; the cabinet's own log shows the same bad data, which used not to crash) | `layout` runs with `Logger::Log` turned into a no-op | `src/fakeio/nolog.c`, `layout-quiet` |

What was verified (2026-10-07): volume from Volume Control reaching PulseAudio; CALIBRATE and
the joystick calibration (all four directions and both buttons); boot to the ION attract loop and menus; Operator Setup with all
its menus, *Diagnostics → I/O Test* showing every coin channel, SETUP, CALIBRATE, DS1 and the
lockout/meter outputs; registering an operator key and opening setup with it plus its PIN;
coins giving credits with free play off; classic games (Tri Towers) and Unity games (Clocker
Solitaire, Cardboard Chaos, Super Run 21) launched from the menu with sound; returning to the
menu when a game's player exits.

Since then (2026-10-07 to 10-09): Super Boxxi (cabinet zlib); the sidebar and its switcher;
DHCP on the virtual network, the Connection Wizard (wired), MegaNet registration check and
updates from a community server, TournaMAXX listing the server's tournaments; the key image
(Operator Setup's key-gated options, Tournament Mode, the MegaNet page); Trix added to the menu
via Game Setup and played through several tricks at 1280×800; Milky Way Mini Golf started; the
scalable window following resolution changes, with touches mapped at any size.

## Known issues

- *Hardware Serial Number* and S.M.A.R.T. warnings in the log are expected (no disk). `wlan0`
  errors too: there is no wireless card.
- `key footer failed checksum` in the log means the loader ran without a key image
  (`MEGA_LOADER_KEY=none`, or a `/var` from before key images): it still runs, but key-gated
  options are locked off. `scripts/loader-key.sh` makes one.
- The shell helpers (`scripts/loader.sh run …`) join display `:55` only while a loader runs;
  otherwise they use the desktop's display.
- Not every game has been tried. Linked play (MegaLink) works between cabinets on one PC
  (`MEGA_LOADER_NET=lan`, [operator guide §17](operator-guide.md#17-linked-cabinets-megalink-experimental));
  between PCs it works on the network side but no game has been played that way yet.
- WSLg's sound server can hang (no sound in any Linux app); `wsl --shutdown` fixes it.
