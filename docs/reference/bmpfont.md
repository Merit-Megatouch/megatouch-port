# Bitmap fonts ABI (`FontBase` / `BmpFont` and the `.dlt` glyph files)

The loader's bitmap-font classes as the legacy games use them: `BmpFont::BmpFont(short, short,
unsigned short)`, the `FontBase*` objects in `MegacGraphics`, the vtable slots games call on
both, and the `gamedata/fonts/*.dlt.gz` glyph files the fonts are drawn from. It complements
[sprite-engine.md §5.4](sprite-engine.md), which has a short first pass at the slot table.
This page replaces that table.

Sources: the game libraries only (objdump of `games/*/lib/<game>.so`, the Merit2d
`libmerit2d.so` copies, and the data files under `games/*/data/usr/local/gamedata/fonts`). The
loader binary was not used.

Conventions are the same as [sprite-engine.md](sprite-engine.md):

- **Addresses** are ELF addresses, as objdump prints them, written as game + address
  (`bgammon 0x689f`).
- **Confidence:** **[C]** confirmed from code at several sites, **[L]** likely, **[?]** guess or
  unresolved.
- **i386 cdecl, `this` first.** Argument lists below leave out `this`. A call through a font
  is always `mov (%font),%vt; push args…; push %font; call *0xNN(%vt)`.
- **Counts** ("≥ N games") come from a dataflow scan. It follows `operator new(0x798)` +
  `BmpFont::BmpFont`, `MegacGraphics::GetInstance()->+off`, and pointers stored in and reloaded
  from globals, locals and object fields. It misses some indirect paths, so counts are lower
  bounds. The `[legacy] font … slot` lines from the smoke runs show the same set of slots and no
  others.

Method: the scratchpad scripts `fontscan.py` (dataflow and stack model, PIC string
resolution), `dlt.py`/`comp.py` (frame decoder and delta compositor) and `grid.py` (ASCII glyph
dumps).

---

## 1. Summary of findings

1. **`sizeof(BmpFont)` = 0x798**, polymorphic, with `FontBase` as its primary base (the same
   pointer is passed as `FontBase*`). Games call **18 vtable slots** between +0x00 and +0x68. No
   game reads or writes a BmpFont field inline. Only the vptr at +0 is touched.
2. **Every `new BmpFont` is followed by `Load(name, charset, flag)`** (slot +0x18), with a
   bare file name: `serpb31`, `j96`, `3dwin`, `3dwin50`, `soopa49`, `soop23`, `spn21`, `aoi21`,
   `aoni22`, `afontn`, `18x18w`, `12x16`. The constructor's (w, h) do **not** choose the font.
   They are 0×0, 28×28 (always with `serpb31`, a 28×28 font), 0×1 or 12×16 (with `12x16`).
3. **Glyph files are delta-coded `.dlt` animations.** Frame *k* holds only the pixels that
   differ from frame *k−1*. A glyph is the canvas after frames 0..*k* have been composited.
   Drawing single frames, as we do now, gives broken glyphs, which is the "garbled/mirrored"
   text.
4. **Frame index = character code − 1** (frame 0 = code 1, a solid black key frame; frame 31 =
   space; frame 32 = `!`; frame 64 = `A`). Our code uses frame = code, which is off by one.
   That is the "wrong characters" bug.
5. **The glyphs are not mirrored or transposed** once composited. They are stored upright,
   left to right and top to bottom. Black (RGB565 0x0000) is the background and must be
   treated as transparent.
6. **Cells are fixed size, with no width table** (every frame has the header's w×h, and the
   8-byte trailer is zero). Proportional spacing has to be measured from each glyph's ink. The
   games switch between proportional and fixed spacing with slots +0x4c and +0x50.

---

## 2. Construction, size, lifetime

| Fact | Evidence | |
|---|---|---|
| `operator new(0x798)` before every `BmpFont::BmpFont` | 90 call sites in 60 games (all `push $0x798`) | [C] |
| Ctor args `(this, short w, short h, unsigned short 0xd6)` | 0xd6 at all 90 sites. (w,h) = (0,0) ×41, (0x1c,0x1c) ×41, (0,1) ×7 (mysteryphraze ×3 variants, htquizshow), (12,16) ×1 (airhockey 0x6bfc) | [C] |
| (w,h) is the expected glyph cell or 0 for "from the file" | 28×28 always comes with `Load("serpb31")` (a 28×28 file), 12×16 with `Load("12x16")` (12×16), and 0×0 with j96/3dwin/aoni22/… (100×100, 24×24…) | [L] |
| Third argument 0xd6 (214) | Never varies, so its meaning can't be seen from the games (glyph-table capacity? flags?) | [?] |
| `FontBase` is the primary base (offset 0) | The `new` result is passed unchanged to `CreateSmackTextBox(…, FontBase*, …)` (bigroller 0x64b5) and to virtual calls | [C] |
| No inline field access | A scan of all reads and writes through font pointers found only false positives (Bitmap fields) | [C] |
| Destroyed with `font->vt[0x68](font)` (deleting dtor), null-checked | bgammon 0x68cc/0x68d4, battle31 0x12660, coco_loco 0x106d0, checkerz 0x850c, strippoker 0xc5e2, ≥ 19 games. No game calls `operator delete` on a font. | [C] |

The deleting destructor at +0x68 means the complete-object destructor is at +0x64. No game
calls +0x64. So the dtor is declared after the other virtuals, and the vtable has at least 27
entries (+0x00…+0x68).

A typical sequence (bgammon `BG_DispBackGammonBonus` 0x6788):

```cpp
BmpFont* f = new BmpFont(0, 0, 0xd6);
f->Load("3dwin50", NULL, 1);        // +0x18
f->SetJustify(2, 0);                // +0x24  centre
f->SetSpaceWidth(20);               // +0x60
f->SetColor(0, -255, -255, 0);      // +0x08  red (offsets from white)
f->DrawText(text, 0, 300, 640, 0, 0, 0, 1);   // +0x2c, centred across the 640-wide box
delete f;                           // +0x68
```

---

## 3. The vtable

`R` = return value used by the caller. Types are as pushed: every argument is a 32-bit stack
word, and "short"/"char" means the value range seen.

| vt off | Signature (proposed name) | Games | Semantics and evidence | Conf |
|---|---|---|---|---|
| **+0x00** | `int GetHeight()` | ≥ 14 | Line height in pixels. hearts 0x3717 and euchre 0x4275 pass it as `h` to `WorldClass::AssignString`. monstermadness 0x64c6 passes it as `maxH` to `CreateColoredSmackTextBox`. chess 0x95b5 uses `y - GetHeight()/2`. euchre 0x8475 passes `GetHeight()+3` as a text box height. | [C] |
| +0x04 | `int GetStringWidth(const char* s)` | ≥ 8 | Pixel width of `s` in the current spacing mode. qbzone 0x5b34: `x = 0xb9 - w/2`. hearts 0x72f1 and euchre 0x66ad keep the maximum width over several lines. euchre 0xe180 measures text for a j96 font. | [C] |
| **+0x08** | `void SetColor(short r, short g, short b, unsigned char x)` | ≥ 31 | Text colour as **offsets from white** (channel = 255 + v, the §5.4 convention): (0,−255,−255) red (bgammon 0x682d), (0,0,−255) yellow (boxxi 0xe1b1), (−255,0,−255) green (elevenup 0x740d), (0,0,0) white (airhockey 0x6bc4). It applies to later `DrawText`, `DrawNumber`, `CreateTextBox` **and `Bitmap::CreateSmackTextBox`** with this font (elevenup 0x73ec→0x7493). The 4th argument is 0 in 61 of 69 calls and 40 or 80 otherwise (battle31 0x40e8 `(-100,0,-100,40)`, qbzone 0x4c60 `(20,20,-150,80)`, monstermadness ×5 `…,40`). Its meaning (shadow? intensity?) is unknown. | [C] rgb / [?] x |
| **+0x10** | `void ResetColor()` | ≥ 31 | No arguments. It is called before a new colour is set (elevenup 0x73ec: `ResetColor(); SetColor(-255,0,-255,0); CreateSmackTextBox(…)`), right after drawing in a custom colour (qbzone 0x4c95 after `SetColor(20,20,-150,80); DrawText(…)`), and in `GAME_KillGame` on the shared MegacGraphics fonts (boxxi 0xbb70, celookout 0xe679…). That points to "restore the default colour". Odd site: battle31 0x40fc calls it right after `SetColor`. | [L] |
| **+0x18** | `bool Load(const char* name, const char* charset, int flag)` | ≥ 56 | Loads `gamedata/fonts/<name>.dlt.gz`. `name` has no extension (quikmatch 0x6946 `"serpb31"`, checkerz 0x8344 `"j96"`). `charset` is NULL or the only characters that will be needed: brickbreaker 0x68ba `Load("aoni22", "0123456789,", 0)`, and bigroller 0x6456 / battlegroup 0x15eb5 pass the very string they then render with `CreateSmackTextBox`. So the loader only prepares those glyphs. Treat it as a hint and load everything. `flag` is 0, except 1 for `3dwin`/`3dwin50` in bgammon 0x67cc/0x67fd and in mysteryphraze. The return value is never tested. | [C] name / [L] charset / [?] flag |
| +0x20 | `void ?(int 0)` | 6 | Always 0. Always on MegacGraphics+0x38, between `SetProportional()` and `SetFixedWidth(10)`: lookout family 0x11b76/0x11bca, qbzone 0xce10. | [?] |
| **+0x24** | `void SetJustify(int just, int arg2)` | ≥ 46 | `just`: **0 left, 1 right, 2 centre**. 2 + `DrawText(t, 0, 300, 640, …)` centres the bonus text across the screen (bgammon 0x680a/0x689f). 1 goes with `SetFixedWidth(12)` on score fonts (elevenup 0x57a2, run21 0x6948, solitaire 0x831e) and with a score drawn in a 98-px box (cepixmix 0x5867/0x588f). `CreateSmackTextBox(…, just = -1, …)` uses this setting: chess 0x7b24 does `SetJustify(1)`, `CreateColoredSmackTextBox(…, -1, 58, 18, …)`, `SetJustify(0)`. `arg2` is 0 in 432 of 444 calls and 39 in euchre/hearts/lookout (0x8461, 0xc4f9, 0x708b). | [C] 2 / [L] 1 / [?] arg2 |
| **+0x28** | `void DrawNumber(long n, int x, int y, int w, int a5, int a6)` | ≥ 16 | Draws the **integer** `n` (not a string) on the current video buffer: wild8 0x4535 `DrawNumber(PlrScore, 62, 17, 74, -1, 0)` (`PlrScore` is an `R_386_GLOB_DAT` int whose value is pushed), and nine 0x4b96 `(PlrScore, 0, 0, 92, 0, 1)` on the scrap VB. `w` is the box for justification (its fonts have `SetJustify(1)` + `SetFixedWidth(12)` for right-aligned scores). (a5,a6) are either (−1,0) when drawing to a screen VB (wild8, lookout, strippoker, qbzone) or (0,1) on scrap VBs or boxes (nine, snapshot, monstermadness, pixmix 0x57f7). Whether that is "VB −1 = current" or "clear the box first" is unknown. | [C] n,x,y / [L] w / [?] a5,a6 |
| **+0x2c** | `void DrawText(const char* s, int x, int y, int w, int h, int a6, int a7 0, int a8 1)` | ≥ 24 | Draws `s` on the current video buffer (after `VideoClass::OpenVB`/`OpenScrapVB`), justified by `SetJustify` within the box `x..x+w`. With `w = 0` it is justified about `x`. 8 arguments (wild8 0x4511 `(s, 112, 53, 0, 0, 0, 0, 1)`, bgammon 0x689f `(s, 0, 300, 640, 0, 0, 0, 1)`, cepixmix 0x588f `(s, 526, 399, 98, 20, -1, 0, 1)` inside a `Rect(523,403,101,15)`). `h` is 0 except pixmix's 20. a6 ∈ {−1, 0, 1} [?]. a7 is always 0 and a8 always 1. The return value is never used. | [C] s,x,y / [L] w,h / [?] a6–a8 |
| **+0x30** | `_RADBitmap* CreateTextBox(const char* s, int w, int h, int a4, int a5, int a6)` | 4 | Renders `s` into a new C-API bitmap (checkerz 0x835b and royal 0x5492 `(s, 448/396, 96, -1, 0, 0)` with a j96 font, tennis 0xfd55 `(s, 488, 96, -1, 0, 0)`, goal 0x107d9 `(s, cellw*strlen, cellh, 0, 105, 0)`). Callers then `BitmapColorize`/`BitmapConvPal` it, read **+4 (width)** to centre it (royal 0x5558: `x = 0xd1 - w/2`) and blit it with `BitmapToScreenTrans(b, x, y, BitmapGetPixel(b,0,0))`. So the bitmap is as wide as the text, and pixel (0,0) is a uniform background used as the transparent key. Freed with `BitmapFree` (checkerz 0x8512). | [C] ret / [L] args |
| **+0x38** | `int MeasureText(const char* s, int x, int y, int w, int h, int a6, int a7 0)` | 8 | Like `DrawText` without the trailing `1`, and the **return value is a pixel width**. chess/mysteryphraze/htquizshow 0x9645 `(s, 320, 200, 0, 0, 0, 0)`, compared with a maximum width to get a scale factor. euchre/hearts/spades 0x631a `(word, x, y, 0, 0, 1, 0)` add the result to a running x and wrap when it passes 0x29e. Whether it also draws is not visible. The layout code then draws with other calls, so it probably only measures. | [L] |
| **+0x4c** | `void SetProportional()` | ≥ 36 | No arguments. Called straight after almost every `Load` (battle31 0x86ca, strippoker 0xa5c6, euchre 0xe0e0, snubble 0x902e…) and on MegacGraphics fonts at game start, usually followed by `SetSpaceWidth(3..6)` (wild8 0xa097 + 0xa0af `(5)`, airhockey 0x6b94 `(5)`, golf 0x19c01 `(5)`). Undone by +0x50. | [L] |
| **+0x50** | `void SetFixed()` | 7 | No arguments. The inverse of +0x4c. Called at game exit to restore the shared fonts: wild8 0xa62a `SetFixed(); SetSpaceWidth(10)`, wildapes 0x445f, bgammon `GAME_KillGame` 0xb3e6 (after `SetSpaceWidth(10); SetJustify(0,0); ClearFixedWidth()`), airhockey 0x6b28. So the **loader default** for the MegacGraphics fonts is this mode with space width 10. | [L] |
| **+0x54** | `void SetFixedWidth(int px)` | ≥ 30 | Forces a fixed advance of `px` per character, used for score digits. Values: 12 (×17, MegacGraphics+0x38 score font, with `SetJustify(1)`), 10 (×14), 28 (boxxi 0xf422 on MG+0x14), 13 (mysteryphraze 0xe116), 36 (safari 0xcb95 on `soopa49`, a 48×48 font). | [L] |
| **+0x58** | `void ClearFixedWidth()` | ≥ 12 | No arguments. Ends a `SetFixedWidth` section: bgammon 0x73cd `SetFixedWidth(10)` → `DrawText` 0x7411 → `ClearFixedWidth()` 0x7425, and bgammon 0xb4fe → 0xb64b. Also in KillGame resets (bgammon 0xb3d6, boxxi 0xf5be). Odd site: the battle31 `Scores` ctor 0x3983–0x399e calls it right after `SetFixedWidth(12)`. | [L] |
| **+0x5c** | `unsigned char GetSpaceWidth()` | 6 | Returns a byte (`movzbl %al`). chess `action::freeze` 0x99ad/0x99c9 lays out words one by one: `x += wordBitmap->w (+0x4c) + GetSpaceWidth()`. It is the getter for +0x60. | [L] |
| **+0x60** | `void SetSpaceWidth(int px)` | ≥ 30 | Values 10 (×26, also the restore value), 6, 5, 4, 0, 20, 3. Comes right after `SetProportional()`. In proportional mode the space glyph has no ink, so its advance has to be set. The alternative reading, "extra gap between characters", fits the chess word-gap use worse. | [L] |
| **+0x68** | `virtual ~BmpFont()` (deleting) | ≥ 19 | See §2. | [C] |

Never called by any game: +0x0c, +0x14, +0x1c, +0x34, +0x3c, +0x40, +0x44, +0x48 and +0x64
(complete dtor). Our vtable must still have entries there.

### 3.1 The wild8 smoke log, decoded

`build/smoke/wild8/log.txt` lines (the logger prints 8 stack words, so trailing values are
garbage):

| log | meaning |
|---|---|
| `slot 0x10` | `ResetColor()` on MG+0x1c (wild8 0x3304) |
| `slot 0x24 args 2 0` | `SetJustify(2 /*centre*/, 0)` on MG+0x14 / MG+0x20 |
| `slot 0x4c` | `SetProportional()` on MG+0x20 (0xa097). The two at 0x27f3/0x2815 are on MG+0x38/MG+0x40. |
| `slot 0x60 args 5` | `SetSpaceWidth(5)` |
| `slot 0x50 args -1 …` | `SetFixed()` (no arguments, the −1 is stack garbage), at exit (0xa62a), followed by `SetSpaceWidth(10)` |
| `slot 0x28 args 0 47 96 64 -1 0` | `DrawNumber(n = 0, 47, 96, w = 64, -1, 0)` (0x6127) |
| `slot 0x2c args <ptr> 59 407 0 0 0 0 1` | `DrawText(s, 59, 407, 0, 0, 0, 0, 1)` (0x8376) |

---

## 4. Fonts reached through other APIs

| API | Font argument | Notes |
|---|---|---|
| `MegacGraphics::GetInstance()` | `FontBase*` at +0x08…+0x7c, one every 4 bytes | Loader-owned shared fonts (+0x00 is a `short` video-buffer number, 610 reads). Most used: +0x20 (144 loads), +0x1c (124), +0x14 (94), +0x08 (64), +0x38 (29), +0x2c (19). Games change their state (colour, justification, mode, space width) and are expected to put it back at exit (§3 +0x50). Which file each slot holds can't be seen from the games. Hints: +0x38 is the score/number font (`SetFixedWidth(12)`, `DrawNumber`), and +0x14 takes `SetFixedWidth(28)` (boxxi), so it is probably 28 px wide. [?] |
| `Bitmap::CreateSmackTextBox(const char*, FontBase*, signed char just, ushort maxW, ushort maxH, bool)` (58 games) | `BmpFont*` (unadjusted) or MegacGraphics font | Uses the font's glyphs, **current colour (+0x08)** and spacing mode. `just = -1` means "use the font's `SetJustify` value" (chess 0x7b24–0x7b63). See [sprite-engine.md §5.4](sprite-engine.md) for the wrapping and resizing. |
| `Bitmap::CreateColoredSmackTextBox(…, FontBase*, …, short r, short g, short b, uchar, bool)` (48) | same | Colour from the arguments (offsets from white) instead of the font's. |
| `WorldClass::AssignString(SObj*, const char*, int x, int y, int z, BmpFont*, ushort w, ushort h, int, schar, short r, short g, short b, uchar, uchar)` (+ `AssignString1`/`2`, 4 games) | MegacGraphics font cast to `BmpFont*` | hearts 0x378d: `(sobj, text, 0, 0, 0x79e0, MG+0x20, 0x1fc, font->GetHeight(), -1, -1, 0, 0, 0, 0, 0)`. So `h` is one line's height and −1 is the "default" for the int and signed-char arguments. |
| `String::String(BmpFont*, const char*, int just, schar, int r, int g, int b, uchar, uchar)` (wordzap) | `BmpFont*` | A bitmap-font text sprite. |
| `BG_ShowTextFile(uchar, BmpFont**, Bitmap**)` | | bgammon's own function, not a loader API. |

---

## 5. Glyph files (`gamedata/fonts/*.dlt.gz`)

### 5.1 Container

The ordinary version-3 `.dlt` ([legacy.md](legacy.md), `src/common/merit_rle.h`):
`u32 3; u16 w, h, frameCount, delay;` then `frameCount` records `{u32 bytes; u32 w; u32 h;
RLE}` and an 8-byte zero trailer. In all 51 `.dlt.gz` font files (identical in the 19 games
that ship them):

- **frameCount is 363** (362 in `j96`, `afontn`, `qfontn` and `frutgr49`, which lack the last
  glyph). The exception is `bigscore` (10 frames, 100×100). It is a digit strip, not a
  BmpFont file.
- **Every frame is exactly the header's w×h.** This is the cell size: `12x16` 12×16,
  `serpb31` 28×28, `b24x24`/`aoni22`/`soop23`/`spn21`/`aoi21` 24×24, `18x18w`/`20x20w`/
  `serpb20` 20×20, `soopa49` 48×48, `3dwin50` 52×52, `j96`/`3dwin`/`aon96`/`serp96` 100×100,
  `digital` 12×24, `afontn` 20×32, `qfontn` 24×36, `vbbfont0` 80×56.
- **No width or metrics table** anywhere in the file.
- Pixels are opaque RGB565 (opcode 2). The background is **0x0000 black**, which counts as
  transparent. The glyphs are white with grey anti-aliasing (`12x16`, `serpb31`, `j96`) or
  pre-coloured (`3dwin`: golds 0xce59/0xd69a…). The colour offsets therefore scale an already
  shaded glyph.

`deffnt.dat`/`deffont.dat` (2304 bytes) are a plain 8×16 1-bpp VGA font for characters
32..175 (offset 0 = space, 0x10 = `!`). No game names them. They are probably the loader's
built-in fallback [?].

### 5.2 Frames are deltas

Frame 0 is a full cell of black. Every later frame stores only the pixels that differ from the
previous glyph. That includes black pixels that erase the previous glyph, and it leaves out
glyph pixels that happen to have the same colour as before. `12x16`, frame 34 (`#`) on its own
and composited:

```
on its own            composited (frames 0..34)
....--o-oo--          ------o-oo--
....--#-#o--          ------#-#o--
-----o#o#o--          -----o#o#o--
----######o-          ----######o-
     ...                  ...
```

`.` = skipped (unchanged), `-` = black written. The raw frame looks like the composited one
here. For most letters, though, the raw frame is missing every pixel the glyph shares with its
predecessor, which gives broken, half-mirrored-looking shapes. **Decode the file once and
composite frames 0..362 in order, keeping a copy of the canvas after each frame.** That copy is
the glyph.

### 5.3 Character mapping

**glyph(code) = composited frame (code − 1)**, for 8-bit codes 1..255 [C]. Checked by
rendering `12x16`, `serpb31`, `j96`, `3dwin`, `aoni22` and `b24x24`:

| frames | characters |
|---|---|
| 0 | code 1 (black key frame) |
| 1–30 | control range. Mostly blank. Frame 5 (code 6) is a `€`-like glyph in most fonts, and `j96` has small symbols at 5–16 [?] |
| 31 | `' '` (empty, so its width comes from `SetSpaceWidth`) |
| 32–125 | `!` … `~`: 32 `!`, 33 `"`, 47 `0`, 48 `1`, 64 `A`, 65 `B`, 97 `b` |
| 126–190 | 0x7F–0xBF, Windows-125x punctuation where present: 132 `…` (0x85), 137 `Š`, 140 `Ť`, 141 `Ž`, 153 `š`, 156 `ť`, 157 `ž`, 160 `¡`, 161 `¢`, 162 `£`, 175–186 `°`…`»`, 190 `¿` |
| 191–254 | 0xC0–0xFF Latin-1 letters: 191 `À`, 197 `Æ`, 198 `Ç`, 252 `ý`, 254 `ÿ` (253 `þ` is blank) |
| 255–362 | extra accented capitals and lowercase letters (codes 256–363, probably Latin Extended-A). 8-bit strings never reach them [?] |

Fonts with capitals only (`3dwin`, `3dwin50`) draw capitals in the lowercase frames too.

### 5.4 Orientation

The composited glyphs are **upright and not mirrored**. `B` has its bowls on the right, `b`
its stem on the left, and `2`/`C`/`¿` read correctly, in every font checked. They are stored
row by row, top to bottom, left to right, exactly as they are drawn. **No flip or transpose is
needed.**

### 5.5 Widths

Cells are fixed. Each glyph's ink sits inside the cell with a variable left bearing (`12x16`:
`1` at columns 3–7, `W` at 2–9, `i` at 4–6; `serpb31`: `1` at 7–16, `W` at 2–21). How the
loader turns that into advances is not visible from the games. The behaviour they rely on is:

- **Fixed mode** (`SetFixed`, +0x50; the loader default on shared fonts): advance = cell width.
- **Proportional mode** (`SetProportional`, +0x4c): advance = the glyph's ink width plus a
  small gap. The space is `SetSpaceWidth(n)` pixels (default 10).
- **`SetFixedWidth(px)`** (+0x54) overrides the advance with `px` until `ClearFixedWidth()`
  (+0x58). Score digits use it.

The fonts created with `BmpFont` but never switched to proportional (checkerz/royal j96 text
boxes, the 41 `serpb31` + `CreateSmackTextBox` games) need proportional-looking output to fit
their boxes. A 100-px j96 cell per character could not fit "…" into a 396×96 box, and 28-px
serpb31 cells would make CreateSmackTextBox text very wide. So either a fresh `BmpFont` starts
proportional, or the text-box renderers always space proportionally. Both give the same result
for these callers [?].

---

## 6. What is wrong in `src/legacy/bitmap.cpp` today

| Problem | Effect |
|---|---|
| Glyphs are taken from single frames (`merit_read_frames` + `frames_to_px`) with no delta compositing | Broken, half-mirrored-looking glyphs |
| `glyphs[ch]` (frame = code) | Every character shows the next one (`A`→`B`) |
| Width = rightmost ink column + 2, measured on the delta frames | Wrong advances. There is also no left-bearing trim. |
| `font_for(w,h)` picks the file from the ctor size | Wrong font when (w,h) = (0,0), and the following `Load` decides anyway |
| Slot 0x08 reads `(a[1],a[2],a[3])` | Colours shifted (g→r, b→g, x→b) |
| Slot 0x24 treats 1 as centre and 2 as right | Centred text drawn right-aligned and vice versa |
| Slots 0x00, 0x04, 0x10, 0x28, 0x38, 0x4c, 0x50, 0x54, 0x58, 0x5c, 0x68 unimplemented (0x54 is treated as spacing) | Heights and widths are 0 (layout collapses), scores never drawn, delete leaks |
| The 48-slot fake vtable logs everything | Fine for debugging. Real slots stop at 0x68. |

---

## 7. Implementation checklist (`font_slot()` / `BmpFont` rewrite)

1. **Glyph loading.** Decode the `.dlt.gz` (version 3) to frames, then **composite in order**
   (canvas starts at frame 0, and each frame's written pixels overwrite it). Store
   `glyph[code] = canvas after frame code−1` for codes 1..min(255, frameCount). Treat RGB565
   0x0000 as transparent (`kKey`) after compositing, not before.
2. **Metrics per glyph:** the ink bounding box (left and right columns of non-black pixels),
   and the cell w/h from the header.
3. **Font state** (keep it inline in the 0x798-byte object or in the map, since games never
   touch fields): `GlyphSet*`, colour (r,g,b offsets plus the x byte), justification (0 left,
   1 right, 2 centre), mode (fixed/proportional), fixed width (0 = off), space width (default
   10), line height = cell h.
4. **Constructor** `BmpFont(w, h, 0xd6)`: install the vptr and default state (fixed mode,
   space 10, white, left). Remember (w,h) only as a fallback cell size. Don't pick a file from
   (w,h) when a `Load` follows (it always does). Keep a default glyph set so that unloaded
   fonts still draw.
5. **vtable with at least 27 slots** (+0x00…+0x68). Implement:
   - `0x00 GetHeight()` → cell h.
   - `0x04 GetStringWidth(s)` → sum of advances (same rules as drawing).
   - `0x08 SetColor(r, g, b, x)` → **args a[0..2]** are offsets from white. Ignore x (log it
     if it is non-zero).
   - `0x10 ResetColor()` → (0,0,0).
   - `0x18 Load(name, charset, flag)` → `/usr/local/gamedata/fonts/<name>.dlt.gz` (also try
     `.dlt`). Ignore `charset` (load all glyphs) and `flag`. Return 1 on success.
   - `0x20 (int)` → no-op (log it).
   - `0x24 SetJustify(j, arg2)` → j: 0 left, **1 right, 2 centre**. Ignore arg2.
   - `0x28 DrawNumber(n, x, y, w, a5, a6)` → `snprintf("%ld", n)`, then the same as DrawText
     (no commas: games call `CommaStr` themselves before `DrawText`).
   - `0x2c DrawText(s, x, y, w, h, a6, 0, 1)` → draw onto the current VB (`target_bitmap()`).
     Justify within [x, x+w], or about x when w = 0 (right: x−tw; centre: x−tw/2). Y is the top
     of the cell. Return the width.
   - `0x30 CreateTextBox(s, w, h, …)` → `_RADBitmap*` exactly as wide and tall as the
     (wrapped to w) text, filled with a uniform background colour at pixel (0,0) so that
     `BitmapToScreenTrans(b,…, GetPixel(b,0,0))` keys it out.
   - `0x38 MeasureText(s, x, y, w, h, a6, 0)` → return the width (don't draw).
   - `0x4c SetProportional()`, `0x50 SetFixed()`.
   - `0x54 SetFixedWidth(px)`, `0x58 ClearFixedWidth()`.
   - `0x5c GetSpaceWidth()` → `(unsigned char)space`, `0x60 SetSpaceWidth(px)`.
   - `0x64` complete dtor (free our state), `0x68` deleting dtor (free state, then
     `::operator delete(this)`, since the game allocated it with `operator new(0x798)`).
6. **Advance rule** (used by Draw/Measure/Width and the Smack text boxes alike). If a fixed
   width is set: advance = fixed width, ink centred in it. Else in proportional mode: space →
   space width, other glyphs → ink width + 1–2 px, drawn shifted left by the ink's left
   column. Else (fixed mode): advance = cell width, glyph drawn at its cell position. The
   `serpb31`/`j96` text-box callers in §5.5 need proportional output. Simplest: make
   `CreateSmackTextBox`/`CreateTextBox` always lay out proportionally, or make new
   `BmpFont`s default to proportional.
7. **Colour.** Per channel, out = glyph × (255 + offset) / 255, clamped, which keeps the
   anti-aliasing. Pre-coloured fonts (`3dwin`) are tinted the same way. `CreateSmackTextBox`
   uses the font's colour. `CreateColoredSmackTextBox` overrides it.
8. **MegacGraphics fonts:** the same object type and vtable. Start them in fixed mode with
   space width 10 (the state games restore at exit). Give +0x38 a font that suits numbers. The
   real file per slot is unknown, so keep a table that is easy to change.
9. **Logging:** keep the first-call log per slot. Log unknown slots and non-zero values of the
   unknown arguments (SetColor x, SetJustify arg2, Load flag, DrawText a6, DrawNumber a5/a6,
   slot 0x20) so that they can be tracked down later.
