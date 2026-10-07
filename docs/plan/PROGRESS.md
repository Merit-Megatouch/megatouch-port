# Long-term plan and working notes

**Read this first after a context reset.** It is the running state of the project: the goal, what
is done, what is in progress, and how to continue. Update it after every milestone.

## Goal (from the owner, 2026-10-07)

1. **Every game on the cabinet working properly** (graphics, text, sound, input, full play-through).
2. **Then the loader**: start our own loader, do everything a player and operator can do on a real
   Megatouch ION (game menu, attract mode, settings, high scores, credits/free play, languages),
   and launch every game from it.

Games first, loader second. Work autonomously; commit and push (`make publish GAME=all`) after
each working game or shared fix.

## Status by family (update the counts)

| Family | Games (libs) | Working | Route | Next |
| --- | ---: | ---: | --- | --- |
| GameDevice | 22 | 3 (trix, word dojo 2, boxxi blitz) | SDL2 backend + loader stand-ins | survey order: spin card holdem, field goal, zombie cats, megatouch memory… |
| Unity 3.2 | 30 | 1 (tri towers 2) | cabinet LinuxPlayer + launcher.xml + FMOD shim | scaffold all 29, screenshot-verify each |
| Legacy 2D | ~128 games / 143 libs | 1 (fourplay) | `src/legacy` reconstruction of the loader's 2D API | grow by game, cheapest first (docs/reference/legacy.md) |
| Merit3D | ~13 | 0 | GL window + `src/legacy` + 3D-side stand-ins | beer pong 21 in progress (see below) |

Per-game state lives in each `games/<name>/NOTES.md` and in `docs/reference/games.md` (`make docs`).

## How to verify a game without a person

- GameDevice/legacy: `SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy MEGA_SHOT_DIR=/abs/dir
  MEGA_SHOT_EVERY=N MEGA_AUTOCLICK=... timeout 30 games/<g>/run`, then look at the screenshots
  (legacy writes .bmp — convert to PNG to view). GameDevice autoclick is in update ticks (30/s),
  legacy in milliseconds.
- Unity and Merit3D need the real display (WSLg is available). Merit3D: screenshots via
  glReadPixels in `src/legacy` (MEGA_SHOT_DIR). Unity: a glXSwapBuffers hook in
  `libmega_unity.so` (to add) for screenshots.
- Always grep the log for `signal`, `[stub]`, `missing`, `Couldn't pull`, `[ipc]`.

## Method per family

- **GameDevice**: `make new`, `make analyze`, add stand-ins to `src/host/loader_services.cpp`
  (check static vs member + return use in Ghidra output), run, fix. Preloaded cabinet libraries
  (libsettings.so) via `PRELOAD=` in game.conf.
- **Unity**: `make new GAME=<folder>`; check the log for `Couldn't pull` keys and add them in
  `scripts/launch.sh`.
- **Legacy**: `make new`; `make analyze` lists loader functions; decompile the game
  (`make decompile` or analyzeHeadless) and infer each function from its call sites; implement in
  `src/legacy/legacy.cpp` (C API) or new files for the C++ classes (`Bitmap`, `WorldClass`,
  `VideoClass`, `MouseManager`, `BmpFont`…). `make stubs GAME=` gives placeholders so the game
  loads; each logs its first call → implement in call order.
- **Merit3D**: as legacy plus `GL=1` (OpenGL window), GL/GLU preloaded.

## In progress

### Beer Pong 21 (`games/beer_pong_challenge`, Merit3D) — not committed as a game repo yet
- Loads with `make stubs`; enters `Merit3d::Game::Run`; crashed in `Text2d::Create` because
  `TextSystem::GetSpriteChannels` is a stub.
- Decompiled: `games/beer_pong_challenge/decomp/*.c` (game, libmerit3d, libmeritbasegame).
- Findings: `RenderDeviceAlleggl::Swap` = `allegro_gl_flip()`; screen size from
  `layout::ScreenControl::GetCurrentWidth/Height`; legacy `Bitmap` is 44 bytes: +0 w, +4 h,
  +0x1c → int bpp, +0x28 pixels; `FlipBitmapVert(data, w*2, h, bpp)` (row bytes = arg2*bpp/16);
  text: TextDesc (+0x20 size, +0x24/26/28 fg rgb shorts, +0x2a.. outline, +0x38 valign,
  +0x3c halign), `GetSpriteChannels(this, markup, uchar** rgbaOut, uchar**, w, h, …)` returns a
  w×h RGBA buffer (engine flips + uploads); font "quercus" = EFN QuercusC (fontconfig alias).
  Input: MouseGetX/Y, MouseGetButton(n) held state; joystick can stay 0.
- Next: write `src/legacy/merit3d.cpp` (text via pangoft2 dlopen, Bitmap via SDL_image, input,
  SoundOGG via stb_vorbis, singletons/no-ops, Allegro globals `screen`/`key`, allegro_gl_flip),
  set `GL=1` in game.conf, iterate on crashes.

## Loader (phase 2) — notes for later

- Real loader: `/usr/local/bin/loader` (2.9 MB, packed). Menu art: `/usr/local/gamedata/menugraphics`,
  operator: `/usr/local/gamedata/opsetup`, settings `/var/merit/settings.xml`, attract videos
  `/usr/local/ion_only/games/idle/*.mov`, `g_menu` game folder, `opsetup.so`, `sixstars.so`,
  `volumecontrol.so` (legacy-API system modules, ~670 loader functions each).
- Plan: our loader = a front end that reads settings.xml/gamedata.xml, shows the categories and
  game icons, launches games through the existing routes, handles credits/free play, high scores,
  operator setup. Investigate whether g_menu / opsetup.so can run on the reconstructed engine.

## Log (newest first)

- 2026-10-07 — Owner asleep; autonomous run started. Merit3D groundwork committed.
- 2026-10-07 — Fourplay (first legacy), Tri Towers 2 (first Unity), Boxxi Blitz, Word Dojo 2, Trix.
