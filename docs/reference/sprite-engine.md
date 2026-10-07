# Sprite engine ABI (legacy loader 2D C++ layer)

The C++ sprite engine that legacy games import from the loader: `Sprite`, `Group`, `List`,
`EventO`, `String`, `WorldClass`/`DOSLinuxWorld`, `SpriteSignal`, `Bitmap`, `NetSprite`, and the
classes around them. Everything here comes from the **game libraries only**: dynamic symbols,
relocations inside game vtables and typeinfo, `operator new` sizes, and inline field accesses
(objdump plus the Ghidra decompiles). The loader binary was not used. It complements
[legacy.md](legacy.md), which covers the C API (`_RADBitmap`, `Animation*`, `Mouse*`).

Conventions:

- **Addresses** are ELF addresses, as objdump prints them. Ghidra decompiles add 0x10000; where
  a decompile address is quoted it says "decomp".
- **Confidence:** **[C]** confirmed from code at several sites, **[L]** likely, **[?]** guess or
  unresolved.
- **i386, g++ 3.4/4.x, Itanium ABI.** Member functions are cdecl with `this` first on the stack.
  A vtable symbol points at `[offset-to-top=0][typeinfo*][slot 0]…`, and the object's vptr is
  `&_ZTV… + 8`.
- **Ghidra garbles calls to imported thiscall methods.** It shows `this` as a typed first
  argument (`Sprite::SetCord((float)this, …)`) and often drops the last argument. Every argument
  order in this document was checked against disassembly.
- Usage counts ("N games") are the number of the 143 legacy games (`PRELOAD=libmerit_legacy.so`)
  whose own `.so` files import the symbol. That includes `libmerit2d.so` and
  `libmeritbasegame.so`, which ship with the "Merit2d" games (spotmatch, trivia, zenword…).

Method used: scratchpad scripts dumped every `_ZTV*` defined in every game `.so`, resolving
`R_386_32` and `R_386_RELATIVE` relocations to symbols (`vt.py`). They also dumped every derived
`_ZTI*` to get the inheritance, scanned `push $N; call operator new; … call X::X` for sizes, and
extracted call arguments from the disassembly (simulated pushes, with PIC string resolution).

---

## 1. Class hierarchy and sizes

```
Group (0xc, polymorphic)
 ├── Sprite (sizeof 0xac, dsize 0xa9)
 │    ├── NetSprite (0x2c8)          + virtual NetClick(uchar)
 │    │    ├── NetSpriteLock (0x2d0)
 │    │    └── NetTimer (0x330) [L: has NetClick slot]
 │    ├── SObj (~0x208) [L]          legacy "AllocSprite" sprites
 │    └── SharedTimer (0xb8) [L]
 ├── List (0x18)                     ListT<T> has the same layout
 ├── EventO (sizeof 0x18, dsize 0x15)
 ├── SpriteSignal (0x1c)
 ├── SpriteSigHand (0xc)
 └── WorldClass? (0x208) [?]  -> DOSLinuxWorld (0x208, no new fields)
Bitmap (0x94, polymorphic, vtable = dtors only)
LockableClass (polymorphic, vtable = dtors only)
String (0x68)   BmpFont : FontBase (0x798, polymorphic)
```

| Class | sizeof | Evidence |
|---|---|---|
| `Sprite` | **0xac** | 2646 `new(0xac)` → `Sprite::Sprite` sites. Derived classes' first own field is at 0xac (bigroller `Quintzee_Animation` ctor 0xad2c; switcheroo `LinkSprite` 0x5726). Derived classes also put byte members at 0xa9–0xab (see §3). |
| `Group` | **0xc** | Game-inline `ListT<T> : Group` writes its own fields at 0xc/0x10/0x14 right after `Group(this,0,0)` (ginrummy 0x11bc0). `SpriteSigHand` (Group plus no fields) is new'd at 0xc (breakout 0xdf6e). |
| `List` | **0x18** | 411 `new(0x18)` → `List::List`. |
| `EventO` | **0x18** | `QuitEvent`, `RemoveHelp`, `ButtonClickEvent` (no own fields) are new'd at 0x18. quikmatch `seFlashStrings` keeps a bool at +0x15, so the engine may only use up to 0x14. |
| `SpriteSignal` | **0x1c** | breakout stack object `local_2c[28]`. |
| `SpriteSigHand` | **0xc** | breakout. |
| `String` | **0x68** | 1160 sites. |
| `Bitmap` | **0x94** | 2914 sites. Subclasses start at +0x94 (strippoker `MyBitmap` 0xb7d8, battle31 `AnimClass` 0x7278, elevenup `TargaFontBitmap`). |
| `WorldClass`, `DOSLinuxWorld` | **0x208** | 59 games `new(0x208)` + `DOSLinuxWorld()`. 5 games (euchre, hearts, spades, snubble, psolitaire) use `new(0x208)` + `WorldClass::WorldClass()` [C1]. |
| `NetSprite` | **0x2c8** | meteorshower `PulseSyncClass`/`TargetZoneClass` and battlegroup `TileInfo` start their fields at 0x2c8. |
| `NetSpriteLock` | **0x2d0** | battlegroup `NetEvent`/`AnimationInfo` start at 0x2d0. |
| `NetTimer` | 0x330 | `new(0x330)` (coco_loco, qshot). |
| `SharedTimer` | 0xb8 | 8 sites. |
| `SObj` | ~0x208 [L] | snubble `Ball : SObj` starts at 0x208 (0x9bbb). |
| `BmpFont` | 0x798 | 90 sites. |
| `FlatlandObject` | 0x12c | 26 sites (not analysed). |
| `LinuxWorldClass` | 0x228 | breakout, motormatch, racepoker: `LinuxWorldClass(bool)`. A different world class, not covered here. |

**Typeinfo.** Games import `typeinfo for Sprite` (35 games), `EventO` (47), `NetSprite` (26),
`Group` (9), `Bitmap` (7), `NetSpriteLock`, `SpriteSigHand`, `NetTimer`, `SObj` and
`LockableClass`, and reference them from their own `__si_class_type_info` objects. Three games
use `__vmi_class_type_info` with a private base `Sprite` or `NetSprite` at offset 0. No game
`dynamic_cast`s to an engine class. The engine must still export these `_ZTI*` symbols with the
single-inheritance chain above (Sprite → Group, and so on). `typeinfo for WorldClass` and
`typeinfo for DOSLinuxWorld` are never imported.

### Tail padding (critical)

g++ follows the Itanium rule that a derived class may place its members in a non-POD base's
**tail padding**. Games rely on this:

- **Sprite: dsize must be 0xa9.** Many derived classes put `char`/`bool` members at
  **0xa9, 0xaa, 0xab**:
  - switcheroo `RecTimer` 0x53d4 (0xa9)
  - draggle `SpriteButton` (0xa9, ~35 writes, 0x1fcae)
  - cardbandits `SCard` (0xa9/0xaa, 0xe168)
  - motormatch `NewCardClass` (0xa9/0xaa, 0x65be)
  - meteorshower `WeaponButtonClass` (0xaa)
  - ephunt_new `HelpButton` (0xab)
  - magiccharms `Tile` (0xa9–0xab)

  None of them uses 0xa8. So the last data member of `Sprite` is a 1-byte field at +0xa8, and
  **the engine must never write bytes 0xa9–0xab of a Sprite.** In a C++ reimplementation that
  happens automatically if Sprite's last member is a `char`/`bool` at 0xa8, all other members end
  at or before 0xa8, and the class stays non-POD (it is polymorphic).
- **EventO: dsize ≤ 0x15.** quikmatch `seFlashStrings` puts a bool at +0x15 (0x8662). EventO's
  last engine member is a byte at +0x14.

---

## 2. Vtables

Derived from 283 Sprite-derived, 77 EventO-derived and 63 Group-derived vtables defined in game
libraries. Each slot is either the game's override or a relocation to the imported base method.
No engine-derived vtable in any game contains `__cxa_pure_virtual`.

### Group: 6 slots [C]

| slot | vt off | method |
|---|---|---|
| 0 | +0x00 | `Group::~Group()` complete (D1) |
| 1 | +0x04 | `Group::~Group()` deleting (D0) |
| 2 | +0x08 | `int Group::Signal(SpriteSignal*)` |
| 3 | +0x0c | `int Group::Signal(Group::SpriteSignalType)` |
| 4 | +0x10 | `int Group::Message(SpriteSignal*)` |
| 5 | +0x14 | `int Group::Message(Group::SpriteSignalType)` |

Evidence: every Group-derived vtable (bowling `ListT<GLObject>`, `TextureManager::Tex`,
breakout/motormatch `LinuxGameClass`) has these slots at [2]..[7] of the symbol. 62 of 63 point
slots 2–5 at the imported Group methods. Return type is int/bool: breakout's override
`BreakOutClass::Signal(SpriteSignalType)` at 0x83d0 builds `SpriteSignal s(type,0,0,0)` and
returns `this->vt[2](&s)`. Expect the engine's type-only overloads to forward the same way [L].

### Sprite: 11 slots [C]

| slot | vt off | method | overridden by games |
|---|---|---|---|
| 0 | +0x00 | `Sprite::~Sprite()` D1 | always (dtor) |
| 1 | +0x04 | `Sprite::~Sprite()` D0 | always |
| 2 | +0x08 | `Sprite::Signal(SpriteSignal*)` | never |
| 3 | +0x0c | `Sprite::Signal(Group::SpriteSignalType)` | never |
| 4 | +0x10 | `Sprite::Message(SpriteSignal*)` | never |
| 5 | +0x14 | `Sprite::Message(Group::SpriteSignalType)` | never |
| 6 | +0x18 | `int Sprite::SpriteClick()` | 182 of 283 |
| 7 | +0x1c | `int Sprite::DoNextFrame()` | never (except `SObj::DoNextFrame` in snubble) |
| 8 | +0x20 | `int Sprite::DoDraw()` | 11 of 283, plus `SObj::DoDraw` |
| 9 | +0x24 | `int Sprite::DoCommand(int)` | never |
| 10 | +0x28 | `void Sprite::Reset()` | 72 of 283 |

The slot count is exact. 200 derived vtables are 13 words (2 header words + 11 slots) and add no
virtuals. Longer ones (14, 15 or 20 words) add their own virtuals after slot 10: `NetClick`,
rowelink dialogs' `Init`/`SetInx`/…

Override conventions:
- `SpriteClick` overrides return 1 (handled).
- `DoDraw` overrides do their own work, then tail-call `Sprite::DoDraw()` and return its result
  (switcheroo `LinkSprite::DoDraw` 0x5802). switcheroo `RecTimer::DoDraw` returns 1 without
  drawing while `!ClicksEnabled`.
- `Reset` overrides never call the base.

All 11 slots are imported by about 58 games (`Sprite::DoCommand`, `Signal`/`Message`,
`DoNextFrame`, `DoDraw`, `Reset`: 56–59 games each). The engine must export them as real
functions.

### EventO: 8 slots [C]

| slot | vt off | method |
|---|---|---|
| 0, 1 | +0x00/+0x04 | `EventO::~EventO()` D1, D0 |
| 2–5 | +0x08..+0x14 | `Group::Signal`/`Message` (inherited, not overridden) |
| 6 | +0x18 | `int EventO::DoIt(Sprite*, int)`: overridden by 76 of 77; the engine base is imported once (tic_tac_trivia), so it is not pure |
| 7 | +0x1c | `int EventO::GetType()`: never overridden (all 77 use the engine's) |

### SpriteSigHand: 7 slots [C]

Group's 6 slots, then slot 6 (+0x18) `int DoIt(Sprite*, SpriteSignal*)`. Games always override
it, so it is probably pure in the base. Games breakout, motormatch and racepoker import the ctor
`SpriteSigHand::SpriteSigHand()` [C2] **and** `_ZTV13SpriteSigHand` (their inline dtor resets
the vptr), so both must be exported.

### SpriteSignal: 6 slots [C]

Group's 6 slots with SpriteSignal's own D1/D0. The vtable, typeinfo and inline dtor are
**emitted weakly inside the games** (breakout 0x18220, motormatch 0x13200). The engine's class
must add no virtuals, and its dtor must just run `~Group`.

### NetSprite family: 12 slots [C]

Sprite's 11 slots, then slot 11 (+0x2c) `void NetClick(unsigned char from)`.
`NetSprite::NetClick` is never imported, and every one of 40 derived vtables overrides it, so it
is probably pure. `NetTimer::NetClick(unsigned char)` is a real engine function (coco_loco/qshot
`QShotLocalTimer` use it). `NetSpriteLock` has the same 12 slots.

### Bitmap and LockableClass: 2 slots [C]

The vtable is only `{~X() D1, ~X() D0}`: every game subclass vtable (`MyBitmap`,
`TargaFontBitmap`, `AnimClass`, `DbStateMgr`) is 4 words. So the vptr is at +0, and games delete
bitmaps with `(*vptr[1])(bmp)` (bgammon, chess `make_animation`).

### DOSLinuxWorld / WorldClass [L]

Games never define a world vtable. They import `vtable for DOSLinuxWorld` (58 games), because
the `DOSLinuxWorld()` ctor is inline in the game header: it calls `WorldClass::WorldClass()` [C2]
and then sets `vptr = &_ZTV13DOSLinuxWorld + 8` (funkymonkey 0x9846). A scan of every game
library finds only these virtual calls on the world object:

| slot | vt off | use | sites |
|---|---|---|---|
| 0 | +0x00 | D1 (implied) | – |
| 1 | +0x04 | deleting dtor: `if (w) w->vt[1](w)` at shutdown | ~65 |
| 2–5 | +0x08..+0x14 | never called. Possibly Group's Signal/Message if WorldClass derives from Group [?] | 0 |
| 6 | +0x18 | `void ?(int 0, int flag)`: **init/open**, called immediately after construction. flag = 1 in 54 games, 0 in 11 (quikmatch, switcheroo, …) | 69 |
| 7 | +0x1c | `void ?()`: **shutdown/close**, called just before the delete | 70 |

No imported, named WorldClass method corresponds to slot 6 or 7. A reimplementation needs at
least 8 slots, with 0, 1, 6 and 7 at those positions. The 5 games that construct a plain
`WorldClass` [C1] make the same vt[6]/vt[7] calls, so **`_ZTV10WorldClass` (set by the
WorldClass ctor) must have the same slots as DOSLinuxWorld**, and plain WorldClass must not be
abstract.

---

## 3. Object layouts

### Sprite (0xac, dsize 0xa9)

| off | type | meaning | conf | evidence |
|---|---|---|---|---|
| +0x00 | vptr | §2 | C | |
| +0x04 | `List*` | **Child-sprite list** (a Group field). NULL until the game sets it to `new List(0)` and calls `List::Push(spr->+4, child)`. Children are positioned relative to the parent. | C | ginrummy `KnockButton` ctor 0x10366; boxdrop `BoxDrop_ColumnClass` ctor (decomp 0x1e12a); libmerit2d `Sprite2d::CreateChild` 0x2f490; stairs `CardNode::GetShadow` 0x5266 iterates it with `ListT<Sprite>` |
| +0x08 | ? | Group field; games never touch it (probably parent/owner) | ? | |
| +0x0c | float | x | C | everywhere (bigroller `Quintzee_Object::Translate`) |
| +0x10 | float | y | C | |
| +0x14 | float | width, unscaled | C | libmerit2d `Sprite2d::GetWidth` 0x2cbe0 (`+0x14 * (+0x24/65536)`); switcheroo hit test 0x6142; `Move(-w, …)` (switcheroo 0x552d) |
| +0x18 | float | height, unscaled | C | `GetHeight` 0x2cc30; switcheroo `ImageRoller::Restore` |
| +0x1c, +0x20 | ? | never touched by games | | |
| +0x24 | int, 16.16 | x scale (0x10000 = 1.0) | C | libmerit2d `GetWidth`; ginrummy resets it to 0x10000 |
| +0x28 | int, 16.16 | y scale | C | |
| +0x2c | float | rotation angle | L | libmerit2d `Sprite2d::Rotate` 0x2e020 |
| +0x30 | u32 | **flags**, initialised from the ctor's 2nd arg | C | bit table below |
| +0x34..+0x44 | ? | never touched (ephunt_new writes byte +0x46 = 1 in `HintGlassButton::ClearEvents` 0x9298; meaning unknown) | | |
| +0x48 | float | **z, the draw priority** (larger draws later; values 1..24950) | C | 3rd float of SetCord; switcheroo `RecTimer` SetCords z=1028 and then reads +0x48 back as z for Move (0x5472); libmerit2d `SetDrawPriority` |
| +0x4c..+0x58 | ? | engine-private | | |
| +0x5c | u8 | **transparency** (0 opaque, 255 invisible) | C | libmerit2d `GetAlpha` 0x2cb50 (= 255 − value); magiccharms writes 0xff, then FadeIn (0xc0f8); event DoIts write it and then set dirty |
| +0x60 | `Bitmap*` | **current bitmap**; updated immediately by `SetBmp(b, 0)` | C | ginrummy `KnockButton::SpriteClick` compares it; switcheroo `LinkSprite::DoDraw` reads `bmp->+0x2c` |
| +0x64 | `String*` | assigned text (`AssignString`). In String, +0xc is `char* text` and +0x50 is `int8` line spacing (read by libmerit2d) | L | libmerit2d `Sprite2d::GetText` 0x2cbb0, `SetInterLineSpacing` 0x2cb00 |
| +0x68 | `char*` | **name** (`AssignName`), matched with strcmp | C | battlegroup `SHIP::SetPowerMeter` 0x9c84; stairs 0x5277; battlegroup matches names in the world's sprite set |
| +0x6c..+0x78 | ? | engine-private | | |
| +0x7c | `List*` (engine-allocated) | **click-event list**: EventOs fired with reason codes when the sprite is clicked | C | libmerit2d `Button2d::Create` pushes `ButtonClickEvent`; battlegroup `TileInfo::SpawnTile` 0x8c6c (Clear, then Push) |
| +0x80 | `List*` (engine-allocated) | **per-frame event list**: EventOs that stay in it and run every frame (timers, drag handlers) | L | ginrummy `MoveTimer`, `localScoreDisplay::Count`; switcheroo `ColourOverlay` (`DragableSpriteEvent`) |
| +0x84 | ? | probably another list | ? | |
| +0x88 | `List*` (engine-allocated) | **timed event queue**: the EventOs behind Move/Fade/RunAnim/delayed setters, added by `AddEvent`. `List::Clear(spr->+0x88, 8)` stops all animation (542 `Clear(…,8)` calls, mostly here). | C | bigroller `Quintzee_Object::ResetEvents` 0x7caa; quikmatch `UpdatePairNum` 0x870b (`Push(spr+0x88, ev)`) |
| +0x8c | `List*` | **signal-handler list** (`SpriteSigHand`s) on the root sprite | L | breakout (decomp l.3122): `List::Push(root->+0x8c, hand)`. Note that breakout uses LinuxWorldClass. |
| +0x90..+0x9c | ? | engine-private | | |
| +0xa0 | int | `SpriteSignalType` this sprite raises (on click?) | L | breakout sets 0x16 after ctor flags 0x900 (0xe4b7); motormatch `CardHand::SetPos(…, SpriteSignalType)` stores it here (0x6578) |
| +0xa4..+0xa8 | engine-private | the last member is a **1-byte field at +0xa8**; 0xa9–0xab belong to derived classes | C | §1 tail padding |

**Flags at +0x30.** The ctor stores its `flags` argument here. ttunes masks with `andl $0x400`
directly after `Sprite(…,2,…)`.

| bit | meaning | conf | evidence |
|---|---|---|---|
| 0x0002 | ctor default (1457 of ~2900 ctor calls). Children pushed into a parent's +4 list use 0. Guess: "member of the world's top-level draw list". | ? | libmerit2d `CreateChild` passes 0; `CreateEmpty` passes 0x10002 |
| 0x0008 | **delete pending**: the next `NextFrame`/`PlayMovie` unlinks and deletes the sprite. Games never `delete` a sprite themselves; the idiom is `spr->flags |= 8; world->NextFrame(0,1,1,0,0,0); p = 0;`. Derived dtors also set it before `~Sprite`. | C | 1315 `orl $0x8`; tic_tac_trivia 0x757c; boxdrop 0x67d2; battlegroup `SHIP_PLACEMENT` dtor |
| 0x0010 | **dirty / re-render**: set after changing alpha, the bitmap's pixels (after `Bitmap::Compress`) or the text | C | switcheroo `LinkSprite::DoDraw`; libmerit2d Draw* wrappers; battlegroup `LoopFadeEvent::DoIt` |
| 0x0040 | probably "sprite owns its bitmap" (frees it on delete). Set after `SetBmp(LoadBmp(…))`; ctor value 0x42 for disk animations | ? | feedingfrosty `Level::Shutdown` `|= 0x48` |
| 0x0080 | **disabled/hidden**: cleared by `Enable`, set by `Disable` | C | libmerit2d `GetActive` 0x2cb70 = `!(flags & 0x80)`; quikmatch/boxdrop test `(char)flags < 0` |
| 0x0100 | **clickable**: gets `SpriteClick`. Games toggle it to arm buttons (`orl` ×145, `andl ~` ×99) | C | switcheroo `LinkSprite` ctor `|= 0x100` |
| 0x0400 | ? | ? | ttunes |
| 0x4000 / 0x8000 | **anchor at centre**, x / y | C | libmerit2d `SetAnchorCenter` 0x2cb20 (`|= 0xc000`); ctor value 0xc002 is common |
| 0x0200, 0x0800, 0x1000, 0x10000 | ? (seen in ctor values 0x202, 0x802, 0x902, 0x1002, 0x10002) | ? | |

### Group (0xc)

| off | type | meaning |
|---|---|---|
| +0 | vptr | |
| +4 | `List*` | child list (see Sprite +4) [C] |
| +8 | ? | never accessed by games; probably the parent/owner from the 2nd ctor argument [?] |

### List (0x18) and ListT\<T\> [C]

`List : Group`. The layout below comes from ginrummy's own inline copies of the header template
`ListT<T>`: `ListT(ListObj*)` 0x11bc0, `Clear` 0x119a4, `SetList` 0xf8c0, `Advance` 0xf81a,
`~ListT` 0x11c94.

| off | type | meaning |
|---|---|---|
| +0x0c | `Group*` / `T*` | **current element** (data of the current node); 0 at the end |
| +0x10 | `ListObj*` | current node / header |
| +0x14 | u8 | owns the header (delete it in the dtor or in SetList) |

Nodes use the C list API that games also import directly (`ListObj`, `ListObjHeader`,
`LinkIntoList`, `RemoveFromList`, `IsInList`):

- **ListObj node** (polymorphic, `new(0x18)`): +0xc data, +0x10 prev (0 means this is the
  header), +0x14 next (0 on the tail sentinel).
- **ListObjHeader** (`new(0x1c)`): +0xc tail sentinel, +0x10 = 0, +0x14 first node (equal to the
  tail when the list is empty). The tail's +0xc and +0x10 point back to the header.

**Inline iteration idiom** (magiccharms `CalcMultiplier` 0x824f, `RemoveLines` 0x8346; stairs
0x5270, 0xe09c):

```cpp
List it(srcList);
while (it.cur /* +0xc */) { use(it.cur); it.Advance(1); }
// ~List
```

### EventO (0x18, dsize 0x15) [C]

| off | type | meaning | evidence |
|---|---|---|---|
| +0 | vptr | | |
| +4, +8 | | Group fields | |
| +0x0c | u32 | **start time** = `world->now(+0x170) + delay`, written by derived ctors | battlegroup `LoopFadeEvent`/`AssignStringEvent`/`GameEvents` ctors; boxdrop `seSetBmpAndFlag` (decomp 0x1dea0) |
| +0x10 | u32 | **end time**. 0xefffffff means forever. start == end for a one-shot | boxdrop `seBlinkRecycleColumn`; quikmatch `seFlashStrings` 0x8662 |
| +0x14 | u8 | engine-owned [L]; bytes 0x15–0x17 belong to derived classes | |

### SpriteSignal (0x1c)

Ctor: `SpriteSignal(Group::SpriteSignalType, Sprite*, Sprite*, Gash*)`. breakout uses C1;
motormatch and racepoker call C2 from `RPSignal : SpriteSignal` as `(this,0,0,0,0)` (0x1092e).

| off | type | meaning |
|---|---|---|
| +0x0c | `Group::SpriteSignalType` | type [C] (breakout `BreakOutClass::Signal(SpriteSignal*)` switches on it at 0xa254) |
| +0x10 | `Sprite*` | 2nd ctor arg, source [L] (`RacePokerSigHand::DoIt` 0x10988 copies +0xc and +0x10) |
| +0x14 | `Sprite*` | 3rd ctor arg [?] |
| +0x18 | `Gash*` | 4th ctor arg [L]. Gash is the named-sprite script/layout system (`Gash::ParseSpriteAttribs(Sprite*, Gash::GashFlags)`, `Gash::LocateSprite`) |

### NetSprite (0x2c8) and NetSpriteLock (0x2d0)

| off | type | meaning | evidence |
|---|---|---|---|
| 0..0xab | | Sprite | |
| +0xac.. | u8[~0x100] | **received packet payload**, read in `NetClick` | meteorshower `SyncKillerClass::NetClick` 0xfac0; battlegroup `NetEvent::NetClick` reads +0xac/+0xad |
| +0x1ac | u8 | **outgoing packet length**, set by the game before `SendPacket` (0x1c, 8, 0x10, 2) | meteorshower 0xd37a |
| +0x1b0 | `u8*` | **outgoing payload buffer**, allocated by the engine; games write through it | same |
| +0x1b4..0x2c7 | ? | never accessed | |

NetSpriteLock adds 8 bytes (0x2c8..0x2cf), which games never touch.

### WorldClass / DOSLinuxWorld (0x208)

The world pointer lives at **`MegacGlobals::GetInstance() + 0x207c`**. Games save the loader's
world, install their own, and restore it on exit (§6).

| off | type | meaning | conf | evidence |
|---|---|---|---|---|
| +0x00 | vptr | §2 | C | |
| +0x54 | `ListObj*` | legacy SObj list (first node at +0x14, node +0xc data, +0x14 next) | L | euchre 0x5cfe walks it and calls `DeleteSpriteAllLists` |
| +0x5c | `std::set<Sprite*>*` | **sprite registry**. Uses the libstdc++ `_Rb_tree` layout: begin = `*(set+0xc)`, end = `set+4`, value at node+0x10. **Games instantiate the template code themselves**, so the layout must match g++ 3.4/4.x libstdc++. | C | battlegroup decomp ~4548 and `GAME_AREA::Exit` iterate it and match `Sprite+0x68` names; kid_phunt 0x4699/0x47f9 `insert(world->+0x5c, spr->Copy())` |
| +0x60 | ptr | euchre compares it with list-node fields (0x619f, 0xd471). In LinuxWorldClass games (breakout, motormatch) it is the **root Sprite\***, and new top-level sprites are pushed into `root->+4`. Unconfirmed for WorldClass. | ? | |
| +0x168 | short | every game writes 0 right after vt[6] init (65 sites) | ? | funkymonkey `__EntryPointV12` |
| +0x170 | u32 | **world clock, ms**, sampled once per frame; read-only for games (300+ reads). EventO start/end times and `Sprite::LastEventTime()` use this clock. | L | bigroller `FT_TimeOutHandler` 0x5df0 is a copy of a `SystemTimer()`-based handler with `world+0x170` substituted; boxdrop `seBlinkRecycleColumn` adds 500 to it |
| +0x17c | int | **frames per second** (read-only for games) | C | boxdrop `WaitOnFrame(1000/fps)` 0x664b; stairs `50*fps` |
| +0x18c | int | set to 0 at startup (switcheroo 0x8a6f, kids_color 0x4264) | ? | |
| +0x190 | u8 | load-time conversion toggle, default 1 (opsetup sets 0 around a 32-bit LoadBmp) | ? | opsetup 0x7ea98 |
| +0x19c | int | used by switcheroo as "input frozen / game over"; checked before clicks are handled | ? | switcheroo 0x5a4a, 0x5298 |
| +0x1a0 | u8 | set to 0 or 1 at startup by feedingfrosty 0x8cb4 and texasholdem 0xbca0 | ? | |

### Bitmap (0x94, vptr at +0)

| off | type | meaning | evidence |
|---|---|---|---|
| +0x00 | vptr | `{D1, D0}` | |
| +0x0a/+0x0c/+0x0e | short ×3 | colour effect r,g,b (offsets, see §5.4) | header-inline `Bitmap::SetColorEffect(short,short,short,ulong)` compiled into bgammon 0xfb9a; it also sets +0x54 = (u8)arg4 |
| +0x20 | u16 | colour depth (16/32), probably from the ctor's 4th arg | hoopjones `BmpToBmp` 0x2fc5 |
| +0x22 | u8 flags | 0x02 = **opaque** (use `blit`, not `masked_blit`); 0x80 = streamed **delta (.dlt) animation** | libmerit2d `BitmapManager::BlitToBackBuffer` 0x3b610, `LoadBitmap` 0x3b9d0; quikmatch 0x69aa and cepixmix set it; boxxi clears it |
| +0x2c | `BITMAP*` (Allegro) | **the real Allegro bitmap (16bpp, or 32 after DeCompress32)**. NULL while the bitmap is "compressed". Games test `if (!+0x2c) DeCompress()` and then use it directly: inline `GetPixel` = `_getpixel16(+0x2c,x,y)` (switcheroo 0x9772), inline `SetPixel` = `_putpixel16(…makecol16)` (powertrivia 0xd260), and `blit`/`masked_blit`/`clear_to_color`/`rectfill`/`hline` (quickcell, meteorshower, boxdrop, battlegroup). libmerit2d calls `bmp->vtable->clear_to_color` and checks `vtable->color_depth`. | C |
| +0x30/+0x32 | short ×2 | scale x,y | inline `Bitmap::SetScale(int,int)` (bgammon 0xfb80) |
| +0x34 | u8 | scale enabled | same |
| +0x40 | u16 | Smack animation frame count | nine 0x4ad4 |
| +0x44 | `_RADBitmap*` | RAD/Smack source image consumed by `ConvertSmack`/`DisplaySmack`. **Games write it** (they store `AnimationLoadToBitmap()` here, then zero it) | cepixmix 0x8420; strippoker/wildapes `DeckEntity::LoadCardsFromFLC` |
| +0x48 | int | compressed data size | chess `make_animation` 0x6c4c fwrites +0x48/+0x4c/+0x50 and then `CData()` |
| +0x4c | int | **width** | 228+ sites, e.g. `Bitmap(o->+0x4c, o->+0x50, …)`, centring as `320 - w/2` |
| +0x50 | int | **height** | |
| +0x54 | u8 | draw-effect mode applied by `Display`: 0 none, 2, 4 (set by SetColorEffect), 8, 9. Set just before Display and reset after | boxxi 0xd8da, 0x109aa; celookout; bgammon |

Never touched inline: +0x04–0x08, +0x10–0x1e, +0x24–0x28, +0x38–0x3c, +0x58–0x90.

**Frames** form a doubly linked chain of Bitmap objects. `NextAnim()`/`PrevAnim()` return the
neighbour or NULL. `NextAnim(Bitmap*)`/`PrevAnim(Bitmap*)` set the links (euchre 0xd661 builds a
21-frame chain). The link fields are never accessed inline, so any free offset in
+0x58..+0x90 works.

### String (0x68)

Games never read String fields inline and never delete a String; `Sprite::AssignString` takes
ownership. libmerit2d reads +0x0c (`char* text`) and +0x50 (s8 line spacing) through the
sprite's +0x64.

---

## 4. Global data and free functions

| symbol | games | type and use |
|---|---|---|
| `Sprite::ClicksEnabled` | 49 | **1-byte** bool, accessed through the GOT: `ClicksEnabled = 0/1`, `if ((char)ClicksEnabled)`. A global gate for click dispatch. Games clear it during transitions and animations and set it again afterwards (boxdrop entry, switcheroo `ImageRoller::IsDirty`, libmerit2d `Game::EnableAllSpriteClicks` 0x346a0). Define it as `bool` (padding to 4 bytes is fine). |
| `NetGlob` | 51 | `NetGlobals*`. **NULL when no link is active**; games always null-check it. `NetGlob->+0x40` = u32 shared random seed (meteorshower `Randomize(*(NetGlob+0x40))` when linked). Standalone: export it as NULL. |
| `GetMyId()` (`_Z7GetMyIdv`) | 62 | free function returning `unsigned char`, the cabinet's network node id, compared with `GetMaster()`. Standalone: return the same value as `GetMaster()` (we are the master). |
| `NetGlobals::*` | 10–28 | all non-static with `this = NetGlob`: `ProcNetClicks()` (poll and dispatch packets to `NetClick`; called in busy loops), `GetTrueLinkCount()`, `GetHeadIdx(uchar)`, `GetRank(uchar id)` (1 = winner), `GetLeaderScore()`, `GetScore(int id)`, `GetState(int)`, `GetHeadBmp(uchar,bool)`, `NetSpriteUnRegister(NetSprite*, uchar 0)` (called from NetSprite-derived dtors when NetGlob is set). Games only call them when NetGlob != NULL. |
| `MegacGlobals::GetInstance()` | 128 | +0x207c world pointer; +0x2038 GameId; +0x22dc last key (0x1b = ESC); +0x30 link mode (see legacy.md) |

---

## 5. Method semantics

Ranked roughly by use. All signatures are demangled. A trailing `unsigned long delay` on a
setter is a **start delay in ms relative to the world clock**: 0 = immediate (applied before the
call returns), non-zero = queued as an event on `+0x88`.

Animated methods end in `(unsigned long duration, unsigned long delay, unsigned char smooth)`.
`smooth` is 0..3: libmerit2d `ConvertSmoothType` 0x2cad0 maps its SmoothType 1/2/3 to 1/2/3 and
everything else to 0, so 0 is presumably linear. 0x10 is sometimes OR'ed in (boxdrop), so the
high bits may be flags [?].

### 5.1 WorldClass

| signature | games | semantics |
|---|---|---|
| `WorldClass::WorldClass()` | 58 (C2), 5 (C1) | Construct an empty world: lists, the sprite set at +0x5c, clock, fps at +0x17c. Then vt[6](0, flag). |
| `Bitmap* WorldClass::LoadBmp(char const* name, int lang, float scale, int fmt, int maxFrames, bool b6, bool b7, Bitmap* into)` | 73 | Almost always `(name, 0|1, 1.0f, 3, 999999, true, false, NULL)` (2083 calls). `name` has no extension and maps to `<name>.spr(.gz)` in the game's graphics directory (boxdrop "boxes/red_0000"). One .spr holds all frames. `lang` 1 (885 calls) = search the language directory first, then the root. `fmt` 3 = default 16-bit (10 = 32-bit, opsetup). `maxFrames` 999999 = all (libmerit2d 9999; opsetup 1). `b7` corresponds to Merit2d `FORCE_NO_TRANSPARENCY` [L]. Returns the first frame of a frame chain, or NULL. |
| `Bitmap* WorldClass::LoadAdBmp(char const*, int, float, int, int, bool)` | 22 | LoadBmp variant chosen by a Merit2d flag (`(name,0,1.0,3,maxFrames,b)`); difference unknown [?] |
| `void WorldClass::DeleteBmp(Bitmap*, unsigned char)` | 61 | Free a LoadBmp result (the whole chain); the 2nd arg is always 0 (747 calls). |
| `void WorldClass::PlayMovie(int frames, unsigned long fps, void (*cb)(unsigned char), unsigned char cbArg, bool, bool, bool, bool)` | 68 | **Run the world for `frames` frames**, throttled: events, DoNextFrame, DoDraw, reaping, click dispatch, present. Typical call `(1, 0, NULL, 0, true, false, false, false)`, once per game-loop iteration (685 calls with 1). `PlayMovie(N)` = wait N frames. `fps` 0 = world default (euchre 30; Merit2d `Game+0x144`). `cb(cbArg)` is called each frame (euchre). The return value is unused. |
| `void WorldClass::NextFrame(unsigned char, unsigned char, bool, bool, bool, bool)` | 49 | One unthrottled engine frame, almost always `(0, 1, true, false, false, false)` (snubble `(1,1,1,0,0,0)`). **Must reap sprites flagged 8 before returning** (kill idiom §3). Throttled callers pair it with `WaitOnFrame(1000/fps, false)`. The uchar meanings are unknown. |
| `void WorldClass::WaitOnFrame(unsigned long ms, bool)` | 15 | Sleep until `ms` have passed since the last frame (1000/fps, 30, 33). The bool is 0. |
| `void WorldClass::SetBack(char const* name, int lang)` | 53 | Load and set the background ("bkg", "back", "intro/intro_uk"). |
| `void WorldClass::SetBack(Bitmap*, int)` | 13 | Use a loaded bitmap as the background. Callers follow it with `RefreshArea(0,0,640,480)`, so it does not repaint by itself. |
| `void WorldClass::SetBack(short vb)` | 17 | Capture video buffer `vb` (`*(short*)MegacGraphics::GetInstance()`, after drawing into it via `VideoClass::OpenVB` + `Bitmap::Display`) as the background (boxdrop 0x9d24). |
| `void WorldClass::RefreshArea(int, int, int, int)` | 25 | Mark a region for background redraw. Only ever `(0,0,640,480)`, so (x,y,w,h) vs (x1,y1,x2,y2) is unresolved; accept both. |
| `void WorldClass::ShowAll()` | 36 | Present the composed frame now (often right after NextFrame). |
| `void WorldClass::ClearScreen()` | 9 | |
| `Bitmap* WorldClass::GetFrame(Bitmap* base, int n)` | 36 | Frame n of the chain, clamped (`-99999` = first frame: cardbandits 0xedfe). |
| `int WorldClass::AnimationFrames(Bitmap*)` | 28 | Chain length. |
| `int WorldClass::FrameNumber(Bitmap*)` | 13 | Index of this frame in its chain (quikmatch timer: `== 40` time up). |
| `int WorldClass::FrameCount(char const* name)` | 1 | Frames in a .spr without loading it (texasholdem 0x116ea). |
| `void WorldClass::PlayDeltaAnim(char* name, int x, int y, unsigned long fps, unsigned long, char* sound, unsigned short soundFlags, unsigned char, bool)` | 27 | Blocking one-shot .dlt animation with a sound (boxdrop 0xc688 `("30000/30k_0000", 0x86, 0x98, 30, 0, "30000", 0x440, 1, 0)`). The 5th arg is 0 or 100 [?]. |
| `Bitmap* WorldClass::CopyCompBitmap(Bitmap* src, int×9 matrix, int frames, bool)` | 16 | New bitmap = colour-matrix copy (values /255; 0xff diagonal = copy, 0x55×9 = grey). Grouped per input channel [L]. `frames` 9999999 = all, or 1; the bool is 1. |
| `SObj* WorldClass::AllocSprite(Bitmap*, unsigned short flags)` / `void DeleteSpriteAllLists(SObj*, unsigned char)` | 4 | Legacy SObj API (euchre, hearts, spades, snubble). |
| `void WorldClass::AssignString(SObj*, char const*, int x, int y, int z, BmpFont*, unsigned short w, unsigned short h, int, signed char, short, short, short, unsigned char, unsigned char)` (+`AssignString1` with an `int*`, `AssignString2` with two) | 4 | Bitmap-font text onto an SObj. The three shorts are colour offsets [L]. |
| `void WorldClass::SpriteDrawTarget(Bitmap*)` | 1 | Render sprites into a bitmap; NULL restores the screen. |
| `void WorldClass::ResetTimers()` | 1 | meritthon, between sub-games. |

### 5.2 Sprite

| signature | games | semantics |
|---|---|---|
| `Sprite::Sprite(Bitmap* bmp, unsigned long flags, Sprite* parent)` | 63 (C1), 35 (C2) | `bmp` may be NULL (click zone or text sprite). `flags` → +0x30 (values 2, 0, 0x42, 0x40, 0xc002, 0x10002, 0x102, 0x142, 0x100, 0x900…). `parent` is NULL in ~2850 of 2860 calls; breakout passes the root sprite with 0x900. The sprite registers itself with the current world (`*(G+0x207c)`); games never add it anywhere else. |
| `Sprite::~Sprite()` | 35 | Derived dtors set flag 8 (and often Disable) first. |
| `void Sprite::SetCord(float x, float y, float z, unsigned long delay)` | 63 | z → +0x48 (draw priority). Delays 200/750 seen (boxdrop 0x5877). |
| `void Sprite::SetSize(int w, int h)` | 64 | Sets +0x14/+0x18 (hit-zone or text-box size for bitmapless sprites). |
| `void Sprite::Enable(unsigned long delay)` | 63 | Clear 0x80. |
| `void Sprite::Disable(unsigned long delay, bool b)` | 63 | Set 0x80. `b` is usually 0; it is 1 when hiding children immediately (quikmatch 0x5218). Meaning of `b` unknown [?]. |
| `void Sprite::SetBmp(Bitmap*, unsigned long delay)` | 59 | +0x60 (NULL clears it). Also `ChangeBmp`, `SetBmpCord(Bitmap*,int,int,int,ulong)`, `MoveChangeBmp(…)` (rare). |
| `void Sprite::AssignString(String*, unsigned long delay)` | 59 | Attach text (+0x64), taking ownership. Text is laid out in the sprite's SetSize box. |
| `void Sprite::ChangeString(char const*, unsigned long delay)` | 36 | Replace the assigned String's text. |
| `void Sprite::AssignName(char*)` | 5 | +0x68. |
| `int Sprite::Signal(SpriteSignal*)` etc. | 59 | Virtual (§2). Games never call them non-virtually and only rarely virtually. Guess: Signal goes up to the handler lists (root +0x8c → `SpriteSigHand::DoIt`), Message goes down into the +4 children [?]. |
| `int Sprite::SpriteClick()` | 42 | Base behaviour [L]: fire the click events on +0x7c and raise +0xa0 as a signal. Returns 1 if handled. |
| `int Sprite::DoNextFrame()` / `int Sprite::DoDraw()` / `int Sprite::DoCommand(int)` / `void Sprite::Reset()` | 56–59 | Per-frame update / draw / scripted command / reset. Called by the world. |
| `void Sprite::Move(int x0,int y0,int z0,int x1,int y1,int z1, unsigned long duration, unsigned long delay, unsigned char smooth)` (and an all-float variant) | 47 / 30 | Linear move P0 → P1 (z included). E.g. boxdrop `(6,43,12000, −194,43,12000, 500, 0, 0x10)`. |
| `void Sprite::MoveTo(int|float x, y, z, unsigned long duration, unsigned long delay, unsigned char smooth)` | 20 | Move from the current position. |
| `void Sprite::Curve(int×12, unsigned long duration, unsigned long delay, unsigned char smooth)` | 34 | Bézier through 4 xyz points: (start, start-control, end, end-control) per ginrummy ~0x3893. Exact order [?]. |
| `void Sprite::FadeIn(unsigned long delay, unsigned long duration, int from, int to)` / `FadeOut(…)` | 45 / 31 | **The delay comes first here.** from/to are transparency values; −1 = default (FadeIn → 0, FadeOut → 255). libmerit2d `Fade(a,b,c,d)` → `FadeIn(a, b, 255−c, 255−d)`. |
| `void Sprite::SetAlpha(int transparency, unsigned long delay)` | 36 | +0x5c (255 = invisible). |
| `void Sprite::RunAnim(Bitmap* anim, int first, int last, int loops, unsigned long delay, float speed, unsigned long, int)` | 34 | Animate through the frame chain (bigroller `Quintzee_Animation::Play` 0x7d1e passes `(anim, first, last, count, delay, speed, 0, −1)`). last 99999/999999 = to the end. loops 65535/999999/1000000 ≈ forever. speed 1.0 = normal. The last two args (always 0, −1) are unknown. |
| `void Sprite::RunDiskAnimThenDelete(char const* name, unsigned long delay, float speed, unsigned long, bool)` | 47 | Load animation `name`, play it, then delete the sprite. The caller does `new Sprite(0, 0x42, 0)` and SetCord first. Values seen: `(name,0,1.0,0,0)`, `(name,2000,1.0,1000000,0)`. 4th arg = loops or hold [?]. |
| `void Sprite::DeleteAfterLast(unsigned long delay)` | 24 | Delete when the last queued event + delay has passed. |
| `unsigned long Sprite::LastEventTime()` | 19 | Absolute world time (+0x170 clock) when the last queued event ends. Games chain with `LastEventTime() − now + 1`. |
| `void Sprite::KillAllMovementEvents()` / `KillAllEnableEvents()` | 20 | Remove those event kinds from +0x88. |
| `void Sprite::AddEvent(EventO*)` | 9 | Push onto +0x88. |
| `void Sprite::PlaySound(char* wav, int flags, unsigned long delay)` | 39 | Flags are always 0x440, as for `PlayPreWave`. |
| `void Sprite::ColorIt(int×9 matrix, unsigned long delay)` / `ColoredOff(unsigned long delay)` | 29 / 28 | 3×3 colour matrix, 255 = 1.0 (highlight `255,50,50,50,255,50,50,50,255`; grey 64×9). The flash idiom is ColorIt, then `ColoredOff(100)`. |
| `void Sprite::SetRGB(int,int,int,unsigned long)` / `SetTextRGB(…)` / `FadeRGB(int×6, ulong dur, ulong delay, uchar smooth)` | 21 / 20 / 20 | Colour offsets (−255..0 = darken towards black, §5.4 convention). |
| `void Sprite::CreateTextDropShadow(float dx, float dy, unsigned long delay, int r, int g, int b)` | 32 | Always `(2..5, 2..5, 0, −240,−240,−240)`: a dark shadow. |
| `void Sprite::EffectOutline(int width, unsigned char, unsigned char, unsigned char, bool)` | 27 | `(2,1,1,1,1)`, `(3,0,0,0,0)`. Exact meaning [?]. |
| `Scale(float from, float to, ulong dur, ulong delay, uchar)`, `ScaleXY(float×4, ulong, ulong, uchar)`, `ScaleXY(float sx, float sy, ulong delay)`, `HardScale(int w, int h)` | 8–20 | +0x24/+0x28 (HardScale resamples the bitmap). |
| `void Sprite::Rotate(float, float, Sprite::RotationDirection, unsigned long, unsigned long, unsigned char)` | 20 | RotationDirection 0/1; angle at +0x2c. |
| `EffectImplode/EffectExplode(int,int,int dur,ulong delay)`, `EffectBalloon(int −1, ulong dur, ulong delay)`, `EffectSlideFadeIn(int dir,int dist,int dur,int delay)`, `EffectSquishChangeBmp(Bitmap*, int axis, int frames, ulong, ulong delay, int, int, bool)` | 21–23 | Canned effects. |
| `Sprite** Sprite::PixelCollide(float, int)` | 20 | NULL-terminated array of overlapping sprites (libmerit2d 0x2ddb0 passes 0.0). |
| `void Sprite::EnableAllClicks(unsigned long)` / `DisableAllClicks(unsigned long)` | 11 | Set/clear 0x100 on the sprite and its children [L]. |
| `Sprite* Sprite::Copy()`, `ProcessAssignedString()`, `DoChain(List*,int)`, `ChangeClipping(int×8,ulong,ulong)`, `SetHQScaling(bool)` | 1–7 | Not analysed. |

### 5.3 Group, List, EventO, signals

| signature | games | semantics |
|---|---|---|
| `Group::Group(List*, Group*)` | 9 | Always called as `(this, 0, 0)` by games. |
| `List::List(List* src)` | 60 | `src == 0` gives a new, empty, **owning** list. Otherwise it gives a non-owning **iterator** positioned on src's first element (cur = +0xc). |
| `List::List(ListObj*)`, `void List::SetList(ListObj*)` | 3–4 | Attach to a raw ListObj chain (step past the header); release any owned header. |
| `void List::Push(Group*)` | 59 | Append (= `LinkInto(x, 1)`). |
| `void List::LinkInto(Group*, int pos)` | 4 | 1 = append; 2 also seen (front or sorted?) [?]. |
| `void List::Clear(unsigned char flags)` | 55 | Unlink all. **8 is the dominant flag** (542 calls, mostly on `spr->+0x88`): treat it as "delete the elements too" [L]. Bit 2 deletes the elements via D0 in the inline template [C]. 0 = unlink only. 0x88 is also seen. |
| `bool List::IsEmpty()`, `int Count()`, `bool Contains(Group*)` | 37/3/2 | |
| `bool List::Advance(int n)` | 7 | n steps along next. Sets cur; returns 0 and cur = 0 at the end. |
| `List::Backup(int)` (always 1), `Group* Pop()`, `RemoveFrom(Group*)` (21), `Remove(Group*)`, `UnLinkAndAdvance()`, `Dump()`, `~List()` (deletes the header if owned) | | |
| `ListT<Sprite>` / `ListT<EventO>`: `ListT(List*)`, `~ListT()`, `Advance(int)`, `UnLinkAndAdvance()`, `Set(List*)` | 4 | Same layout as List, typed cur. |
| `EventO::EventO()` / `~EventO()` | 47 | The derived ctor then fills +0xc start and +0x10 end (§3). |
| `int EventO::DoIt(Sprite* owner, int reason)` (virtual) | | The world calls it for events on the owner's lists. `reason`: 0 = timer fired. For click lists, libmerit2d `ButtonClickEvent::DoIt` 0x34410 handles 0 → OnClick, 2 → OnClickRelease, 3 → OnRelease. Every game DoIt returns 1 [? keep/handled]. quikmatch calls `vt[6](ev, spr, 0)` itself for zero-delay events and then deletes the event. |
| `int EventO::GetType()` | 47 | Never overridden. |
| `SpriteSigHand::SpriteSigHand()` | 3 | Games push the handler onto `root->+0x8c`; the engine calls `DoIt(sprite, sig)`. |
| `SpriteSignal::SpriteSignal(Group::SpriteSignalType, Sprite*, Sprite*, Gash*)` | 3 | §3. |

**`Group::SpriteSignalType` values.** No engine enum names can be recovered; the values appear to
be game-chosen. breakout handles 0x0a and 0x12–0x17 and raises 0x16 from a sprite (+0xa0).
motormatch passes values through `CardHand::SetPos`.

### 5.4 Bitmap, String, fonts

**Colour convention (important).** Text and tint colours are **offsets from white**: channel =
255 + v. So (0,0,0) is white, (0,0,−255) is yellow and (−240,−240,−240) is near-black. Proof:
libmerit2d `Sprite2d::SetTextRGB` 0x2e710 and `CreateAsText` pass `round(c*255 − 255)`. The
convention applies to String, `CreateColoredSmackTextBox`, `String::ChangeColor`,
`SetTextRGB`/`SetRGB`, `CreateTextDropShadow`, `SetColorEffect`, and C `BitmapColorize`.

| signature | games | semantics |
|---|---|---|
| `Bitmap::Bitmap(int w, int h, unsigned char a3, unsigned char bpp)` | 113 | bpp = 16 in 2942 of 2944 sites. a3 is 1 mostly before loads and 0 before grabs; meaning [?]. |
| `void Bitmap::Display(int x, int y, int f)` | 86 | Draw to the current video buffer, honouring +0x22 opaque, +0x54 effect, scale. f = 1 almost always [?]. |
| `void Bitmap::DisplayRegion(int sx,int sy,int w,int h,int dx,int dy,int 1)`, `DisplayZ(int x,int y,int z,uchar 0)` | 7/5 | |
| `void Bitmap::CopyToBitmap(Bitmap* dst, int sx, int sy, int w, int h, int dx, int dy)` | 66 | `this` is the source (switcheroo `ImageRoller::Restore`/`Dump` 0x5888/0x5934). |
| `void Bitmap::CopyFromCurrent(int x, int y)` | 74 | Grab its own size from the current video buffer (usually after SetDim). Also `CopyFromScreen(int,int,bool 1)` (41) and `CopyFromVB(int,int,int vb −1)` (11). |
| `void Bitmap::SetDim(int w, int h)` | 58 | (Re)allocate; usually called on `Bitmap(0,0,…)`. |
| `bool LoadPCX(char*, int −1, int 8, uchar 0)` (54), `LoadJPEG(char*, int −1, int, uchar)` (34), `LoadData(char*, uchar 0, int −1)`, `LoadTGA(char*, int)`, `LoadTGA_32(char*)`, `LoadCompressedData(char*, FILE*)`, `LoadPNG`, `LoadRawPNG` | | File loaders. −1 probably means "default directory/language" [?]. |
| `void Compress(bool, int, unsigned short*)` (43), `DeCompress()` (37), `DeCompress32()` (27) | | Always `Compress(1,3,0)`. DeCompress (re)creates +0x2c. libmerit2d always does DeCompress → modify → Compress(1,3,0) → sprite dirty. **Simplest compatible design: keep +0x2c always valid, so Compress and DeCompress do nothing.** |
| `unsigned short* CData(int frame)`, `setCData(unsigned short*)`, `freeCData()`, `TTFtoCData(uchar*, uchar*, int, int)`, `FreeMemory()` | 4–20 | Compressed-data access. |
| `Bitmap* NextAnim()` (42), `PrevAnim()`, `NextAnim(Bitmap*)`, `PrevAnim(Bitmap*)` | | Frame chain. |
| `void CreateSmackTextBox(char const* text, FontBase* font, signed char just, unsigned short maxW, unsigned short maxH, bool)` | 58 | Render with a bitmap font, word-wrapping at maxW, and **resize the bitmap to fit the text** (quikmatch 0x69a5: maxW = 0.8·screen w). just −1 or 0. |
| `void CreateColoredSmackTextBox(char const*, FontBase*, signed char, unsigned short maxW, unsigned short maxH, short r, short g, short b, unsigned char 0, bool)` | 48 | Same with colour offsets. maxW 0 = one line, auto width (battle31 0x3c8b re-renders at 620 if too wide). |
| `void CreateColoredTextBoxTTF[S](char const* text, int x, int y, int w, int h, int r, int g, int b, int size, int align, bool 1, char* fontName, int spacing)` | 32/47 | TrueType text **into the existing bitmap**, inside the box. Font is "bureau" (181 sites), "olea" or "Castanea"; size 0x14–0x78; align 2 = centred (1/4 likely left/right); spacing usually −5. What "S" means is unknown. |
| `GreyBmp()` (44), `Clear()` (25), `Colorize(int,int,int)`, `ReplaceTrans(bool)`, `Scale(int,int,uchar)`, `ScaleInto(Bitmap*,int,int,uchar)`, `Crop(Bitmap*,uchar)` | | Effects. |
| `DrawPoint(x,y,r,g,b)`, `DrawLine(x1,y1,x2,y2,r,g,b)`, `DrawRectangle(x,y,w?,h?,r,g,b)`, `DrawCircle(x,y,rad,r,g,b,bool)`, `DrawEllipse(x,y,rx,ry,r,g,b)`, `DrawArc(x,y,float,float,rad,r,g,b)`, `DrawTriangle(6,rgb)`, `DrawSpline(8,rgb)` | 20 | Absolute RGB 0–255, drawn on +0x2c. |
| `SmackAnimationLoad(char*, uchar 0)`, `JumpTo(int)`, `Display(int x,int y,uchar 0|1|8)`, `Rewind()`, `Unload()`, `Advance(uchar)`, `ChangeFRate(int)`, `LoadSmack(char*, uchar, uchar)`, `ConvertSmack(uchar)` (converts +0x44), `DisplaySmack(ushort,ushort,uchar)` | 13–34 | Smack/RAD animation (nine uses it as a sprite sheet). |
| `bool DeltaOpen(char*)`, `bool DeltaAdvance(bool decode, int*)` | 20 | Streamed .dlt animation; DeltaAdvance returns false at the end. Sets +0x22 bit 0x80. |
| `String::String(char const* fontName, int size, char const* text, int just, signed char spacing, int r, int g, int b, unsigned char, unsigned char, unsigned short style)` | 59 | Decoded from libmerit2d `Sprite2d::CreateAsText` 0x30750, whose enums have names. **just**: 1 left, 2 right, 4 centre, plus 0x80 for top-aligned (else vertically middle); 0xc seen. **r,g,b**: offsets from white. **style**: 0x08 bold (most games), 0x10 italic, 0x20 underline, 0x100 no wrap, 0x200 char wrap, 0x400 word wrap, 0x800 exact fit. The two uchars are 0. |
| `String::String(BmpFont*, char const*, int just, signed char, int r, int g, int b, uchar, uchar)` | 1 | wordzap. |
| `String::ChangeColor(int r, int g, int b, unsigned char)`, `String* String::Copy()` | 4/2 | |

**FontBase/BmpFont.** Games create it as `new BmpFont(short w, short h, 0xd6)` (w and h 0 or
0x1c). Its virtual slots, from calls in quikmatch, bgammon, strippoker, tennis, royal and
funkymonkey:

| vt off | call |
|---|---|
| +0x08 | `(0, r, g, b)` colour |
| +0x18 | `Load(char const* font, char const* glyphs|NULL, flag)` (quikmatch 0x6946 `("serpb31", text, 0)`) |
| +0x24 | `(2, 0)` justification |
| +0x2c | `DrawText(text, x, y, w, 0, 0, 0, 1)` |
| +0x30 | `_RADBitmap* CreateTextBox(text, w, h, −1, 0, 0)`; tennis and royal read `+4` (width) of the result |
| +0x4c | `()` after Load |
| +0x60 | `(n)` spacing |
| +0x68 | `()` on discard (deleting dtor? then D1 is at +0x64) [?] |

---

## 6. The frame loop

**Lifecycle**, identical in about 65 games (boxdrop `__EntryPointV12` 0xd542, quikmatch 0x64a3,
switcheroo, funkymonkey 0x72dc, chess, moondrop, ginrummy, battlegroup, bigroller):

```cpp
WorldClass* old = G->world;              // G = MegacGlobals::GetInstance(), world at +0x207c
G->world = new DOSLinuxWorld;            // operator new(0x208); WorldClass() then vptr
G->world->vt[6](0, 1);                   // init (flag 0 in 11 games)
*(short*)(G->world + 0x168) = 0;
G->world->NextFrame(0, 1, true, false, false, false);   // funkymonkey: prime one frame
... load bitmaps (LoadBmp), SetBack("bkg"), create sprites ...
PlayOneGame();                           // game loop below
G->world->vt[7]();                       // shutdown
if (G->world) G->world->vt[1]();         // delete
G->world = old;
```

**Game loop.** The game owns the loop and calls `PlayMovie(1, 0, NULL, 0, true, …)` once per
iteration. Inside PlayMovie the engine:

1. advances the clock (`+0x170`, ms);
2. runs each sprite's due events (`+0x88` timed queue: start ≤ now ≤ end, removed after end;
   `+0x80` per-frame events);
3. calls `DoNextFrame` (movement, animation frames) and `DoDraw` in z order (+0x48), with
   children (+4) relative to their parent;
4. deletes sprites flagged `+0x30 & 8`;
5. dispatches touches: if `Sprite::ClicksEnabled`, the topmost enabled (`!0x80`), clickable
   (`0x100`) sprite under the touch gets the virtual `SpriteClick()` and its `+0x7c` click
   events (`DoIt(spr, 0|2|3)`);
6. presents the frame, throttled to fps (+0x17c).

Most game logic is reactive. `SpriteClick` overrides and `EventO::DoIt` set game flags, and the
outer loop polls them.

- **quikmatch** `QuikMatchClass::PlayOneGame` (decomp 0x1bae2): `do PlayMovie(1); while
  (!keypressed() && !flag && state == 0);` then `FrameNumber(timer->bmp)` for hurry-up/time-up,
  `SystemClass::CheckKey()`, and `G+0x22dc == 0x1b` (ESC) to quit. In linked play it calls
  `Card_PollLinks` when `G+0x30 == 1`.
- **boxdrop** `PlayOneGame`: a `while(true)` state machine ending in `if (G+0x22dc == 0x1b)
  exit; PlayMovie(1);`. Waits are `PlayMovie(N)`. The destructor flushes with 15 ×
  (`NextFrame` + `WaitOnFrame(1000/fps)`).
- **switcheroo** `__EntryPointV12`: `for (;;) { while (screen->state == 0) PlayMovie(1);
  PlayMovie(0x20); …continue screen… }`. All gameplay happens in `SpriteClick` and
  `DragableSpriteEvent::DoIt` (in `+0x80`), which hit-tests the touch against the sprite's
  x/y/w/h.
- **Merit2d games** (`Merit2d::Game::Run`, libmerit2d 0x34bf0, about 20 games): `loop { if quit
  → vt[0x1c](), return; vt[0x18]() /*Update*/; Render(); PlayMovie(world, 1, Game+0x144 /*30*/,
  0, 0, true, …); vt[0x20](); }`.
- **Networking.** When `NetGlob != NULL`, games and libmeritbasegame call
  `NetGlob->ProcNetClicks()` every update. That routes packets to `NetSprite::NetClick(from)`.
  Standalone `NetGlob` is NULL, so none of this runs.

**Killing a sprite:** `spr->+0x30 |= 8;` (optionally `List::Clear(spr->+0x88, 8)` first), then
`world->NextFrame(0,1,1,0,0,0)`. Games never call `delete` on a Sprite.

---

## 7. Notes for the reimplementation

- Export as real symbols, with the exact mangled names, every method in §5 plus all vtable
  entries in §2: `Sprite::Signal`/`Message`/`SpriteClick`/`DoNextFrame`/`DoDraw`/`DoCommand`/
  `Reset`, `Group::Signal`/`Message`, `EventO::DoIt`/`GetType`, and `NetTimer::NetClick`.
- Export these typeinfos: `_ZTI5Group`, `_ZTI6Sprite`, `_ZTI6EventO`, `_ZTI6Bitmap`,
  `_ZTI9NetSprite`, `_ZTI13NetSpriteLock`, `_ZTI13SpriteSigHand`, `_ZTI8NetTimer`, `_ZTI4SObj`,
  `_ZTI13LockableClass`. Export these vtables: `_ZTV13DOSLinuxWorld`, `_ZTV13SpriteSigHand`
  (plus the ones the C1/C2 ctors install).
- Ctor variants: Sprite C1+C2, EventO C1+C2, Group C2, WorldClass C1+C2, SpriteSignal C1+C2,
  NetSprite C2, NetTimer C1+C2, SpriteSigHand C2. Destructor variants: D2 for Sprite, Group,
  EventO and NetSprite, D1 for Sprite.
- Sprite: `static_assert(sizeof(Sprite) == 0xac)`, and verify dsize 0xa9 with a test subclass
  whose `char` member must land at 0xa9. The same check applies to EventO (`bool` at 0x15).
- Engine-allocated sprite lists at +0x7c, +0x80 and +0x88 must be valid `List*` right after
  construction (games `List::Clear`/`Push` on them without checking). +4 must start NULL (games
  test it and create it lazily).
- Bitmap: keep a valid liballeg `BITMAP*` (16bpp, real `vtable`, `line[]`) at +0x2c at all
  times. Width/height go at +0x4c/+0x50. Honour +0x22, +0x54, +0x0a..0e and +0x30..34 in
  `Display`, and accept a `_RADBitmap*` written to +0x44.

### What's wrong in the current `src/legacy/legacy.cpp` `class Bitmap`

1. **+0x2c is never set.** Games need a real Allegro `BITMAP*` there: inline
   `_getpixel16`/`_putpixel16` and direct `blit`/`masked_blit`/`clear_to_color`/`rectfill`
   calls. Keeping pixels only in `BmpData` behind +0x88/+0x8c works only while no game touches
   them inline. (+0x88/+0x8c are never accessed by games, so the side-table pointer may stay.)
2. **Game-written fields are ignored:** +0x22 (opaque/delta), +0x20 depth, +0x0a..0e and +0x54
   (effects in Display), +0x30..0x34 scale, +0x40 frame count, +0x44 (`_RADBitmap*` handed to
   ConvertSmack/DisplaySmack), +0x48 compressed size.
3. **No frame chain** (`NextAnim`/`PrevAnim` getters and setters). LoadBmp results are chains,
   used by GetFrame, AnimationFrames, FrameNumber and RunAnim.
4. **Colour handling is wrong.** `CreateColoredSmackTextBox` multiplies (r,g,b) as a tint;
   colours are 255 + v offsets. The text should wrap at maxW and the bitmap should be resized to
   the text. `BitmapTextTTF` ignores colour.
5. **Font slots.** BmpFont slot +0x18 is `Load(font, glyphs, flag)`, not "string width". Slot
   +0x30 must return a usable `_RADBitmap*` (tennis and royal read its width).
6. **Missing methods** (declared by games, absent in the class): CreateSmackTextBox, TTF/TTFS,
   Compress/DeCompress/DeCompress32, GreyBmp, NextAnim/PrevAnim, CopyFromScreen/CopyFromVB,
   LoadJPEG/LoadTGA, Draw*, Delta*, CData, FreeMemory, Scale/Crop/Colorize,
   SmackAnimationAdvance, ConvertSmack, LoadSmack, DisplayZ, Clear.

---

## 8. Open questions

- **Engine-private ranges:** Sprite +0x08, +0x1c/+0x20, +0x34–0x44, +0x4c–0x58, +0x6c–0x78,
  +0x84, +0x90–0xa8 (frame index and RunAnim state must be in here); Group +8; EventO +0x14.
  Games never touch them, so any internal layout works.
- **World:** the names and exact semantics of vt slots 6/7; whether WorldClass derives from Group
  (slots 2–5); fields +0x60 (root sprite?), +0x168, +0x18c, +0x190, +0x19c, +0x1a0; whether
  every sprite is inserted into the `+0x5c` set automatically (kid_phunt also inserts copies by
  hand); the clock unit at +0x170 (ms per bigroller; a frame count is less likely).
- **Sprite flags:** the meaning of 0x02, 0x40 (owns bitmap?), 0x200, 0x400, 0x800, 0x1000,
  0x10000; the Sprite ctor's `parent` argument; the `Disable` bool; the last two RunAnim
  arguments; RunDiskAnimThenDelete's 4th argument; EffectOutline's arguments.
- **List:** `Clear` flag semantics (8 vs 2 vs 0x88); `LinkInto` pos 2.
- **Signals:** Signal vs Message routing; no named SpriteSignalType values. The +0xa0 and +0x8c
  evidence is from LinuxWorldClass games (breakout, motormatch, racepoker).
- **Calls:** RefreshArea encoding; LoadBmp `b6`/`b7`/`fmt`; the NextFrame uchar arguments;
  `Bitmap::Bitmap` 3rd argument; `Display`'s 3rd argument; TTF vs TTFS.
- **Not covered:** `LinuxWorldClass` (0x228) and `FlatlandObject` (0x12c).
