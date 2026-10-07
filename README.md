# megatouch-port

The Megatouch ION (2014) cabinet games, running on Linux and WSL2 using the cabinet's real game
and engine binaries. The goal is **every game on the platform**, and in the end **the cabinet's
loader itself**. Today the cabinet backend for the 2009+ "GameDevice" engine is replaced by SDL2,
and the cabinet's Unity player runs on the same runtime. A reconstruction of the loader's own 2D engine runs the first pre-2009 game. Five games are playable so far.

| | Games | Status |
| --- | ---: | --- |
| GameDevice engine | 22 | **Trix, Word Dojo 2, Boxxi Blitz playable**; the rest need a few stand-ins each ([survey](docs/reference/gamedevice-survey.md)) |
| Unity 3.2 | 30 | **Tri Towers 2 playable**; the cabinet's own Unity player runs them ([unity](docs/reference/unity.md)) |
| Merit3D | 13 | needs the loader's 3D services |
| Legacy sprite engine | 143 | **Fourplay playable** on a reconstruction of the loader's 2D engine ([legacy](docs/reference/legacy.md)) |

Every title: [docs/reference/games.md](docs/reference/games.md).

## Quick start

```bash
git clone --recurse-submodules https://github.com/Merit-Megatouch/megatouch-port ~/megatouch-port
cd ~/megatouch-port
echo 'IMG="/path/to/Megatouch ION 2014 HDD Keyless.img"' > cabinet.local.conf
make setup                       # once: toolchain, 32-bit runtime, shared cabinet data (~3 min)
make games                       # extract each game's code + assets from the cabinet
make run GAME=g_trix             # play   (F11 = fullscreen)
```

You need Ubuntu on Linux or WSL2 (with WSLg) and
`sudo apt install git gcc g++ make python3 python3-venv curl e2fsprogs binutils`. Nothing else is
installed system-wide. Step-by-step instructions from a bare Windows machine:
[docs/guides/quick-start.md](docs/guides/quick-start.md).

You also need the cabinet disk image, or a snapshot made from it with `make snapshot`. It is only
used to extract files; ported games never read it. Cabinet code and assets are never committed.

## Documentation

| | |
| --- | --- |
| [Quick start](docs/guides/quick-start.md) | From nothing to playing |
| [Porting a game](docs/guides/porting-a-game.md) | The workflow, worked through on Word Dojo 2 |
| [Cabinet loader](docs/guides/cabinet-loader.md) | Run the cabinet's own, unmodified loader: menus, Operator Setup, coins, keys, every game |
| [I/O board](docs/reference/io-board.md) | The USB I/O board, security key and operator key, as read from the image |
| [Debugging](docs/guides/debugging.md) · [Troubleshooting](docs/guides/troubleshooting.md) | When something goes wrong |
| [Contributing](docs/guides/contributing.md) | Repos, submodules, publishing |
| [Roadmap](docs/roadmap.md) | Every family, and the loader |
| [Reference](docs/README.md#reference-look-things-up) | Commands and settings, architecture, engine ABI, loader services, cabinet layout, file formats, known bugs |
| [History](docs/history/README.md) | How the first port was done, chapter by chapter |

## How it works

On the cabinet, a protected program called the loader `dlopen`s each game library and gives it
graphics, sound and input through `libgame_device_sprite.so`. GameDevice games reach that backend
through one function, `CreateNewGameDevice()`, and after that only through virtual calls on
engine base classes. We ship our own `libgame_device_sprite.so` on SDL2. It builds engine objects
with their own constructors and points them at copies of their vtables with our functions in the
slots. Our host program, `megatouch-host`, stands in for the dozen loader functions the games
still call, and redirects the cabinet's file paths into a per-game `data/` folder.
[Architecture](docs/reference/architecture.md).

The cabinet's own loader also runs, unmodified, in a sandbox built from the image
(`make loader-setup`, `make loader-run`). Stand-ins replace the hardware it expects: a fake USB
I/O board (coins, buttons, operator and player keys on F-keys), a fake touchscreen controller,
OSS sound through PulseAudio, a virtual wired network (MegaNet and TournaMAXX work), a
security-key image of the cabinet's own licence, and a nested X server that can change resolution.
[Cabinet loader](docs/guides/cabinet-loader.md).

## Commands

```
make setup                       toolchain + runtime + shared cabinet data (once)
make                             rebuild host + backend
make new GAME=<dll> [FORCE=1]    scaffold a game from the cabinet into games/<dll>
make run GAME=<dll> [DEBUG=shots|files|sound|profile]
make analyze GAME=<dll>          loader symbols still missing
make decompile GAME=<dll>        Ghidra C of the game (needs scripts/setup.sh --ghidra)
make package GAME=<dll> DEST=<dir>   standalone copy
make snapshot                    copy what porting needs out of the image
make games                       regenerate every game's lib/ + data/ (after a clone)
make publish GAME=<dll>|all      push to GitHub; games are submodules
make docs | make survey          regenerate the game catalogue | the porting survey
make loader-setup                the cabinet's own loader: extract its partitions, fetch Xephyr (once)
make loader-run                  run it (F1 setup, F5-F8 coins, F9 operator key, F10 player key)
make loader-reset                put its /var (settings, NVRAM, books) back as on the image (backed up first)
make loader-backup               snapshot its settings (every run also keeps one)
scripts/loader-option.sh --list  its game options (TournaMAXX, free play, …)
```

All options and environment variables: [docs/reference/commands.md](docs/reference/commands.md).

## Layout

| Path | In git | What |
| --- | --- | --- |
| `Makefile`, `cabinet.conf`, `repos.conf` | yes | Commands; image settings (your path goes in `cabinet.local.conf`, ignored); GitHub org |
| `scripts/` | yes | `setup`, `new-game`, `publish`, `snapshot-cabinet`, `launch`, `loader`, `loader-setup`; `lib/` helpers; `packages/` lists |
| `src/backend/` | yes | SDL2 backend: `device` `textures` `sprites` `input` `sound` `net` `spr` |
| `src/host/` | yes | `megatouch-host`: `main`, `fs_shim`, `loader_services`, `profiler` |
| `src/legacy/` | yes | `libmerit_legacy.so`: the loader's legacy 2D engine, reconstructed |
| `src/unity/` | yes | `libmega_unity.so`, preloaded into the Unity player (FMOD → PulseAudio) |
| `src/fakeio/` | yes | Stand-ins for running the cabinet's loader: fake I/O board (`libusb-1.0.so.0`, `megaio`), touchscreen driver, OSS sound, `ossmix`, startup/crash preloads, test tools |
| `tools/` | yes | `analyze` `decompile` `package` `catalog` `survey` `vtdump` `sprdump` `profreport` `gameinfo` `largest-png` `gameids` |
| `docs/` | yes | `guides/`, `reference/`, `history/`, `roadmap.md`, `data/` |
| `games/<dll>/` | own repo | `Merit-Megatouch/<dll>` as a submodule: `game.conf`, `NOTES.md`, `notes/`; `lib/` + `data/` regenerated |
| `shared/`, `toolchain/`, `build/` | no | Generated by `make setup` / `make` |
| `reference/`, `cabinet/` | no | Decompiled engine (local); optional image snapshot |

## Games

| Game | Repo | GameId | State |
| --- | --- | ---: | --- |
| Trix | [g_trix](https://github.com/Merit-Megatouch/g_trix) | 245 | Playable |
| Word Dojo 2 | [g_word_dojo_2](https://github.com/Merit-Megatouch/g_word_dojo_2) | 258 | Playable |
| Boxxi Blitz | [g_boxxi_template](https://github.com/Merit-Megatouch/g_boxxi_template) | 257 | Playable |
| Tri Towers 2 (Unity) | [g_tri_towers_2](https://github.com/Merit-Megatouch/g_tri_towers_2) | 286 | Playable |
| Fourplay (legacy) | [fourplay](https://github.com/Merit-Megatouch/fourplay) | 7 | Playable |
