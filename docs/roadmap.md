# Roadmap: the whole platform

This repository aims to run **every game on the Megatouch ION cabinet** off the cabinet, and in the
end to replace **the loader itself**, the protected program that runs the menu, credits,
settings and the 2D engine that most games are built on.

Each game is its own repo under `games/`. Everything shared (backends, host, the eventual loader,
tools, docs) lives here. A fix made for one game helps every game of its family.

## Where things stand

| Family | Games | Status | What porting takes |
| --- | ---: | --- | --- |
| GameDevice (2009+) | 22 | **3 playable** (Trix, Word Dojo 2, Boxxi Blitz); 1 more with 0 missing symbols | Shared SDL2 backend (done). A few loader stand-ins per game: see the [survey](reference/gamedevice-survey.md). |
| Unity 3.2 | 30 | not started | Run the cabinet's `LinuxPlayer`; replace the cabinet plugins in .NET |
| Merit3D | 13 | not started | The loader's 3D services: ODE, OpenGL, Allegro |
| Legacy sprite | 143 | not started | **The loader's 2D engine**: `Sprite`, `Bitmap`, `WorldClass`, Allegro |
| Service entries | 3 | — | Menu, ads, online: not games |

Per-game list: [reference/games.md](reference/games.md).

## Milestones

### 1. Finish the GameDevice family (now)

Easiest first, in the order of the [survey](reference/gamedevice-survey.md):

1. ~~Boxxi Blitz~~ (done, 2026-10-07). **Spin Card Hold'em**: 0 missing symbols; `make new` and play-test.
2. **The GameId overloads** of `HighScoresManager` (`HighEnough(GameIds,int,int,int)`,
   `HighestScore(GameIds,int)`, `HighestName(GameIds,int)`). They unblock about 15 games.
3. **No-op services:** `LightShow`, `LightShowManager::Play`, `ads::in_game_ads::get_ad_name`,
   `ChampEditionI::*`, `Profiler::*`.
4. **Preloaded cabinet libraries:** `libsettings.so` provides `Settings::*` for 6 games. Needs a
   `PRELOAD=` option in `game.conf` ([loader services](reference/loader-services.md#libraries-the-loader-preloaded)).
5. **`MegacGlobals`:** first contact with real loader state (photo hunt games). Its layout must
   be recovered.
6. **Irrlicht backend** (`libgame_device_irrlicht.so`): Mystery Phraze HD uses a second
   GameDevice backend. Same technique, different base classes.
7. Known Trix issues: OGG streaming, `.spr` decode cache.

### 2. Unity games (30)

The player (`ion_only/games/launcher/LinuxPlayer`, Unity 3.2.0f4, 32-bit, not stripped) is an
ordinary Linux program. Cabinet integration is in managed DLLs (`LauncherUnity.dll`,
`LauncherFile.dll`, `AMIUnityExtensions_Plugins.dll`). Plan: lay out `Data/` the way the loader's
symlink did, run it on our 32-bit runtime, then stub or patch the plugins in C# (Mono.Cecil or
dnSpy). That is cheaper than native reverse engineering. Output: a `unity` launcher in `src/`
and `make new` support for the family.

### 3. The loader, part 1: the legacy 2D engine (143 games)

Legacy games import the loader's engine directly: Allegro 4 calls, `Sprite`, `Bitmap`,
`WorldClass`, `VideoClass`, `MegacGlobals`, `NVRAMData`, `PlrScore`… (about 600 symbols across
the closure of a typical game). Two routes, which can be combined:

- **Reimplement** the engine as a host library (`src/loader/`) on SDL2, with the shared pieces
  already in place: path shim, old-ABI `stat`, fonts, Pango, `.spr` (legacy games use the same
  format), translations. The `Bitmap` and `Sprite` layouts must be recovered, because games touch
  their fields inline.
- **Unpack the real loader** (`/usr/local/bin/loader`, 2.9 MB, packed and obfuscated) to recover
  the original engine code and reuse it. This needs unpacking first. If it works it is the
  shortest path to exact behaviour.

Start with one simple legacy game, keep a list of the symbols each new game adds, and grow the
library the way loader services grew.

### 4. Merit3D (13 games)

Builds on part 1, plus ODE physics and OpenGL through `libmerit3d.so`. The 3D libraries are
normal shared objects; the work is in the loader services they expect.

### 5. The loader, part 2: the cabinet itself

The game-select menu, attract mode (the `idle/*.mov` videos), operator settings
(`/var/merit/settings.xml`), credits and free play, high-score tables and name entry,
languages. The result is a front end that lists the ported games and launches them, which
replaces `make run` for players.

### 6. Windows native (optional)

Everything runs on WSL2 today. A native Windows build would need the engine's Linux `.so` files
loaded on Windows, which means an ELF loader. Not planned until the platform work is further
along.

## How to pick something up

1. Read [guides/porting-a-game.md](guides/porting-a-game.md).
2. Pick the next item above, or the top unported row of the survey.
3. Log what you learn in that game's `NOTES.md`. Put shared findings in `docs/reference/` and
   new bugs in [known-bugs.md](reference/known-bugs.md).
