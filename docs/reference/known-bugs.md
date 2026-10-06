# Known bugs and fixes

Every failure hit so far, grouped by where it came from, with what to look for if it happens
again. The "#" numbers 1–20 match the original journal ([history/10](../history/10-bug-catalogue.md)).

## Open issues

| Game | Issue | Idea |
| --- | --- | --- |
| g_trix | `gfx/hud/common_files/tricks` not found | The cabinet ships an empty `tricks/` folder next to `tricks.spr.gz`; the folder wins. A real data quirk. |
| g_trix | One sound requested with an empty name | Harmless; find the caller with `DEBUG=sound` |
| all | Music track start stalls ~90 ms (whole OGG decoded) | Stream with `stb_vorbis_open_memory` + `get_samples` |
| all | First use of large `.spr` stalls 100–200 ms | Decode cache or preload at layout load |
| g_trix | Card fanning art not shipped | Leave `CARD_FANNING` off |
| g_word_dojo_2 | Bonus round and help screen not verified | Play-test (`word_dojo_2/auto_bonus` flag jumps to it) |
| all | `MEGA_FPS` other than 30 breaks timing | Games count ticks; keep 30 |

## Engine bugs that old glibc hid

| # | Symptom | Cause | Fix |
| --- | --- | --- | --- |
| 15 | Segfault in `_Rb_tree_increment` ← `BaseSoundManager::StopSound` | The engine erases a `std::map` node and keeps iterating from it. The 2008 allocator left freed memory intact. | `ImplementationStopSound` returns 0, so the entry isn't erased (the voice is still silenced). `GLIBC_TUNABLES=glibc.malloc.tcache_count=0` as a general safety net. |

If a new game crashes inside libstdc++ containers with no obvious cause, suspect the same kind
of bug. Find the erase in the decompiled engine function and make our side of it avoid
triggering the free, as with `StopSound`.

## Environment and ABI

| # | Symptom | Cause | Fix |
| --- | --- | --- | --- |
| 2 | `failed to load …/sysroot/usr/lib32/lib/g_trix.so` | Started through `ld-linux.so.2`, so `/proc/self/exe` is the dynamic loader | `run` sets `MEGA_HOME` |
| 3 | `libpng16.so.16: undefined symbol: inflateReset2` | The image's 2008 `libz.so.1` shadowed modern zlib | Never ship the image's libz (`extract-libs.sh` skips it) |
| 6 | Resource locator never looks in the right place | Assets belong under `/usr/local/ion_only/games/` (a mount point) | Data tree layout |
| 7 | Relative paths (`gfx/…`, `timer_layout`) fail | The loader started games with CWD = asset dir | Host `chdir`s to `ASSET_DIR` |
| 8 | `DirExists` false for existing folders; `found [] for […]` | Old `__xstat`/`__lxstat` use the 32-bit `struct stat` and fail with EOVERFLOW on 64-bit inodes | Shim reimplements them on `stat64` and converts |
| 11 | Every PNG: "not a regular file or pipe" | SDL's file opening rejects WSL drvfs files | Read into memory, decode from RWops |
| 12 | All text renders as boxes | (a) fonts not configured; (b) Pango 1.14 shaping modules missing | Cabinet `fonts.conf`; ship the modules, generate `pango.modules`, `PANGO_RC_FILE` |
| — | `make analyze` died on `libc.so` | It's a linker script, and `nm` failed under `pipefail` | Tolerate nm errors |
| — | `g++32` picked the wrong `bits/wordsize.h` | Include order | Sysroot headers via `-idirafter` |

## Backend (our code)

| # | Symptom | Cause | Fix |
| --- | --- | --- | --- |
| 1 | `libmeritbasegame.so: undefined symbol: _ZN6Sprite6Signal…` | The original backend needs the loader's legacy engine | Replaced the backend |
| 4 | SIGSEGV at 0x4 in `INetLink::RegisterNetMessageManager` | `CreateNetLink()` returned null | Return a minimal `INetLink` |
| 5 | `Unable to read []`, crash in `Button2D::ListenToEvents` | Locator found nothing, so layouts were missing and buttons null | See 6–8 |
| 10 | "pure virtual method called" when groups are destroyed | `BaseRenderable2D` and `BaseSoundManager` destructors are pure virtual | Fill slots 0/1 |
| 13 | `.spr` animations missing | Format only decoded by the loader | Decoded it ([file-formats](file-formats.md)) |
| 14 | `gfx/cardfan/*` not found | Card fanning on; art not shipped | Off by default |
| 16 | No music; tracks restart every frame | `SDL_BuildAudioCVT` "no conversion" leaves `len_cvt` unset → empty buffers → `IsPlaying` false → playlist advances | Use `len` when no conversion |
| — | TGA images missing (Word Dojo 2) | `IMG_Load_RW` can't detect TGA | `IMG_LoadTyped_RW` with the type from the extension |
| — | Game-over screen blank (Word Dojo 2) | Art lives in the fallback game `/usr/local/games/default` | Part of shared data, linked into every game |
| — | `Failed to find resource [gfx/end_tile_explosion]` (Boxxi Blitz); Trix fire animations missing | Animations stored as a folder of frames are listed by the locator's wildcard search, which builds `//usr/local/...` paths; the shim only matched one leading slash | Shim collapses repeated leading slashes |
| — | `libinput_sprite.so: cannot open shared object` (Word Dojo 2) | Listed as NEEDED but unused | Empty stub libraries |
| 18 | Profiled windowed run dies with no trace | `backtrace()` in the SIGPROF handler unwound into llvmpipe JIT code | `sigsetjmp` guard, recovered from the crash handler |
| 19 | `MEGA_FPS=60` makes AI turns very slow | Game logic counts ticks | Keep 30 |

## Loader stand-ins

| # | Symptom | Cause | Fix |
| --- | --- | --- | --- |
| 9 | `basic_string::_S_construct null not valid` in Translate | `Translator::Translate` is **static**; a member version read the wrong stack slot | Declared static |
| 17 | Help shows "HELP_TEXT", "Text line 1" | Pass-through translator | Real translation tables |
| — | High score never kept | `HighEnough` always returned false and stored nothing | Save the best to `/var/merit/highscores/<id>.txt` |

## Tooling and scripts

| # | Symptom | Cause | Fix |
| --- | --- | --- | --- |
| 20 | Waiting script never fires | `pgrep -f name` matched its own command line | Use `[n]ame`-style patterns |
| — | `NOTES.md` overwritten by re-scaffolding | One file for facts and the human log | Split: `notes/scaffold.md` (generated) and `NOTES.md` (yours) |
| — | Wrong window size for `SUPER_HIGH_RESOLUTION` games | Mapped to 1024×768 | 1280×800, plus a largest-PNG check |
| — | Game submodules always dirty | Date in the generated `scaffold.md` | No date |
| — | `game-repo.sh` re-ran `git init` in submodules | Their `.git` is a file, not a folder | Test with `-e` |
| — | `grep -v` with no output killed scripts | Non-zero exit under `pipefail` | `|| true` |

## Diagnosing a new crash

1. Read the crash trace: `lib+offset (symbol)` per frame. Engine libraries export most
   functions, so it usually names the method.
2. `pure virtual method called` → a slot isn't filled. `vtdump.py` on the base class.
3. A crash in our code at a small address (0x4, 0x8…) → a null object the engine expected us to
   create.
4. `Unable to read []` / empty file names → paths. Use `DEBUG=files` and the
   `files/resource_locator` flag.
5. A crash in `std::` containers inside engine code → suspect use-after-free (see above).
6. `undefined symbol` at start → `make analyze`, then a stand-in.
