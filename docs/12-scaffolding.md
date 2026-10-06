# 12 — The workspace and scaffolding a new game

Everything is driven from the top-level `Makefile`:

```
make setup                     toolchain + shared runtime + cabinet data + build   (scripts/setup.sh)
make                           rebuild megatouch-host + SDL2 backend → shared/bin
make new GAME=<name> [FORCE=1] scaffold games/<name> from the cabinet           (scripts/new-game.sh)
make run GAME=<name> [DEBUG=shots|files|sound|profile]
make analyze GAME=<name>       unresolved loader symbols → games/<name>/notes/unresolved.txt
make decompile GAME=<name>     Ghidra C of the game → games/<name>/decomp   (needs setup.sh --ghidra)
make package GAME=<name> DEST=<dir>   self-contained copy (symlinks resolved)
make snapshot                  copy what porting needs out of the image → cabinet/
make games                     regenerate lib/ + data/ for every game (after a fresh clone)
make publish GAME=<name>|all   push to GitHub (repos.conf); games become submodules
```

## Repositories

The main repo (`Merit-Megatouch/megatouch-port`) holds the shared code, scripts and docs. Each
game is its own repo (`Merit-Megatouch/<name>`) mounted at `games/<name>` as a git submodule:
its `game.conf`, `NOTES.md`, `README.md` and `notes/` are tracked there, its cabinet-derived
`lib/` and `data/` are ignored. `make new` initialises the game repo; `make publish GAME=<name>`
creates it on GitHub (visibility from `repos.conf`), pushes it, and adds the submodule the first
time. Commit game work inside `games/<name>`, then commit the updated submodule pointer in the
main repo (`make publish GAME=all` does both and pushes everything).

## Layout

```
~/trix-port/                       git repo
├── Makefile  README.md  cabinet.conf  repos.conf  .gitignore  (cabinet.local.conf: yours, ignored)
├── scripts/
│   ├── setup.sh                   idempotent; steps marked done with .done files
│   ├── new-game.sh                scaffolder
│   ├── publish.sh                 create/push GitHub repos, register game submodules
│   ├── snapshot-cabinet.sh        image → cabinet/
│   ├── launch.sh                  generic launcher; each game's `run` links here
│   ├── lib/cabinet.sh             read cabinet files from the image (debugfs) or the snapshot
│   ├── lib/extract-libs.sh        library + dependency closure from the cabinet
│   ├── lib/game-repo.sh           make games/<name> its own git repo
│   ├── lib/github.sh              GitHub API via your git credential helper
│   └── packages/{i386,multilib}.txt
├── src/
│   ├── common/env.h               MEGA_* / TRIX_* settings lookup
│   ├── backend/                   libgame_device_sprite.so: engine.cpp (vtable cloning), device,
│   │                              textures, sprites, input, sound, net, spr, merit_abi.h, backend.h
│   ├── host/                      megatouch-host: main, fs_shim, loader_services, profiler
│   └── third_party/stb_vorbis.c
├── tools/                         analysis helpers (see README)
├── docs/
├── games/<name>/                  separate repo (submodule): game.conf NOTES.md notes/ · lib/ data/ run … (generated)
├── shared/        (generated)     runtime/ bin/ engine-sdk/ data-common/ cabinet-libs/
├── toolchain/     (generated)     sysroot/ i386/ apt-i386/ cache/ venv/ ghidra/ g++32
├── reference/     (local only)    decompiled engine libraries
└── cabinet/       (optional)      snapshot of the cabinet image
```

## A game folder

```
games/<dll>/
├── game.conf          LIB ASSET_DIR GAME_ID WIDTH HEIGHT LANGUAGE TITLE [CARD_FANNING]
├── NOTES.md           yours: status, checklist, dated log (created once, never overwritten)
├── notes/scaffold.md  facts found by new-game.sh (regenerated)
├── notes/unresolved.txt, missing-libs.txt
├── run → scripts/launch.sh     megatouch-host → shared/bin     runtime → shared/runtime
├── lib/               game .so + engine closure; libgame_device_sprite.so → shared/bin
└── data/              usr/local/ion_only/games/<dir>/ (assets) · gamedata, etc, pango → shared ·
                       var/merit/ (private, writable)
```

Every `game.conf` key is exported as `MEGA_<KEY>`; the environment wins
(`MEGA_WIDTH=1024 make run GAME=...`).

## What `make new` does

1. Finds the game in `gamedata.xml` (master list, else the game's own copy). `<name>` can be a
   DLLName, asset folder or GameId.
2. Extracts `/usr/local/lib/<dll>.so` + closure, drops what the shared runtime supplies.
3. Detects the engine family; for GameDevice games swaps in the SDL2 backend.
4. Extracts assets, links shared data, runtime, host and launcher.
5. Writes `game.conf`: GameId via `shared/bin/gameids` (the engine's `libenums.so`); window size
   from the declared resolution, overridden by the largest PNG when that is a screen size.
6. `tools/analyze.sh` → `notes/unresolved.txt`; writes `notes/scaffold.md`, and `NOTES.md` if missing.

## Working loop

```bash
make new GAME=g_foo
cat games/g_foo/notes/scaffold.md games/g_foo/notes/unresolved.txt
$EDITOR src/host/loader_services.cpp          # stand-ins for unresolved symbols
make && make analyze GAME=g_foo                # until 0 unresolved
make run GAME=g_foo DEBUG=shots                # screenshots → games/g_foo/notes/shots
make run GAME=g_foo DEBUG=files                # every path the game opens
git add src games/g_foo && git commit          # code + that game's notes together
```

`src/` is shared: a stand-in or vtable fix for one game helps the rest. Keep game-specific
behaviour behind `game.conf` keys.

## Example: Word Dojo 2 (scaffolded)

GameId 258, 1280×800, one extra engine library (`libbrush.so`), 8 unresolved symbols:
`HighScoresManager::HighestName/HighestScore`, `Locale::LanguageManager::GetInstance/Active`,
and Allegro's Unicode helpers `ugetat`, `ustrcmp`, `ustrlen`, `ustrupr`.

## Is the disk image needed?

Only to run `make setup` and `make new`. A scaffolded game never reads it. `make snapshot`
copies the porting-relevant parts (engine + all game libraries, system libraries, cabinet data,
game assets except attract videos and Unity games; `--all` for the whole games partition) into
`cabinet/`; `scripts/lib/cabinet.sh` then reads from there and the image can be archived.

## Known limits
* `HIGH_RESOLUTION` → 1024×768 and the no-declaration default 800×600 are unverified guesses;
  the largest-PNG check usually corrects them.
* Games with assets outside `/games/<dir>` may need `ASSET_DIR` fixed by hand.
* Only the GameDevice family runs on the shared backend today.
* `setup.sh` targets Ubuntu (i386 packages from archive.ubuntu.com for the host's release).
