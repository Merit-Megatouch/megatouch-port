# 09 — Debugging & profiling toolkit

Shortcuts: `make run GAME=<name> DEBUG=shots|files|sound|profile`.

All switches are environment variables on a game's `run` script (`MEGA_<NAME>`; the original `TRIX_<NAME>` spelling still works).

| Variable | Effect |
|---|---|
| *(always on)* | Crash handler: on SIGSEGV/SIGABRT/SIGFPE prints `lib+offset (symbol)` per frame. Engine libs export most functions, so traces read like source. |
| `MEGA_TRACE_FILES=1` | Logs every path passing through the filesystem shim (`[file] ...`) and every `glob` (`[glob] pattern -> result`). |
| `MEGA_SHOT_DIR=dir` `MEGA_SHOT_EVERY=n` | Saves `frameNNNNN.png` every n rendered frames — look at what's on screen without being at the machine. |
| `MEGA_AUTOCLICK="tick:x,y;tick:x,y"` | Taps the screen at given update ticks (30/s) — scripted UI testing. |
| `MEGA_DEBUG_SOUND=1` | Logs play/stop/volume/pause/is-playing with timestamps. |
| `MEGA_FRAME_STATS=1` | One line per second: fps, worst frame gap, late frames, render/present ms. |
| `MEGA_HITCH_MS=n` | Logs any frame slower than n ms with what happened in it (uploads, image loads, sound decodes, render, present). |
| `MEGA_PROFILE=out.prof` `MEGA_PROFILE_US=2000` | Wall-clock sampling profiler of the main thread; summarize with `tools/profreport.py out.prof game.log`. |
| `MEGA_RENDERER=software\|opengl` `MEGA_VSYNC=0` | Renderer experiments. |
| `MEGA_FPS=n` | Override update/frame rate (experimental — breaks Trix's game timing). |
| `SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy` | Headless runs (no window, no sound). |
| `LD_DEBUG=bindings` | Which library each symbol binds to (glibc). |

## Engine debug flags

`libdebug_shared.so`'s `Debug_Flag_Set(category, flag)` is true if
`/var/merit/debug/<category>/<flag>` exists. Example — resource locator trace:

```bash
mkdir -p game/data/var/merit/debug/files && touch game/data/var/merit/debug/files/resource_locator
```

prints every candidate path (`checking: ...`, `found [...] for [...]`). Grep the engine
libraries for other `Debug_Flag_Set` string pairs to find more.

## The profiler (`src/profiler.cpp`)

* POSIX timer with `SIGEV_THREAD_ID` → SIGPROF to the main thread only, every 2 ms of wall time
  (so blocking time is visible, not just CPU).
* The handler records EIP from the ucontext, then `backtrace()` (DWARF unwinding through the
  sigreturn trampoline — gets through libc, Mesa, SDL which have no frame pointers).
* Unwinding llvmpipe's JIT code can fault, so the handler runs under `sigsetjmp`; the crash
  handler calls `trix_profile_recover()` which `siglongjmp`s back and keeps only the PC.
  The crash handler is installed with `SA_NODEFER`.
* Samples are symbolized with `dladdr` + `__cxa_demangle` at exit.
* `profreport.py` drops idle samples (stack contains `usleep`/`nanosleep` = the engine's frame
  limiter) and prints self/inclusive time by library and function, overall and inside the
  `[hitch]` windows from the log.

Trix findings: main thread 91% idle; of busy time ~82% waiting in `SDL_RenderPresent` → GLX →
llvmpipe; steady 30 fps; stalls only on first-time image loads (100–200 ms) and on music track
starts (~90 ms full OGG decode).

## Workflow that worked

1. Run, read the crash trace → find the function in `decomp/<lib>.c`.
2. If paths are involved → `MEGA_TRACE_FILES` and the resource_locator flag.
3. If behaviour is wrong → screenshots + `MEGA_DEBUG_SOUND` + autoclick, compare to the
   decompiled logic.
4. For ABI questions → disassemble the *caller* (`objdump -d -R`) and count pushed args / see
   which slot offset is called.
