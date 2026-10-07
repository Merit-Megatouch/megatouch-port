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
| GameDevice | 21 games | 20 run (3 played by hand; 17 smoke-tested) | SDL2 backend + loader stand-ins | g_mystery_phraze_hd deferred: needs DBFClass (dBase reader), RandomizedArrayClass and the gendef record xml_gamerandom::MystPICRAND_record (abstract_xml_record subclass) — 18 symbols |
| Unity 3.2 | 30 | 30 run (1 played by hand) | cabinet LinuxPlayer + launcher.xml (real format) + FMOD shim + fs shim | hand play-tests; clocker/close-the-clock washed out |
| Legacy 2D | 128 scaffolded | 119 of 135 run in the batch (2026-10-07 pm); ~118 published | `src/legacy` (C API, sprite engine, Bitmap, Allegro subset, TTF) + `src/gendef` | Gash/LinuxWorldClass (breakout, motormatch, racepoker; spec docs/reference/gash.md), SObj (euchre, hearts, spades, snubble; docs/reference/sobj.md), golf (8-bit software 3D on allocframe/BMAP), safari (flagged, see below); bitmap-font text garbled everywhere (docs/reference/bmpfont.md); hangs with ~0 frames: cepixmix/chpixmix/pixmix family, ginrummy, locboxxi; early exits: chboxxi |
| AllegroGL / Merit3D | 19 (14 scaffolded) | 14 run: luxor, chainz2, monkeybusiness, triviawhiz2/jr, VideoSales, minigolf x3, pool, nineball, bowling, beer_pong_challenge, stickerbook | GL window + `src/legacy` + 3D-side stand-ins | hand play-tests; 5 have no art on the image (below) |

Per-game state lives in each `games/<name>/NOTES.md` and in `docs/reference/games.md` (`make docs`).

## How to verify a game without a person

- `tools/smoke.sh <game> [secs]` + `toolchain/venv/bin/python tools/contact.py out.png <games…>` (contact sheet).
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

## Decision: no running of the cabinet's own loader (2026-10-07)

The cabinet loader is protected (packed, security key on the USB I/O board, anti-tamper code).
Running it would mean defeating that protection, so this project does not do it. Legacy and
Merit3D games run on our own reconstruction of the engine API (`src/legacy`), as Fourplay does,
and phase 2 is our own front end (menu, settings, high scores) that launches games through the
existing routes.

### Decision: games' anti-tamper / key hooks (2026-10-07)
About 41 legacy games import `t_i_l()` and `u_m_m_s(...)` from the loader: integrity checks
(scanning loader code for NOP patches) and an obfuscated "Key System I.O. has stopped" message
decoder. These are part of the cabinet's copy protection. The port does not implement anything
aimed at satisfying or bypassing them — they only get the generic `make stubs` placeholder. If a
game refuses to run because of them, list it here for the owner instead of working around it.

### Not on this disk image
luxor2, snake, racing, shooter and beer_pong (the original Beer Pong) ship their code
(/usr/local/lib/*.so) but their art is linked to /usr/local/ion_only/gamegraphics/*, which this
image does not have (the ion partition only holds games/). They cannot run without that content.
idle (attract mode) belongs to the front end, not the games list.

### Flagged for the owner
- **safari**: its game loop (`i_g_d_c`, which also calls `mprotect` and `SanityCheck`, beside the
  t_i_l/u_m_m_s/CheckKey imports) reaches `Bitmap::Display` on a bitmap it has not created yet.
  It looks protection-related, so it was left alone.
- luxor's `menu_music.wav` is not on the image (silent menu on the cabinet too).

## In progress

### Legacy engine (2026-10-07)
- `tools/legacy-missing.py [--per-game] [game…]` ranks what each legacy game still misses (vs
  libmerit_legacy + megatouch-host). `make stubs GAME=` (now skips what we implement) lets a game load.
- Allegro: the cabinet liballeg is unusable (its asm drawing core was linked into the loader), so
  `src/legacy/allegro.cpp` implements the used subset with 4.0 layouts (BITMAP line[] at +0x40,
  43-slot GFX_VTABLE from __linear_vtable32 relocs, gfx_driver w/h at +0x68/+0x6c).
  `_RADBitmap` = { BITMAP*; w; h; px; tag } — games blit `*(BITMAP**)rad` directly.
- `BitmapToBitmap(dst, src, x, y)`: (x,y) = dest position if src fits in dst, else source cut offset.
- Palette index 0 is transparent black for the *Trans calls with key 0; 5 = magenta key.
- Autoclick also presses the polled mouse (games use MouseX/Y, GetTouchCoord, mouse_b).
- BitmapTextTTF(bmp, text, x, y, w, h, c1, c2, c3, size, align, bold, family, spacing): c1..c3
  meaning unknown (0,0,-255 / 0,-100,-255 / 0,0,0) — drawn white for now. Check against a real
  screenshot when possible.
- Sprite engine implemented (`src/legacy/sprite.{h,cpp}`) from `docs/reference/sprite-engine.md`
  (agent-derived ABI: vtable orders, sizes, offsets). Bitmap rewritten (`bitmap.{h,cpp}`) with a
  16-bit Allegro BITMAP at +0x2c, frame chains, compressed data (setCData/DeCompress), z-list.
  Worlds without SetBack: show the VB the game drew into, else restore only under sprites.
- File formats added: archive records (`LoadCompressedData(name, FILE*)`), FLIC (.flc/.fli).
- `tools/legacy-batch.sh [secs]` smoke-tests every legacy game (stubs regenerated) →
  build/smoke/legacy-batch.txt. Host crash reports now print pc, frame walk and a stack scan.
- `libmerit_gendef.so` (src/gendef) implements the loader's gendef classes from
  `docs/reference/gendef-records.md`; legacy-preload adds it (after libgendef_xml/common) for games
  that import them. Built with the old std::string ABI like the rest of the project.
- Loader globals: MegacGlobals+0xc is a std::map<GameId, gamedata_record(0x24c)> (host fills the
  current game's entry; +0xb0 = folder name), +0x207c the world (a default world is installed).
- `merit_services.cpp`: SystemInfo (ION), MeritInput/MeritSound managers, Push/PopWorld.
- Anti-tamper hooks `t_i_l`/`u_m_m_s` are left as plain stubs on purpose (see the decision above).
- Running (2026-10-07): fourplay conquest nine bgammon airhockey quickcell puckshot goal tennis
  checkerz royal chug21 battle31 strippoker funkymonkey wildapes moondrop chess qbzone brickbreaker.

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
- Plan: our loader = our own front end that reads settings.xml/gamedata.xml, shows the categories
  and game icons (menugraphics art), launches games through the existing routes, handles free
  play, high scores and an operator settings screen. Written from scratch; the cabinet's
  protected loader is not run or modified.
- Assets found (2026-10-07): `menugraphics/main/2k3` (ionbkg, ionlogo, optionwindow, langwindow,
  buttons), `menugraphics/game/logos/<lang>/<game>.spr[.gz]` (one logo per game; the ION-only
  ones in `/usr/local/ion_only/menugraphics/game/logos`), `menugraphics/game/buttons` (p1..p4,
  play, info, more), `menugraphics/idle` (attract jpgs/dlt), `credit`, `hiscore`, `keyboard`.
  All readable with `src/common/merit_rle.h` + SDL_image.
- Build order: (1) `src/frontend/` SDL2 app: catalogue from games/*/game.conf (+ cabinet
  gameinfo for categories), menu with logos, player-count select, launch `games/<g>/run` as a
  child and come back; (2) attract loop; (3) free play / credits screen; (4) high scores
  (/var/merit/highscores per game, already written by megatouch-host); (5) operator settings
  (language, volume, free play) written to a settings file that `run` passes as MEGA_* env.

## Log (newest first)
- 2026-10-07 pm: GL group all running (TextSystem at Pango's 40 dpi, 32-bit image loaders in GL
  mode, Allegro poll functions, List+0x14 header, DBGlobals); dominoes/take2/wild8/zenword/golf
  progress; content-pack links fixed (spotmatch, pe*); 29 newly published. Spec agents writing
  docs/reference/{sobj,gash,bmpfont}.md.

- 2026-10-07 — Legacy: sprite engine, Bitmap rewrite, gendef library, translations; 105/128 run,
  89 published as game repos. `tools/legacy-batch.sh` is the regression check.
- 2026-10-07 — 17 more GameDevice games run headless (smoke.sh + contact sheets); fixes: libsettings preload, v4 .spr, content packs (2 GB shared), absolute links in assets, g_ prefix, window sizes for fight_the_landlord/text_twist2 (800x600), stunt_squirrel (640x480), g_space_farmer (1024x768). All published.

- 2026-10-07 — Owner asleep; autonomous run started. Merit3D groundwork committed.
- 2026-10-07 — Fourplay (first legacy), Tri Towers 2 (first Unity), Boxxi Blitz, Word Dojo 2, Trix.
