# Porting a game

A step-by-step guide for GameDevice games. **Unity games** are simpler: `make new GAME=<folder>`,
`make run`, then fill in missing launcher settings; see [reference/unity.md](../reference/unity.md). Word Dojo 2 is the worked example: it went from `make new` to fully playable in one
session. For other engine families see [the roadmap](../roadmap.md).

## 0. Pick a game

- [reference/gamedevice-survey.md](../reference/gamedevice-survey.md) lists every GameDevice
  game, sorted by how many loader symbols are still missing. Start at the top.
- [reference/games.md](../reference/games.md) lists every game on the cabinet with its family.

Check the family yourself if you need to:

```bash
. ./cabinet.conf; . scripts/lib/cabinet.sh
cab_dump root /usr/local/lib/<dll>.so /tmp/x.so && readelf -d /tmp/x.so | grep NEEDED
```

`libgame_device_sprite.so` among them means it's GameDevice.

## 1. Scaffold

```bash
make new GAME=g_word_dojo_2
```

Read what it found:

```bash
cat games/g_word_dojo_2/notes/scaffold.md      # GameId, family, assets, window size, extra libs
cat games/g_word_dojo_2/notes/unresolved.txt   # what's missing
```

For Word Dojo 2:

| Fact | Value |
| --- | --- |
| GameId | G_WORD_DOJO_2 = 258 |
| Window | 1280×800 (declared `SUPER_HIGH_RESOLUTION`, confirmed by the largest PNG) |
| Extra engine libraries | `libbrush.so` |
| Unresolved | 8 symbols |

**Check the window size.** If the largest PNG (a full-screen background) disagrees with
`game.conf`, trust the PNG. A wrong size shows up as art cut off or sitting in a corner.

## 2. Make it load: stand-ins

`megatouch-host` loads the game with `RTLD_NOW`, so every symbol in `unresolved.txt` must exist
before anything runs. Word Dojo 2's eight:

```
_ZN17HighScoresManager12HighestScoreEi       HighScoresManager::HighestScore(int)
_ZN17HighScoresManager11HighestNameEi        HighScoresManager::HighestName(int)
_ZN6Locale15LanguageManager11GetInstanceEv   Locale::LanguageManager::GetInstance()
_ZNK6Locale15LanguageManager6ActiveEv        Locale::LanguageManager::Active() const
ugetat  ustrcmp  ustrlen  ustrupr            Allegro 4 UTF-8 helpers
```

For each one:

1. **Find the caller** and see how it's called:
   ```bash
   objdump -d -C -M intel games/g_word_dojo_2/lib/g_word_dojo_2.so | grep -B15 'call.*LanguageManager6Active' | less
   ```
   Count the pushed arguments: a member function gets `this` first.
2. **Find out what the result is used for.** `make decompile GAME=g_word_dojo_2` (needs
   `scripts/setup.sh --ghidra`) and search `decomp/g_word_dojo_2.c`. `Active()` turned out to
   pick the dictionary folder `scripts/dictionaries/<language>/`, with 0 = English.
3. **Write the stand-in** in `src/host/loader_services.cpp`, with a comment saying what it is.
   Look at the existing ones first; [reference/loader-services.md](../reference/loader-services.md)
   covers them all.
4. Rebuild and re-check:
   ```bash
   make && make analyze GAME=g_word_dojo_2      # until: 0 unresolved
   ```

A **missing library** instead of a missing symbol (Word Dojo 2:
`libinput_sprite.so: cannot open shared object file`) means the game lists an old backend
library it doesn't use. `new-game.sh` now links empty stubs for all three automatically.

## 3. First run

```bash
make run GAME=g_word_dojo_2 DEBUG=shots
```

Screenshots go to `games/g_word_dojo_2/notes/shots/` every 3 seconds, so you can see what
happened even when the window closes fast.

| You see | Look at |
| --- | --- |
| Crash trace | The top frames name the engine method. [Debugging guide](debugging.md#reading-a-crash-trace). |
| "pure virtual method called" | A backend class is missing a slot ([engine ABI](../reference/engine-abi.md)) |
| Black screen, no crash | `DEBUG=files`: are layouts found? Is the window size right? |
| Missing images | `DEBUG=files` plus the `files/resource_locator` flag; check the formats used (`find data -name '*.tga'`) |
| Boxes instead of text | Fonts or Pango (shared, should already work); check `data/etc/fonts.conf` exists |
| Key names instead of text (`HELP_TEXT`) | Translation table missing: `gamedata/translations/<dll>.utf8` |
| Silence | `DEBUG=sound`. Make sure `SDL_AUDIODRIVER` isn't `dummy`. |

Word Dojo 2's first run showed instructions, the board, the timer and music right away. Two
things were missing and were fixed in shared code:

- **TGA images:** `IMG_Load_RW` can't detect TGA from content, so the backend now passes the type
  from the extension.
- **Game-over screen:** the art lives in the cabinet's fallback game folder
  `/usr/local/games/default`. That folder is now part of shared data and linked into every game.

## 4. Play it through

Play a full game by hand, then once more with the profiler:

```bash
make run GAME=g_word_dojo_2 DEBUG=profile 2>&1 | tee games/g_word_dojo_2/notes/game.log
python3 tools/profreport.py games/g_word_dojo_2/notes/game.prof games/g_word_dojo_2/notes/game.log
```

The profiler catches stalls, and a full game catches engine bugs that only show after a while
(the `StopSound` use-after-free in Trix appeared mid-game). Check, at least:

- [ ] start from attract mode by clicking
- [ ] a full round: input, scoring, timers
- [ ] the game-over screen
- [ ] the hi-score panel (`data/var/merit/highscores/<id>.txt` gets written)
- [ ] the help screen
- [ ] music and sound effects
- [ ] bonus rounds and special modes (engine [debug flags](../reference/commands.md#engine-debug-flags) can jump there)

## 5. Record and commit

Keep `games/<name>/NOTES.md` up to date: set the status line, tick the checklist, and add a
dated log entry for anything you learned. `catalog.py` reads the status line for
[games.md](../reference/games.md).

```bash
git -C games/g_word_dojo_2 add -A && git -C games/g_word_dojo_2 commit -m "Playable"
git add src docs && git commit -m "Stand-ins for Word Dojo 2"
make publish GAME=g_word_dojo_2        # first time: creates the GitHub repo and the submodule
make publish GAME=all                  # later: push every game, then the main repo
make docs                              # refresh the catalogue
```

Details: [contributing](contributing.md).

## Rules of thumb

- **Shared first.** A fix in `src/` helps every game. Put game-specific behaviour behind a
  `game.conf` key and keep the default right for the other games.
- **Never commit cabinet files.** `lib/` and `data/` are ignored for a reason.
- **One stand-in at a time**, each with a comment saying which game needed it and why it returns
  what it returns.
- **Trust the engine.** If something looks wrong, the engine is usually right and our side
  (backend slot, stand-in result, path) is wrong. Compare with the decompiled original backend
  (`reference/` or `make decompile`).
- **Write down dead ends** in `NOTES.md` too.

## Doing it by hand

What `make new` automates, for when a game doesn't fit the template:

1. `scripts/lib/extract-libs.sh games/<dll>/lib <dll>.so`, then remove the original
   `*_sprite`, `libmerit2d`, `libmerit3d`, `libmeritbasegame`, `libmerit_threads`; link
   `shared/bin/libgame_device_sprite.so` and the stubs.
2. Copy assets to `games/<dll>/data/usr/local/ion_only/games/<dir>/` (or wherever the locator
   trace says it looks); link `gamedata`, `games/default`, `etc`, `pango` from
   `shared/data-common`; copy `var-template/merit` to `data/var/merit`.
3. Link `run`, `runtime` and `megatouch-host`, and write `game.conf`.
4. `tools/analyze.sh games/<dll>`.

Games with assets outside `/games/<dir>` (some keep them under `media/` or in
`/usr/local/gamedata`) may need `ASSET_DIR` fixed by hand.
