# Legacy games (the loader's 2D engine)

143 cabinet game libraries predate the GameDevice engine. They don't use engine libraries:
they call the cabinet loader directly, through an immediate-mode API inherited from the DOS era
(bitmaps, blits, named touch zones, RAD Smacker-style animations, Allegro sound). The loader is
packed and obfuscated, so `src/legacy/legacy.cpp` (`libmerit_legacy.so`) reconstructs that API
from how the games call it. First game: **Fourplay** (GameId 7), 2026-10-07.

```bash
make new GAME=fourplay     # family "legacy": assets from /usr/local/gamedata/gamegraphics/<dir>
make run GAME=fourplay
```

## Size of the job

| | Symbols |
| --- | ---: |
| Distinct loader functions/data imported by all legacy games | 2,099 |
| Median per game | 112 |
| Fourplay (smallest real game) | 44 — all implemented |
| Most used | `MegacGlobals::GetInstance` (128 games), `SystemTimer` (123), `AddPreWave`/`PlayPreWave` (121), `Random` (109), `SystemClass::ConfirmExit` (107), `NVRAMData`/`NVRAMMap::GamesOpt` (100), `Bitmap::Bitmap` (99), `HighScoresManager::HighestScore` (97), `textSystem`, `FileLoc` (94), `Bitmap::Display` (91), `SystemClass::CheckKey` (91) |

Two API generations show up: the C functions Fourplay uses (`BitmapAlloc`, `BitmapToScreen`,
`AnimationOpen`, `MouseAdd`, `InputCharOrDelay`…), and a C++ layer most games use (`Bitmap`,
`WorldClass`, `VideoClass`, `MouseManager`, `BmpFont`, `UniversalTranslator`…). Each game added
grows the library, like `loader_services.cpp` grew for GameDevice games.

## How a legacy game runs

- The game exports `__EntryPointV12` (no `main`). `megatouch-host` calls it after preloading
  `libmerit_legacy.so` (`PRELOAD=` in `game.conf`).
- The game runs its own loop: draw, then `InputCharOrDelay(buf, ms)` (returns the name of a
  touched zone), check `SystemTimer()`, repeat. There is no frame callback; the engine presents
  the screen whenever the game waits.
- Working directory = `/usr/local/gamedata/gamegraphics/<dir>`; assets are opened by bare name.
  Language-specific art is in `<language>/`, and assets shared by all legacy games (226 sounds,
  common images, databases) are in `gamegraphics/misc` (`shared/data-common`, linked into every
  legacy game).
- Screen 640×480. Colours are 8-bit palette indices from the old API; the art is RGB565.

## Reconstructed semantics

| API | Behaviour (as implemented) | Evidence |
| --- | --- | --- |
| `_RADBitmap` | `{ u32 tag; u32 width; u32 height; … }` | Fourplay reads +4 and +8 to centre a bitmap |
| `BitmapAlloc(w, h, 8)` | New bitmap filled with colour 0 (black) | 8 = bits per pixel in the DOS API |
| `BitmapClear(b, c, -1, -1, -1)`, `BitmapFilledBox`, `ScreenFilledBox` | Fill with colour `c` | |
| Colour 5 | **Transparency key**. Pixels the `.dlt` RLE skips are key-coloured. | `BitmapClear(b, 5)` then transparent blits with key 5 |
| Colour 0 | Black | |
| `BitmapToBitmap(dst, src, x, y)` | Copy the dst-sized region of src starting **at (x, y) in src** | Sprite-sheet cells extracted with x = 0, 19, 38, 57 |
| `…Trans(…, key)` | Same, skipping pixels of colour `key` | |
| `BitmapToScreen(b, x, y)`, `BitmapFromScreen(b, x, y)` | Blit to / grab from the screen | save-under / restore pattern |
| `BitmapChangeColor(b, from, to)` | Recolour pixels | `ChangeColor(b, 5, 0)` then `Trans(…, 0)` |
| `BitmapSetPalette`, `BitmapPaletteToPalette` | No-ops (RGB565 art) | |
| `ShowNumber(n, dst, digits, 1, key, 1)` | Draw `n` into `dst` with digit bitmaps 0–9 (+ `[10]` comma), right-aligned, thousands separated | Score panel shows `1,600` |
| `AnimationOpen(lang, name, ?)` → `_MSmack*` | Load `<name>.dlt` | |
| `AnimationToBitmap(a, b)` | Bind `b` as output; copy the current frame into it | RAD SmackToBuffer style |
| `AnimationAdvanceNoPalette(a, ?)` | Next frame (wraps), decoded into the bound bitmap | loops then blit the bound bitmap |
| `AnimationStillDelay(a)` | Non-zero until the frame delay (`.dlt` header, 1/60 s units — assumed) has passed | busy-wait loops call it |
| `AnimationGoto(a, n)` | Frame n, 1-based | `Goto(a, 1)` before playing |
| `AnimationLoadToBitmap(name, lang)` | Open, decode frame 1 into a new bitmap, close | `lang` = 1 for files in `english/` |
| `AnimationPlay(x, y, first, last, name, lang)` | Play frames to the screen and return | `AnimationPlay(0, 382, 0, -1, …)` |
| `MouseAdd(name, x, y, w, h, MouseBmp)` | Register a named touch zone | `COL0`–`COL6`, `QUITBOX` |
| `MouseRemoveAll()`, `ClearTouch()` | Clear zones / pending touches | |
| `InputCharOrDelay(buf, ms)` | Wait up to `ms` for a touch, copy the zone name into `buf` | `strcasecmp("QUITBOX", buf)` |
| `Delay(ms)`, `SystemTimer()` | Sleep; milliseconds since start | `local + 20000 < SystemTimer()` = 20 s idle |
| `Random(n)`, `Randomize(seed)` | 0..n-1 | |
| `AddPreWave(name, flags, …)`, `PlayPreWave(name, flags, loop?, vol, ?, pan)` → voice | Preload / play `<name>.wav` (game folder, then `misc`) | |
| `voice_get_position(voice)` | -1 once the voice finished | Fourplay waits for a sound this way |
| `MegacGlobals::GetInstance()` | Static block: **+0x24 player count** (`PLAYERS`), **+0x2038 current GameId** | |
| `SystemClass::ConfirmExit(…)` | `true` (quit) | object at `MegacGlobals+0x22dc` |
| `HighScoresManager::HighEnough(id, player, …)`, `HighestScore(id, …)` | Shared high-score stand-ins (saved best per game) | |

Unknowns are noted as "assumed" above; the flags arguments of `AddPreWave`/`PlayPreWave`
(0, 2, 0x40, 0x440) are ignored for now.

## .dlt files

`<name>.dlt.gz`: `u32 version (3); u16 width, height, frameCount, frameDelay;` then
`frameCount` frame records `{ u32 bytes; u32 w; u32 h; data }` and an optional 8-byte trailer.
Frame data is the same run-length format as `.spr` (`src/common/merit_rle.h`). All 56 Fourplay
files decode cleanly.

**Frames are delta-coded.** Frame 1 is a full picture; every later frame holds only the pixels
that change (Fourplay's thinking animation: 61 KB, then 4–6 KB per frame; a 4-byte frame means
"no change"). The engine keeps a composited canvas per animation and redraws it from frame 1
when seeking backwards. Drawing frames on their own gives scraps of the picture.

## Settings

`game.conf` for a legacy game:

```
LIB=fourplay.so
ASSET_DIR=usr/local/gamedata/gamegraphics/fourplay
GAME_ID=7
WIDTH=640
HEIGHT=480
PRELOAD=libmerit_legacy.so
PLAYERS=1                 # MegacGlobals player count (1 or 2)
```

`MEGA_SHOT_DIR`/`MEGA_SHOT_EVERY` save `.bmp` screenshots every N presents,
`MEGA_AUTOCLICK="ms:x,y;…"` taps at milliseconds since start (legacy timing, not update ticks),
and `MEGA_DEBUG_TOUCH=1` logs zones, every touch (640×480 coordinates, zone hit) and when the
game reads it. Mouse positions from SDL are already in logical coordinates; don't convert them
again (that bug made the right-hand columns unreachable).

## Next legacy games

Ranked by missing symbols (2026-10-07): conquest 46, bgammon (Back Jammin) 51, nine (Pharaohs
Nine) 53, puckshot 58, tennis 61, airhockey 63, chess 64, goal 65, kids_color 65, moondrop 66.
`*_hm.so` libraries in the list are Irrlicht-backend GameDevice variants, not legacy games.
