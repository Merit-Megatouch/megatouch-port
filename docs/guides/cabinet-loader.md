# Running the cabinet's own loader

The Megatouch ION loader from the disk image (menus, attract mode, Operator Setup, coins,
operator and player keys, and every game it launches) runs unmodified on Linux / WSL2. The
missing cabinet hardware is replaced by stand-ins: a fake USB I/O board, a fake touchscreen
controller, an OSS sound device that plays through PulseAudio, and a nested X server that can
change resolution the way the cabinet does.

```bash
make loader-setup     # once: extract the image's partitions, download Xephyr (~5 GB, a few minutes)
make loader-run       # builds the stand-ins and starts the loader in its own window
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
| `make loader-reset` | put the loader's `/var` (settings, NVRAM, books, high scores, logs) back as it was on the image |
| `scripts/loader.sh shell` | a shell inside the cabinet userland |
| `scripts/loader.sh run <cmd>` | run a command inside it; joins the running loader's display |
| `scripts/loader.sh run /opt/fakeio/shots.sh /var/merit/shot` | screenshot every window to `build/loader/var/merit/shot-*.png` |
| `scripts/loader.sh run /opt/fakeio/xtouch click X Y` | a scripted touch (also `drag`, `key NAME[+NAME]`) |

Volume: Operator Setup → *System* → *Volume Control* (and the loader's own volume changes) run
`ossmix`, which here sets the PulseAudio volume of the loader's and the games' sound streams.

The joystick is off by default because a present joystick changes the attract loop and some
games. With it on, *CALIBRATE* (F2) continues into the joystick calibration after the
touchscreen one.

Settings (environment): `MEGA_LOADER_VAR=<dir>` uses another directory as `/var` (a second,
throwaway session: copy `build/loader/var.orig`, pick another `MEGA_LOADER_DISPLAY`), `MEGA_LOADER_X=host` draws on the desktop's X server instead of
Xephyr (no resolution changes), `MEGA_LOADER_DISPLAY` (default 55), `MEGAIO_TRACE=1` logs every
board command, `OSSFAKE_TRACE=1` every sound ioctl, `MEGA_EXTRA_ENV="A=1 B=2"` passes variables
in, `MEGAIO_SERIAL` sets the cabinet serial number of a new board EEPROM.

Logs: the loader's own log is appended to `build/loader/var/merit/logging/logs/*.running.log`
(fields separated by `|`, records by `\x03`); crash reports land in
`build/loader/var/merit/logging/crashes/`; the stand-ins print to the terminal (`[fakeio]`,
`[twfake]`, `[ossfake]`, `[nx]`, `[crash]`).

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
| X server with RandR (640×480 menu, 768×480 widescreen, 800×600 and 1280×800 games) | Xephyr from Ubuntu's package, its RandR size table patched to include 768×480 and 1280×800 | `scripts/loader-setup.sh` |
| `/sys` (Unity's graphics-card probe crashes without it) | the host's, read-only | `scripts/loader.sh` |
| glibc's charset converters (SDL 1.2 needs them to set window titles; the layout manager finds the sidebar and switcher by title) | `GCONV_PATH` → the runtime's `gconv/` | `scripts/loader.sh` |
| zlib 1.2.3 (the loader's sprite reader relies on its `gzread`/`gzseek`/`gztell` behaviour; on the modern zlib Super Boxxi and others crash) | `gz*` calls from cabinet code go to a private copy of the cabinet's zlib (renamed `libz-cabinet.so`), runtime libraries keep the modern one; `MEGA_ZLIB=modern` turns it off | `src/fakeio/zlibcompat.c` |
| `layout` logging (crashes in the cabinet's `liblogging` on the modern runtime; the cabinet's own log shows the same bad data, which used not to crash) | `layout` runs with `Logger::Log` turned into a no-op | `src/fakeio/nolog.c`, `layout-quiet` |

What was verified (2026-10-07): volume from Volume Control reaching PulseAudio; CALIBRATE and
the joystick calibration (all four directions and both buttons); boot to the ION attract loop and menus; Operator Setup with all
its menus, *Diagnostics → I/O Test* showing every coin channel, SETUP, CALIBRATE, DS1 and the
lockout/meter outputs; registering an operator key and opening setup with it plus its PIN;
coins giving credits with free play off; classic games (Tri Towers) and Unity games (Clocker
Solitaire, Cardboard Chaos, Super Run 21) launched from the menu with sound; returning to the
menu when a game's player exits.

## Known issues

- *Hardware Serial Number*, S.M.A.R.T. and network warnings in the log are expected (no disk,
  no MegaNet).
- `key footer failed checksum` in the log: there is no security-key image; this build of the
  loader does not need one.
- The shell helpers (`scripts/loader.sh run …`) join display `:55` only while a loader runs;
  otherwise they use the desktop's display.
