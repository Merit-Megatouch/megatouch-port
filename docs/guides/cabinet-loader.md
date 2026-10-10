# Running the cabinet's own loader

The Megatouch ION loader from the disk image (menus, attract mode, Operator Setup, coins,
operator and player keys, and every game it launches) runs unmodified on Linux / WSL2. The
missing cabinet hardware is replaced by stand-ins: a fake USB I/O board, a fake touchscreen
controller, a security-key image, an OSS sound device that plays through PulseAudio, a virtual
wired network, and a nested X server that changes resolution the way the cabinet does, shown
scaled in a normal window.

This is the technical guide. **Operators**: the [operator guide](operator-guide.md) covers
installing, running and setting up the cabinet step by step.

```bash
make setup            # once: the 32-bit runtime the cabinet's programs run on
make loader-setup     # once: extract the image's partitions (~11 GB), download Xephyr, Xvfb, SDL2, slirp4netns
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
build/loader/bin/megaio status                 # polls, lockout flags, meters, status bytes
```

### Setting up the operator key

The image has free play on and no operator keys. To use **F9** as the operator key:

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
| `make loader-reset` | put the loader's `/var` (settings, NVRAM, books, high scores, logs) back as it was on the image (a backup is taken first) |
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
in, `MEGAIO_SERIAL` sets the cabinet serial number of a new board EEPROM.

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

The image's settings are for wireless. To connect once: **F1** → *Network* → *Connection Wizard*
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
the first time the loader starts with this `/var`. It carries the cabinet's own identity from
NVRAM (`SA362801 R00`; a different part number would make the loader wipe NVRAM) and leaves every
option to the operator, defaulting to its current value, except options for hardware we don't
have (MindSpark mode, TouchTunes, credit card, the Rowe download selector), which stay locked
off. Prices, coin values, country and languages stay as the loader saw them without a key.

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

## How it works

`make loader-setup` (`scripts/loader-setup.sh`) dumps the image's side-B partitions with
`debugfs`: root → `build/loader/root`, /var → `var` (plus `var.orig` for resets), /home → `home`.
The ion_only partition (`ion`, including the `content/` packs some games need) is dumped too.
A partition directory that already exists is never extracted again, so `/var` keeps its state.
`scripts/loader.sh` turns them into a rootless sandbox (`bwrap`, user namespaces) and runs what
`/home/maxx/.xinitrc` and `layout_start` ran: `db_state`, `layout_daemon`, the start, sidebar
and loading layouts, then `/usr/local/bin/start -name merit-start --videomode F`.

`start` is the 2021 "Keyless" build of the loader (see `docs/reference/io-board.md`): it needs
the I/O board, not the security key. Everything below is ours; no cabinet file is modified.

| Cabinet had | Stand-in | Source |
|---|---|---|
| 2013 glibc, `/lib/ld-linux.so.2` | the modern i386 runtime (`shared/runtime`); its `ld-linux.so.2` is mounted over the cabinet's, so every cabinet program runs on it | `scripts/loader.sh` |
| glibc entered constructors and `main` with `%ebp = 0` (the loader's crash handler walks frame pointers to 0) | `startfix.so` wraps `__libc_start_main` and calls both through a trampoline that clears `%ebp` and keeps the stack 16-byte aligned (the code uses `fxsave` on stack buffers) | `src/fakeio/startfix.c` |
| `iopl`/`ioperm` for the pre-ION ISA board (ports 0x228–0x22A) | granted; the faulting `in`/`out` instructions are emulated (reads 0xFF) | `startfix.c`, `crashlog.c` |
| 32-bit programs could execute heap memory (Allegro 4 compiles blitters into `malloc`'d buffers) | an instruction fetch from a non-executable page makes that page RWX and retries; the loader's own SIGSEGV handler is kept behind ours | `src/fakeio/crashlog.c` |
| USB I/O board (Cypress FX1 `04b4:6473`) | `libusb-1.0.so.0` that is the board: firmware download, RAM mailbox, the 0xA1 command set, coins/buttons/DIP switches, EEPROM with the cabinet's serial, both iButton readers | `src/fakeio/fakeio.c`, `megaio.c` |
| 3M MicroTouch controller + TouchWare daemon (`libTwDrvFifo.so`) | a client library that reports one USB controller and answers reset/status/calibration; touches arrive as X mouse events, as on the cabinet | `src/fakeio/twdrvfifo.c` |
| OSS v4 `/dev/dsp`, `/dev/mixer` | `ossfake.so`: `/dev/dsp` is a socket pair fed to PulseAudio, `/dev/mixer` a stub; FMOD (Unity games) is switched to its PulseAudio output | `src/fakeio/ossfake.c` |
| OSS v4's `ossmix` / `savemixer` (volume) | `ossmix` sets the PulseAudio volume of the cabinet's streams; PulseAudio remembers it per application | `src/fakeio/ossmix.c` |
| Touchscreen calibration | the fake controller reports every calibration target as touched; CALIBRATE runs through all resolutions | `src/fakeio/twdrvfifo.c` |
| Joystick accessory | optional, on the arrow keys, raw values matching the loader's default calibration | `src/fakeio/fakeio.c` |
| ION 945GC motherboard (platform detection) | a `/proc/bus/pci/devices` listing that board's chipset | `src/fakeio/pci-devices.ion945gc` |
| The cabinet's monitor | Xephyr runs on an invisible display (Xvfb); `megaview`, an SDL2 window, shows its screen scaled (on the GPU through WSLg when the 64-bit SDL2 from `loader-setup` is there; only changed frames are redrawn) and sends mouse and keys back with XTest. `MEGA_LOADER_VIEW=xephyr` shows Xephyr's own fixed-size window instead | `src/fakeio/megaview.c`, `scripts/loader.sh` |
| X server with RandR (640×480 menu, 768×480 widescreen, 800×600 and 1280×800 games) | Xephyr from Ubuntu's package, its RandR size table patched to include 768×480 and 1280×800 | `scripts/loader-setup.sh` |
| `/sys` (Unity's graphics-card probe crashes without it) | the host's, read-only | `scripts/loader.sh` |
| glibc's charset converters (SDL 1.2 needs them to set window titles; the layout manager finds the sidebar and switcher by title) | `GCONV_PATH` → the runtime's `gconv/` | `scripts/loader.sh` |
| zlib 1.2.3 (the loader's sprite reader relies on its `gzread`/`gzseek`/`gztell` behaviour; on the modern zlib Super Boxxi and others crash) | `gz*` calls from cabinet code go to a private copy of the cabinet's zlib (renamed `libz-cabinet.so`), runtime libraries keep the modern one; `MEGA_ZLIB=modern` turns it off | `src/fakeio/zlibcompat.c` |
| Ethernet port, DHCP | its own network namespace with a virtual `eth0` from slirp4netns (user-mode NAT); the cabinet's `network_manager` and `dhclient` configure it. Network admin rights exist only inside that namespace | `scripts/loader.sh`, `src/fakeio/xinit.sh` |
| Cabinets in one venue on one Ethernet switch (MegaLink: UDP broadcasts on port 4700 to find partners, then TCP) | `MEGA_LOADER_NET=lan`: `megalan`, a userspace Ethernet switch with a libslirp router (DHCP, DNS, NAT) built in; each cabinet's `lantap` is its `eth0` on it. The MegaLink ID is the last number of the address | `src/fakeio/megalan.c`, `src/fakeio/lantap.c`, `scripts/new-cabinet.sh` |
| Programs started with an empty environment (`dhclient` runs its script that way) found the 2013 libc | the runtime is mounted at `/lib32` (the modern loader's first default directory) and the cabinet's `/etc/ld.so.cache` is hidden | `scripts/loader.sh` |
| A cabinet's own serial number and MegaNet ID | a per-install identity (`build/loader/var.identity`): the serial goes into the fake board's EEPROM, the MegaNet ID into the cabinet's encrypted network settings through its own network library (`netcfg`) | `scripts/loader-identity.sh`, `src/fakeio/netcfg.cpp` |
| Security key (licence iButton) | a key image of this cabinet's own key, made from NVRAM; served block by block by the fake board | `scripts/loader-key.sh`, `src/fakeio/fakeio.c` |
| The 2008 allocator, which left freed memory intact (the sound manager's `StopSound` erases a map entry and keeps walking from it; on the modern runtime Trix froze after the first card, its last sound repeating) | `soundfix.so`: every sound backend's `ImplementationStopSound` still stops the voice but reports "not stopped", so the entry is not erased mid-walk (known bug 15, as in the SDL2 backend) | `src/fakeio/soundfix.c` |
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
- Not every game has been tried. Linked play between cabinets (MegaLink) needs cabinets on one
  network and is not set up yet ([roadmap](../roadmap.md)).
- WSLg's sound server can hang (no sound in any Linux app); `wsl --shutdown` fixes it.
