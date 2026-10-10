# Troubleshooting

Problems you might hit while setting up or running the cabinet, and what to do about each one.
The [cabinet loader](#cabinet-loader) section is the one operators need; the sections after it are
for the standalone game ports. For debugging a game you're porting, see [debugging](debugging.md).

## Setup

**`missing: debugfs`**, **`missing tool: bwrap`** (or another tool)
: `sudo apt install e2fsprogs binutils gcc g++ make python3 python3-venv curl bubblewrap`.

**`cabinet image not found: ''`**
: Create `cabinet.local.conf` with `IMG="/full/path/to/the.img"`. Quote it, because the name
  has spaces. Or put a snapshot at `./cabinet/`.

**`debugfs: Bad magic number in super-block`**
: Wrong partition offset or wrong image. The offsets in `cabinet.conf` are for the
  *Megatouch ION 2014 HDD Keyless* 60 GB image. For another image, run `fdisk -l image.img`,
  multiply each start sector by 512, and match labels via `/etc/fstab` on the root partition.
  Put the new values in `cabinet.local.conf`.

**`apt-get download` fails / `(skip <package>)` lines**
: A few skips are normal: names differ between Ubuntu releases. If SDL2 or Mesa is missing,
  `make run` will fail later. Check internet access, then delete `toolchain/i386` and
  `toolchain/apt-i386` and rerun `make setup`. On a non-Ubuntu distribution the archive URLs won't
  match; use Ubuntu (WSL makes that easy).

**Compiler errors about `bits/wordsize.h` or `gnu/stubs-32.h`**
: The sysroot is incomplete. Delete `toolchain/sysroot` and rerun `make setup`. After moving the
  repository, rerun `make setup` too: it rewrites the absolute paths in the sysroot's linker
  scripts and `g++32`.

**Setup is slow**
: It's much slower on `/mnt/c` or `/mnt/e`. Clone into the Linux home directory.

## Cabinet loader

**No sound, also in other Linux apps** (`pactl info` hangs or says `Connection failure: Timeout`)
: WSLg's sound server has stopped responding. In PowerShell: `wsl --shutdown`, then reopen Ubuntu
  and start again. The cabinet's settings are not affected.

**No sound from the cabinet only**
: Check the cabinet's volume (*System → Volume Control*) and the Windows volume. Look for
  `[ossfake]` lines in the terminal.

**`no cabinet root: run make loader-setup`** / **`no fake I/O board: run make loader`**
: Run `make loader-setup` (once), then `make loader-run` (which builds the stand-ins).

**`Xvfb did not start` / `Xephyr did not start` / `megaview did not start`**
: See `build/loader/xvfb.log`, `xephyr.log` or `megaview.log`. A stale display from a killed run
  can block the number: wait a few seconds and retry, or pick another one with
  `MEGA_LOADER_DISPLAY=57 make loader-run`. `MEGA_LOADER_VIEW=xephyr make loader-run` skips the
  scalable window and shows Xephyr's own (fixed size) window.

**`bwrap: setting up uid map: Permission denied`** (or another `bwrap` error about namespaces)
: The sandbox needs unprivileged user namespaces. WSL normally allows them. On a Linux desktop
  check `sysctl kernel.unprivileged_userns_clone` (Debian) or
  `sysctl kernel.apparmor_restrict_unprivileged_userns` (Ubuntu 24.04+), and use the distribution's
  `bubblewrap` package.

**The window opens but stays black**
: The cabinet takes about 20 seconds to start. After an unclean stop it first shows *Performing
  database maintenance*, which can take longer. If it stays black, check the terminal for
  `[crash]` lines.

**The whole cabinet closes when a game starts or ends**
: Fixed (the viewer didn't survive the resolution change). Update with `git pull && make loader`.

**F1 / F5–F8 do nothing**
: The keys are read from the cabinet's window: click into it first. In the attract loop, touch
  once before F1.

**A game freezes, or loops a sound**
: Note the game and what you did, and keep `build/loader/var/merit/logging/logs/*.running.log`
  and the terminal output. Trix used to freeze after the first card (fixed by `soundfix.so`).

**Lots of `[crash]` lines in the terminal**
: The card reader's expected crash at shutdown is no longer shown (it's in
  `build/loader/console.log`). A stream of them that never stops was a bug in `crashlog.so`,
  fixed: update. Any other `[crash]` is real: report it with the console log.

**Where's all the output?**
: The terminal leaves out known-harmless lines. `build/loader/console.log` (previous run:
  `console.log.1`) has everything; `MEGA_LOADER_VERBOSE=1 make loader-run` shows everything.

**Network shows *No Internet* / MegaNet connection fails**
: Run the Connection Wizard for **Wired Ethernet** ([operator guide](operator-guide.md#11-network-meganet-and-tournamaxx)).
  `build/loader/slirp.log` shows the virtual network; `/var/merit/fakeio/net.txt` (in
  `build/loader/var/merit/fakeio/`) shows the cabinet's view of it 25 s after start.

**TournaMAXX: the Tournament buttons stay greyed out**
: TournaMAXX needs *Tournament Mode: ON-LINE* (System → Options), free play off, and a
  successful MegaNet update with tournaments on the server for your machine.
  `scripts/loader-option.sh TOURNAMAXX_ENABLED` shows the option.

**`key footer failed checksum` in the cabinet's log**
: The cabinet ran without a key image (`MEGA_LOADER_KEY=none`, or a `/var` from before key
  images). `scripts/loader-key.sh` makes one; key-gated options then show up in Operator Setup.

**`stop the loader first (it rewrites nvram.dat)`**
: `loader-option.sh` and `loader-backup.sh --restore` only change settings while the cabinet is
  stopped. Close its window first.

**Kiosk: the cabinet keeps coming back after closing it**
: That's kiosk mode: anything but **Ctrl+Alt+End** counts as a crash. Or run `make kiosk-stop`
  from a terminal or over SSH.

**Kiosk: restarts over and over**
: `scripts/cabinet.sh status` and `build/loader/kiosk.log` show each exit and its status. Stop
  it (`make kiosk-stop`) and run `make loader-run` in a terminal to see the error. A hang
  restart says `cabinet hung: picture frozen …`; raise `KIOSK_HANG_SECS` if a game legitimately
  sits on a still picture with a busy CPU.

**Kiosk doesn't start at login**
: Check `~/.config/autostart/megatouch-cabinet.desktop` exists (`scripts/cabinet.sh autostart on`)
  and that the session is a desktop session (GNOME, KDE, Xfce…). For booting without a login
  screen, turn on automatic login.

**`make update` says "local changes to tracked files"**
: Something in the project's own files was edited. `git status` lists them; `git stash` sets them
  aside (or `git checkout -- <file>` drops them), then update again.

**An update made things worse**
: `scripts/update.sh --rollback` (cabinet stopped) returns to the previous version; settings are
  never changed by updates, and a backup was taken before (`before-update`). Kiosk boxes roll back
  by themselves when the cabinet keeps failing right after a nightly update; `build/loader/kiosk.log`
  says so.

**Settings look wrong after an experiment**
: `scripts/loader-backup.sh --list`, then `make loader-restore BACKUP=<file>`. Every start made one.

**Real hardware, lights or MQTT don't react**
: Start with `scripts/hwbridge.py --check` (are the `HW_` settings seen?) and
  `scripts/hwbridge.py --watch` (does the cabinet produce the events?), then
  `build/loader/var/merit/fakeio/hwbridge.log`. The [connectors guide](connectors.md#checking-and-troubleshooting)
  has a table of causes.

**F4 does nothing / no printout**
: The cabinet only prints on its attract screen. Leave menus and Operator Setup, wait for the attract
  loop, then press F4. `megaio print` says when nothing printed within 150 s.

**Joystick games disappeared from the menu after turning on the light show**
: Expected: a board with the light-show PSoC makes the menu drop joystick-only games when no joystick
  is fitted (as on a real cabinet). Add `MEGAIO_JOYSTICK=1`, or turn `MEGAIO_LIGHTSHOW` off; then
  put the games back in *Games → Game Setup*.

## Standalone ports: starting a game

**`games/<name>/run: No such file or directory`**
: The game folder hasn't been extracted yet (normal after a fresh clone). Run `make games` or
  `make new GAME=<name>`.

**`games/<name>` is empty**
: Submodules weren't fetched. Run `git submodule update --init`.

**`no game.conf in …`**
: Same as above, or you're running `run` from a copy without `game.conf`.

**`error while loading shared libraries: libSDL2-2.0.so.0`**
: The runtime is missing or a symlink broke. `ls -la games/<name>/runtime` should point at
  `shared/runtime`. Rerun `make setup`.

**`undefined symbol: _ZN…`** right after launch
: The game needs a loader service we don't have yet. `make analyze GAME=<name>`, then see
  [porting](porting-a-game.md#2-make-it-load-stand-ins).

**`cannot open display` / `SDL_CreateWindow: …`**
: No display server. On WSL, check `echo $DISPLAY $WAYLAND_DISPLAY` (they should be set), run
  `wsl --update` in PowerShell, then `wsl --shutdown` and reopen. On plain Linux over SSH, use a
  local session or `SDL_VIDEODRIVER=offscreen` for headless tests.

## Standalone ports: while playing

**No sound**
: Check that you aren't exporting `SDL_AUDIODRIVER=dummy` from an earlier headless test
  (`env | grep SDL`). Check Windows sound works. Restart WSL (`wsl --shutdown`) if WSLg audio got
  stuck. `DEBUG=sound` shows whether the game is asking for sounds.

**Music doesn't play but effects do**
: That was the `len_cvt` bug (fixed). If it comes back, `DEBUG=sound` will show music tracks with
  `samples=0`.

**Text shows as boxes**
: The Pango modules aren't being found. Check that `games/<name>/data/pango/modules/` has `.so`
  files and that `$XDG_RUNTIME_DIR/megatouch-$(id -u)/pango.modules` was written. Re-scaffold
  with `make new GAME=<name> FORCE=1`.

**Help shows `HELP_TEXT` or other key names**
: The translation table wasn't found: check
  `shared/data-common/usr/local/gamedata/translations/<dll>.utf8` exists.

**Window is tiny or huge**
: The window is resizable. F11 toggles fullscreen. The picture keeps its aspect ratio.

**Art is cut off or in a corner**
: `WIDTH`/`HEIGHT` in `game.conf` don't match the art. Try the size of the largest PNG
  (`python3 tools/largest-png.py games/<name>/data/usr/local/ion_only/games/<dir>`). Test with
  `MEGA_WIDTH=1024 MEGA_HEIGHT=768 make run GAME=<name>`.

**Occasional stutter**
: First-time loads of big animations and music track changes cause 100–200 ms hitches (known).
  Constant lag means something else: run `DEBUG=profile`.

**Game logic is too slow or too fast**
: You have `MEGA_FPS` set. Remove it; games are tuned for 30.

**Hi-score is wrong or you want to reset it**
: Delete `games/<name>/data/var/merit/highscores/<GAME_ID>.txt`. Set `MEGA_PLAYER_NAME` to choose
  the saved name.

**Crash in `_Rb_tree_increment` or another `std::` function**
: Make sure you start through `run` (or `make run`), which sets
  `GLIBC_TUNABLES=glibc.malloc.tcache_count=0`. Starting `megatouch-host` directly skips the whole
  launcher environment.

## Git and publishing

**`make publish`: `uncommitted changes`**
: Commit first, in the game repo (`git -C games/<name> commit`) and/or the main repo.

**Authentication fails on push**
: The scripts use your git credential helper. On WSL, use Git for Windows' credential manager:
  ```bash
  git config --global credential.helper "/mnt/c/Program\ Files/Git/mingw64/bin/git-credential-manager.exe"
  ```
  The token needs permission to create repositories in the org (`repos.conf`).

**Main repo shows `modified: games/<name> (new commits)`**
: The game repo moved ahead. Commit the new pointer in the main repo (`git add games/<name>`),
  or let `make publish GAME=all` do it.

**Submodule in "detached HEAD"**
: Normal after `git submodule update`. Run `git -C games/<name> switch main` before committing.
