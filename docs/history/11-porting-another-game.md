# 11 — Porting another game

## Pick the family first

```bash
debugfs -R "dump /usr/local/lib/<dll>.so /tmp/x.so" "$ROOT"
readelf -d /tmp/x.so | grep NEEDED
debugfs -R "ls /games/<dir>" "$ION"
```

* `libgame_device_sprite.so` in NEEDED → **GameDevice family** — follow the checklist below.
* asset dir contains `Data/` with `Managed/*.dll` → **Unity family** (see below).
* `libmerit3d.so` → **Merit3D**; otherwise **legacy sprite**.

GameDevice games on this image (22): brain_in_gear, fight_the_landlord, funky_monkey_2,
g_boxxi_template, g_field_goal, g_little_shop_road_trip, g_mystery_phraze_hd,
g_phillies_hottest_phunt, g_space_farmer, g_spin_card_holdem, **g_trix**, g_word_dojo_2,
g_zombie_cats, jackpot_corner, little_shop_of_treasures, megatouch_memory,
photo_hunt_hd, stunt_squirrel, text_twist2, touchdown_poker, tug_of_words.
(`launcher.so` also uses this engine but hosts the Unity player rather than being a game.)
(Some of these — e.g. photo_hunt_hd — keep assets under `media/` or in `/usr/local/gamedata`
instead of `ion_only/games/<dir>/gfx`.)

## Checklist for a GameDevice game

1. **Find the game ID.** From the decompiled `__EntryPointV12` (`Logger::SetApplicationID(N)` /
   `ResourceLocator(N)`) or the `xml_gameinfo::GameIds` enum. Trix = 245.
2. **Extract code + closure** — `make new GAME=<dll>` does this (chapter 12). By hand:
   `scripts/lib/extract-libs.sh games/<dll>/lib <dll>.so`, then
   remove the original `*_sprite`, `libmerit2d/3d`, `libmeritbasegame`, `libmerit_threads`.
   Diff the closure against Trix's — extra libraries may bring new host symbols.
3. **Recompute the host surface** (chapter 04, step 4) over the kept libs. Anything beyond Trix's
   list (`PlrScore`, Translator, ContinueControl, HighScoresManager, Logger) needs a stand-in in
   `host.cpp`. For each, disassemble one caller to learn static-vs-member and what the return
   value means.
4. **Copy assets** to `data/usr/local/ion_only/games/<dir>/` (or wherever the trace says the
   locator looks); copy the same `gamedata`, `var/merit`, `etc`, `pango` folders.
5. **Parameterize the backend**: game ID (`SetGameID`), window size (check the game's
   `UseResolution` — 4:3 games are 1024×768 or 800×600), `chdir` target and `g_<name>.so` in
   `main.cpp`. (Making these command-line/env options is the first refactor for a second game.)
6. **Run with the crash handler, `MEGA_TRACE_FILES`, the resource_locator flag and screenshots.**
   Expect: new loader symbols, new resource paths, maybe new asset formats.
7. **Check every cloned vtable for pure slots** with `vtdump.py` if the game uses additional
   engine classes through the backend.
8. **Verify translations/help** (`translations/<dll>.utf8`, `help/english/<dll>.utf8`).
9. **Play it with the profiler on** to find engine bugs that relied on old glibc behaviour.

## Unity family (~30 games) — the promising next target

* Player: `/usr/local/ion_only/games/launcher/LinuxPlayer` — 32-bit, **Unity 3.2.0f4**, not
  stripped, with debug info.
* At runtime the loader symlinks `launcher/Data → /var/merit/games/launcher/Data`, pointing at
  the selected game's `Data/` (e.g. `g_ice_rift/Data`: `mainData`, `level0`,
  `sharedassets*.assets`, `resources.assets`, `Managed/`).
* Cabinet integration lives in .NET: `Managed/LauncherUnity.dll`, `LauncherFile.dll`,
  `AMIUnityExtensions_Plugins.dll`, `Assembly-CSharp.dll`.

Plan: set up a directory with `LinuxPlayer` + `<game>_Data`-style layout (or the symlink),
run it with the same 32-bit runtime, see what the managed plugins expect (IPC sockets under
`/dev/merit_ipc/`, files under `/var/merit/`), and stub those **in C#** (decompile/patch the DLLs
with ILSpy/dnSpy/Mono.Cecil) — much easier than native reverse engineering. Unity 3.2's player
links X11/OpenGL directly, which WSLg provides.

## Merit3D and legacy families

These link directly against loader-provided APIs: Allegro calls, `Sprite`, `Bitmap`,
`WorldClass`, `VideoClass`, `MegacGlobals`, `NVRAMMap`... (the 600-symbol list from chapter 04).
Two options:
1. **Reimplement the loader's engine** as a host library — big, but shared by 150+ games, so it
   amortizes. Layouts of `Bitmap` and `Sprite` must be recovered (the engine libraries access
   fields inline).
2. **Unpack the loader** (it's packed/obfuscated) to recover the legacy engine implementation
   and reuse the original code — needs unpacking first; possibly the shortest path if it works.

Shared groundwork from this port still applies: data tree, path shim, old-ABI stat, fonts,
Pango, `.spr` decoder (the legacy engine uses the same format), translations.

## Packaging a port

`make package GAME=<dll> DEST=<dir>` makes a self-contained copy: launcher, `megatouch-host`, `lib/` (engine + backend), `runtime/`
(32-bit glibc, libstdc++, SDL2, Mesa llvmpipe, PulseAudio client, 2008 libs), `data/`.
Copy the folder anywhere on a Linux/WSL machine and run it. On Windows:
`wsl ~/trix-port/games/g_trix/run` (a desktop shortcut works).
