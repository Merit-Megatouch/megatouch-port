# 04 — Mapping the runtime: finding the seam

Goal: find the smallest set of code to replace so the original game + engine run without the
cabinet loader.

## Step 1: what does the closure leave unresolved?

```bash
cd lib
for f in *.so; do nm -D --defined-only $f | awk '{print $3}'; done | sed 's/@.*//' | sort -u > def.txt
for f in *.so; do nm -D --undefined-only $f | awk -v f=$f '{print $2" "f}'; done | sed 's/@[^ ]*//' | sort -u > undef.txt
join -v1 <(awk '{print $1}' undef.txt | sort -u) def.txt   # minus libc/libstdc++/libgcc names
```

For the full Trix closure that was **~600 symbols**: Allegro (`blit`, `makecol`,
`masked_blit`, `set_alpha_blender`), the legacy engine (`Sprite::*`, `Bitmap::*`,
`WorldClass::*`, `VideoClass::*`, `MegacGlobals`, `NetGlobals`), data (`PlrScore`, `NVRAMData`,
`FileLoc`, `NetGlob`), ODE physics, OpenGL... all exported by the loader. Far too much to
reimplement.

## Step 2: draw the dependency graph

```bash
for f in *.so; do echo "$f: $(readelf -d $f | grep NEEDED | grep -o '\[.*\]' | tr -d '[]' \
   | grep -vE '^lib(stdc\+\+|m|gcc_s|pthread|c|dl)\.so' | tr '\n' ' ')"; done
```

The key observation for GameDevice games:

```
g_trix.so ─► libgame_device_sprite.so ─► libgraphics_sprite.so ─┐
                                      ├► libinput_sprite.so    ├─► loader's legacy engine
                                      ├► libsound_sprite.so    │   (libmerit2d / libmeritbasegame
                                      └► libmeritbasegame.so ──┘    + Allegro inside loader)
           ─► libgame_device.so, libgraphics.so, libinput.so, libmerit_sound.so, ...  (clean C++)
```

Nothing except the four `*_sprite` libraries depends on `libmerit2d`, `libmerit3d`,
`libmeritbasegame` or `libmerit_threads`.

## Step 3: what crosses the seam?

Which symbols do the *kept* libraries import from the sprite libraries?

```bash
join <(nm -D --undefined-only $KEEP... | sort) <(nm -D --defined-only *_sprite.so | sort)
```

Answer: **one** — `g_trix.so ← GameDevice::CreateNewGameDevice()`.
Everything else (texture loading, drawing, sound, input) happens through virtual calls on
objects whose base classes are in `libgame_device.so`, `libgraphics.so`, `libinput.so`,
`libmerit_sound.so`.

## Step 4: what does the rest still need from the loader?

Rerun step 1 over the kept libraries only. For Trix:

| Symbol | Used by | Our stand-in |
|---|---|---|
| `PlrScore` (data) | g_trix | `int PlrScore[8]` |
| `ef_fp_filename` (data) | libeffect_loader | `char[256]` |
| `Translator::GetInstance/LoadTranslations/Translate` | g_trix, libtranslate_start | real implementation reading the translation tables (**`Translate` and `LoadTranslations` are static** — chapter 10) |
| `ContinueControl::ContinueControl/~/display` | g_trix | `display()` returns true (free play) |
| `HighScoresManager::Instance/HighEnough/Winner` | g_trix | `HighEnough` → false |
| `Logger::SetApplicationID` | g_trix | no-op |

Plus ordinary libc. That's the whole host surface.

## Step 5: how the game starts

From the decompiled `main` → `__EntryPointV12`:

```c
dev = GameDevice::CreateNewGameDevice();
GameConfig cfg; cfg.ParseArgs(argc, argv); cfg.SetWindowWidth(640); cfg.SetWindowHeight(480);
Logger::SetApplicationID(245);
resources::ResourceLocator loc(245);           // 245 = G_TRIX
if (dev->Initialize(cfg, &loc)) {              // vslot: BaseGameDevice::Initialize
    dev->RegisterUpdate(Update);
    Trix::GameClass::GetInstance()->Initialize(dev);
    while (dev->Run()) {}
    ...
}
```

So the launcher only needs to: set up the environment, `dlopen("g_trix.so", RTLD_NOW|RTLD_GLOBAL)`
and call its exported `main`. Global symbol resolution makes the launcher's exported functions
(path shim, loader stand-ins) visible to every library.
