# megatouch-port

The Megatouch ION (2014) cabinet games, running on Linux and WSL2 using the cabinet's real game
and engine binaries. The goal is **every game on the platform**, and in the end **the cabinet's
loader itself**. Today the cabinet backend for the 2009+ "GameDevice" engine is replaced by SDL2,
and two games are fully playable.

| | Games | Status |
| --- | ---: | --- |
| GameDevice engine | 22 | **Trix, Word Dojo 2 playable**; the rest need a few stand-ins each ([survey](docs/reference/gamedevice-survey.md)) |
| Unity 3.2 | 30 | next family ([roadmap](docs/roadmap.md)) |
| Merit3D | 13 | needs the loader's 3D services |
| Legacy sprite engine | 143 | needs the loader's 2D engine, the big milestone |

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
```

All options and environment variables: [docs/reference/commands.md](docs/reference/commands.md).

## Layout

| Path | In git | What |
| --- | --- | --- |
| `Makefile`, `cabinet.conf`, `repos.conf` | yes | Commands; image settings (your path goes in `cabinet.local.conf`, ignored); GitHub org |
| `scripts/` | yes | `setup`, `new-game`, `publish`, `snapshot-cabinet`, `launch`; `lib/` helpers; `packages/` lists |
| `src/backend/` | yes | SDL2 backend: `device` `textures` `sprites` `input` `sound` `net` `spr` |
| `src/host/` | yes | `megatouch-host`: `main`, `fs_shim`, `loader_services`, `profiler` |
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
