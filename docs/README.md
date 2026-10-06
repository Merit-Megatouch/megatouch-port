# Megatouch ION → Linux/WSL porting cookbook

How Megatouch **Trix** (Megatouch ION 2014, software PG3002-01 V40.02) was taken off a
60 GB cabinet disk image and made to run as a normal windowed game on Linux / WSL2 —
running the **original 2013 game and engine binaries**, with only the cabinet-specific
display/sound/input backend replaced by an SDL2 one.

Written so the same process can be repeated for other games from the platform.

## Chapters

| # | Chapter | What you get out of it |
|---|---------|------------------------|
| 01 | [Platform anatomy](01-platform-anatomy.md) | Disk layout, partitions, where games/configs live, the four engine families |
| 02 | [Toolchain setup (no root)](02-toolchain-setup.md) | debugfs offsets, 32-bit gcc, Ghidra headless, private i386 apt, Python helpers |
| 03 | [Extracting a game](03-extracting-a-game.md) | Game catalogue, assets, code, dependency closure, old system libraries |
| 04 | [Mapping the runtime](04-mapping-the-runtime.md) | Who-loads-whom, the host-symbol surface, finding the replaceable boundary |
| 05 | [Reverse engineering the backend](05-reverse-engineering.md) | Decompiling, vtable dumps, recovered object layouts and slot tables |
| 06 | [Writing the SDL2 backend](06-writing-the-backend.md) | The vtable-cloning technique and each replacement class |
| 07 | [Host shim, filesystem & data layout](07-host-and-data.md) | Loader stand-ins, path redirection, old glibc ABI, fonts, Pango, translations |
| 08 | [Asset formats](08-asset-formats.md) | PNG/TGA, the `.spr` sprite format (fully decoded), OGG/WAV |
| 09 | [Debugging & profiling toolkit](09-debugging-and-profiling.md) | Crash handler, file tracing, engine debug flags, screenshots, autoclick, profiler |
| 10 | [Bug catalogue](10-bug-catalogue.md) | Every failure hit on the way: symptom → cause → fix |
| 11 | [Porting another game](11-porting-another-game.md) | Step-by-step checklist, what is game-specific, plans per engine family |
| 12 | [Scaffolding a new game](12-scaffolding.md) | The template: `new-game.sh`, the workspace layout, the working loop |

## Where things are

See the [top-level README](../README.md) for the repository layout and commands, and
[chapter 12](12-scaffolding.md) for the workspace in detail. Chapters 02–11 describe how the
first port was done by hand; `make setup` and `make new` now automate most of it.

## The whole journey in one screen

1. **Inspect the image** with `debugfs -R "..." "img?offset=<start*512>"` — no mounting, no root.
2. **Catalogue games** from `/var/merit/settings.xml` + `/usr/local/gamedata/config/gamedata.xml`.
3. **Locate the game**: assets in `/usr/local/ion_only/games/g_<name>/`, code in `/usr/local/lib/g_<name>.so`.
4. **Close over dependencies** (`readelf -d` NEEDED, recursively) and pull old system libs from the image.
5. **Find the seam**: the game only imports `GameDevice::CreateNewGameDevice()` from
   `libgame_device_sprite.so`; everything else goes through virtual calls on engine base classes.
6. **Decompile** the original sprite backend (Ghidra headless) to recover object sizes, field offsets
   and which vtable slots it overrides.
7. **Write a replacement `libgame_device_sprite.so`** on SDL2: construct engine base objects with their
   exported constructors, then point them at *cloned base vtables* with only our slots patched.
8. **Stand in for the cabinet loader**: ~10 C++ functions (Translator, ContinueControl, HighScores...)
   plus a filesystem shim mapping `/usr/local/...` and `/var/merit/...` into a local `data/` tree.
9. **Fix the environment**: CWD = game dir, 64-bit-inode-safe `__xstat`, cabinet `fonts.conf`,
   Pango 1.14 modules file, translation tables, old-glibc use-after-free tolerance.
10. **Decode the remaining asset formats** (`.spr`), wire up sound, verify with screenshots, profile.
