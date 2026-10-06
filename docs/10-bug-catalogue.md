# 10 — Bug catalogue (in the order they were hit)

| # | Symptom | Cause | Fix |
|---|---|---|---|
| 1 | `libmeritbasegame.so: undefined symbol: _ZN6Sprite6SignalEP12SpriteSignal` | Original sprite backend needs the loader's legacy engine | Replace the backend (chapter 04–06) |
| 2 | `failed to load .../sysroot/usr/lib32/lib/g_trix.so` | Run via explicit `ld-linux.so.2` → `/proc/self/exe` is the loader | Launcher passes `TRIX_HOME` |
| 3 | `libpng16.so.16: undefined symbol: inflateReset2` | 2008 `libz.so.1` from the image shadowed modern zlib | Don't ship the image's zlib |
| 4 | SIGSEGV at 0x4 in `INetLink::RegisterNetMessageManager` | `CreateNetLink()` returned null; NetMessageManager registers on it | Return a minimal `INetLink` |
| 5 | `Unable to read []`, crash in `Button2D::ListenToEvents` | Resource locator found nothing (see 6–8) → layouts missing → null buttons | — |
| 6 | Locator never looks in the right place | Assets belong under `/usr/local/ion_only/games/` (partition 10 mount point) | Data tree layout |
| 7 | Relative lookups (`timer_layout`, `gfx/...`) fail | Loader starts games with CWD = asset dir | `chdir` before `main` |
| 8 | `DirExists` false for existing dirs | Old `__xstat`/`__lxstat` with 32-bit `struct stat` → EOVERFLOW on 64-bit inodes | Reimplement on `stat64`, convert |
| 9 | `basic_string::_S_construct null not valid` in `Fake_Translator::Translate` | `Translator::Translate` is **static**; member version read the wrong stack slot | Declare static |
| 10 | "pure virtual method called" when groups are destroyed | `BaseRenderable2D` and `BaseSoundManager` destructors are pure virtual | Provide dtor slots 0/1 |
| 11 | Every PNG: "not a regular file or pipe" | SDL's file opening rejects WSL drvfs files | Read into memory, decode via RWops |
| 12 | All text renders as boxes | (a) fonts not configured, (b) **Pango 1.14 shaping modules missing** | Cabinet `fonts.conf`; ship modules + generated `pango.modules`, `PANGO_RC_FILE` |
| 13 | `.spr` animations missing (deal, crown, fire...) | Format decoded only by the obfuscated loader | Reverse-engineered the format (chapter 08) |
| 14 | `gfx/cardfan/*` not found | Card fanning enabled by our config; art isn't shipped | `SetCardFanning(false)` |
| 15 | Segfault in `_Rb_tree_increment` ← `BaseSoundManager::StopSound` | **Engine bug**: erases a map node, frees it, keeps iterating from it. Old glibc malloc left freed memory intact | `ImplementationStopSound` returns 0 (entry not erased; voice still silenced); `GLIBC_TUNABLES=glibc.malloc.tcache_count=0` as general safety |
| 16 | No music; tracks restart every frame | `SDL_BuildAudioCVT` "no conversion" leaves `len_cvt` unset → 0-sample buffers → `IsPlaying` false → playlist advances | Use `cvt.len` when no conversion |
| 17 | Help shows "HELP_TEXT", "Text line 1" | Pass-through translator | Real translation tables |
| 18 | Profiled windowed run segfaults with no trace | `backtrace()` in the SIGPROF handler unwound into llvmpipe JIT code | `sigsetjmp` guard + recover from crash handler |
| 19 | `MEGA_FPS=60` slows AI turns badly (one gap 0.6 s → ~12 s) | Some game logic counts update ticks | Keep 30 |
| 20 | Waiting script never fires | `pgrep -f trix-bin` matched its own command line | Match `[l]d-linux.so.2` style patterns |

## Known remaining issues
* `gfx/hud/common_files/tricks` is not found (data quirk). The game-over art was in the cabinet's
  fallback game folder `/usr/local/games/default` — now part of shared data, linked into every game.
* One sound is requested with an empty name.
* Music track changes stall ~90 ms (whole-file OGG decode) — fix by streaming.
* First use of large `.spr` animations stalls 100–200 ms — fix with a decode cache / preload.
