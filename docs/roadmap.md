# Roadmap

The main route is **the cabinet itself**: the Megatouch ION's own software, unmodified, with its
hardware simulated ([cabinet-loader](guides/cabinet-loader.md)). It already runs the menus, the
games, Operator Setup, coins, keys, MegaNet and TournaMAXX. The second route, **standalone
ports** of single games, came first and is kept for development (below).

## The cabinet

**Done:** boot, menus, every stand-in (I/O board, touchscreen, sound, security key, network,
display), Operator Setup, coins and keys, MegaNet and TournaMAXX, settings and backups, a
scalable fullscreen window, the [operator guide](guides/operator-guide.md).

**Next:**

1. **Try every game** from the menus and fix what misbehaves (as with Super Boxxi and Trix).
2. **Linked cabinets (MegaLink).** Several installs, on one PC, a LAN or the internet, on one
   virtual cabinet network: a small virtual switch per install (replacing slirp4netns) with its
   own internet router, plus an optional hub that joins installs. First step: two cabinets on one
   PC, to learn how MegaLink finds its partners.
3. **A unique identity per install** (serial, MegaNet ID, key image), so each install is its own
   machine to MegaNet servers and to linked play.
4. **Easier installs:** a one-command installer and a Windows shortcut.

## Standalone ports: where things stand

| Family | Games | Status | What porting takes |
| --- | ---: | --- | --- |
| GameDevice (2009+) | 22 | **3 playable** (Trix, Word Dojo 2, Boxxi Blitz); 1 more with 0 missing symbols | Shared SDL2 backend (done). A few loader stand-ins per game: see the [survey](reference/gamedevice-survey.md). |
| Unity 3.2 | 30 | **route works**: Tri Towers 2 plays with sound | `make new GAME=<folder>`; per game, find the launcher settings it misses ([unity](reference/unity.md)) |
| Merit3D | 13 | not started | The loader's 3D services: ODE, OpenGL, Allegro |
| Legacy sprite | 143 | **Fourplay playable** on `libmerit_legacy.so` (44 of 2,099 loader functions) | Grow the reconstructed engine game by game ([legacy](reference/legacy.md)) |
| Service entries | 3 | — | Menu, ads, online: not games |

Per-game list: [reference/games.md](reference/games.md).

## Standalone ports: milestones

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

### 2. Unity games (30) — route done 2026-10-07

The cabinet's player (`ion_only/games/launcher/LinuxPlayer`, Unity 3.2.0f4) runs unchanged on our
32-bit runtime. Done: `make new` scaffolds Unity games, `launch.sh` writes the `launcher.xml` the
loader used to write, and `libmega_unity.so` routes FMOD's sound to PulseAudio. Tri Towers 2 plays.
Next: scaffold and play-test the other 29, find the remaining launcher keys
(`AllPlayersLoggedIn`), and high scores, which went through the loader's messaging. Patching the
cabinet's .NET plugins (Mono.Cecil) is the fallback where settings aren't enough.

### 3. The loader, part 1: the legacy 2D engine (143 games) — started 2026-10-07

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

Started with the reimplementation route: `src/legacy/legacy.cpp` covers Fourplay's C API (bitmaps,
delta `.dlt` animations, touch zones, timers, sounds). Next, in order of missing symbols: Conquest
(46), Back Jammin (51), Pharaohs Nine (53), Puckshot, Tennis, Air Hockey, Chess. The C++ layer
(`Bitmap`, `WorldClass`, `VideoClass`, `MouseManager`, `BmpFont`) comes in with the larger games.

### 4. Merit3D (13 games)

Builds on part 1, plus ODE physics and OpenGL through `libmerit3d.so`. The 3D libraries are
normal shared objects; the work is in the loader services they expect.

### 5. The loader, part 2: the cabinet itself

Done differently: instead of rewriting the menu, attract mode, operator settings, credits and
high scores, the cabinet's own loader now runs unmodified with stand-in hardware
([guides/cabinet-loader.md](guides/cabinet-loader.md)). A hand-written front end (`./menu`) was
started for this and removed on 2026-10-09 once the real loader worked.

### 6. Windows native (optional)

Everything runs on WSL2 today. A native Windows build would need the engine's Linux `.so` files
loaded on Windows, which means an ELF loader. Not planned until the platform work is further
along.

## How to pick something up

1. Read [guides/porting-a-game.md](guides/porting-a-game.md).
2. Pick the next item above, or the top unported row of the survey.
3. Log what you learn in that game's `NOTES.md`. Put shared findings in `docs/reference/` and
   new bugs in [known-bugs.md](reference/known-bugs.md).
