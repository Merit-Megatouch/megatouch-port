# Debugging and profiling

Which tool to use for which symptom, and how to read what each one gives you. Every switch is
listed in [reference/commands.md](../reference/commands.md#environment-variables).

## Symptom → tool

| Symptom | First move | Then |
| --- | --- | --- |
| Won't start: `undefined symbol` | `make analyze GAME=…` | Stand-in ([porting](porting-a-game.md#2-make-it-load-stand-ins)) |
| Won't start: `cannot open shared object` | `ls -la games/…/lib` (broken symlink?) | `LD_DEBUG=libs make run …` |
| Crash | Read the trace | `make decompile`, find the function |
| "pure virtual method called" | `vtdump.py` on the base class | Fill the slot |
| Black screen | `DEBUG=shots` and `DEBUG=files` | Window size, asset dir, locator flag |
| Missing image | `DEBUG=files 2>&1 \| grep -i name` | `files/resource_locator` flag |
| Wrong or missing text | Check `translations/<dll>.utf8` | `translations/untranslated` flag |
| No sound | `DEBUG=sound` | Check `SDL_AUDIODRIVER`; run `pactl info` on WSLg |
| Lag or stutter | `DEBUG=profile` | `profreport.py` |
| Crash or freeze mid-game, no clear cause | Look for `[ipc]` lines; `DEBUG=files` for `/dev`, `/proc`, `/var/merit` reads | Possible hardware or key check ([loader services](../reference/loader-services.md#hardware-and-key-checks)) |
| Logic wrong | Screenshots plus `MEGA_AUTOCLICK` to repeat it | Compare with the decompiled game |

## Reading a crash trace

Example (the null-`INetLink` bug from the first port):

```
*** signal 11 at address 0x4
  #0 /…/games/g_trix/megatouch-host+0x5a1c  (?)
  #1 /…/lib/libmessaging.so+0x8f21  (_ZN7NetLink8INetLink24RegisterNetMessageManagerEPN9Messaging17NetMessageManagerE)
  #2 /…/lib/libmessaging.so+0x7d10  (_ZN9Messaging17NetMessageManagerC1Ev)
  …
```

- `address 0x4`: a field read through a null pointer, offset 4. Something we should have
  created wasn't.
- Each frame is `library+offset (symbol)`. Pipe through `c++filt` to read the names.
- `(?)` frames are local functions. Look the offset up: `addr2line -f -e <lib> 0x…` for our
  libraries (built with `-g`), or Ghidra for cabinet ones (Ghidra address = offset + 0x10000).
- Frames in `libgame_device_sprite.so` are ours, in `src/backend/`.

## Seeing the screen without watching

```bash
make run GAME=g_trix DEBUG=shots                       # a PNG every 90 frames
MEGA_SHOT_EVERY=10 make run GAME=g_trix DEBUG=shots    # more often
SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy MEGA_SHOT_DIR=/tmp/s make run GAME=g_trix   # headless
```

Scripted input: `MEGA_AUTOCLICK="60:640,400;150:900,700"` taps (640,400) at tick 60 (2 s) and
(900,700) at tick 150. Coordinates are logical (game resolution). Combine with headless mode and
screenshots to test a flow without a person.

## Paths

```bash
make run GAME=g_trix DEBUG=files 2>&1 | grep -v '\.png$' | less
mkdir -p games/g_trix/data/var/merit/debug/files
touch games/g_trix/data/var/merit/debug/files/resource_locator     # engine's own search trace
```

The locator prints `checking: <dir>` for each candidate and `found [<file>] for [<name>]`.
`found []` means nothing matched: check the asset dir, the game folder name, the resolution
folder (`1280x800`, `default`) and whether the file is in the shared `games/default`.

## Sound

`DEBUG=sound` prints each call with a millisecond timestamp:

```
[mega] t=10234 play sfx/sounds/deal.wav loop=0 vol=1
[mega]   -> id=12 pcm=0x… samples=88200
[mega] playing? id=7 -> 1
[mega] stop id=12
```

- `samples=0` → a decode problem (format, or the `len_cvt` bug).
- Music restarting over and over → `IsPlaying` returns false too early.
- Nothing at all on WSLg → check that Windows audio works, then `pactl info` inside WSL if you have
  `pulseaudio-utils` installed (it talks to WSLg's PulseAudio server). `SDL_AUDIODRIVER=dummy` left in your environment is silent by design.

## Performance

```bash
make run GAME=g_trix DEBUG=profile 2>&1 | tee games/g_trix/notes/game.log
python3 tools/profreport.py games/g_trix/notes/game.prof games/g_trix/notes/game.log --top 30
```

- `[frames]` lines (each second): fps, worst frame gap, frames over 34 and 50 ms, render ms, present ms.
- `[hitch]` lines: any frame over `MEGA_HITCH_MS` (40 with this preset), with what happened in it:
  texture uploads, image decodes, sound decodes.
- The report leaves out idle samples (stacks containing `usleep`/`nanosleep`, which is the
  engine's frame limiter). It then shows self and inclusive time per library and function, for
  the whole run and for the hitch windows only.

Trix baseline (WSLg, llvmpipe, 1280×800): 30 fps steady, main thread about 91% idle, about 4 ms
per frame, mostly in `SDL_RenderPresent` → GLX → llvmpipe. Hitches only on first image loads
(100–200 ms for big `.spr` files) and music track starts (about 90 ms).

How the profiler works: a POSIX timer sends SIGPROF to the main thread every 2 ms of wall time.
The handler records the PC and a `backtrace()`, guarded by `sigsetjmp` because unwinding through
llvmpipe's JIT code can fault. Symbols are resolved with `dladdr` at exit. More in
[history/09](../history/09-debugging-and-profiling.md).

## ABI questions

- **Which slot does this call use?** In the caller's disassembly, `call [eax+0x48]` after
  loading the vptr is slot 0x48/4 = 18.
- **How many arguments?** Count the `push`es (or `mov [esp+N]`) before the call. One more than
  the signature means a member function (`this`), or a hidden return pointer for class
  return values.
- **Which library does a symbol bind to?** `LD_DEBUG=bindings make run … 2>&1 | grep <name>`.
- **What does the original backend do?** The cabinet's `libgame_device_sprite.so` and friends are
  small; decompile them (`tools/decompile.sh` on a folder holding them) and read the override.

## The engine's own logs

Our backend's lines start with `[mega]`; the shim's with `[file]`/`[glob]`/`[ipc]`; the profiler's with
`[profile]`, `[frames]`, `[hitch]`. The engine logs a lot to stderr too (`Unable to read`, `found []`, state changes). Grep for
`Debug_Flag_Set` strings to switch on more ([flags](../reference/commands.md#engine-debug-flags)),
and `Logger` output appears on stderr too.
