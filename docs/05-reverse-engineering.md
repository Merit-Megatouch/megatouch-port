# 05 — Reverse engineering the backend

The original `libgame_device_sprite.so`, `libgraphics_sprite.so`, `libinput_sprite.so`,
`libsound_sprite.so` are small (11–35 KB). Decompiling them tells us exactly what a backend
must do. The base-class libraries tell us the rest.

## Method

1. `tools/vtdump.py <sprite lib>` → every virtual slot of each backend class, and which ones it
   overrides (named `...::Sprite::...`) vs inherits (named `Base...`).
2. `tools/vtdump.py <base lib> Base...` → the base vtable, and which slots are
   `__cxa_pure_virtual` (must be provided — including **pure virtual destructors**).
3. Ghidra output (`decomp/*.c`) for each override → field offsets and semantics.
4. Allocation sizes come from `operator new(N)` before each constructor call: derived size, and
   the first field the derived class writes gives the base size.
5. `nm -S` on `vtable for X` gives the vtable byte size → slot count. If derived > base, the
   derived class added virtuals of its own (append them).

**Slot numbering**: a vtable symbol points at `[offset-to-top][typeinfo][slot 0][slot 1]...`.
`vtdump.py` prints raw indices, so *slot = printed index − 2*. In decompiled code a call
`(*(code**)(*obj + 0x48))(obj, ...)` is slot `0x48/4 = 18`.

**Calling convention**: GCC i386 member functions are cdecl with `this` as the first stack
argument. A function returning a class by value takes a hidden result pointer *before* `this`
and pops it (`ret 4`) — let the C++ compiler do that by actually returning the class type.

## Recovered layouts (engine build PG3002-01 V40.02)

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
