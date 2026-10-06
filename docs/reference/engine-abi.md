# Engine ABI reference

What the 2013 engine (build PG3002-01 V40.02) expects from a backend: the classes, their sizes,
field offsets and vtable slots, and what our backend puts in each slot. Everything here was
recovered from the original `*_sprite` libraries and the base-class libraries. The method is in
[history/05](../history/05-reverse-engineering.md).

## Conventions

- **i386 cdecl.** A member function is a plain function with `this` as the first stack
  argument. Our slot implementations are `static R fn(void* self, args…)`.
- **Return by value.** A function returning a class by value takes a hidden result pointer
  *before* `this` and pops it (`ret 4`). Let the compiler handle it by actually returning the
  class type.
- **Slot numbers.** A vtable symbol points at `[offset-to-top][typeinfo][slot 0][slot 1]…`.
  `vtdump.py` prints raw indices, so slot = printed index − 2. A call
  `(*(code**)(*obj + 0x48))(obj, …)` in decompiled code is slot `0x48 / 4 = 18`.
- **Slot count.** `nm -S` on `vtable for X` gives the size in bytes: size/4 − 2 = slots.
- **Pure virtuals** are `__cxa_pure_virtual` in the base vtable. Each one must be filled,
  **including destructors** (slots 0 and 1), or the engine aborts with "pure virtual method called".
- **C++ library ABI.** GCC 4.1, copy-on-write `std::string`, the old `std::list`. Compile with
  `-D_GLIBCXX_USE_CXX11_ABI=0`. `std::deque`, `std::map` and `std::vector` layouts match the
  modern libstdc++.
- **Static vs member** doesn't change a mangled name, only the calling convention. Before you
  declare a stand-in, count the arguments the caller pushes.

## Objects the backend creates

| Object | Base class (library) | Allocated | Base ctor | Slots we fill | Source |
| --- | --- | --- | --- | --- | --- |
| Game device | `GameDevice::BaseGameDevice` (libgame_device) | 0x118 | `C2Ev` | 0 1 4 7 11 12 28 29 30 | device.cpp |
| Texture | `Graphics::BaseTexture` (libgraphics) | 0x38 + 4 | `C2Ev` | 0 1 16–24 | textures.cpp |
| Resource manager | `Graphics::BaseResourceManager` (libgraphics) | 0x24 | `C2Ev` | 13 14 | textures.cpp |
| Renderable 2D | `Graphics::BaseRenderable2D` (libgraphics) | 0x10 | `C2Ev` | 0 1 3 4 7 | sprites.cpp |
| Object factory | `Graphics::BaseObjectFactory` (libgraphics) | 0x2c | `C2EPNS_5SceneEPN10TextSystem11ITextSystemE` | 48 49 | sprites.cpp |
| Input manager | `Input::BaseInputManager` (libinput) | 0xf0 | `C2Ev` | 0 | input.cpp |
| Sound manager | `Sound::BaseSoundManager` (libmerit_sound) | 0x34 | `C2Ev` | 0–4 17–22 | sound.cpp |
| Net utils | `NetLink::INetUtils` (libgame_device) | 0x10 | `C2Ev` | 2–13 | net.cpp |
| Net link | `NetLink::INetLink` (libgame_device) | 8 | `C2Ev` | 2 3 4 | net.cpp |

Singletons we set: `Graphics::IResourceManager::m_pInstance`,
`Input::IInputManager::m_pCurrentInstance`.

Base functions we call explicitly: `BaseGameDevice::Destroy`, `BaseTexture::Destroy`,
`BaseInputManager::Update`, `BaseSoundManager::Update(float)`, `BaseSoundManager::Destroy`, and
the `D2` destructors of the device, texture, renderable and sound manager.

## Adding a new cloned class

1. `vtdump.py <base lib> <ClassName>`: list slots and mark the pure ones.
2. Find the original backend's override (`vtdump.py` on the cabinet's `*_sprite` library) and
   its decompiled body: that tells you the semantics and field offsets.
3. Allocation size: the `operator new(N)` before the original constructor call.
4. In the backend:
   ```cpp
   static void** g_vt;
   void* thing_new() {
       if (!g_vt) {
           g_vt = clone_vtable("_ZTVN…E");             // copy of the base vtable
           g_vt[0] = (void*)thing_dtor; g_vt[1] = (void*)thing_dtor_delete;
           g_vt[N] = (void*)thing_method;              // every pure slot, at least
       }
       void* o = operator new(SIZE);
       sym<void(*)(void*)>("_ZN…C2Ev")(o);             // engine base constructor
       *(void***)o = g_vt;                             // now it is "our" class
       return o;
   }
   ```
5. Read and write fields with `at<T>(self, offset)` from `backend.h`.

## Class layouts

### GameDevice::BaseGameDevice — size 0x114 (derived sprite device: 0x118)

| Offset | Field |
|---|---|
| +0x04 | registered update callback `bool(*)(float)` |
| +0x08 | `resources::ResourceLocator*` |
| +0x0c | `Messaging::CMessageManager*` |
| +0x10 | object factory (`Graphics::IObjectFactory*`) — **backend creates** |
| +0x14 | `std::vector<Graphics::Scene*>` (Initialize pushes scene 0) |
| +0x20 | input manager — **backend creates** |
| +0x24 | sound manager — **backend creates** |
| +0x28 | net utils — **backend creates** |
| +0x2c | text system (`TextSystem::PangoTextSystem`, created by base) |
| +0x30 | updates per second (from config, default 30) |
| +0x34 | updates per frame limit (default 2) |
| +0x68 | max frames per second (default 30) |
| +0x74 | `GameConfig` copy (0xa0 bytes) |

`BaseGameDevice::Initialize(cfg, locator)` order: store locator, create message manager, push
Scene 0, read timing from cfg, create PangoTextSystem, **call ImplementationInitialize(cfg)**,
copy cfg, set window size on locator, init text system, seed RNG.

`Run()`: fixed-timestep loop — `Update(dt)` up to N times, then `Render(t)`, then `usleep`.
`Update(dt)` calls `ImplementationUpdate(dt)` (its return value = keep running), input `Update()`,
sound `Update(dt)`, net `Update(dt)`, then broadcasts `GD_OUT_UPDATE_MSG`.
`Render(t)` calls `ImplementationRender(t)`.

Vtable slots (all 31):

| Slot | Method | Backend? |
|---|---|---|
| 0/1 | dtor (complete/deleting) | yes |
| 2 | Initialize(GameConfig&, ResourceLocator*) | base |
| 3 | Run() | base |
| 4 | Destroy() | **pure** |
| 5 | RegisterUpdate | base |
| 7 | ShowCursor(bool) | **pure** |
| 11 | CreateNetLink() → `NetLink::INetLink*` | yes (**must not return null**) |
| 12 | SaveScreenToFile(const std::string&) | yes |
| 28 | ImplementationInitialize(GameConfig&) → bool | **pure** |
| 29 | ImplementationUpdate(float) → bool | **pure** |
| 30 | ImplementationRender(float) | **pure** |

Other slots (Get*Manager, PushScene, GetFPS...) are base.

### Graphics::BaseTexture — size 0x38 (sprite texture: 0x4c)

| Offset | Field |
|---|---|
| +0x0c | `TextureInfo { int bpp; int byteSize; unsigned w; unsigned h; }` |
| +0x20 | `std::string` image file |
| +0x24 | `Core::Rect` source rect (x,y,w,h at +0x28..+0x34) |

Slots: 0/1 dtor · 16 GetTextureType (→2) · 17 MakeHardwareTexture · 18 MakeSoftwareTexture ·
19 ImplementationBind · 20 ImplementationUnbind · **21 ImplementationLock → pixel ptr** ·
**22 ImplementationUnlock** · **23 FillTextureInfo(TextureInfo&)** · 24 Destroy (call base
`BaseTexture::Destroy` after). Slots 16–23 are pure. The sprite class adds two `LoadImage`
virtuals (25, 26) that only its own resource manager calls.

Lock/Unlock matters: the text system renders strings by locking a texture and writing pixels.

### Graphics::BaseRenderable2D — size 0x10 (sprite: 0x68)

| Offset | Field |
|---|---|
| +0x04 | `std::string` image resource name |
| +0x08 | `IResourceManager*` (set by base Draw) |
| +0x0c | `bool` visible |

Slots: **0/1 dtor (pure!)** · 2 Draw (base: resolves the ImageResource, checks frame bounds,
calls slot 7) · 3 SetImageResource(const std::string&) · 4 SetVisible(bool) ·
**7 ImplementationDraw(src, dst, clip, frame, color, angleDeg, pivot, ImageResource*)** (pure).

ImplementationDraw arguments, from `Sprite2D::Render`:
* `src` = `Rect(0, 0, ±w, ±h)` — **negative w = mirror horizontally, negative h = flip vertically**
* `dst` = screen rect (position − origin, size × scale)
* `clip` = optional `Rect*`
* `frame` = animation frame index → `ImageResource::GetFrame(frame)` → `Frame{textureIndex, Rect}`
* `color` = `Core::Color{r,g,b,a}` floats (tint + alpha)
* `angle` in degrees, rotation about `dst.xy + pivot`

### Graphics::BaseObjectFactory — size 0x2c

Constructor `(Scene*, TextSystem::ITextSystem*)`. Slots: 48 **ImplementNewRenderable2D** (pure)
→ return a new renderable; 49 ImplementXMLParsing (sprite-added, return 0).

### Graphics::BaseResourceManager — size 0x24

+0x20 = manager type (sprite sets 2). Singleton pointer:
`Graphics::IResourceManager::m_pInstance`. Slots: 3 SetResourceLocator ·
**13 ImplementationLoadTexture(const std::string& file, ImageResource*)** →
create texture, `AddFrame` once per animation frame, `AddTexture`; return 1 ·
**14 ImplementationLoadTexture(uchar* pixels, int w, int h, TEXTURE_PIXEL_FORMAT)** → texture.
Pixel formats: 0/1 → 16 bpp (RGB565), 2 → 24 bpp, 3 → 32 bpp ARGB.

Original file handling: `.png` → 32-bit, `.tga` → 32-bit, names containing `_DSK`/`_DYN` →
streamed from disk, anything else (e.g. `.spr`) → the loader's `WorldClass::LoadBmp`.

### Input::BaseInputManager — size 0xd8 (sprite: 0xf0)

| Offset | Field |
|---|---|
| +0x94 | `std::deque<PendingKey{int state; int key;}>` |
| +0xc4 / +0xc8 | mouse x / y (float) |

Slot 0 = Update(): push `{2,0}` while the button is held, `{1,0}` when up, set mouse pos, then
call `BaseInputManager::Update()` (derives press/release events). Singleton:
`Input::IInputManager::m_pCurrentInstance`. Old and new libstdc++ `deque` layouts match.

### Sound::BaseSoundManager — size 0x30 (sprite: 0x34)

Slots: **0/1 dtor (pure!)** · 2 Initialize · 3 Update(float) (call base) · 4 Destroy (call
base) · 5 SetResourceLocator · 17 ImplementationPreloadSound · 18 ImplementationPlaySound ·
19 ImplementationStopSound · 20 ImplementationSetSoundVolume · 21 ImplementationIsPlaying ·
22 ImplementationPauseSound (17–22 pure).

`SoundInfo`: +0x00 handle (int, −1 = none, **write it in Play**), +0x04 `std::string` short
name, +0x08 `std::string` resolved full path, +0x0c bool loop, +0x10 float volume,
+0x18 bool paused. `BaseSoundManager` keeps a `std::map<int, SoundInfo>`;
`IsPlaying(name)` → map lookup → slot 21.

### NetLink::INetUtils (size 4) and NetLink::INetLink (size 8)

INetUtils slots 2–13 all pure: Initialize, Update(float), SyncTimers, GetTimer (float),
GetNumUnitsLinked, WaitForAllMachinesToRespond, CheckStateOfMachine, ForceDisconnect,
GetMyLinkId, GetMasterLinkId, GetAllLinkedIds (`std::list<int>` by value), GetHeadIndices.
INetLink (+4 = NetMessageManager*): slots 2 Broadcast(IMessage*, uchar), 3 ForceDisconnect,
4 NetClick(uchar). `Messaging::NetMessageManager`'s constructor calls `CreateNetLink()` and
registers on the result — a null link crashes.

### Core types (libcore)

`Core::Rect { vptr; float x, y, w, h; }` (20 bytes), `Core::Color { vptr; float r, g, b, a; }`,
`Core::Vector2D { vptr; float x, y; }`, `Graphics::Frame { int textureIndex; Core::Rect rect; }`.

## Loader symbols the engine libraries need

These come from the cabinet loader, not a library. The host defines them; see
[loader-services.md](loader-services.md) for each one.

| Symbol | Kind | First needed by |
| --- | --- | --- |
| `PlrScore` | `int[8]` data | g_trix (every GameDevice game uses it) |
| `ef_fp_filename` | `char[256]` data | libeffect_loader |
| `Translator::*` | static functions | g_trix, libtranslate_start |
| `ContinueControl::*` | class | g_trix |
| `HighScoresManager::*` | singleton | g_trix, g_word_dojo_2 and most of the [survey](gamedevice-survey.md) |
| `Locale::LanguageManager::*` | singleton | g_word_dojo_2 |
| `Logger::SetApplicationID` | static | g_trix |
| `ustrlen ugetat ustrcmp ustrupr` | Allegro UTF-8 | g_word_dojo_2 |
