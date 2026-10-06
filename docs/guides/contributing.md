# Contributing: repos, commits, publishing

## How the repositories fit together

```
Merit-Megatouch/megatouch-port        main repo: src/, scripts/, tools/, docs/, Makefile
├── games/g_trix         → Merit-Megatouch/g_trix          (submodule)
├── games/g_word_dojo_2  → Merit-Megatouch/g_word_dojo_2   (submodule)
└── games/<next>         → Merit-Megatouch/<next>          (added by make publish)
```

- The **main repo** holds everything shared: the backend, host, scripts, tools, docs and, later,
  the loader. It records *which commit* of each game goes with its own code (that is what a
  submodule is).
- Each **game repo** holds only what is specific to that game: `game.conf`, `NOTES.md`,
  `README.md`, `notes/scaffold.md`, `notes/unresolved.txt`. Its `lib/`, `data/` and links are
  ignored and regenerated from the cabinet.
- **Never commit cabinet files**: binaries, art, sound, translations, the image. They are
  copyrighted, large, and reproducible with `make setup` and `make new`. The `.gitignore` files
  enforce this; don't force-add anything under `lib/`, `data/`, `shared/`, `toolchain/`,
  `reference/` or `cabinet/`.

## Everyday commands

```bash
# get everything, or update
git clone --recurse-submodules https://github.com/Merit-Megatouch/megatouch-port
git pull && git submodule update --init

# work on a game
git -C games/g_trix switch main                 # submodules check out detached; get on a branch
$EDITOR games/g_trix/NOTES.md
git -C games/g_trix commit -am "Notes: profiled the bonus round"

# work on shared code
$EDITOR src/host/loader_services.cpp
make && make run GAME=g_trix                    # re-test the games you might affect
git commit -am "loader: HighEnough(GameIds,…) overload"

# push
make publish GAME=g_trix                        # one game
make publish GAME=all                           # every game, then the main repo (commits pointer bumps)
```

## Starting a new game repo

`make new GAME=<dll>` runs `git init` in `games/<dll>` and writes `.gitignore` and `README.md`.
It copies `user.name`, `user.email` and `credential.helper` from the main repo. Commit there,
then:

```bash
make publish GAME=<dll>
```

The first publish creates `Merit-Megatouch/<dll>` (public, from `repos.conf`), pushes it, and adds
it to the main repo as a submodule, replacing the plain folder. Commit that in the main repo
(`make publish GAME=all` does it for you).

## Credentials

`scripts/lib/github.sh` gets a token from your git credential helper (`git credential fill` for
github.com) to call the GitHub API, which it uses only to check whether a repo exists and to create it. The token is never
printed or written anywhere. On WSL, point git at Windows' credential manager so you sign in
once in a browser:

```bash
git config --global credential.helper "/mnt/c/Program\ Files/Git/mingw64/bin/git-credential-manager.exe"
```

## Commit style

- Short imperative subject saying what changed: `Word Dojo 2 runs: loader stand-ins …`,
  `new-game: stable scaffold.md`.
- Prefix with the area when it helps: `loader:`, `backend:`, `new-game:`, `docs:`, `tools:`.
- Put shared code and the docs that describe it in the same commit.
- In game repos, the log in `NOTES.md` is the main record. Commit messages can be short.

## What to update when you change things

| You changed | Also update |
| --- | --- |
| A loader stand-in | [reference/loader-services.md](../reference/loader-services.md); rerun `make survey` |
| A backend slot or object | [reference/engine-abi.md](../reference/engine-abi.md) |
| A `MEGA_*` variable, `game.conf` key or make target | [reference/commands.md](../reference/commands.md); `make help` text in the Makefile |
| Fixed a bug | [reference/known-bugs.md](../reference/known-bugs.md) |
| A game's status | Its `NOTES.md` status line; `make docs`; the Games table in the root README |
| Finished a roadmap item | [roadmap.md](../roadmap.md) |

## Testing before you push

There is no automated test suite yet. Before you push shared changes:

1. `make` builds without warnings that matter.
2. `make analyze GAME=<each ported game>` still says 0.
3. Each ported game starts and reaches gameplay. Headless check:
   ```bash
   SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy MEGA_SHOT_DIR=/tmp/t MEGA_SHOT_EVERY=150 \
     MEGA_AUTOCLICK="90:640,400" timeout 20 games/g_trix/run; ls /tmp/t
   ```
   Then look at the screenshots.
4. For anything touching sound, timing or memory, play one full game.
