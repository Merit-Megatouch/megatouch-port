# Architecture

How a ported game runs, from `make run` to pixels, and which piece is responsible for what.

## The idea in one picture

On the cabinet:

```
 loader (packed, protected)  ── dlopen ──►  g_trix.so  ──►  engine libs (libgraphics, libgame_device, …)
   ├ Allegro, legacy sprite engine                │
   ├ coin/credits, security, IPC                  └──►  libgame_device_sprite.so ─► *_sprite libs ─► loader's
   └ exports PlrScore, Translator, …                    (the cabinet backend)                       2D engine
```

In a port:

```
 megatouch-host  ── dlopen ──►  g_trix.so  ──►  engine libs   (unchanged, from the cabinet)
   ├ fs_shim          (paths)          │
   ├ loader_services  (stand-ins)      └──►  libgame_device_sprite.so   (ours: SDL2)
   └ profiler, crash handler                   ├ device, textures, sprites, input, sound, net
                                               └ objects built on the engine's own base classes
```

Everything the game and engine do themselves is untouched: game logic, layouts, effects, text
layout, state machines and messaging. We replace only:

1. the **backend**: one library, the only one the game imports from;
2. the **loader services**: a dozen functions and two data symbols the loader exported;
3. the **environment**: file paths, working directory, fonts, the 2008 C library ABI.

## Components

| Component | Source | Built to | Role |
| --- | --- | --- | --- |
| Launcher | `scripts/launch.sh` | `games/<name>/run` (symlink) | Environment: library path, fonts, Pango, malloc tunable |
| Host | `src/host/main.cpp` | `shared/bin/megatouch-host` | Read `game.conf`, crash handler, `chdir`, `dlopen` the game, call `main` |
| Filesystem shim | `src/host/fs_shim.cpp` | (in host) | Rewrite cabinet paths into `data/`; old-glibc `stat`/`readdir` on 64-bit calls |
| Loader services | `src/host/loader_services.cpp` | (in host) | `PlrScore`, Translator, ContinueControl, HighScoresManager, LanguageManager, Logger, Allegro UTF-8 helpers |
| Profiler | `src/host/profiler.cpp` | (in host) | SIGPROF sampler with backtraces |
| Backend | `src/backend/*.cpp` | `shared/bin/libgame_device_sprite.so` | `GameDevice::CreateNewGameDevice()` and every object it creates |
| Stubs | `Makefile` | `shared/bin/stubs/lib{graphics,input,sound}_sprite.so` | Empty libraries for games that list the old ones as NEEDED |
| gameids | `tools/gameids.cpp` | `shared/bin/gameids` | Prints the engine's GameId enum (libenums) |
| Runtime | `scripts/setup.sh` | `shared/runtime/` | 32-bit glibc, libstdc++, SDL2, Mesa, PulseAudio, 2008 libraries |
| Shared data | `scripts/setup.sh` | `shared/data-common/` | Fonts, translations, config, Pango modules, fallback game screens |

The host is linked with `-rdynamic`, so its functions are in the global symbol table. Libraries
loaded afterwards bind to them: a definition in the main program beats any library, including
versioned references like `glob@GLIBC_2.0`. That one fact makes both the shim and the stand-ins
possible.

## Startup sequence

```
make run GAME=g_trix
└─ games/g_trix/run  (scripts/launch.sh)
   │  LD_LIBRARY_PATH=lib:runtime:runtime/pulseaudio   LIBGL_DRIVERS_PATH=runtime/dri
   │  FONTCONFIG_FILE=data/etc/fonts.conf   PANGO_RC_FILE=$XDG_RUNTIME_DIR/megatouch-UID/pangorc
   │  GLIBC_TUNABLES=glibc.malloc.tcache_count=0   MEGA_HOME=games/g_trix
   └─ exec runtime/ld-linux.so.2 megatouch-host
      ├─ crash handler (SIGSEGV/SIGABRT/SIGFPE, SA_NODEFER)
      ├─ game.conf → MEGA_LIB, MEGA_ASSET_DIR, MEGA_GAME_ID, MEGA_WIDTH, … (environment wins)
      ├─ data root = $MEGA_HOME/data;  profiler if MEGA_PROFILE
      ├─ chdir data/usr/local/ion_only/games/g_trix         (the loader did this too)
      ├─ dlopen lib/g_trix.so  RTLD_NOW|RTLD_GLOBAL
      │    → NEEDED: libgame_device_sprite.so (ours), libgraphics.so, … (cabinet)
      └─ main(argc, argv)   ← the game's own exported main
         └─ __EntryPointV12
            ├─ dev = GameDevice::CreateNewGameDevice()        ← backend: operator new(0x118),
            │                                                    BaseGameDevice ctor, cloned vtable
            ├─ GameConfig cfg; cfg.ParseArgs(); Logger::SetApplicationID(245)
            ├─ resources::ResourceLocator loc(245)
            ├─ dev->Initialize(cfg, &loc)                     ← engine (BaseGameDevice::Initialize)
            │   ├─ message manager, Scene 0, timing (30/s), PangoTextSystem
            │   └─ ImplementationInitialize(cfg)              ← backend
            │       ├─ sound manager (+0x24), net utils (+0x28)
            │       ├─ cfg: GameID, window size, language, card fanning
            │       ├─ object factory (+0x10), input manager (+0x20)
            │       ├─ SDL window + renderer (logical WIDTH×HEIGHT)
            │       └─ resource manager singleton
            ├─ dev->RegisterUpdate(Update); Game::GetInstance()->Initialize(dev)
            │   └─ loads layouts → textures via ResourceManager slot 13 (ours) → PNG/TGA/.spr
            └─ while (dev->Run()) {}                          ← engine fixed-step loop
```

## The frame loop

`BaseGameDevice::Run()` (engine) runs one iteration per frame:

```
Run()
├─ up to 2× Update(1/30)
│   ├─ ImplementationUpdate(dt)          backend: SDL_PollEvent → mouse, F11, quit; autoclick
│   ├─ input->Update()                   backend slot 0 → PendingKey deque → base derives press/release
│   ├─ sound->Update(dt)                 engine base; calls our IsPlaying/Stop slots
│   ├─ net->Update(dt)                   backend: single unlinked machine
│   └─ broadcast GD_OUT_UPDATE_MSG       game logic and state machines run here
├─ Render(t)
│   └─ ImplementationRender(t)           backend: clear; scene->Render() for each scene
│        └─ Sprite2D::Render → Renderable2D::Draw (engine) → ImplementationDraw (ours)
│             → upload dirty texture frames → SDL_RenderCopyExF
│        then: screenshot?, SDL_RenderPresent, frame stats
└─ usleep(rest of 1/30 s)                the engine's frame limiter (shows as idle in profiles)
```

Measured on WSLg with llvmpipe at 1280×800: about 4 ms per frame for render and present; the
main thread is about 90% idle.

## Data flow: files

```
game asks for                           shim maps to                                 under games/<name>/
/usr/local/ion_only/games/g_trix/…  →  $MEGA_DATA/usr/local/ion_only/games/g_trix/…  data/usr/local/ion_only/… (copied assets)
/usr/local/games/default/…          →  $MEGA_DATA/usr/local/games/default/…          → shared/data-common (symlink)
/usr/local/gamedata/…               →  $MEGA_DATA/usr/local/gamedata/…               → shared/data-common (symlink)
/var/merit/…                        →  $MEGA_DATA/var/merit/…                        data/var/merit (private, writable)
/dev/merit_ipc/…                    →  $MEGA_DATA/dev/merit_ipc/…                    (rarely created)
gfx/hud/x.png  (relative)           →  unchanged; CWD is the asset dir
```

Wrapped calls: `open open64 fopen fopen64 access opendir mkdir rmdir unlink remove rename
symlink realpath stat64 lstat64 glob`, plus the old-ABI `__xstat __lxstat __fxstat __xstat64
readdir`. `glob` results are mapped back to cabinet paths, so callers can keep building on them.

### How the engine finds a file

`resources::ResourceLocator::get_file(subdir, name, type)` tries each combination of:

| Level | Candidates, in order |
| --- | --- |
| base | `/var/merit/games`, `/usr/local/ion_only/games/`, `/usr/local/games` |
| game | `<game dir>`, `default` |
| resolution | `<W>x<H>`, `default`, none |
| language / country | from the config, then none |
| subdir / name.ext | as asked |

It checks each directory with `__xstat` and then `__lxstat`. The `default` game folder is the
cabinet's fallback, with the shared game-over, winner/loser and quit prompts for each resolution.
Turn on `files/resource_locator` to watch the search ([debug flags](commands.md#engine-debug-flags)).

## Data flow: images

```
layout XML <sprite2d bitmap_file="…">
  → Graphics::ImageResource (engine) → ResourceManager::ImplementationLoadTexture(file, res)  [slot 13, ours]
      ├ .spr/.spr.gz → load_spr(): gunzip, decode RLE frames → ARGB per frame
      └ else         → read the whole file → IMG_LoadTyped_RW(type from extension) → ARGB8888
  → texture object (cloned BaseTexture vtable) holding CPU pixels per frame
  → res->AddFrame({textureIndex, Rect(0,0,w,h)}) per frame; res->AddTexture
first draw → streaming SDL_Texture per frame, uploaded lazily (and again after Lock/Unlock)
```

Text works the same way: the Pango text system renders a string, locks a texture (slot 21),
writes pixels into it and unlocks it (slot 22), which marks it dirty for upload.

## Data flow: sound

```
game: sound->PlaySound("tile_click")             engine BaseSoundManager: name → SoundInfo, map<int, SoundInfo>
  → ImplementationPlaySound(SoundInfo&)          ours: decode on first use (cached)
       WAV: SDL_LoadWAV_RW + SDL_AudioCVT → 44.1 kHz S16 stereo
       OGG: stb_vorbis_decode_filename (whole file; ~90 ms for a music track)
     writes the voice id into SoundInfo+0
SDL audio callback mixes every active voice (volume, loop, pause)
music: the game polls IsPlaying(track) and starts the next track when it reports false
```

## Who owns what memory

- Objects the engine may `delete` (textures, renderables, managers) are allocated with the
  engine's `operator new` and built with the base constructor. Our extra state sits after the
  base object's size.
- Destructors we provide call the base `D2` destructor, and `operator delete` for the deleting
  variant.
- The engine has use-after-free bugs that the 2008 allocator hid. `tcache_count=0` brings back
  the old behaviour, and `StopSound` never lets the engine erase a map entry it is iterating.

## Repository and game folders

```
megatouch-port/                    main repo (Merit-Megatouch/megatouch-port)
├── Makefile  cabinet.conf  repos.conf  README.md
├── scripts/     setup, new-game, publish, snapshot, launch, lib/, packages/
├── src/         backend/  host/  common/  third_party/
├── tools/       analysis helpers
├── docs/        guides/  reference/  history/  data/
├── games/<name> one submodule per game (Merit-Megatouch/<name>)
├── shared/      (generated) bin/ runtime/ engine-sdk/ cabinet-libs/ data-common/
├── toolchain/   (generated) sysroot/ i386/ apt-i386/ cache/ venv/ ghidra/ g++32
├── build/       (generated) object files
├── reference/   (local) decompiled engine libraries
└── cabinet/     (optional) snapshot of the image

games/<name>/                      game repo
├── game.conf  NOTES.md  README.md  .gitignore             tracked
├── notes/scaffold.md  notes/unresolved.txt                tracked
├── notes/shots/  notes/*.prof  notes/ghproj/  decomp/     ignored
├── lib/        game .so + engine closure (cabinet)        ignored
├── data/       assets + links to shared data              ignored
└── run  runtime  megatouch-host                           ignored symlinks
```
