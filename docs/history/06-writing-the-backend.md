# 06 — Writing the SDL2 backend

Source: `src/backend/` — `engine.cpp` (vtable cloning, shared state), `device.cpp`, `textures.cpp`,
`sprites.cpp`, `input.cpp`, `sound.cpp`, `net.cpp`, `spr.cpp`, `backend.h`, `merit_abi.h`. Output:
`lib/libgame_device_sprite.so` — same name as the original, so `g_trix.so`'s `NEEDED` entry
picks it up. The other three `*_sprite` libraries and `libmerit2d/3d`, `libmeritbasegame`,
`libmerit_threads` are simply left out.

## The vtable-cloning technique

Re-declaring each engine base class in C++ would mean reproducing dozens of virtual signatures
exactly (one wrong parameter type → wrong mangled name → unresolved symbol). Instead, build
objects the way the compiler does:

```cpp
static void** clone_vtable(const char* vtSym, int extraSlots = 0) {
    Dl_info info; ElfW(Sym)* es;
    void* vt = dlsym(RTLD_DEFAULT, vtSym);                      // e.g. "_ZTVN8Graphics11BaseTextureE"
    dladdr1(vt, &info, (void**)&es, RTLD_DL_SYMENT);            // symbol size = vtable size
    void** copy = (void**)calloc(es->st_size / 4 + extraSlots, 4);
    memcpy(copy, vt, es->st_size);
    return copy + 2;                                            // skip offset-to-top + typeinfo
}

void* tex_new() {
    if (!g_texVtbl) {
        g_texVtbl = clone_vtable("_ZTVN8Graphics11BaseTextureE");
        g_texVtbl[0] = (void*)tex_dtor;  g_texVtbl[1] = (void*)tex_dtor_delete;
        g_texVtbl[21] = (void*)tex_lock; g_texVtbl[22] = (void*)tex_unlock; /* ... */
    }
    void* t = operator new(0x38 + sizeof(TexData*));            // base size + our fields
    ((void(*)(void*))dlsym(RTLD_DEFAULT, "_ZN8Graphics11BaseTextureC2Ev"))(t);  // base ctor
    *(void***)t = g_texVtbl;                                    // become "derived"
    return t;
}
static void* tex_lock(void* self) { ... }                       // `self` = this
```

Properties:
* Inherited behaviour stays exactly the engine's (unpatched slots point into the engine).
* Typeinfo stays the base's, so engine `dynamic_cast`s to the base type keep working.
* Our per-object state goes after the base size; access fields with `*(T*)((char*)obj + off)`.
* Destructors: call the base's `D2` (`_ZN...D2Ev`), and for the deleting slot also
  `operator delete`. **Fill every `__cxa_pure_virtual` slot**, destructors included.
* Use `operator new`/`delete` from libstdc++ (engine code may delete our objects).

Direct (non-virtual) calls into the engine use ordinary declarations in `merit_abi.h` — only
names and parameter types matter for mangling (`Core::Rect::Rect(float,float,float,float)`,
`Graphics::ImageResource::AddFrame(Graphics::Frame const&)`,
`GameDevice::GameConfig::SetWindowWidth(unsigned int)`, ...). Link against the original
`libgame_device.so libgraphics.so libcore.so libinput.so libmerit_sound.so`.

## Classes

### Game device (`CreateNewGameDevice`)
`operator new(0x118)` → `BaseGameDevice C2` → patch slots 0,1,4,7,11,12,28,29,30.

`ImplementationInitialize(cfg)`:
1. sound manager → +0x24, then `SetResourceLocator(+0x08)` and `Initialize()`
2. net utils → +0x28
3. config: `SetGameID(245)`, players, window/screen 1280×800, language `"english"`,
   card fanning **off** (its art isn't shipped)
4. object factory(`scene[0]`, `+0x2c` text system) → +0x10
5. input manager → +0x20
6. SDL window + renderer (1280×800 logical size, resizable, F11 fullscreen)
7. resource manager singleton, `SetResourceLocator`

`ImplementationUpdate`: autoclick (debug) + `SDL_PollEvent` → mouse state, quit, F11. Returns
`!quit`. `ImplementationRender`: clear, `scene->Render(t)` (slot 3) for each scene, optional
screenshot, present, frame stats.

### Texture
`TexData { w, h, bpp; vector<vector<uint8>> frames; vector<SDL_Texture*> gpu; vector<bool> dirty; }`.
CPU pixels per frame in the engine's format; uploaded lazily to a streaming ARGB8888
`SDL_Texture` on first draw or after `Unlock`. 16-bit source converts RGB565, with magic pink
`0xF81F` → transparent.

### Resource manager
`ImplementationLoadTexture(file, res)`: decode (`.spr` → `load_spr`, else SDL_image), create a
texture, `AddFrame({GetTotalTextures(), Rect(0,0,w,h)})` once per frame, `AddTexture`.

### Renderable
`ImplementationDraw` → `SDL_RenderCopyExF` with color/alpha mod, flip from negative src size,
rotation around the pivot, optional clip rect.

### Input
Mouse/touch → `PendingKey` deque + mouse position, then base `Update()`. Clicking anywhere
starts a game from attract mode.

### Sound
Own mixer on an SDL audio device (44.1 kHz stereo S16). WAV via `SDL_LoadWAV_RW` +
`SDL_AudioCVT`; OGG via `stb_vorbis_decode_filename`. Voices keyed by an integer handle stored
in `SoundInfo+0`. **`ImplementationStopSound` returns 0** on purpose (chapter 10).

### Net
INetUtils: single unlinked machine (0 units, ids 0, timers advance). INetLink: drop broadcasts.

## Build

`src/Makefile` (`make install` → builds and copies `megatouch-host` and
`libgame_device_sprite.so` into `shared/bin/`, which every game links to). Flags that matter:
`-m32 -fPIC -D_GLIBCXX_USE_CXX11_ABI=0`, link `-lSDL2 -lSDL2_image -lz -ldl`,
`-Wl,--allow-shlib-undefined`. When replacing files under a running game use
`cp x x.new && mv x.new x` (never overwrite a mapped file in place).

## Launcher (`shared/launch.sh`, symlinked as each game's `run`)

```sh
D=$(cd "$(dirname "$0")" && pwd); R="$D/runtime"          # the game folder
export LD_LIBRARY_PATH="$D/lib:$R:$R/pulseaudio"
export LIBGL_DRIVERS_PATH="$R/dri"
export FONTCONFIG_FILE="$D/data/etc/fonts.conf"
# Pango 1.14: generate pango.modules with absolute paths, point PANGO_RC_FILE at a pangorc
export GLIBC_TUNABLES=glibc.malloc.tcache_count=0     # old-allocator behaviour (chapter 10)
export MEGA_HOME="$D"
exec "$R/ld-linux.so.2" "$D/megatouch-host" "$@"
```

`megatouch-host` (`src/main.cpp` + `host.cpp` + `profiler.cpp`, linked `-rdynamic` so its symbols
are global): crash handler, reads `game.conf` into `MEGA_*` variables, sets the data root,
`chdir`s into `ASSET_DIR`, `dlopen`s `lib/<LIB>` and calls its `main`. The backend reads
`MEGA_GAME_ID`, `MEGA_WIDTH`, `MEGA_HEIGHT`, `MEGA_LANGUAGE`, `MEGA_TITLE`, `MEGA_CARD_FANNING`.
(The first version was called `trix-bin` with Trix's values hard-coded — chapter 12.)
