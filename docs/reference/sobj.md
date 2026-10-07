# SObj ABI (legacy "AllocSprite" sprite API)

The older sprite API that four legacy games use instead of (or alongside) `Sprite`: **euchre**,
**hearts**, **spades** and **snubble**. It covers `SObj`, the `WorldClass` methods that create,
label and delete SObjs, and the two C list helpers that only these games import (`ClearList`,
`PushListObjForward`). None of it is implemented in `src/legacy/libmerit_legacy.so` yet.

Everything here comes from the four game libraries only (`games/<g>/lib/<g>.so`): dynamic
symbols, the one SObj-derived class a game defines (snubble `Ball`), `operator new` sizes, call
arguments, and inline field accesses (objdump plus Ghidra decompiles made in a scratch
directory). The loader binary was not used. Read this with [sprite-engine.md](sprite-engine.md),
which documents `Sprite`, `List`/`ListObj`, `WorldClass` and `Bitmap`. This document uses the
same conventions:

- **Addresses** are ELF addresses as objdump prints them. Ghidra adds 0x10000; a quoted
  decompile address says "decomp".
- **Confidence:** **[C]** confirmed from code (several sites, or one unambiguous site), **[L]**
  likely, **[?]** guess.
- i386, g++ 3.4/4.x, Itanium ABI. Methods are cdecl with `this` first. Ghidra drops the last
  argument of most imported thiscalls, so every argument order below was checked against the
  pushes in the disassembly.
- **Time unit: frames.** Every SObj duration and delay counts world frames (one `NextFrame` or
  one `PlayMovie` step, about 30 per second). It is not the millisecond clock at
  `WorldClass+0x170` [L].

---

## 1. Symbol inventory

Each game's `notes/unresolved.txt`, demangled. "e h s n" = euchre, hearts, spades, snubble.

| Symbol | Games | Section |
|---|---|---|
| `SObj::SObj(Bitmap*, unsigned short)` (`_ZN4SObjC2EP6Bitmapt`, C2 only) | n | §4.1 |
| `SObj::~SObj()` (`_ZN4SObjD2Ev`, D2 only) | n | §4.1 |
| `typeinfo for SObj` (`_ZTI4SObj`) | n | §2 |
| `SObj::DoNextFrame()`, `SObj::DoDraw()` (vtable slots 7, 8 of `Ball`) | n | §2, §6 |
| `SObj::SetBmp(Bitmap*)` | e h s | §4.2 |
| `SObj::SetBmpSeq(Bitmap*)` | n | §4.2 |
| `SObj::SetCord(int, int, int)` | e h s | §4.2 |
| `SObj::MoveSprite(int×8, unsigned char)` | e h s n | §4.3 |
| `SObj::CurveSprite(int×14, unsigned char)` | e h s n | §4.3 |
| `SObj::CurveNum(int*, int, int, int, int, int, int, unsigned char)` | e h s | §4.3 |
| `SObj::CurveNum(unsigned char*, uchar, uchar, uchar, uchar, int, int, unsigned char)` | h n | §4.3 |
| `SObj::LoopAllAnim(Bitmap*, int, unsigned char, int)` | e h s n | §4.4 |
| `SObj::LoopObjAnim(Bitmap*, int, int, int, int, int, unsigned char)` | n | §4.4 |
| `SObj::LoopObjAnimAfterLast(Bitmap*, int, int, int, int, int, unsigned char)` | n | §4.4 |
| `SObj::LoopRestAnim(unsigned char)` | n | §4.4 |
| `SObj::HideAfterLast(int)` | e h s | §4.5 |
| `SObj::DeleteAfterLast(int, unsigned char)` | e h s n | §4.5 |
| `SObj::FramesLeft()` | e h s | §4.5 |
| `SObj::StripEvents(int)` | n | §4.5 |
| `SObj::PlaySound(char*, unsigned short, unsigned long)` | e h s n | §4.5 |
| `SObj::DebugSprite()` | n | §4.5 |
| `WorldClass::AllocSprite(Bitmap*, unsigned short)` | e h s n | §4.1 |
| `WorldClass::DeleteSpriteAllLists(SObj*, unsigned char)` | e h s n | §4.1 |
| `WorldClass::AssignString(SObj*, char const*, int, int, int, BmpFont*, ushort, ushort, int, signed char, short, short, short, uchar, uchar)` | e h s n | §4.6 |
| `WorldClass::AssignString1(SObj*, char const*, int*, int, int, int, BmpFont*, …same tail)` | e h s n | §4.6 |
| `WorldClass::AssignString2(SObj*, char const*, int*, int*, int, int, int, BmpFont*, …same tail)` | s | §4.6 |
| `ClearList(ListObj**, unsigned char)` | e h s n | §5 |
| `PushListObjForward(ListObj*)` | e h s | §5 |
| `GetHelpFileName(char(&)[255], xml_gameinfo::GameIds)` | e | not SObj: help-file path for `fopen` (euchre decomp 0x12220) |
| `GetHelpFileName(char(&)[255], GameIds, Locale::Languages, char const*)` | h s | not SObj. Our version takes `char const* game` as its 2nd argument, so this mangled name does not match it. |
| `HighScoresManager::LowestScore(GameIds, int)` | s | not SObj: returns the lowest high score (spades decomp 0x26561) |

Call-site counts: AllocSprite 41/49/37/21 (e/h/s/n), MoveSprite 34/44/35/7, SetCord 43/40/30/0,
AssignString 23/22/17/6, ClearList 45/35/29/10.

---

## 2. Class, size, vtable, typeinfo

```
Group (0xc) ── Sprite (0xac, dsize 0xa9) ── SObj (0x200)  ── snubble Ball (0x224)
```

**SObj derives from Sprite** [C]. snubble's `vtable for Ball` (0xb1a0, 13 words = 2 header
words + 11 slots) fills every slot it does not override with a Sprite method, and two slots with
SObj methods:

| slot | vt off | Ball vtable entry | So SObj's slot is |
|---|---|---|---|
| 0, 1 | +0x00, +0x04 | `Ball::~Ball` D1 (0x9c4e), D0 (0x9c14) | `SObj::~SObj` D1/D0 |
| 2–5 | +0x08..+0x14 | `Sprite::Signal`/`Message` (both overloads) | inherited |
| 6 | +0x18 | `Sprite::SpriteClick()` | inherited |
| 7 | +0x1c | **`SObj::DoNextFrame()`** | SObj override: per-frame update (§6) |
| 8 | +0x20 | **`SObj::DoDraw()`** | SObj override: draw (§6) |
| 9 | +0x24 | `Sprite::DoCommand(int)` | inherited |
| 10 | +0x28 | `Sprite::Reset()` | inherited |

`DebugSprite` is not virtual: `Ball::DebugSprite` (0x9824) is a non-virtual weak function that
hides it, and it is not in the vtable.

**sizeof(SObj) = 0x200** [C]. The `sprite-engine.md` estimate of ~0x208 is wrong. Evidence:
- snubble `new(0x224)` then `Ball::Ball` (0x36d5, 0x3862).
- `Ball::Ball` (0x9b92) calls `SObj::SObj(this, bmp, flags)`, sets its vptr, and zeroes
  +0x208..+0x21c (6 ints) and the bytes +0x220, +0x221.
- `Ball::DebugSprite` (0x9824) logs `"Conn%i:%i"` from +0x208[0..5] and
  `"State:%i Color:%i IsOnTop:%i"` from +0x204, +0x200 and byte +0x220. Color and State are Ball
  members that the ctor leaves uninitialised. The game writes +0x200 straight after
  construction (decomp 0x13684) and +0x204 later.
- So Ball's first member is an int at 0x200. SObj's dsize lies in (0x1fc, 0x200], and its
  sizeof is 0x200. **The engine must never write at +0x200 or beyond in an SObj.**

**Typeinfo.** `_ZTI4SObj` must be a `__si_class_type_info` with base `_ZTI6Sprite`. snubble's
`typeinfo for Ball` refers to it (0xb1dc).

**Ctor and dtor variants.** Games import only C2 and D2, through `Ball`. The engine's own
`AllocSprite` objects also need C1 plus a real `_ZTV4SObj` with D1/D0. Games never import
`vtable for SObj`.

**Ownership.** Games never `delete` an SObj or call its vtable. Each SObj dies through
`DeleteSpriteAllLists`, `DeleteAfterLast`, the kill bit (+0xe4 & 0x40), or `ClearList(…, 0x40)`.
A `Ball` must be destroyed through its virtual deleting dtor (slot 1), so that `Ball::~Ball`
runs and then calls `SObj::~SObj` (D2). D2 must therefore do the complete unlinking itself.

---

## 3. SObj layout (offsets games touch)

The Sprite part (+0x00..+0xa8) keeps Sprite's meanings where games use it. SObj's own fields
are at +0xac..+0x1ff.

| off | type | meaning | conf | evidence |
|---|---|---|---|---|
| +0x00 | vptr | §2 | C | |
| +0x04 | `List*` | Group child list. Never touched by these games; keep it NULL. | L | |
| +0x0c, +0x10 | float | **x, y** (top-left [L]). Games read them and write them directly (an instant move) | C | hearts writes 0x440dc000 (567.0) etc. (decomp 0x1fc7f); snubble ball physics |
| +0x14, +0x18 | float | **w, h** of the current bitmap. Used for hit tests. | C | hearts `touch.x < spr->w + 192` (0xb84d) |
| +0x1c | float | **game-written float** (7.0 on the snubble slider sprite). Probably a draw offset / hotspot x [?] | C (write) | snubble 0x504a. **Conflicts with our `Sprite::st`**, see §7 |
| +0x20 | float | game-written float (−15.0 on the snubble ceiling sprite). Probably hotspot y [?] | C (write) | snubble 0x4ed4 |
| +0x24, +0x28 | int 16.16 | **x/y scale**. Animated by `CurveNum(&spr->+0x24, …)` from 5000 (0.076) to 0x10000 (zoom-in), and reset to 0x10000 | C | euchre 0x8bf3/0x8c1e (`lea 0x24/0x28`) |
| +0x48 | float | **z**, draw priority. Larger draws on top (text 30000+ over cards 10000) | C | 3rd argument of SetCord/MoveSprite; read by games |
| +0x5c | u8 | **transparency**: 0 opaque, 255 invisible. Animated by `CurveNum(uchar*)` | C | snubble SetUpRoundText: writes 0xff (0x2cae), then fades 255→0 |
| +0x60 | `Bitmap*` | **current bitmap** (the frame being drawn) | C | snubble writes it together with +0xfc (0x8d53) |
| +0xe4 | u32 | **SObj flags**. 0x01 = draw mirrored in X [L]; 0x20 = game marker, see below [L]; 0x40 = **kill**: remove at the end of the frame [L] | C (bits used) | see the list below the table |
| +0xe8 | int | **frame index for `SetBmpSeq`** (0..179 for the snubble launcher) | L | snubble 0x5008 (= 90), 0x6a48 (`fistpl`), then `SetBmpSeq` |
| +0xec, +0xf0, +0xf4 | float | **previous x, y, z**: the position at the start of the current frame | L | snubble CCD (0x7bea…) sweeps from +0xec to +0xc after `NextFrame`. Card games call `MoveSprite(prev → current)` to slide a card from where it was drawn (euchre 0x7b76). hearts also writes them as a start point (0xfa6b) |
| +0xfc | `Bitmap*` | **base bitmap**: chain start for `LoopObjAnim`. Games read it and write it | L | snubble 0x8d4d (`+0xfc = +0x60 = colour frame`), `LoopObjAnim(spr, spr->+0xfc, 4, 4, …)` (0x5ed8) |
| +0x108 | int | **visibility**: 0 hidden, 2 visible, 1 = ? (treat it as visible). Games write it constantly. **Default after AllocSprite: 2** [L] | C (0/2) | ~150 writes. snubble's debug toggle writes `on ? 2 : 0`. Help screens set 0, then 2 to restore. snubble's slider/ceiling/score sprites never set it and must still show. 1 is written only at snubble 0x6b7f/0x6ceb |
| +0x10c | int | written 0 by snubble on three sprites (0x4f3a…). Meaning unknown | ? | |
| +0x110 | int | **show delay in frames**: while it is >0 it counts down, and when it reaches 0 the sprite becomes visible (+0x108 = 2) | L | snubble SetUpRoundText (0x2b28): `+0x108 = 0; +0x110 = d` where d equals the delay of that sprite's first Move/Curve/fade (0x2cb2, 0x2e23, 0x2f70, 0x3001) |
| +0x114 | `SObj*` | **next sprite in a chain**. Only read (euchre 0x5d14), never written by games; deleted along with the sprite. Keep it 0 | L | §6 cleanup walk |
| +0x128 | int | game-written marker: 100 = "keep showing when idle", else 0. Engine: initialise it to 0, otherwise ignore | ? | euchre 0x96a2, tested at 0x5279 |
| +0x148 | `ListObj*` | **pending-event list header**. Must be non-NULL from construction. Games test it for empty (`hdr->+0xc == hdr->+0x14`) and cancel all animations with `ClearList(&spr->+0x148, 4)` | C | euchre 0x5131, 0x526b; hearts 0x41da; ~30 ClearList sites |
| +0x186 | u8 | **clip enable** | C | snubble 0x4daa (=1) |
| +0x188, +0x18c, +0x190, +0x194 | int | **clip rect x1, y1, x2, y2**, inclusive screen pixels (x2 = 639, y2 ≤ 479) | C | snubble 0x4e17, `CheckClip` (0x289a) clamps to 0..479 |
| +0x198..+0x1ff | | not touched by games; engine-private | | |

Bits of +0xe4:
- **0x01** is set on the two right-moving sprites of a 4-way sparkle (hearts 0x63ff, 0x65b9), so
  it means mirror in X [L].
- **0x20** is set by euchre/hearts/spades (euchre 0xe35d, hearts 0x9083, spades 0xfdce). Those
  games later delete every SObj that has it (euchre 0x5d09). The engine can ignore this bit.
- **0x40** is set by snubble on popped balls, on bubbles that rise past the ceiling, and on
  one-frame "doink" button sprites (0x3650, 0x5789, 0x5a9e). snubble never deletes those
  sprites itself. The bubbles stay in a game list that is walked every frame, so the engine
  must also unlink them (see DeleteAfterLast in §4.5).

---

## 4. Methods

### 4.1 Creation and deletion

| Function | Semantics | conf |
|---|---|---|
| `SObj::SObj(Bitmap* bmp, ushort flags)` | Runs the Sprite base ctor. Sets +0x60 = +0xfc = bmp (NULL allowed), w/h from bmp, scale 0x10000, +0x5c = 0, +0x108 = 2, and zero for +0x110, +0x114, +0x128, +0xe4 and the clip fields. **Allocates an empty header at +0x148.** Links `this` into the current world's SObj list (+0x54) and its `std::set<Sprite*>` (+0x5c). Keep `flags` (§8). | L (C for +0x148) |
| `SObj::~SObj()` (D2) | Unlinks from the world list and set, and from every engine-tracked list (§5). Frees the event list. Does not free the bitmap (games own them). | L |
| `SObj* WorldClass::AllocSprite(Bitmap* bmp, ushort flags)` | `return new SObj(bmp, flags)`. bmp is often NULL for text sprites that get `AssignString` next. flags seen: card games always 0x2 or 0xc002; snubble 0 or 0xc000 | L |
| `void WorldClass::DeleteSpriteAllLists(SObj* s, uchar f)` | Deletes `s` now through its virtual deleting dtor (vt[1]), after unlinking it from **all** lists: the world list, the +0x5c set, its pending events, and game lists built with the C list API. f is always 0 [?]. It must be safe while the game walks the world list: euchre reads `node->next` before the call (0x5d3a) | L |

snubble also inserts each AllocSprite result into `*(world+0x5c)` itself, with inline
`std::set<Sprite*>::insert` (27 sites, e.g. 0x2bd5). Set insertion is idempotent, so that is
harmless.

### 4.2 Immediate setters

| Function | Semantics | conf |
|---|---|---|
| `void SObj::SetBmp(Bitmap* b)` | +0x60 = b now (+0xfc too [L]); w/h from b. Typical use: `SetBmp(card->sobj, card->front)`, the "flip" | C (immediate) |
| `void SObj::SetBmpSeq(Bitmap* seq)` | +0x60 = frame number `this->+0xe8` of the `seq` chain (`NextAnim`) | L |
| `void SObj::SetCord(int x, int y, int z)` | +0xc = x, +0x10 = y, +0x48 = z (floats), now. **Leaves +0xec..+0xf4 alone**: games SetCord the new layout position and then `MoveSprite(prev → new)` | L |

### 4.3 Motion and value curves (queued events)

Each of these appends an event to +0x148. `delay` is in frames **from the call** (not from the
last event): euchre queues `MoveSprite(…, 20 frames, delay 0)` and then the return trip
`MoveSprite(…, 20 frames, delay 200)` on one sprite. The event is active for frames
`[delay, delay + frames)` and is removed when it finishes.

| Function | Arguments | Semantics | conf |
|---|---|---|---|
| `int SObj::MoveSprite(x0, y0, z0, x1, y1, z1, frames, delay, uchar ease)` | 8 ints + uchar | Linear interpolation of x, y **and z** from (x0,y0,z0) to (x1,y1,z1). With delay 0 the start point applies at once: snubble reads +0xc right after the call (decomp 0x16226). With delay > 0 the sprite stays where it is until the event starts; games position it beforehand. `ease`: 0 in snubble physics (needs constant speed); 1 in nearly all card-game moves; 2 and 3 rarely. Read 0 as linear and nonzero as eased (reuse our `ease()` mapping) [?] | C (order), L (rest) |
| `int SObj::CurveSprite(x0,y0,z0, x1,y1,z1, x2,y2,z2, x3,y3,z3, frames, delay, uchar ease)` | 14 ints + uchar | Cubic Bezier through 4 control points (e.g. card fly-off: current → (250,6) → (−90,180) → (400,446)). ease is always 1 | C (order), L (Bezier) |
| `int SObj::CurveNum(int* p, a, b, c, d, frames, delay, uchar ease)` | | Animates `*p` along a 4-value Bezier. p is always `&spr->+0x24` or `+0x28` (scale). With frames = 1 and `delay = FramesLeft()` it means "set to d after the last event" | C (targets), L |
| `int SObj::CurveNum(uchar* p, uchar a, b, c, d, int frames, int delay, uchar ease)` | | Same for a byte; always `&spr->+0x5c`, for fades (255,255,0,0 is a fade-in) | C (targets), L |

Return values are never used.

### 4.4 Bitmap animation (queued events)

A "chain" is a LoadBmp result linked through `Bitmap::NextAnim()` (frame order).

| Function | Semantics | conf |
|---|---|---|
| `LoopAllAnim(Bitmap* anim, int loops, uchar u, int delay)` | From `delay` on, set +0x60 to each frame of `anim` in turn, one per frame, `loops` times. 999999999 = forever (the event never ends, so the list never empties). `u` is always 0. Usually followed by `HideAfterLast(0)` or `DeleteAfterLast(0, …)` | L |
| `LoopObjAnim(Bitmap* anim, int first, int last, int dur, int loops, int delay, uchar u)` | Shows frames first..last of `anim` (index into the chain, 1-based [?]). One pass takes `dur` frames: (1,15,dur 15) is a 15-frame pop, (1,1,30) holds one frame for 30. Repeats `loops` times (999999999 = forever) after `delay`. The `dur` and index meanings are [?] | L / ? |
| `LoopObjAnimAfterLast(…same…)` | Same, but it starts `delay` frames after the last pending event ends | L |
| `LoopRestAnim(uchar u)` | Plays from the current +0x60 frame to the end of its chain, then stops. Used on a fresh `AllocSprite(popAnim)` followed by `DeleteAfterLast` | L |

### 4.5 Scheduling, sound, misc

| Function | Semantics | conf |
|---|---|---|
| `int SObj::FramesLeft()` | Frames until the last pending event ends (0 if idle). Used as a `delay` | L |
| `void SObj::HideAfterLast(int delay)` | Queues "set +0x108 = 0" for `delay` frames after the last event ends | L |
| `void SObj::DeleteAfterLast(int delay, uchar unlink)` | Queues deletion `delay` frames after the last event. With **unlink = 1** the sprite is also removed from every game list (C list API) that holds it. snubble 0x3611 links a bubble into game list +0x48c (0x3563), and the same function walks that list every frame, reading `spr->+0x10`. Card games use unlink = 0 on transient effect sprites | L |
| `void SObj::StripEvents(int mask)` | Drops pending events, now. snubble: `StripEvents(4)` on a ball that hit something before it repositions the ball by hand; `StripEvents(1)` before it re-queues level-icon loops. Treat it as "drop all" [?]. A mask of event kinds is possible | L / ? |
| `void SObj::PlaySound(char* wav, ushort flags, ulong delay)` | After `delay` frames, `PlayPreWave(wav, flags, …)`. flags is always 0x440. **Copy the name**: snubble passes the shared buffer `world+0x64` | L |
| `void SObj::DebugSprite()` | Logging; it can be a no-op | L |
| `int SObj::DoNextFrame()`, `int SObj::DoDraw()` | Vtable slots 7/8; see §6 | L |

### 4.6 Text: `WorldClass::AssignString*`

```
AssignString (SObj* s, const char* text,               int x, int y, int z, BmpFont* font,
              ushort w, ushort h, int just, schar spacing, short r, short g, short b, uchar u1, uchar u2)
AssignString1(SObj* s, const char* fmt, int* v,        …same from x on…)
AssignString2(SObj* s, const char* fmt, int* v1, int* v2, …same from x on…)
```

- Renders text with the bitmap font into a w×h bitmap, makes it the sprite's +0x60 (and sets
  w/h), and places the sprite at (x, y, z) [L]. `font` is a `BmpFont*`, either the game's own
  (`new(0x798)` + `BmpFont(short, short, ushort)`) or `MegacGraphics+0x20`. `h` is usually the
  font height from `font->vt[0]()`, which the game calls itself (euchre 0x4275).
- (r, g, b) are **offsets from white**, as in `CreateColoredSmackTextBox` [C]. Examples: (0, 0,
  −255) is yellow on the euchre team score (0xc64a); (−20, −20, −20) is light grey;
  (−219, 0, −255) is green.
- `just`: −1 or 0. `spacing`: −1, 0 or 1. u1: 0 or 1. u2: always 0. The tail maps onto our
  `String(BmpFont*, char const*, int just, schar spacing, int r, int g, int b, uchar, uchar)`
  ctor, so reuse it [L].
- **AssignString1/2 are live counters** [L]. snubble calls
  `AssignString1(score, "SCORECOMMASTR", &PlrScore, 506, 73, 20000, …)` exactly once (0x9481),
  and the score changes all game. spades uses `AssignString2(…, "%i/%i", &bid, &tricks, …)`
  (0xc154). euchre passes `""`. So: every frame, if `*v` (or `*v1`, `*v2`) changed, re-render
  `fmt` formatted with the values. An empty fmt shows the number alone. snubble's
  "SCORECOMMASTR" probably asks for a comma-grouped number (`CommaStr`) [?].
- AssignString does not change +0x108; euchre sets it to 2 itself afterwards (0x42bf).

---

## 5. C list API additions

The node layout is the existing `ListObj`: +0xc data, +0x10 prev, +0x14 next; the header's
data points at its tail; the tail has next = 0. An empty list has `hdr->+0xc == hdr->+0x14`
[C] (games test exactly that on +0x148). `LinkIntoList`, `RemoveFromList` and `IsInList`
already exist in `src/legacy/sprite.cpp`.

| Function | Semantics | conf |
|---|---|---|
| `void ClearList(ListObj** pp, uchar flags)` | If `*pp == NULL`, **allocate a new empty header into `*pp`**: snubble does `local = 0; ClearList(&local, 0x10); LinkIntoList(local, …)` (0x567f), and euchre initialises its pile globals with `ClearList(&g, 0)`. Otherwise unlink and free every node and **keep the header** (euchre does `ClearList(&deck, 8)` and then deletes the header via vt[1], decomp 0x27047). Flags: **0** unlink only; **4** also delete the data (SObj events, +0x148); **8** also delete the data through its virtual dtor (euchre `HCard : Group`, new(0x1c)); **0x40** also delete the data as SObjs (snubble's ball list, 0x4c36); **0x10** only seen with NULL lists | C (NULL, header kept), L (flags) |
| `void PushListObjForward(ListObj* n)` | Swaps node `n` with its successor (n moves one step toward the tail). Used by euchre's bubble sort of a hand (0x4ff0, call at 0x5067) | C |

For the unlink behaviour of `DeleteSpriteAllLists` and `DeleteAfterLast(…, 1)`, the engine has
to know every list header. `find_header` already finds headers from any node; add a registry of
headers created by `ClearList`, `List(0)` and `LinkIntoList`.

---

## 6. How the world drives SObjs

**World fields** (in addition to sprite-engine.md §3):

| off | meaning | conf | evidence |
|---|---|---|---|
| +0x54 | `ListObj*` header of **all SObjs**. Games dereference it with no NULL check: `hdr->+0x14` first node, `node->+0xc` SObj, stop at `node->+0x14 == 0` | C | euchre 0x5cfe–0x5d42 |
| +0x5c | `std::set<Sprite*>*`. In these games **every member is an SObj**: euchre/hearts/spades walk it inline and write `+0x108 = 0` on every member except `world->+0x60` (hide all for the help screen) | C | euchre 0x6186–0x61d9, hearts 0x6daa, spades 0x699b |
| +0x60 | the one sprite excluded from that hide-all (background/root?). NULL is safe | ? | euchre 0x619f, 0xd471 |
| +0x64..+0x163 | **char[256] scratch buffer** that games own: `Trans<255>(…, world+0x64, key)` (hearts 0x680e), `sprintf(world+0x64, "pop%i")` then `PlaySound`/`PlayPreWave` with it | C | all four games (`add $0x64`) |

**Game loop.** The games run the frame themselves:
- Card games: `NextFrame(0,1,1,0,0,0); WaitOnFrame(30,0); ShowAll();` (euchre 0x5ceb, 0x5d54,
  0x5d65). They also call `PlayMovie(N, 30, cb, 0, 1, 0, 0, 0)` to run N frames with the
  animations going.
- snubble: `NextFrame(1,1,1,0,0,0); WaitOnFrame(33,1); ShowAll();`.

They wait for animations by polling the +0x148 list for empty, or by `PlayMovie(FramesLeft())`.

**Per-frame algorithm** (run inside `NextFrame` and each `PlayMovie` step, together with the
existing Sprite frame) [L]:

1. `frame_no++`. For every SObj in the +0x54 list (iterate over a snapshot):
   `vt[7]()` = `SObj::DoNextFrame()`:
   - copy x, y, z into +0xec/+0xf0/+0xf4;
   - if +0x110 > 0, decrement it; when it reaches 0, set +0x108 = 2;
   - run due events from +0x148 in order (motion/curves write +0xc/+0x10/+0x48, +0x24/+0x28 or
     +0x5c; anims write +0x60; hide/sound/delete fire); remove the finished ones.
2. Refresh live AssignString1/2 text.
3. Draw every visible SObj (+0x108 ≠ 0) by z, merged with ordinary Sprites, through `vt[8]()`
   = `SObj::DoDraw()`. Draw +0x60 at (x, y), offset by +0x1c/+0x20 [?], with scale
   +0x24/+0x28, alpha 255 − +0x5c, mirror if +0xe4 & 1, clipped to +0x188..+0x194 when +0x186
   is set.
4. Reap: delete SObjs with +0xe4 & 0x40 or a fired delete event. Unlink them from the world
   list, the set, and all registered lists (always for kill/0x40 and `DeleteSpriteAllLists`;
   for `DeleteAfterLast` only when unlink = 1 [L]). Delete through vt[1].
5. `ShowAll` presents the frame (it already does).

**Game-side cleanup walk** (euchre 0x5cfe): for each node, it saves `next`, and if
`sobj->+0xe4 & 0x20` it repeats `t = s->+0x114; DeleteSpriteAllLists(s, 0); s = t` until s is 0.
So `DeleteSpriteAllLists` may free the current node but no other node, and +0x114 should stay 0.

---

## 7. Coexistence with our current layouts (conflicts)

1. **`Sprite::st` at +0x1c (CONFLICT).** snubble writes floats into SObj +0x1c (7.0f, 0x504a)
   and +0x20 (−15.0f, 0x4ed4). Our `Sprite` keeps `SpriteState* st` at +0x1c and dereferences
   it in `Sprite::SetSize`, `SetBmp` and the destructor, so this crashes. Move `st` into a Sprite
   range none of the SObj games touch: +0x34..+0x44, +0x4c..+0x58 or +0x6c..+0x78 (all unused
   by these four games and listed as engine-private in sprite-engine.md §8). Keep +0x1c/+0x20
   as plain floats.
2. **WorldClass +0x54 is NULL.** `WorldClass()` memsets everything from +0x0c, so
   `sobj_list` starts NULL. Allocate an empty `ListObjHeader` there in the ctor (euchre
   dereferences it unconditionally), and free it with the world.
3. **+0x5c set membership.** Our `Sprite::Sprite` inserts every sprite into `world->sprites`
   (sprite.cpp:312). That is right for SObjs. But the card games write `+0x108` (an SObj-only
   offset) into **every** member. Any plain `Sprite` (0xac bytes) the engine creates while a
   card game runs would be corrupted at +0x108. In these games, never create internal Sprites
   that go into the set (dialogs, `Sprite::Copy`).
4. **+0x60 `root`.** Games compare it with SObj pointers. Leave it NULL, or point it at a
   background SObj if one is ever added. Never point it at a non-SObj.
5. **+0x64..+0x163 belong to the game** (256-byte scratch). This is `pad64` today; keep engine
   data out of it.
6. **Clock.** The existing Sprite events use `world->clock` (ms, +0x170). SObj events must count
   frames (a separate counter in `WorldState`); otherwise all the frame-based durations break.
7. **Sprite base behaviour.** If `SObj : Sprite` reuses our Sprite ctor, the Sprite event lists
   (+0x7c..+0x88) are allocated but stay empty. `WorldClass::frame()` must not treat SObjs as
   Sprites for drawing/reaping semantics that differ: visibility (+0x108), alpha, kill bit
   +0xe4 & 0x40 vs Sprite +0x30 & 8, and SObjs are not clickable. Dispatch through vt[7]/vt[8]
   and skip Sprite-only steps for SObjs (for example with a `dynamic_cast<SObj*>` or a tag).
8. **sizeof limits.** Our own SObj class must be exactly 0x200 bytes, with all engine members in
   +0xac..+0x1ff outside the offsets listed in §3. Members at +0xa9..+0xab are fine
   (no SObj-derived game class uses Sprite's tail padding; Ball starts at 0x200).
9. **BmpFont.** `class BmpFont` is only a local declaration in `bitmap.cpp:488`
   (`BmpFont(short, short, unsigned short)`), and `String(BmpFont*, …)` exists. AssignString*
   should build a `String` with it and render through the existing text path. Games call the
   font's vt[0] (height) and vt[1] (string width) themselves, so those slots must work.
10. `sprite-engine.md` says "SObj ~0x208" (§1 table and tree). The correct value is 0x200.

---

## 8. Open questions

- AllocSprite/SObj `flags`: card games always set 0x2, snubble never; 0xc000 on about a quarter
  of the sprites (snubble balls via `Ball(bmp, 0xc000)`). No behaviour could be tied to them [?].
- +0x108 = 1 (snubble "doink" sprites, which also get the kill bit): perhaps "draw this frame
  only".
- +0x10c, +0x128, +0x1c/+0x20 (hotspot?), +0xe4 & 0x20 (engine meaning, if any).
- The trailing `uchar` of MoveSprite/CurveSprite/CurveNum (easing?), and the `uchar` of
  LoopAllAnim/LoopObjAnim/LoopRestAnim (always 0).
- LoopObjAnim: whether `dur` is per pass or per frame, and whether `first`/`last` are 0- or
  1-based.
- `StripEvents(mask)`: whether the mask selects event kinds.
- `DeleteSpriteAllLists`' uchar (always 0); `ClearList` flag 0x10.
- `WorldClass+0x60`.

---

## Implementation checklist

The minimum for euchre, hearts, spades and snubble to load (RTLD_NOW), start and play:

1. **Exports** with the exact mangled names in §1. Also `_ZTI4SObj` (si → `_ZTI6Sprite`),
   `_ZTV4SObj`, SObj C1+C2, D0+D1+D2, and real `SObj::DoNextFrame`/`SObj::DoDraw`. The Sprite
   slot methods (`SpriteClick`, `Reset`, `Signal`×2, `Message`×2, `DoCommand`) already exist.
2. **`ClearList`** (NULL → new header; flags 0/4/8/0x40 as in §5; keep the header) and
   **`PushListObjForward`** (swap with next).
3. **WorldClass ctor**: allocate the empty SObj list header at +0x54. Keep +0x5c a valid set,
   +0x60 NULL, and +0x64..+0x163 untouched.
4. **Move `Sprite::st` off +0x1c** (§7.1).
5. **`class SObj : public Sprite`**, sizeof 0x200. The ctor initialises §3 defaults
   (+0x108 = 2, scale 0x10000, +0x148 = empty header, +0x60 = +0xfc = bmp, w/h) and registers
   the sprite in +0x54 and +0x5c. D2 unlinks it from everything. `AllocSprite` = `new SObj`.
6. **Immediate setters**: `SetCord`, `SetBmp`, `SetBmpSeq`.
7. **Frame-based event queue on +0x148**: MoveSprite, CurveSprite, both CurveNums,
   LoopAllAnim, LoopObjAnim(+AfterLast), LoopRestAnim, HideAfterLast, DeleteAfterLast (with
   the unlink flag), PlaySound (copies the name), StripEvents, FramesLeft. Finished events must
   leave the list: the games poll it for "idle", so a list that never empties means a game that
   waits forever.
8. **NextFrame / PlayMovie** run §6 steps 1–4 for SObjs: the prev-position copy, the show-delay
   countdown, events, then z-sorted drawing honouring +0x108, +0x5c, +0x24/+0x28, +0xe4 & 1 and
   the clip, then reaping of the kill bit and delete events.
9. **`DeleteSpriteAllLists`**: unlink from world list, set and registered lists, then delete
   through vt[1]. It must be safe during the game's walk of +0x54.
10. **AssignString/1/2** through the `BmpFont` → `String` path, with colours as offsets from
    white. AssignString1/2 re-render when the bound ints change (score displays).
11. Fix the two non-SObj blockers in the same `unresolved.txt` files: the
    `GetHelpFileName(char(&)[255], GameIds)` and `(…, GameIds, Languages, char const*)`
    overloads, and `HighScoresManager::LowestScore(GameIds, int)` (spades).
