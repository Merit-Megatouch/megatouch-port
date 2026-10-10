# Commands, settings and switches

Everything in one place: make targets, the scripts behind them, the tools, every `game.conf` key
and every environment variable. Run `make help` for the short version.

- [Make targets](#make-targets)
- [game.conf](#gameconf)
- [Cabinet loader](#cabinet-loader)
- [Environment variables](#environment-variables)
- [Engine debug flags](#engine-debug-flags)
- [Tools](#tools)
- [Configuration files](#configuration-files)

## Make targets

Run them from the repository root. `GAME=` takes a game folder name under `games/`, which is
the game's DLLName, for example `g_trix`.

| Target | What it does | Typical time |
| --- | --- | --- |
| `make setup` | Toolchain, runtime, shared cabinet data and build. Run once. | 2–3 min |
| `make` / `make build` | Rebuild `shared/bin/` (host, backend, `gameids`, stubs) | seconds |
| `make new GAME=<name> [FORCE=1]` | Scaffold `games/<name>` from the cabinet | 10–60 s |
| `make games` | Run `new` for every game folder that has no `lib/` yet (after a clone) | 30 s per game |
| `make run GAME=<name> [DEBUG=…]` | Play | — |
| `make analyze GAME=<name>` | Loader symbols still missing → `notes/unresolved.txt` | seconds |
| `make decompile GAME=<name>` | Ghidra C of the game library → `games/<name>/decomp/` | 1–5 min |
| `make package GAME=<name> DEST=<dir>` | Self-contained copy, symlinks resolved | seconds |
| `make snapshot` | Copy what porting needs out of the image → `cabinet/` | 5–15 min |
| `make publish GAME=<name>\|all` | Push to GitHub, creating repos and submodules as needed | seconds |
| `make docs` | Regenerate [games.md](games.md) | 30 s |
| `make survey` | Regenerate [gamedevice-survey.md](gamedevice-survey.md) | 5–10 min |
| `make loader-setup` | The cabinet's own loader: dump its partitions to `build/loader/`; download Xephyr (patched), Xvfb, SDL2 and slirp4netns into `toolchain/debug/` ([guide](../guides/cabinet-loader.md)) | 3–10 min |
| `make loader` | Build the loader's stand-ins into `build/loader/bin/` | seconds |
| `make loader-run` | Build the stand-ins and run the cabinet in a scalable window (F11 fullscreen) | — |
| `make loader-reset` | Restore the loader's `/var` from `build/loader/var.orig` (backed up first) | seconds |
| `make loader-backup` / `make loader-restore BACKUP=<file>` | Snapshot / restore the loader's `/var/merit` (`build/loader/backups`) | seconds |
| `scripts/loader-option.sh --list` / `NAME 0\|1` | Show / set the loader's game options (loader stopped) | seconds |
| `scripts/loader-key.sh [--show\|--force]` | Make / decode the fake board's security-key image | seconds |
| `make clean` | Delete `build/` (object files) except `build/loader/` (extracted cabinet, loader settings); the loader's stand-ins are rebuilt by `make loader` | — |
| `make help` | Command summary | — |

### make setup

Runs `scripts/setup.sh`. Each step leaves a `.done` marker and is skipped next time. Delete a
folder to redo its step.

| Step | Output | Source |
| --- | --- | --- |
| Check host tools | — | `gcc g++ make python3 curl debugfs apt-get dpkg-deb readelf nm objdump` |
| 32-bit compiler | `toolchain/sysroot/`, `toolchain/g++32` | Ubuntu multilib packages (`scripts/packages/multilib.txt`, `@GCC@` = host gcc major) |
| 32-bit libraries | `toolchain/i386/` | 95 i386 packages via a private apt (`scripts/packages/i386.txt`): SDL2 (sdl2-classic), SDL2_image, Mesa, PulseAudio client, libpng, … |
| Python helpers | `toolchain/venv/` | pyelftools, capstone |
| Ghidra (only `scripts/setup.sh --ghidra`) | `toolchain/ghidra/` | Temurin JDK 21 and Ghidra 12.1.4, about 800 MB |
| 2008 libraries | `shared/cabinet-libs/` | From the image: libexpat.so.0, libsqlite.so.0, libsqlite3.so.0, libssl.so.6 (and crypto, krb5…), Pango 1.14, glib, fontconfig, freetype |
| Engine SDK | `shared/engine-sdk/` | From the image: libgame_device, libgraphics, libcore, libinput, libmerit_sound, libenums plus their closure (the backend links against these) |
| Shared data | `shared/data-common/` | gamedata `config translations help ttf fonts`, var/merit `locale settings`, `.fonts.conf`, Pango modules, `usr/local/games/default` |
| Legacy shared assets | `shared/data-common/usr/local/gamedata/gamegraphics/misc` | From the image: sounds, images and databases shared by legacy games (216 MB) |
| Unity player | `shared/unity/` | From the image: `LinuxPlayer` and libGLU, libcurl.so.3, libfmodex, libtheora, libgthread with their closure |
| Runtime | `shared/runtime/` | glibc, libstdc++, libgcc from the sysroot; i386 libs; the 2008 libraries last. Never the image's libz. |
| Build | `shared/bin/` | `make build` |

The Ubuntu release is taken from `/etc/os-release`, so the i386 packages always match the host.

### make new

`scripts/new-game.sh <name> [--force]`. `<name>` can be:

- a DLLName (`g_word_dojo_2`)
- an asset folder name
- a GameId (`G_WORD_DOJO_2`), case-insensitive

It looks in the master `gamedata.xml` first, then in each game folder's own `gamedata.xml`.

| Step | Result |
| --- | --- |
| Identify | GameId name, DLL, asset folder, description, declared resolution |
| Extract code | `lib/<dll>.so` plus its NEEDED closure. Drops libraries the runtime supplies (libc, libm, libstdc++, libz, …) and the original backend (`*_sprite`, libmerit2d/3d, libmeritbasegame, libmerit_threads). Links our `libgame_device_sprite.so` and the three empty stubs. |
| Family | GameDevice, Unity, Merit3D or legacy, written to `notes/scaffold.md`. Only GameDevice gets the backend. |
| Extract assets | `data/usr/local/ion_only/games/<dir>` (or `data/usr/local/games/<dir>` for older games). Links shared `gamedata`, `games/default`, `etc`, `pango`. Copies a private, writable `data/var/merit`. |
| Links | `run → scripts/launch.sh`, `runtime → shared/runtime`, `megatouch-host → shared/bin/megatouch-host` |
| game.conf | GameId number from `shared/bin/gameids`; window size from the declared resolution, replaced by the largest PNG when that is a screen size |
| Notes | `notes/scaffold.md` (regenerated each time), `notes/unresolved.txt`, `NOTES.md` (created once, never overwritten) |
| Repo | `git init` if `games/<name>` is not a repo yet. Writes `.gitignore` and `README.md` and copies your git identity and credential helper. |

`FORCE=1` re-extracts code and assets and rewrites `game.conf`. `NOTES.md` is never touched.

Resolution names: `RESOLUTION_WxH` → W×H, `SUPER_HIGH_RESOLUTION` → 1280×800,
`HIGH_RESOLUTION` → 1024×768 (unreliable; Boxxi Blitz is 800×600, which the largest-PNG check caught), none → 800×600 (unverified).

### make run

Runs `games/<name>/run`, which is `scripts/launch.sh`. The launcher:

1. sets `LD_LIBRARY_PATH=lib:runtime:runtime/pulseaudio`, `LIBGL_DRIVERS_PATH=runtime/dri` and
   `FONTCONFIG_FILE=data/etc/fonts.conf`;
2. writes `pango.modules` and `pangorc` into `$XDG_RUNTIME_DIR/megatouch-<uid>/`, with module
   paths pointing into `data/pango/modules`, and sets `PANGO_RC_FILE`;
3. sets `GLIBC_TUNABLES=glibc.malloc.tcache_count=0` and `MEGA_HOME`;
4. runs `runtime/ld-linux.so.2 megatouch-host`.

`megatouch-host` then reads `game.conf`, installs the crash handler, starts the profiler if
asked, sets the data root, changes to `data/<ASSET_DIR>`, loads `lib/<LIB>` with
`RTLD_NOW | RTLD_GLOBAL` and calls its `main`.

`DEBUG=` presets:

| DEBUG | Sets | Output |
| --- | --- | --- |
| `shots` | `MEGA_SHOT_DIR=games/<name>/notes/shots MEGA_SHOT_EVERY=90` | a PNG every 3 seconds |
| `files` | `MEGA_TRACE_FILES=1` | every path the game touches, on stderr |
| `sound` | `MEGA_DEBUG_SOUND=1` | every play, stop, volume, pause and is-playing |
| `profile` | `MEGA_PROFILE=games/<name>/notes/game.prof MEGA_HITCH_MS=40 MEGA_FRAME_STATS=1` | samples; fps lines and `[hitch]` lines on stderr |

You can also set any variable by hand: `MEGA_WIDTH=1024 MEGA_TRACE_FILES=1 make run GAME=g_trix`.
To keep a log: `make run GAME=g_trix DEBUG=profile 2>&1 | tee games/g_trix/notes/game.log`.

### make snapshot

`scripts/snapshot-cabinet.sh [--all]` copies into `cabinet/{root,ion,var,home}`:

- `/usr/local/lib`, all engine and game code
- system libraries the closures need
- `/usr/local/gamedata`, `/usr/local/games`
- `/var/merit` config, `/home/maxx/.fonts.conf`, the Pango modules and registry
- every game asset folder, including the Unity player and Unity games' `Data/`, except the attract videos (`idle/`)

That includes the Unity games' `Data/`. `--all` also copies the attract videos (the whole games partition, about 6.7 GB). Once `cabinet/`
exists, every script reads from it instead of the image (`scripts/lib/cabinet.sh`), and the
image can be archived.

### make publish

`scripts/publish.sh <game> | --main | --all`, using `repos.conf`:

- **Game:** creates `<GITHUB_ORG>/<game>` if it doesn't exist (visibility from `repos.conf`),
  sets `origin` and pushes. The first time, it registers `games/<game>` as a submodule of the
  main repo.
- **`--main`:** pushes the main repo, creating `<GITHUB_ORG>/<MAIN_REPO>` if needed.
- **`--all`:** every game, then commits the changed submodule pointers ("Update game
  submodules") and pushes the main repo.

It refuses to run with uncommitted changes. The GitHub token comes from `git credential fill`
(your normal credential helper) and is never printed or stored. See
[guides/contributing.md](../guides/contributing.md).

## game.conf

One per game, tracked in the game's repo. `KEY=value` lines, `#` comments. `megatouch-host`
exports each key as `MEGA_<KEY>` **unless that variable is already set**, so the environment
always wins.

| Key | Required | Meaning | Example |
| --- | --- | --- | --- |
| `LIB` | yes | Game library in `lib/` | `g_trix.so` |
| `ASSET_DIR` | yes | Asset folder under `data/`; becomes the working directory | `usr/local/ion_only/games/g_trix` |
| `GAME_ID` | yes | `xml_gameinfo::GameIds` number (selects the resource folder and the high-score file) | `245` |
| `WIDTH`, `HEIGHT` | yes | Logical screen size. Must match the game's art. | `1280`, `800` |
| `LANGUAGE` | no | `GameConfig` language string (default `english`) | `english` |
| `TITLE` | no | Window title | `Megatouch TRIX` |
| `CARD_FANNING` | no | Operator option for card games (default 0; Trix doesn't ship the art) | `1` |
| `HOME`, `DATA` | no | Override the game folder or the data root (normally derived) | — |
| `PRELOAD` | no | Libraries in `lib/` to load before the game, space-separated (the loader had them loaded) | `libmerit_legacy.so` |
| `PLAYERS` | legacy | Player count in `MegacGlobals` (1 or 2) | `1` |
| `ENGINE` | Unity only | `unity`: `run` starts the shared Unity player instead of megatouch-host | `unity` |
| `GAME` | Unity only | The game's folder, written to `launcher.gameid` | `g_tri_towers_2` |

Unity games have no `LIB`/`ASSET_DIR`; see [unity.md](unity.md).

## Cabinet loader

Settings of `make loader-run` (`scripts/loader.sh`) and its stand-ins, given as environment
variables, e.g. `MEGA_LOADER_VIEW=xephyr make loader-run`. Operators rarely need any of them; see
the [operator guide](../guides/operator-guide.md) and the [cabinet-loader guide](../guides/cabinet-loader.md).

### Scripts

| Command | What it does |
| --- | --- |
| `scripts/install.sh [--image P] [--kiosk] [--no-shortcut] [--yes] [--dir D]` | One-command install: packages, clone, image check, setup, shortcut, kiosk autostart |
| `scripts/cabinet.sh` / `stop` / `status` / `autostart on\|off` | Kiosk mode (`make kiosk` / `make kiosk-stop`): fullscreen, restart on exit, crash or hang, login autostart |
| `scripts/loader.sh` | What `make loader-run` runs: start the cabinet |
| `scripts/loader.sh shell` / `run <cmd>` | A shell / a command inside the cabinet's userland (joins a running cabinet's display) |
| `scripts/loader-backup.sh [label]` / `--list` / `--restore FILE` | Snapshot / list / restore `build/loader/var/merit` (`--auto` keeps the newest 20) |
| `scripts/loader-option.sh --list` / `NAME\|INDEX [0\|1]` | Show or set the 120 NVRAM game options (cabinet stopped) |
| `scripts/loader-identity.sh [--new\|--set SERIAL ID\|--apply]` | This cabinet's serial and MegaNet ID: show, new random ones, set, apply (done at every start) |
| `scripts/loader.sh run /opt/fakeio/netcfg [meganet-id ID\|server NAME\|megalink-id ID]` | Show or change network settings through the cabinet's own network library (cabinet stopped) |
| `scripts/loader-key.sh [--force\|--show]` | Make or decode the security-key image `var/merit/fakeio/key.bin` |
| `build/loader/bin/megaio …` | Drive the fake I/O board from a terminal: `coin`, `setup`, `calibrate`, `dip`, `fob`, `status` |

### The sandbox, display and network

| Variable | Default | Meaning |
| --- | --- | --- |
| `MEGA_LOADER_VAR` | `build/loader/var` | Directory used as the cabinet's `/var` (a second, throwaway session: a copy of `build/loader/var.orig`, with its own `MEGA_LOADER_DISPLAY`) |
| `MEGA_LOADER_DISPLAY` | `55` | Nested X display number (Xvfb uses 100 + this) |
| `MEGA_LOADER_VIEW` | `megaview` | `megaview`: scalable window. `xephyr`: Xephyr's own window, always the cabinet's exact resolution |
| `MEGA_LOADER_X` | `xephyr` | `host`: draw on the desktop's X server directly (no resolution changes) |
| `MEGA_LOADER_NET` | `slirp` | `slirp`: own network namespace with a virtual wired `eth0` (NAT, DHCP 10.0.2.15). `host`: the desktop's network (the cabinet then shows none) |
| `MEGA_LOADER_BACKUP` | `auto` | Settings snapshot at start: `auto` (every start, newest 20), `daily` (one a day, newest 14), `none` |
| `MEGA_LOADER_IDENTITY` | on | `off`: don't apply `<var>.identity` (serial for a new board EEPROM, MegaNet ID) at start |
| `MEGA_LOADER_KEY` | make | `none`: no security-key image is made (key-gated options stay locked off) |
| `MEGA_LOADER_BIN` | `/usr/local/bin/start` | Program to start inside the sandbox |
| `MEGA_EXTRA_ENV` | — | `"A=1 B=2"`: extra variables passed into the sandbox (debugging) |

### The window (megaview)

| Variable | Effect |
| --- | --- |
| `MEGAVIEW_FULLSCREEN=1` | Start fullscreen (F11 / Alt+Enter toggle it anyway) |
| `MEGAVIEW_STRETCH=1` | Stretch instead of keeping the cabinet's shape (Ctrl+Alt+S toggles) |
| `MEGAVIEW_SCALE=<n>` | Initial window size: n × the cabinet's resolution (default 2 for 640×480) |
| `MEGAVIEW_HIDE_CURSOR=1` | Hide the mouse pointer (touchscreens) |
| `MEGAVIEW_ALIVE=<file>` | Touch this file every few seconds while the picture changes (kiosk hang detection) |
| `MEGAVIEW_STATS=1` | Per-frame grab / upload / draw times in `build/loader/megaview.log` |
| `MEGAVIEW_DEBUG=1` | Log mouse buttons as sent to the cabinet |
| `MEGAVIEW_NOSHM=1` | Grab with `XGetImage` instead of MIT-SHM |
| `MEGAVIEW_XTST=<path>` | libXtst to use (set by the launcher for the 32-bit fallback build) |

### Kiosk mode (scripts/cabinet.sh)

Environment variables or lines in `cabinet.local.conf`.

| Variable | Default | Meaning |
| --- | --- | --- |
| `KIOSK_HANG_SECS` | `300` | Frozen picture for this long while the main program is busy → restart (`0`: off) |
| `KIOSK_FULLSCREEN` | `1` | `0`: a normal window (F11 still toggles) |
| `KIOSK_HIDE_CURSOR` | `1` | `0`: show the mouse pointer |
| `KIOSK_BACKUP` | `daily` | Passed on as `MEGA_LOADER_BACKUP` |

Keys at the box: **Ctrl+Alt+End** quits and stops the kiosk (megaview exits with status 42); any
other ending is restarted (5 s; 1 min after 3 quick failures, 5 min after 6). Log:
`build/loader/kiosk.log`.

### The fake I/O board and other stand-ins

| Variable | Effect |
| --- | --- |
| `MEGAIO_JOYSTICK=1` | Joystick accessory present: arrow keys, Space / Enter |
| `MEGAIO_OPERATOR_KEY` / `MEGAIO_PLAYER_KEY` | ROM IDs (16 hex digits, family `02`, valid CRC) for F9 / F10. Defaults `024d454741100130` / `02504c4159455205` |
| `MEGAIO_SERIAL` | Cabinet serial number written into a **new** board EEPROM (default `043010MRE60023`) |
| `MEGAIO_KEY_ID` | Security-key ROM ID used by `loader-key.sh` / `loader-option.sh` to encode / decode `key.bin` (default `8c14fc020040000e`) |
| `MEGAIO_NO_KEYS=1` | Don't read F-key hotkeys from the display |
| `MEGAIO_TRACE=1` | Log every I/O board command |
| `MEGAIO_DIR` | Board state directory inside the sandbox (set by the launcher: `/var/merit/fakeio`) |
| `OSSFAKE_TRACE=1` | Log every sound ioctl |
| `OSSFAKE_FMOD_OUTPUT=<n>` | FMOD output for Unity games (default PulseAudio) |
| `MEGA_ZLIB=modern` | Don't route the cabinet's `gz*` calls to its own zlib 1.2.3 |

## Environment variables

The standalone ports' settings. All are read with the `MEGA_` prefix. The old `TRIX_` prefix still works for each one.

### Set by game.conf (see above)
`MEGA_LIB MEGA_ASSET_DIR MEGA_GAME_ID MEGA_WIDTH MEGA_HEIGHT MEGA_LANGUAGE MEGA_TITLE MEGA_CARD_FANNING`

### Paths
| Variable | Default | Meaning |
| --- | --- | --- |
| `MEGA_HOME` | set by `run` | Game folder (`game.conf`, `lib/`, `data/`). Needed because `/proc/self/exe` is `ld-linux.so.2`. |
| `MEGA_DATA` | `$MEGA_HOME/data` | Root that `/usr/local/{games,gamedata,ion_only}`, `/var/merit` and `/dev/merit_ipc` map to |
| `MEGA_PLAYER_NAME` | `$USER` | Name saved with a new best score |

### Diagnostics
| Variable | Effect |
| --- | --- |
| `MEGA_TRACE_FILES=1` | `[file] <path>` for every path through the shim; `[glob] pattern -> n` for globs |
| `MEGA_DEBUG_TOUCH=1` | Legacy games: log touch zones, touches and when the game reads them |
| `MEGA_TRACE_IPC=1` | Log every connection attempt to the cabinet loader's `/dev/merit_ipc/` sockets. Without it, each endpoint is logged once as `[ipc] connect <path> -> <error>`. |
| `MEGA_DEBUG_SOUND=1` | Timestamped sound calls: play (file, loop, volume → voice id and sample count), stop, volume, pause, is-playing |
| `MEGA_SHOT_DIR=<dir>` | Save `frameNNNNN.png` there… |
| `MEGA_SHOT_EVERY=<n>` | …every n rendered frames (default 60 = 2 s) |
| `MEGA_AUTOCLICK="t:x,y;t:x,y"` | Tap at logical (x,y) on update tick t (30 ticks per second). Scripted tests. |
| `MEGA_FRAME_STATS=1` | Once per second: fps, worst gap, late frames, render and present ms |
| `MEGA_HITCH_MS=<n>` | Log frames slower than n ms (default 50 when profiling) with what happened in them: texture uploads, image loads, sound decodes |
| `MEGA_PROFILE=<file>` | Sampling profiler of the main thread → file; read with `tools/profreport.py` |
| `MEGA_PROFILE_US=<µs>` | Sample interval (default 2000) |

### Rendering
| Variable | Effect |
| --- | --- |
| `MEGA_RENDERER=software\|opengl\|opengles2` | SDL render driver by name (default: SDL's choice, normally OpenGL through Mesa llvmpipe) |
| `MEGA_VSYNC=0` | Disable vsync (default on) |
| `MEGA_FMOD_OUTPUT=<n>` | Unity games: FMOD output (default 13 PulseAudio; 0 auto, 10 OSS, 11 ALSA, 12 ESD) |
| `MEGA_FPS=<n>` | Override the 30 updates/frames per second (15–240). **Breaks game timing**: the AI and animations count ticks. |

### Useful non-MEGA variables
| Variable | Effect |
| --- | --- |
| `SDL_VIDEODRIVER=offscreen` | No window (headless; combine with `MEGA_SHOT_DIR`) |
| `SDL_AUDIODRIVER=dummy` | No sound device (silent, which is easy to mistake for an audio bug) |
| `LD_DEBUG=bindings` / `LD_DEBUG=libs` | Which library each symbol binds to / how libraries are found |
| `GLIBC_TUNABLES` | Set by `run`; don't drop `tcache_count=0` (see [known bugs](known-bugs.md)) |

## Engine debug flags

The engine's `Debug_Flag_Set(category, flag)` is true when the file
`/var/merit/debug/<category>/<flag>` exists. In a port that is
`games/<name>/data/var/merit/debug/<category>/<flag>`:

```bash
mkdir -p games/g_trix/data/var/merit/debug/files
touch    games/g_trix/data/var/merit/debug/files/resource_locator
```

Flags found by scanning the libraries' calls (Trix and Word Dojo 2 closures):

| Flag file | Library | Effect |
| --- | --- | --- |
| `files/resource_locator` | libresources | Prints every candidate path the resource locator tries, and what it found |
| `translations/untranslated` | libtranslate | Reports strings with no translation |
| `logging/locale` | liblocale | Locale manager logging |
| `logging/sqlite3`, `logging/db_state` | libgendef_db | Database logging |
| `LibData/TrackQuery` | libgendef_db | Query tracing |
| `games/short_game` | g_trix | Shorter games (developer option) |
| `trix/auto_play` | g_trix | The computer plays your hand |
| `word_dojo_2/accept_any_word` | g_word_dojo_2 | Every word is valid |
| `word_dojo_2/accept_any_letter` | g_word_dojo_2 | Any letter may be played |
| `word_dojo_2/all_powerups` | g_word_dojo_2 | Start with every power-up |
| `word_dojo_2/auto_bonus` | g_word_dojo_2 | Go straight to the bonus round |
| `word_dojo_2/generate_alphabet` | g_word_dojo_2 | Developer tool for the letter tables |
| `word_dojo_2/keyboard` | g_word_dojo_2 | Keyboard input |

The game flags are named from their strings and have not all been tried. To find flags in
another game, disassemble the calls to `Debug_Flag_Set` (the two string arguments are loaded just
before each call):

```bash
objdump -d games/<name>/lib/<lib>.so | grep -B8 'call.*Debug_Flag_Set' | grep -E 'lea|call'
```

## Tools

| Tool | Usage | What for |
| --- | --- | --- |
| `tools/analyze.sh` | `tools/analyze.sh games/<name>` | Symbols nothing provides → `notes/unresolved.txt` |
| `tools/decompile.sh` | `tools/decompile.sh games/<name> [extra.so…]` | Ghidra headless → `decomp/<lib>.c`. Ghidra loads at 0x10000; subtract that from addresses. |
| `tools/package.sh` | `tools/package.sh games/<name> <dest>` | Standalone copy |
| `tools/catalog.sh` | `tools/catalog.sh` | Writes `docs/reference/games.md` |
| `tools/survey.sh` | `tools/survey.sh` | Writes `docs/reference/gamedevice-survey.md` |
| `tools/gameinfo.py` | `gameinfo.py <name> <gamedata.xml>…` | Prints `GAMEID_NAME DLL DIR DESC RES` as shell assignments |
| `tools/largest-png.py` | `largest-png.py <dir>` | Largest PNG, `WxH (path)`: the likely screen size |
| `tools/vtdump.py` | `toolchain/venv/bin/python tools/vtdump.py <lib.so> [filter]` | Every vtable with resolved slots; marks `__cxa_pure_virtual`. Printed index − 2 = slot. |
| `tools/sprdump.py` | `sprdump.py file.spr.gz outprefix [frames] [--mode 0\|1]` | `.spr` frames → PNG |
| `tools/profreport.py` | `profreport.py game.prof [game.log] [--top N]` | Profile summary by library and function, overall and within hitches |
| `shared/bin/gameids` | `LD_LIBRARY_PATH=shared/engine-sdk:shared/runtime shared/runtime/ld-linux.so.2 shared/bin/gameids [NAME]` | The engine's own GameId table, all of it or one name → number |
| `scripts/lib/extract-libs.sh` | `extract-libs.sh <outdir> <lib.so>…` | Library plus NEEDED closure from the cabinet |
| `scripts/lib/cabinet.sh` | `. cabinet.conf; . scripts/lib/cabinet.sh` | `cab_dump root /etc/fstab out`, `cab_ls ion /games`, `cab_rdump`, `cab_readlink`, `cab_exists`. Works on the image or the snapshot. |

Reading the image by hand (no root):

```bash
. ./cabinet.conf
debugfs -R "ls -l /usr/local/lib"         "$IMG?offset=$ROOT_OFF"
debugfs -R "dump /etc/fstab /tmp/fstab"    "$IMG?offset=$ROOT_OFF"
debugfs -R "rdump /games/g_trix /tmp"      "$IMG?offset=$ION_OFF"
debugfs -R "cat /merit/settings.xml"       "$IMG?offset=$VAR_OFF"
```

Binary inspection that comes up all the time:

```bash
readelf -d lib.so | grep NEEDED                       # dependencies
nm -DC --defined-only lib.so | grep Class             # what it exports
nm -D --undefined-only lib.so                         # what it imports
objdump -d -R -C -M intel --no-show-raw-insn lib.so   # disassembly with call targets
nm -S -C lib.so | grep 'vtable for'                   # vtable sizes (bytes/4 - 2 = slots)
c++filt _ZN8Graphics11BaseTextureC2Ev                 # demangle
```

## Configuration files

| File | Tracked | Contents |
| --- | --- | --- |
| `cabinet.conf` | yes | Image partition offsets, snapshot location. Sources `cabinet.local.conf` last. |
| `cabinet.local.conf` | **no** | Your `IMG="…"` path (and any overrides) |
| `repos.conf` | yes | `GITHUB_ORG`, `MAIN_REPO`, `VISIBILITY` |
| `scripts/packages/multilib.txt` | yes | amd64 multilib packages for the 32-bit compiler |
| `scripts/packages/i386.txt` | yes | i386 runtime packages |
| `games/<name>/game.conf` | game repo | See above |
| `.gitmodules` | yes | Game submodules |
