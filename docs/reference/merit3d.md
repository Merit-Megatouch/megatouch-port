# Merit3D and AllegroGL games: the loader API they import

These are the loader-provided symbols that the OpenGL games need, and what our host must do for
each one. The games are the libmerit3d games (luxor, chainz2, stickerbook, triviawhiz2,
triviawhizjr, monkeybusiness, VideoSales, beer_pong_challenge) and the older AllegroGL games
(pool, nineball, bowling, minigolf, minigolfatw, minigolfspace).

Everything here comes from the game libraries and the helper libraries the cabinet ships:
dynamic symbols, relocations, `operator new` sizes, call sites in the disassembly, and the
Ghidra decompiles. The loader binary was not used. This document complements
[legacy.md](legacy.md) and [sprite-engine.md](sprite-engine.md), which cover the 2D API the same
games also use (`Bitmap`, `WorldClass`, `MegacGlobals`, sound).

## Conventions

- **Addresses** are ELF addresses, as objdump prints them. The Ghidra decompiles in
  `games/beer_pong_challenge/decomp/` add 0x10000; where a decompile address is quoted it says
  "decomp".
- **Confidence:** **[C]** confirmed from code, usually at several sites. **[L]** likely.
  **[?]** guess or unresolved.
- **Platform:** i386, g++ 3.4/4.x, Itanium ABI, the old COW `std::string`.
  - Member functions are cdecl with `this` first.
  - A function that returns a class by value takes a hidden result pointer *before* `this`, and
    pops it itself (`ret $4`).
  - A `std::string` passed by value is a pointer to a temporary that the caller owns and
    destroys.
- **libmerit3d.so** has the same md5 in all 8 games (`59057e1f…`), and so does
  **libmeritbasegame.so** (`f793ff4e…`). In libmerit3d the GOT is at 0x7bcf8.

---

## 0. Summary: what to build, by priority

### Already defined by shipped libraries

Load these instead of reimplementing them.

| Symbols | Provided by | Notes |
|---|---|---|
| `Merit3d::RenderToTexture::*`, `Merit3d::VideoObjectData::*` | `/usr/local/lib/libmvideo.so` [C] | Only beer_pong ships it in `lib/`. The other 7 libmerit3d games import the symbols but lack the file, so add it to their PRELOAD. It needs libogg, libvorbis and libtheora (cabinet `/usr/lib`), GL, and 8 Allegro audio-stream and voice functions (§4.6). |
| `CoinJamManager::*` | `libmoney.so` | **Do not load it.** It has 192 undefined loader symbols. Stub it as in §6.1. |
| `ExtendedCrashInfo` ctor, dtor, vtable, typeinfo | `libdebug_mock.so` [C] | Add `libdebug_mock.so` to PRELOAD for every game; beer_pong already has it. |
| `allegro_gl_flip`, `__aglXGetProcAddressARB` | `libagl.so` | **Do not load it.** It needs about 100 liballeg internals. Define these 2 symbols ourselves (§5). |
| `layout::ScreenControl::GetCurrentWidth/Height` | `liblayout.so` | Our own definition already exists (`legacy.cpp`). Keep it. Loading the real one pulls in X11. |

### Must implement, ordered by what blocks start-up and rendering

1. **`textSystem` / `TextSystem` / `TextDesc` / `Colour`** (§1, §2). Every game creates text in
   its first frame. **Bug in our code:** `legacy.cpp:871` defines `TextSystem textSystem;` as an
   object. Every game reads the symbol as a **`TextSystem*`** (§1.1).
2. **`allegro_gl_flip`, `__aglXGetProcAddressARB` (a data object), and `FlipBitmapVert`
   (3 arguments)** (§5).
3. **`MeritInput::InputManager::GetMouse()` must return `Mouse_T` by value.** Our version in
   `merit_services.cpp` returns `void*`, so `Scene::Update` and every `Button2d` get an
   uninitialised mouse (§7.3).
4. **`DoRegularProcessing()`** must pump input, because it is the per-frame service hook (§6.3).
5. **libmvideo:** add it with its codec libraries and the Allegro audio-stream functions (§3, §4).
6. **Small functions** (§6):
   - `Locale::LanguageManager::IndexOf` must be **static**. `loader_services.cpp:241` declares
     it as a member, which shifts its argument by one slot.
   - `ContinueControl::display(uint,uint,uint)`, `display()`, and the static `allowed()`.
   - `MyMeritManager`, `Profiler`, `CoinJamManager`, `Heartbeat_WaitForAll`,
     `debug_check_address`.
7. **`TextUtils::ConvertToMarkup` and `TextSystem::ConvertToMarkup` must keep markup tags.**
   Callers wrap strings in `<b>…</b>` *before* converting them. Our version in
   `src/gendef/trivia.cpp:273` escapes `<` and `>` (§1.6).

---

## 1. Text: `TextSystem`, `TextDesc`, `GetSpriteChannels`

### 1.1 `textSystem` is a pointer [C]

Every user loads the GOT slot and then **dereferences it**, passing the value as `this`:

- libmerit3d `Text2d::Create` 0x4cb4d: `mov -0x80(%ebx),%edx; mov (%edx),%esi; push %esi;
  call SetTextDescription`. GOT 0x7bc78 = `textSystem`.
- bigroller `Quintzee_Text::SetDefaults` 0x5aaf: `mov (%eax),%eax; mov %eax,0xac(%esi)` stores
  the pointer in the object.
- pool 0x225ec, luxor 0x70ebd, monkeybusiness 0xfce8 and phunt_new 0x89f7 all do `push (%eax)`.

So `textSystem` must be declared `TextSystem* textSystem = &g_text;`, and the pointer must not
be NULL. With the current `TextSystem textSystem;` (an empty class), `this` becomes whatever
bytes follow the object. That is harmless while every method ignores `this`.

### 1.2 Members used, with exact call shapes [C]

| Member | Kind | Callers | Notes |
|---|---|---|---|
| `void SetTextDescription(TextDesc const&)` | member | everyone | Copies the descriptor into the system's current state. The caller's `TextDesc` is a stack object that is modified and passed again (monkeybusiness changes the colour between two calls, 0xfcb3 and 0xfe4a), so **copy it; do not keep the reference**. |
| `void UseDefaultTextDescription()` | member | luxor 0x6d0de | Resets the state to the default `TextDesc()` (§1.3). |
| `void GetSpriteChannels(char const* markup, uchar** out1, uchar** out2, int w, int h, bool, bool, int bpp, float scale)` | member | libmerit3d, pool, nineball, bowling, minigolf×3, luxor, chainz2, monkeybusiness, and about 15 legacy games | §1.4 |
| `CoordT<int> GetTextSize(char const* markup, int maxWidth, bool)` | member, returns a struct | luxor 0x70f7e, chainz2 | Called as `(&ret, this, text, 800, true)`; the caller pops 0x1c of the 0x20 bytes pushed, so the callee pops the hidden pointer. Returns `{w, h}` in pixels of the text laid out with the current description and wrapped at `maxWidth`. luxor then sizes its texture as `w*k+2` by `2*h+2`. [L for meaning] |
| `static char* ConvertToMarkup(char const*)` | **static** (one push) | bigroller 0xba55, monkeybusiness 0xfd8e, pool, nineball, luxor, chainz2 | §1.6 |
| `void LoadTranslations(char const*, bool)`, `LoadTranslations(xml_gameinfo::GameIds)`, `SetCurrentLanguage(Locale::Languages)` | member | luxor 0x6cf4e/0x6cf61, minigolf, bowling | Already implemented in `legacy.cpp` (forwarding to Translator). |
| `DisplayOnBitmap(NBitmap&, char const*, CoordT<int> const*, CoordT<int> const*, bool, bool)` | member | symboltritowers only (legacy) | Not needed by the GL games. |

### 1.3 `TextDesc`: sizeof 0x54 [C]

Evidence for the size:

- idle.so 0x2668e does `new(0x54)` and then calls `TextDesc::TextDesc(char const*, int)`.
- pool `CreateTextBitmap` (0x2258e) has its stack `TextDesc` at -0x68 and the next local (the
  out pointer) at -0x14. -0x68 + 0x54 = -0x14.

The inline constructors define every field's default:

- `TextDesc(char const* font, int size)`: chainz2 0x819d4, pool 0x226f0, luxor 0x70cb0.
- `TextDesc()`: elevenup 0x100aa.

| Off | Type | Default | Meaning | Evidence |
|---|---|---|---|---|
| +0x00 | 32 bytes | set by `SetFontName` | **font family name**. Only `SetFontName` and our TextSystem touch it, so its layout inside 0x00–0x1f is ours to choose. Use an inline `char[32]` (truncated, NUL-terminated). Games copy `TextDesc` by value, so do not keep a heap pointer. | first inline field is +0x20 [C] |
| +0x20 | int | ctor arg; `TextDesc()` = 12 | **point size** | all ctors; libmerit3d 0x4ca14 stores Create's `size` here [C] |
| +0x24, +0x26, +0x28 | short r, g, b | `Colour::white` | **text colour**, each 0–255 | monkeybusiness 0xfcb8: `Colour(8,8,8)`, with r → +0x24, g → +0x26, b → +0x28 [C]. symboltritowers `CreateTextString` 0x7192 [C] |
| +0x2a, +0x2c, +0x2e | short r, g, b | `Colour::black` | second colour: outline, shadow or background | default only; no game was seen writing it [L] |
| +0x30 | int | 0 | ? | [?] |
| +0x34 | int | 0 | ? | [?] |
| +0x38 | int | 0 | **horizontal alignment**: 0 left, 1 centre, 2 right (Pango order) | libmerit3d copies `HorzAlign` 0/1/2 (0x4cf81). pool maps -1/0/other → 0/1/2 (0x225cd). luxor maps -1/0/1 → 0/1/2 (0x70e6d). symboltritowers stores its `TextDesc::ALIGNMENT` arg (0x71d0) [C] |
| +0x3c | int | 0 | **vertical alignment**: 0 top, 1 centre, 2 bottom | libmerit3d copies `VertAlign` 0/1/2 (0x4cfa1). luxor: arg -1 → 0, else 1 (0x70e60) [C for 0/1/2, L for meaning] |
| +0x40 | int | 2 | a mode with values 0, 2 or 3 | luxor sets 2 when it has a fixed box and flag +0x2c, 0 for a fixed box, 3 for auto-size (0x70eb5, 0x70ee7, 0x70efd). Possibly wrap or ellipsize; Pango `ELLIPSIZE_END` = 3 [?] |
| +0x44 | int | 0 | ? | [?] |
| +0x48 | byte | 0 | flag; idle sets 1 | idle 0x26707 [?] (bold? outline on?) |
| +0x4c | int | 0 | ? | [?] |
| +0x50 | int | -5 | **line spacing**, probably pixels added between lines | symboltritowers passes an explicit arg (callers pass -5). libmerit3d 0x4cb46 and bigroller 0xb9d5 set 0 when `LanguageManager::Active()` is 0x10, 0x18, 0x1b, 0x1c or 0x1e (tall scripts). idle sets -8 [L] |

- **`TextDesc::SetFontName(char const*)`**: a member that copies the name into +0x00. Font names
  seen include "quercus" (beer pong; fontconfig alias to EFN QuercusC) and "Bureau" (libmerit3d
  FPS counter, 0x4b2xx). Resolve them through the project's existing fontconfig setup.
- **The unknown fields** (+0x30, +0x34, +0x40, +0x44, +0x48, +0x4c, and the second colour) only
  change appearance. Rendering with name, size, colour, alignment and spacing is enough to start
  with.

### 1.4 `GetSpriteChannels`: buffer contract

Every call site seen, from a survey of every cabinet `.so`:

| Caller | w, h | bool, bool | bpp | scale | What it does with the result |
|---|---|---|---|---|---|
| libmerit3d `Text2d::Create` 0x4cbb2 | Create's w, h | 1, 1 | 32 | 1.0 | Swaps rows top↔bottom in `out1` (w×4 bytes per row, h/2 swaps, 0x4cbe8–0x4cda3). Then `memblit32`s into a zeroed power-of-two RGBA buffer and calls `TextureManager::LoadTextureFromMemory(buf, p2w, p2h, 4)` (GL_RGBA, GL_UNSIGNED_BYTE). **Never frees `out1` or `out2`.** |
| pool and nineball `CreateTextBitmap` 0x22639 | bitmap w, h (+0x4c/+0x50) | 1, 1 | 32 | 1.0 | Copies row `h-1-i` of `out1` (w×4 bytes) into row i of its own buffer. Never frees. |
| luxor and chainz2 `GLText::Text` 0x71045 | from `GetTextSize`, or explicit | 1, 1 | 32 | 1.0 | Reads `out1` as u32 per pixel with alpha in bits 24–31 (`and $0xff000000`). Blends a shadow. Pads to a power of two with malloc and copy, and frees only its own copies. |
| bowling, minigolf×3 | | 1, 1 | 32 | 1.0 | |
| monkeybusiness 0xfdcc | 170×30 | 1, 1 | **16** | `world+0x1a4` float | Then calls `Bitmap::TTFtoCData(bmp, out1, out2, 170, 30)` |
| legacy games (bigroller, dominoes, opsetup, phunt*, take2, zip21…) | | usually 1, 1 (phunt passes its own args) | **16** | 1.0 | Then calls `TTFtoCData(bmp, out1, out2, w, h)` |
| luxor2 (no content) | | 1, 1 | **24** | | |

Contract derived from these callers:

- **Ownership [C]:** no caller ever frees `out1` or `out2`. TextSystem owns both buffers. Reuse
  or grow them on each call; they must stay valid at least until the next `GetSpriteChannels`.
  Every caller consumes them immediately.
- **Dimensions [C]:** exactly `w × h` as passed. The caller fixes the size; it is not measured.
  Text is laid out inside the box (wrapped at `w` [L]) and aligned according to +0x38/+0x3c. It
  is clipped to the box.
- **Orientation [C]:** row 0 is the **top** line. libmerit3d and pool both flip the rows
  themselves for GL.
- **bpp = 32 [C]:** `out1` is w×h×4 bytes of **R, G, B, A** (a u32 `0xAABBGGRR` on
  little-endian). This is the GL_RGBA/UNSIGNED_BYTE upload format.
  - RGB = the text colour (+0x24) and A = glyph coverage. Use straight (non-premultiplied)
    alpha [L]. libmerit3d sets the colour to white (0xff) and tints with the vertex colour, and
    it blends with `GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA`.
  - Transparent pixels should be all zero.
  - `out2` is never read in 32-bpp mode [C]. Point it at an 8-bit w×h alpha plane anyway.
- **bpp = 16 [L]:** `out1` = w×h `uint16` 16-bit colour and `out2` = w×h 8-bit alpha, the two
  "channels" that `Bitmap::TTFtoCData` packs into the legacy sprite format. Our `TTFtoCData` is
  a no-op, so this matters only for monkeybusiness and the legacy games' text.
- **bpp = 24:** RGB with 3 bytes per pixel [?] (only luxor2, which cannot run).
- **scale [L]:** a font-size multiplier. It is 1.0 everywhere except monkeybusiness, which passes
  `MegacGlobals+0x207c → WorldClass+0x1a4` (the world's resolution scale).
- **The two bools [?]:** always `true, true` in the GL games. phunt-family `CreateTextString`
  forwards its own two bools, and every observed caller passes 1, 1. Ignore them.
- **Return value:** void [C]; nobody reads `eax`.

### 1.5 Recommended implementation

Use the Pango and FreeType path the project already uses for `BitmapTextTTF` (`ttf.cpp`):

1. `pango_layout_set_markup(markup)` with the family and `size*scale` from the current
   `TextDesc`.
2. `set_width(w*PANGO_SCALE)`, wrap by word, alignment from +0x38, spacing from +0x50.
3. Render into an 8-bit FT_Bitmap of w×h, offset vertically according to +0x3c (top, centre,
   bottom of the box).
4. Write `out2` = coverage and `out1` = colour + coverage.

`libtext_system.so` (`TextSystem::PangoTextSystem`, a sibling implementation by the same team)
does the same thing: `pango_ft2_render_layout` into a 256-gray FT_Bitmap (`GenerateRawImg`
0x1b70). That supports this design [L].

### 1.6 `ConvertToMarkup` must keep tags [C]

Callers build markup *before* converting:

- libmerit3d `Text2d::Create` wraps the text in `<b>`/`</b>`, `<i>`/`</i>` or `<u>`/`</u>`
  (attrib bits 1, 2 and 4; strings at 0x76f06…0x76f1c) and *then* calls
  `TextUtils::ConvertToMarkup` (0x4cb86).
- phunt_new `CreateTextString` (0x8978) does `strcpy("<b>") + strcat(text) + strcat("</b>")`,
  then `TextUtils::ConvertToMarkup`.
- monkeybusiness passes `"<b>" + Translate("START") + "</b>"` to `TextSystem::ConvertToMarkup`
  (0xfd8e; strings at 0x19ead/0x19eb1).
- symboltritowers does the reverse: `snprintf("<b>%s</b>", TextUtils::ConvertToMarkup(text))`.

So the conversion must leave `<`, `>` and tags alone. Escape only a bare `&` (one that does not
start an entity) as `&amp;`, and convert Latin-1 input that is not valid UTF-8 to UTF-8. Return
a static or rotating buffer; callers never free it [C].

---

## 2. `Colour::white`, `Colour::black` [C]

- **Type:** `struct Colour { short r, g, b; }`, so **sizeof 6**.
  - The inline `Colour::Colour(int r, int g, int b)` (chainz2 0x8128e) stores 3 16-bit values at
    +0, +2 and +4.
  - monkeybusiness keeps two stack `Colour`s 6 bytes apart (-0xcc and -0xc6).
- **Usage:** games and libmerit3d copy the 3 shorts into `TextDesc+0x24` (white) and `+0x2a`
  (black) through `R_386_GLOB_DAT` (libmerit3d GOT 0x7bbfc and 0x7bc1c, read at 0x4ca05/0x4ca2d).
  Define them as data objects: `Colour Colour::white = {255,255,255}, Colour::black = {0,0,0};`
  [L for values: the colour fields are 0–255 elsewhere].
- **Side finding (legacy):** symboltritowers `CreateTextString` (0x716b) adds 255 to all three
  components if any one is negative. That explains the `BitmapTextTTF` colour triples
  "0,0,-255" and "0,-100,-255" noted in PROGRESS.md: they become (255,255,0) and (255,155,0)
  [L: same family, same convention].

---

## 3. `Merit3d::RenderToTexture` (defined in libmvideo.so)

### 3.0 Where it lives: a shipped library, not the loader

- **[C]** All of `Merit3d::RenderToTexture` and `Merit3d::VideoObjectData` are defined in the
  cabinet library `/usr/local/lib/libmvideo.so`. The copy in `games/beer_pong_challenge/lib/` is
  byte-identical.
- luxor, chainz2, stickerbook, monkeybusiness, VideoSales, triviawhiz2 and triviawhizjr import
  these symbols but do not ship the library. The fix is to add `libmvideo.so` to every libmerit3d
  game; we do not need to reimplement these classes.
- libmerit3d does not NEED libmvideo. libmvideo NEEDS only libstdc++, libm, libgcc_s, libpthread
  and libc. Everything else it uses must already be loaded globally before it:
  - **GL:** `glBindTexture`, `glDeleteTextures`, `glEnable`, `glGenTextures`, `glTexImage2D`,
    `glTexParameteri`, `glTexSubImage2D`.
  - **Theora:** the old-API `theora_*` from the cabinet's `/usr/lib/libtheora.so.0.2.0`
    (luxor's `lib/libtheora.so.0` has the same md5). It NEEDS `libogg.so.0`.
  - **Ogg and Vorbis:** `ogg_*` and `vorbis_*` from the cabinet's `libogg.so.0.5.3` and
    `libvorbis.so.0.3.1`. These are preferred because libmvideo embeds their structs at fixed
    sizes [L].
  - **Allegro:** `play_audio_stream`, `get_audio_stream_buffer`, `free_audio_stream_buffer`,
    `stop_audio_stream`, `voice_start`, `voice_stop`, `voice_get_position`, `voice_set_volume`.
    **None of these exist in `src/legacy/allegro.cpp` yet** (§4.6).
  - **Legacy:** `SystemTimer()` (exists) and `Bitmap::Bitmap(int,int,uchar,uchar)` (exists;
    only reached from `GenerateBitmap`, which libmerit3d never calls).
- **Suggested PRELOAD order, after GL:**
  `libogg.so.0 libvorbis.so.0 libtheora.so.0 libmvideo.so`. Beer pong's game.conf has
  libmvideo but not the codec libraries, which is why `notes/unresolved.txt` lists `theora_*`.

### 3.1 Layout: sizeof 0x14 [C]

| Off | Type | Meaning |
|---|---|---|
| +0x00 | GLuint | texture name (`glGenTextures(1, this)` in Create, 0x4a90). **The only field libmerit3d reads inline.** |
| +0x04 | uint | texture width: a power of two clamped to 16..1024 |
| +0x08 | uint | texture height: same rule |
| +0x0c | uint | components: 3 = GL_RGB, 4 = GL_RGBA |
| +0x10 | bool | created |

- The ctors (0x4180/0x41b0) zero all five fields. The ctor taking `(w,h,c)` (0x4d70) zeroes them
  and then calls Create.
- Size evidence: in libmerit3d's 0x70-byte animation object the RTT is at +4 and the next field
  is at +0x18. In `VideoObjectData` the RTT is at +0x27c and the next field is at +0x290.

### 3.2 Methods [C]

- **`Create(w,h,c)`** (0x4a90):
  - Calls `glEnable(GL_TEXTURE_2D)`. Returns at once if already created with the same w, h, c;
    otherwise calls Destroy first.
  - Rounds w and h up to a power of two, clamps them to 16..1024, and clamps c to 3..4.
  - `glGenTextures`, then bind. Sets `WRAP_S/T = GL_CLAMP` and `MIN/MAG_FILTER = GL_LINEAR`.
  - Uploads `glTexImage2D(…, fmt, w, h, 0, fmt, GL_UNSIGNED_BYTE, zeros)` with fmt = `GL_RGB` if
    c is 3, else `GL_RGBA`. The zero buffer is new[]/delete[].
  - Sets created = 1 and binds texture 0.
- **`Destroy()`** (0x4280): `glDeleteTextures(1,&tex)`, zero all fields, bind 0. It is called
  even when nothing was created (it deletes texture name 0, which is harmless). The dtor just
  calls Destroy.
- **`UpdateTexture(uchar* px)`** (0x4a10): if px is not NULL, calls `glTexSubImage2D(0,0,0,texW,
  texH, c==3?GL_RGB:GL_RGBA, GL_UNSIGNED_BYTE, px)`. The buffer is texture-sized, row 0 first.
- **`UpdateDeltaTextureFull(ushort* frame, uchar* rgba, ushort yoff)`** (0x4340):
  - Decodes one RLE frame into the caller's persistent texW×texH×4 RGBA buffer.
  - Zeroes every pixel whose u32 is below 0x18000000 (alpha < 0x18).
  - Uploads the whole texture.
- **`UpdateDeltaTextureDirty(ushort* frame, ushort yoff)`** (0x45f0): decodes each row into a
  stack `uint32[512]`, so the texture must be at most 512 wide. Uploads only the dirty span of
  each row (`glTexSubImage2D(x0,row,span,1,…)`).

### 3.3 Delta frame format: the opcode stream of `src/common/merit_rle.h`

- Word 0 is a starting row; in practice it is always an opcode-0 "skip rows" word [C].
- After that, each word is opcode = `w>>10` and n = `w&0x3ff`:
  - **1:** x += n.
  - **2:** n opaque RGB565 words.
  - **3:** next row.
  - **4:** end of frame.
  - **5:** n pairs of words (colour, then a in the low byte).
  - **Anything else:** prints `"Error in reading data!!!!!!!!!!"`.
- **Pixels:** `R=(c>>11)<<3`, `G=((c>>5)&63)<<2`, `B=(c&31)<<3`, written as bytes R, G, B, A.
  The low bits are not replicated.
- **Alpha:** opcode 2 gives A = 0xff; opcode 5 gives `A = (0xff - 8*a) & 0xff`. So here **a is
  transparency** (0 = opaque). **[?] This contradicts `merit_rle_decode`**, which treats a = 31 as
  opaque. `stickerbk/finishpicture.dlt.gz` has its a-histogram peaking at 29 and 31, which is
  consistent with libmvideo's reading. Check against a screenshot.
- **Orientation:** frame row y goes to texture row `texH-1-(r0+yoff+k)`, i.e. bottom-aligned and
  bottom-up.

### 3.4 Use from libmerit3d [C]

- **`Animation2dManager::LoadDeltaAnimation(name, bool full, bool preload)`**:
  - `new(0x70)`, then `RenderToTexture::RenderToTexture(obj+4)`.
  - `GetDeltaTextureStats` reads the `.dlt.gz` header into +0x24 w, +0x28 h, +0x2c/+0x30 the
    power-of-two sizes, and +0x34 = 4.
  - If `full`: +0x38 = `calloc(texW*texH*4)`.
  - Opens the file with gzopen, skips the v3 header (u32 3, then 8 bytes), and calls
    `Create(obj+4, p2w, p2h, 4)`.
  - If `preload`: reads every `{u32 size, w, h, data}` record into malloc'd buffers at
    +0x1c/+0x20.
- **`Animation2dManager::GetTextureId(anim, frame)`** (0x28ec0):
  - Frame 0 clears the buffer, calls `UpdateTexture(zeros)` and rewinds.
  - It then applies frames up to the requested one with `UpdateDeltaTextureFull(obj+4, data,
    buf, p2h-h)` (0x2919a) or `…Dirty` (0x29244).
  - It returns `*(obj+4)`, the GL name.
- Animation type 1 (0x2a181) calls `Create(w,h,c)` and then `UpdateTexture(pixels)`.
  `UnloadAnimation` (0x29460) calls Destroy and the dtor.
- **0x70 object:**

  | Off | Field |
  |---|---|
  | +0 | type (0 texture list, 1 single RTT, 2 delta) |
  | +4 | RTT |
  | +0x18 | gzFile |
  | +0x38 | full buffer |
  | +0x3c | current frame |
  | +0x40 | std::string name |
  | +0x44 | refcount |
  | +0x48 | vector<int> |
  | +0x54 | vector<string> |
  | +0x60 | -1 |
  | +0x68 | 200 |
  | +0x6c | 3 |

- **Delta files on disk:** `gamegraphics/misc/loadnew.dlt.gz` (256×88, 26 frames; every
  Merit3D game uses it as the loading animation) and `stickerbk/finishpicture.dlt.gz` (512×512,
  118 frames).
- **A GL context must be current** before any Create, which happens when an animation or video is
  loaded.

---

## 4. `Merit3d::VideoObjectData` (defined in libmvideo.so)

### 4.1 sizeof 0x57c, polymorphic [C]

- `VideoObjectManager::LoadVideo` (libmerit3d 0x6e990) does `new(0x57c)`.
- The vtable (libmvideo 0x161a0) holds the dtors. libmerit3d deletes through slot 1
  (`call *0x4(%eax)`, 0x6e8a7).

Fields libmerit3d reads or writes inline are in **bold**:

| Off | Meaning |
|---|---|
| +0x00 | vptr |
| +0x04 | useTexture = 1 |
| +0x05 | initialised |
| +0x08 | FILE* |
| +0x0c | ogg_sync_state |
| **+0x28 / +0x2c** | **theora_info width / height (`GetVideoSize` 0x6ecd0)** |
| +0x90 | theora_comment |
| +0xd0 | theora ogg_stream_state |
| +0x238 | theora_state |
| +0x260…+0x26c | buffer w, h, comps (3), bytes |
| **+0x27c** | **RenderToTexture: tex at +0x27c (`GetTextureId` 0x6edc8), tex w/h at +0x280/+0x284 (`GetTextureSize` 0x6ed67)** |
| +0x290 | RGB24 frame buffer |
| **+0x2b8** | **bool playing (`IsVideoPlaying` 0x6ee43)** |
| +0x2bc | std::string filename |
| **+0x2c0** | **end mode, written by LoadVideo (0x6e9ea): 1 = stop and close audio, 2 = loop via Restart(0), other = stop** |
| **+0x2c8** | **vorbis header count; `HasAudio` (0x6ecb2) tests it for != 0** |
| +0x2cc… | vorbis state |
| +0x3d4 | AUDIOSTREAM* |
| +0x545 | hasAudio |
| +0x568 | start ms |
| +0x574 | paused |
| +0x575 | noAudio |
| +0x576 | firstFrame |
| +0x578 | time offset |

### 4.2 Methods [C]

- **Ctor** (0x3ee0): constructs the RTT, sets +4 = 1 and calls `Clear()`. Clear sets playing = 1
  and paused = 1.
- **`Load(path, bool noAudio)`** (0x32a0):
  - `fopen(path,"rb")`. On failure it prints `"Bogus filename %s given to
    VideoObjectData::Load"` and returns.
  - Otherwise it parses the Ogg headers: Theora is required, Vorbis is optional. It then calls
    `allocate_data(w,h,3)`, which runs `RTT::Create(w,h,3)` and mallocs the RGB buffer.
  - **It calls `exit(1)` on corrupt headers.** Only feed it valid Ogg/Theora files.
- **`Play()`**: with no file or theora, sets playing = 0. Otherwise playing = 1. If hasAudio:
  `play_audio_stream(0x800, 16, channels, rate, 255, 128)` and `SetVol(255)`. Un-pausing calls
  `voice_start`.
- **`Pause()`**: paused = 1 and `voice_stop`.
- **`Stop()`**: playing = 0 and close the audio.
- **`Restart(int)`**: 0 = full reload and Play; libmerit3d always passes 0 (0x6eec5).
- **`Update(float)`** (0x3970): **the float is ignored**; the clock is `SystemTimer()` in ms.
  - Returns 0 if there is no file, or it is not playing, or it is paused.
  - Decodes Vorbis to unsigned 16-bit interleaved samples (`s*32768+0x8000`) into
    `get_audio_stream_buffer` chunks of 0x800 × channels. Decodes Theora until it catches up.
  - At EOF it applies the end mode.
  - When a frame is due: `write_video` (YUV420 → RGB24, top row first, stride texW×3), then
    `RTT::UpdateTexture`, and returns 1.
- **`FirstFrame()`** returns +0x576. **`isVideoLoaded()`** returns `file && theora_p`.
  **`Shutdown()`** tears everything down and calls `RTT::Destroy` and `Clear()`.

### 4.3 Call order from libmerit3d [C]

- **Load:** `VideoObject2d::Load(name, bool withAudio, int endMode)` (0x6f870) →
  `VideoObjectManager::UnloadVideo(old)` → `LoadVideo(name, endMode, withAudio)`. LoadVideo does
  `new(0x57c)`, the ctor, `Load(name, !withAudio)`, sets +0x2c0 = endMode, and returns the index
  in a `vector<VideoObjectData*>`.
- **Every frame:** `Scene::Update` (0x36ffc) → `VideoObjectManager::Update(dt)` → each
  `VideoObjectData::Update`.
- **Draw:** `VideoObject2d::Draw` (0x6f1f0) binds the +0x27c texture and computes texture
  coordinates from video size / texture size.
- **Unload:** `Shutdown()`, then the deleting dtor.

### 4.4 Which games play video [C]

- Only **VideoSales** and **triviawhiz2** import `VideoObject2d`.
  - **VideoSales:** `StatePlay::LoadVideo` (0x9335) calls `Load(GLGame::GetVideoToPlay(), true,
    1)`. The only file is `gamegraphics/VideoSales/Videos/ami_access.ogg`: Theora 640×480 at
    25 fps, plus Vorbis stereo 44.1 kHz. That gives a 1024×512 RGB texture.
  - **triviawhiz2:** loads `"../videowhiz/videos/…"` (0x2ac67). **That folder is not on the
    image**, so fopen fails and the game carries on.
- luxor, chainz2, monkeybusiness and stickerbook pull libmvideo in only through libmerit3d (for
  the RTT delta animations). Their `.ogg` files are Vorbis audio for `SoundOGG`, not video.

### 4.5 Minimum behaviour if video is not decoded

- With the real libmvideo and a missing file, this already works: `isVideoLoaded()` = false,
  `Update` returns 0, `Play()` sets playing = 0, and the texture is 0.
- A stand-in, if one were ever needed, must keep:
  - sizeof ≥ 0x57c and a vptr whose slot 1 is the deleting dtor.
  - +0x27c/+0x280/+0x284/+0x28/+0x2c/+0x2c8 = 0.
  - Play → +0x2b8 = 0.

### 4.6 What libmvideo needs from us

- **Shared libraries:** libogg.so.0, libvorbis.so.0 and libtheora.so.0, preloaded globally
  before libmvideo.
- **Allegro audio-stream functions in `allegro.cpp`:**
  - `AUDIOSTREAM* play_audio_stream(int len, int bits, int stereo, int freq, int vol, int pan)`.
    libmvideo only reads `stream->voice` (int at +0).
  - `void* get_audio_stream_buffer(AUDIOSTREAM*)`: returns NULL until the previous chunk is
    consumed. A chunk is len × channels × 2 bytes of unsigned 16-bit samples.
  - `free_audio_stream_buffer`, `stop_audio_stream`, `voice_start`, `voice_stop`,
    `voice_set_volume(voice, 0..255)`, `voice_get_position` (effectively unused).
- **Simplest acceptable version:** `play_audio_stream` returns NULL. Every path null-checks the
  stream (Play, SetVol, Update), so video plays silently at wall-clock speed. The only cost is
  that Vorbis pages queue up in memory, about 7 MB for VideoSales.
- Voice ids must share the id space of the existing legacy voices: minigolf also imports
  `voice_set_volume`, `voice_check` and `voice_ramp_volume`.

---

## 5. AllegroGL and window glue

### 5.1 The loader set up GL before the game runs [C]

- **No game library and no libmerit3d import** `install_allegro`, `allegro_gl_set`,
  `set_gfx_mode` or `install_allegro_gl`. The setup functions they do have are empty:
  - libmerit3d `RenderDeviceAlleggl::Initialize` (0x346f0) and `Terminate` (0x34700).
  - minigolf `OldMerit3d::RenderDeviceAlleggl::Initialize` (0x4c73c).
  - pool and nineball `Game::InitAllegroGL` (0xfd98).
- The loader opened the window and the GL context, and set `screen`, `gfx_driver`, the mouse,
  keyboard and timers, before calling `__EntryPointV12`.

**Context requirements:**

- **Double-buffered RGBA with a depth buffer.** libmerit3d clears with `glClear(0x4100)` =
  COLOR|DEPTH (around 0x350c3); pool enables `GL_DEPTH_TEST`.
- **No stencil.** No game imports `glStencil*`.
- **A fixed-function (compatibility) profile.** The games call `glBegin`, `glLightfv` and
  `glLightModelf`.
- **`GL_ARB_multitexture`.** libmerit3d, pool and minigolf import `glActiveTextureARB` directly.
  minigolf also imports `glClientActiveTextureARB` and `glMultiTexCoord2fvARB`. libmerit3d
  imports `glCopyTexImage2D`; chainz2 and minigolf import `glReadPixels`.
- Our `MEGA_GL=1` path in `legacy.cpp` (SDL GL 2.1, depth 24, double buffer) meets all of this.

**Viewport and 2D space = the window size** [C]:

- libmerit3d `RendererOpenGL::Initialize` calls `glViewport(0,0,W,H)`.
  `RendererOpenGL::SetProjectionMode` (0x34da0) calls `glOrtho` with the same values (0x34eed).
- W and H are `layout::ScreenControl::GetCurrentWidth/Height()` on `ScreenInfo::GetInstance()+
  0x58` (every call site does `add $0x58,%eax`: 0x34dce, 0x360ce, 0x3b11b).
- pool and nineball call `glViewport(0,0,gfx_driver->w,gfx_driver->h)` (+0x68/+0x6c,
  0xfc7e–0xfc9d). If `gfx_driver` is NULL the viewport becomes 0×0.
- bowling uses `gluOrtho2D(0,640,0,480)` (0x14cce) on top of the loader's viewport.
- **Rule [L]:** ScreenControl width/height = `gfx_driver->w/h` = GL drawable size = the game's
  design size. Beer pong and the triviawhiz games are 800×600, luxor and chainz2 are 1024×768,
  the rest 640×480. Beer pong centres its coin-jam box at (400,300). To scale the window, render
  to an FBO and blit it scaled; never change the drawable size, or the viewport and mouse mapping
  break.

### 5.2 What each library imports [C]

Only **two** libagl exports are imported by any game:

- `allegro_gl_flip`: libmerit3d, beer_pong, triviawhiz2, triviawhizjr, luxor, chainz2, pool,
  nineball, bowling, minigolf×3.
- `__aglXGetProcAddressARB`: libmerit3d only.

Allegro imports and where they are used:

| Library | Uses |
|---|---|
| libmerit3d | `position_mouse` (`Scene::SetMousePosition` 0x36710) |
| beer_pong, triviawhiz2, triviawhizjr | On exit, `__EntryPointV12` does `clear_bitmap(screen); allegro_gl_flip(); clear_bitmap(screen)`. Beer pong also reads `key[]` (debug keys) and calls `voice_set_volume`. |
| pool, nineball | `Game::Run` loop: `keyboard_needs_poll`/`poll_keyboard`, `CheckKey`, a key[] diff calling `OnKeyDown/Up`, `MouseManager::CheckLoc`, mouse_b/x/y calling `OnMouseDown/Up/OnMotion`, then `Update`, then `Draw`, which flips. `install_int(cb,1)` is a 1 ms tick (0xfd8b). `CreateBitmap` (0x22356) uses `set_color_depth(32)` and `create_bitmap`. `save_bitmap` runs only when recording frames. `OnMotion` re-centres the cursor with `position_mouse(w/2,h/2)` (0x1a20f). |
| bowling | flip, `create_bitmap_ex` (32 bpp, in `GetFlipped32Bitmap`), `install_int`, mouse_x/y/b, `FlipBitmapVert` |
| minigolf×3 | `RenderDeviceAlleggl::Swap` flips. `screen`/`blit`/`masked_blit` appear only in `MyCheckCoinJam`. Also `position_mouse`, `voice_*`, ScreenInfo/ScreenControl. |
| luxor, chainz2 (MumboJumbo engine) | flip, `blit`, `create_bitmap_ex`, `mouse_b`, `FlipBitmapVert`. chainz2 also uses `key`, `voice_get_position` and `mouse_button_presses_cached`. `Image::blitSection` (chainz2 0xa3ddc) memcpys into and out of `bmp->dat` (+0x28), so **memory BITMAPs need contiguous `dat` with pitch = w × bytes per pixel.** |

- **`MyCheckCoinJam(bool)`** exists in every game and is the coin-jam overlay. It is the only
  user of `FlipBitmapVert`/`glDrawPixels`/`screen` blits, and it runs only when a coin jam is
  signalled. Beer pong's equivalent is `BeerPongGame::CoinJam`, which calls
  `Merit3d::Game::HandleCoinJam`.
- **`mouse_button_presses_cached`** is a Merit extension in the cabinet liballeg (an `int`). The
  mouse-update code ORs each pressed button bit into it (liballeg 0x69e66). chainz2 clears it and
  tests it for nonzero as "a click happened since the last check" (0x7f5aa). **It is missing from
  our allegro.cpp.**

### 5.3 `allegro_gl_flip` [C]

- **Signature:** `extern "C" void allegro_gl_flip(void)`.
- **Real implementation:** libagl 0x15dd0 calls the driver's flip, which is
  `glXSwapBuffers(display, window)` (0x316f0). There is no glFinish and no event handling;
  Allegro's X input ran in a background thread, so `mouse_*` and `key[]` changed
  asynchronously.
- **Ours:** `SDL_GL_SwapWindow(win)` and then `legacy::pump()`. Also take the debug-screenshot
  hook here. Use vsync (swap interval 1): `Merit3d::Game::Run` has no frame cap by default
  (§7.2).

### 5.4 `__aglXGetProcAddressARB`: a data object, NULL allowed [C]

- **Type:** in libagl it is bss (0x40458), a function-pointer variable. libmerit3d reads it
  through `R_386_GLOB_DAT` (GOT 0x7bc98).
- **Use:** in `RendererOpenGL::Initialize`, if the extension string contains
  `ARB_vertex_buffer_object`, libmerit3d calls through the pointer, **or `glXGetProcAddress`
  directly if it is NULL**. It fetches 11 `gl*BufferARB` functions into renderer slots
  +0x38…+0x60, and enables VBOs (byte +0x36) only if all 11 are non-NULL.
- **Definition:** `extern "C" void* (*__aglXGetProcAddressARB)(const unsigned char*) = nullptr;`.
  **It must not be a function:** libmerit3d would load the code bytes as the pointer and jump
  to them.

### 5.5 `FlipBitmapVert`: 3 arguments [C]

- **Signature:** `extern "C" void FlipBitmapVert(void* pixels, int rowLenIn16bitUnits, int
  height)`.
- **Semantics:** swaps rows top↔bottom in place, where one row = `2*arg2` bytes.
- **Call sites:**
  - bowling 0xa642–0xa64e pushes exactly 3 words: `(bmp->0x2c->dat, bmp->0x4c, bmp->0x50)` for
    a 16-bpp PCX bitmap, so the row is w×2 bytes.
  - libmerit3d `TextureManager::GetTextureData`, `LoadTexture` and `LoadTexture_640x480`
    (0x2e3db, 0x2f30e, 0x3000d) pass `(dat, BITMAP->w*2, BITMAP->h)` for a 32-bpp PNG, so the
    row is w×4 bytes.
  - The extra word that libmerit3d pushes first (bpp at 0x2e3c9) is stack padding. Its value
    differs at the other sites, and `(%esp)` is reused for `operator new[]` straight after.
- **What it requires of `Bitmap`:** `LoadPNG`/`LoadRawPNG` must leave an Allegro BITMAP at
  `Bitmap+0x2c` with contiguous `dat` (+0x28) and a truthful `vtable->color_depth` (first int at
  BITMAP+0x1c → 32 for PNGs). libmerit3d sizes its copy as `w*h*(color_depth/8)`.

### 5.6 libagl.so: replace it with an empty stand-in [L]

- **What links against it:** pool and bowling (and nineball, which has the same set) NEED
  `libagl.so`. libmerit3d and minigolf do not.
- **Why the real one fails:** it resolves about 100 liballeg internals at load time (`_xwin`,
  `_screen_vtable`, `_gfx_driver_list`, `font_vtable_*`, `system_driver`…, many of them
  GLOB_DAT relocations), and our allegro.cpp does not provide them.
- **The GUI symbols** (`init_dialog`, `update_dialog`, `_gui_button_proc`, `d_yield_proc`,
  `gui_*`, `text_mode`, `textout`…) are imported **only by libagl, not by the games**. Inside
  libagl they are used only by `algl_do_dialog`, `algl_popup_dialog` and `algl_alert3`, which no
  game imports. **They are unreachable; do not implement them** [C].
- **Recommendation:**
  1. Define `allegro_gl_flip`, `__aglXGetProcAddressARB` and `FlipBitmapVert` in
     `libmerit_legacy.so`.
  2. Ship an empty `libagl.so` (soname `libagl.so`, the same `$(BIN)/stubs` mechanism as
     `libgraphics_sprite.so`) for pool, nineball and bowling. The preload interposes the real
     symbols.
- **Other shipped GL helpers:**
  - pool and nineball also NEED libglut.so.3 (no glut function is imported), libftgl.so.0
    (FTGL bitmap fonts) and libXmu.
  - bowling's 16 glut imports belong to the dead dev harness `glutmain` (0x994c); they only need
    to resolve.

### 5.7 Allegro globals and BITMAPs in GL mode

| Symbol | Requirement | Evidence |
|---|---|---|
| `gfx_driver` | non-NULL, with w/h at +0x68/+0x6c = window size (already done) | pool 0xfc7e [C] |
| `screen` | Non-NULL. In GL mode `clear_bitmap(screen)` should mean `glClear(COLOR\|DEPTH)` and must **not** present the software screen over GL. Blits to `screen` occur only in coin-jam and record paths. | beer pong exit [C], AllegroGL semantics [L] |
| `mouse_x`, `mouse_y`, `mouse_b` | volatile ints in window pixels, updated by `pump()` | pool `Game::Run`, minigolf [C] |
| `mouse_button_presses_cached` | int, `\|= mouse_b` on every update | §5.2 [C]; **missing** |
| `position_mouse` | sets mouse_x/y immediately and warps the pointer (existing version OK) | [C] |
| `key[]`, `keyboard_needs_poll`, `poll_keyboard` | present; all-zero is fine | [C] |
| `install_int` / `remove_int` | asynchronous timer; pool uses 1 ms | [C] |
| `create_bitmap(_ex)`, `blit`, `masked_blit`, `destroy_bitmap`, `set/get_color_depth`, `save_bitmap` | 16- or 32-bpp memory bitmaps with contiguous `dat` | chainz2, libmerit3d [C] |
| `layout::ScreenControl::GetCurrentWidth/Height` (this = `ScreenInfo::GetInstance()+0x58`) | the window size (already in legacy.cpp) | [C] |

The real `ScreenControl` lives in liblayout.so. It maps a resolution enum to a size: 1 →
640×480, 2 → 800×600, 3 → 1024×768 (1024×800 widescreen), 0x82 → 960×600, 0x83 → 1280×800.
Our own definition makes the library unnecessary.

---

## 6. The small functions

### 6.1 `CoinJamManager` (real one in libmoney.so; stub it) [C]

| Function | Behaviour |
|---|---|
| `static CoinJamManager* Instance()` | A static object. Any size is fine; games only pass it back. |
| `void RegisterCallback(void (*cb)(bool))` | The real one pushes onto a `std::deque<void(*)(bool)>` (libmoney 0x30900). |
| `void UnregisterCallback(void (*cb)(bool))` | Removes the callback. |

- **Call shape** (beer pong 0x26f42): `RegisterCallback(Instance(), BeerPongGame::CoinJam)` at
  entry, and `Unregister` at exit.
- **Callback meaning:** `true` = a coin jam was detected (the game freezes its frame timer,
  plays "coinjam" and shows `misc/coinjam/c<lang>_alpha.png`, via `Merit3d::Game::HandleCoinJam`
  0x3c2e0), `false` = cleared.
- **Minimum:** store the callbacks and never call them.

### 6.2 `Locale::LanguageManager::IndexOf(Locale::Languages)`: **static**, returns int [C]

- libmerit3d `HandleCoinJam` 0x3c50e does `mov %eax,(%esp)` (Active()'s result) and calls it
  with **one** argument. It discards the `GetInstance()` result.
- The result formats `"%s%s/c%d_alpha.png"` with `/usr/local/gamedata/gamegraphics/misc/` and
  `coinjam` (strings at 0x75bbf/0x75bf8). The files `c0…c22_alpha.png` exist, so it is the
  language's index in the supported list, with English = 0 [L].
- **Fix in `loader_services.cpp`:** declare it `static int IndexOf(Languages)`.

### 6.3 `DoRegularProcessing()` [C for shape, L for meaning]

- **Shape:** `void DoRegularProcessing(void)`, no arguments.
- **Caller:** only `Merit3d::Game::Update` (0x3b771), once per frame, **before** the mouse is
  read. It is the loader's periodic service hook (input queue, coin, IPC heartbeat).
- **Ours:** `legacy::pump()`, so that `GetMouse()` sees fresh input.

### 6.4 `debug_check_address(void*)` → bool [C]

- **Caller:** only libmeritbasegame `MeritBaseGame::CrashDump` object-dump code (decomp near line 3225), when it writes an
  object dump. `false` prints "BAD ADDRESS!" instead of dereferencing the pointer.
- **Ours:** return `p != NULL`. A stricter test, such as `msync`/`mincore` on the page, is
  optional.

### 6.5 `Heartbeat_WaitForAll(unsigned long ms, bool)` → void [C]

- **Caller:** only `NetworkManager::WaitForAllMachinesToRespond` (link play). The return value is
  unused.
- **Ours:** a no-op. `Heartbeat_Check(int,int)` should return something other than 1, since
  `CheckStateOfMachine` tests `== 1`.

### 6.6 `MyMeritManager` [C]

| Function | Behaviour | Evidence |
|---|---|---|
| `static MyMeritManager* Instance()` | Returns an object of **≥ 0xc0 bytes**, zero-initialised. libmeritbasegame reads and writes `+0xbc` inline as the "temporary game level" (`MyMeritHaveTmpLevelData` tests `>0`, 0x6290; `MyMeritSetTempGameLevel` stores it, 0x62c0). | |
| `bool PlayerLoaded() const` | `false` (no MyMerit card). Callers use the low byte. | 0x627c, luxor 0x6f03d |
| `char const* Name() const` | Returns a pointer in eax, which callers pass through. Return `""`. | luxor `MMPlayerName` 0x6f1a5 |
| `int GetGameLevel(xml_gameinfo::GameIds)` | Called as `(this, MegacGlobals+0x2038)`. luxor clamps against it (`MMGetMaxLevel` 0x6f08c). Return 0. | |
| `void SetGameLevel(xml_gameinfo::GameIds, int)` | A no-op. | 0x6318 |

### 6.7 `ContinueControl` [C]

| Function | Notes | Callers |
|---|---|---|
| `ContinueControl()` / `~ContinueControl()` | Stack object of **≤ 0x14 bytes**. | |
| `bool display(unsigned x, unsigned y)` | Box position; returns true = continue. | luxor/chainz2 pass (0x9f, 0xf0); libmeritbasegame `DisplayContinueBox` forwards; bowling. **Exists.** |
| `bool display(unsigned x, unsigned y, unsigned)` | pool and minigolf pass `(0x9f, 0xf0, game+0x107c)` (pool 0x1e2d8). | **Missing.** |
| `bool display()` | beer pong `StateContinue::Update` 0x2afbb. | **Missing.** |
| `static bool allowed()` | One call with no `this` (beer pong 0x2af95). If false, the continue prompt is skipped. | **Missing.** |

- **Home use:** `allowed()` = false, so games skip the prompt (or true and `display` = true).
- With free play, returning **true from display** continues the game. libmeritbasegame
  `IsContinueAllowed` instead reads NVRAM options 1 and 2.

### 6.8 `Profiler` [C]

- **Shapes:**
  - `static Profiler* GetInstance()`.
  - `void Start(std::string)` and `void Stop(std::string)`: members; the string is a by-value
    temporary owned by the caller.
  - `void DumpResults()`: a member (beer pong 0x2c7be pushes the instance).
  - `static void DelInstance()`.
- **Callers:** beer pong `StateMenu::Exit` 0x2c4e7–0x2c7c4 and the triviawhiz games.
- **Ours:** no-ops. `GetInstance` and `DumpResults` already exist (`loader_services.cpp`). Add
  `Start`, `Stop` and `DelInstance`, compiled with the old string ABI.

### 6.9 `ExtendedCrashInfo` [C]

`libdebug_mock.so` defines the ctors (C1/C2), the dtors (D0/D1/D2), the vtable and the typeinfo.
Add it to PRELOAD for all the libmerit3d and AllegroGL games. Nothing else is needed.

---

## 7. Start-up and the frame loop

### 7.1 What must exist before `__EntryPointV12`

| Item | Requirement | Evidence |
|---|---|---|
| GL context | Current, double-buffered with depth, drawable = design size (§5.1) | |
| `gfx_driver`, `screen` | Non-NULL; `gfx_driver->w/h` = size | pool 0xfc7e, beer pong exit |
| `ScreenInfo::GetInstance()+0x58` | `GetCurrentWidth/Height` = size | libmerit3d 0x3b11b |
| `textSystem` | Non-NULL `TextSystem*` with the default description | §1.1 |
| `FileLoc` | The game's asset directory **with a trailing slash**. Beer pong sets `Merit3d::Game::m_asset_dir = FileLoc` and the texture dir to `"%stextures"`. | beer pong `__EntryPointV12` (decomp 0x36f20) |
| MegacGlobals | `+0x2038` GameId (translations, MyMerit). `+0x207c` world: the game calls `PushWorld(NULL,NULL,true)` itself. `+0x24` player count (byte, beer pong 0x29be2). `+0x30` link/edition mode: 0 normal, 1 MegaLink (enables NetworkManager), 4 Championship. `+0x22d8` joystick enabled (0). `+0x2040` card-fanning option. `+0x22dc` SystemClass with the last key in byte 0; luxor treats '1'/'2' as coin-jam on/off debug keys. `+0x22e0` MouseManager; after `CheckLoc`, luxor reads `+0x5b60` x, `+0x5b64` y and `+0x5b68` "touch down". | libmeritbasegame `GameCFG::*` (0xa6f8–0xa8ba), luxor `MeritMJInterface::Events` (0x6dcf4) |
| `Locale::LanguageManager::Active()` | 0 = English | |
| `MeritSound::SoundManager`, `MeritInput::InputManager` | Instances present (they exist) | |

- **[?] Open conflict:** `GameCFG::IsCardFanningEnabled` and `BaseGame::IsLinkActive` test
  `MegacGlobals+0xc == 1`. PROGRESS.md says `+0xc` is the start of a `std::map<GameId,
  gamedata_record>`. If so, `+0xc` holds the map's comparator padding and is not a flag. It
  must not equal 1, or the game thinks link play is active. Check what the host writes there.

### 7.2 Merit3D games: start-up and frame order [C]

Beer pong `__EntryPointV12` (0x26f20):

```
MegacGlobals::PushWorld(NULL, NULL, true)
CoinJamManager::Instance()->RegisterCallback(BeerPongGame::CoinJam)
Game::m_asset_dir = FileLoc;  snprintf(s_TextureDirectory, "%stextures", FileLoc)
BeerPong::GetInstance()                  // constructs the Merit3d::Game subclass
Merit3d::Game::Run()
BeerPong::DeleteInstance(); CoinJamManager::UnregisterCallback(...)
clear_bitmap(screen); allegro_gl_flip(); clear_bitmap(screen)
MegacGlobals::PopWorld()
```

`Merit3d::Game::Run` (0x3c710; decomp 0x4c710) calls vtable +0x14 Initialize and then loops:

```
loop:
  if (this[0x198]) { vtable+0x1c Terminate(); return; }     // quit flag set by the game
  if (this[0x148]) { budget = round(1000/this[0x14c]); t0 = SystemTimer(); }   // fps cap, off by default (fps 30)
  vtable+0x18 Update()     -> Merit3d::Game::Update (0x3b750):
        DoRegularProcessing()
        mouse = InputManager::GetMouse()           // Mouse_T by value, this = game+0x10c
        Scene::Update(dt, mouse)                   // sprites, Button2d, VideoObjectManager::Update
        BaseGame::Update(dt)                       // NetworkManager (link only), SoundManager::Update()
  Render()                 -> UpdateFrameTimer(): dt = SystemTimer() delta, clamped to 66 ms, x multiplier
                              renderer->BeginFrame (clear) ; Scene::Draw or ApplyBlur
                              device->Swap() = allegro_gl_flip()
  if capped: busy-wait on SystemTimer() until budget ms have passed
```

`Merit3d::Game::Initialize` (0x3b900) runs, in order:

1. `BaseGame::Initialize`: SoundManager, and NetworkManager if `+0x30 == 1`.
2. `Randomize(SystemTimer())`.
3. `new RenderDeviceAlleggl` (empty Initialize).
4. `RendererOpenGL::GetInstance()`: viewport, extensions, VBO probe.
5. `Translator::LoadTranslations(GameId)`.
6. `Animation2dManager`.
7. `Scene` (0x170 bytes) and `Scene::Initialize`, which creates the VideoObjectManager.
8. A cursor `Sprite2d`, `SkyBox`, `Camera`, and an FPS `Text2d`.
9. `CollisionManager`.
10. `RenderTarget(2, W, H, 1024, 1024)` (uses `glCopyTexImage2D`).

What this requires of us:

- **`SystemTimer()` must be a monotonic millisecond counter that keeps advancing** (and pumps
  events). Ours does both (`legacy.cpp:595`).
- **Input must be pumped from `DoRegularProcessing` or `allegro_gl_flip`.** Nothing else in the
  loop polls.
- **There is no frame cap by default**, so use vsync.

### 7.3 `MeritInput::InputManager::GetMouse()` returns `Mouse_T` by value [C]

- **Call shape** (0x3b776–0x3b783): `lea -0x24(%ebp),%esi; push this(game+0x10c); push %esi;
  call GetMouse`. That is a hidden result pointer, then `this`.
- **`Mouse_T` (0x18 bytes, from the stack slot [L]):**

  | Off | Field |
  |---|---|
  | +0 | int x |
  | +4 | int y |
  | +8 | int button 0 |
  | +0xc | int button 1 |
  | +0x10, +0x14 | unused by libmerit3d (probably button 2 and the wheel) |

- **How it is read:** `Button2d::Update` (0x4b040, decomp 0x5b040) treats "+8 == 0 and +0xc ==
  0" as released; otherwise it hit-tests `Sprite2d::Collision(x, y)` (0x4b17a–0x4b1cf).
- **Implementation:** `Mouse_T InputManager::GetMouse() { Mouse_T m = {mouse_x, mouse_y,
  mouse_down, 0, 0, 0}; return m; }`. Returning a struct makes g++ emit the hidden pointer and
  the `ret $4`.
- **Why it matters:** our current `void* GetMouse()` leaves the caller's buffer uninitialised,
  so buttons react to stack garbage, and it leaves the stack 4 bytes off (harmless here, because
  the frame is ebp-based).

### 7.4 AllegroGL games (pool, nineball, bowling, minigolf)

- **They own their loop.** pool and nineball `Game::Run` poll Allegro input themselves
  (§5.2), and `Draw` calls `allegro_gl_flip`. minigolf uses `OldMerit3d` with
  `RenderDeviceAlleggl::Swap` → flip.
- **They rely on asynchronous Allegro state:** `mouse_x/y/b` updated "in the background" (we
  update it in `pump()`, called from flip, `CheckKey`, `QueFlush` and `SystemTimer`) and
  `install_int` timers.
- **Settings/gendef:**
  - triviawhiz2/jr use `Settings::*` from the shipped `libsettings.so` and gendef
    `GameSettingsTriviaRandom_record` (our `src/gendef`).
  - pool, nineball and bowling use `SettingsTable::Instance()` and
    `db_config::GameInfo_record` (gendef).
  - Every game reads `NVRAMMap::GamesOpt(n)`; libmeritbasegame `IsContinueAllowed` reads options
    1 and 2.

---

## 8. Open questions

1. The meaning of `TextDesc` +0x2a colour, +0x30, +0x34, +0x40 (values 0/2/3), +0x44, +0x48 and
   +0x4c, and of the two bools of `GetSpriteChannels`. Compare against cabinet screenshots of
   luxor or chainz2 text (shadow and outline) to decide.
2. The exact 16-bpp channel format for `TTFtoCData` (RGB565 vs 555; alpha 0–255 vs 0–31).
   Only monkeybusiness among the GL games, plus legacy text.
3. Delta-frame alpha: libmvideo reads `a` as transparency (`0xff-8a`), which is the opposite of
   `merit_rle.h` (§3.3).
4. MegacGlobals `+0xc`: a link flag or the gamedata map (§7.1)?
5. `Colour::white`/`black` values are inferred (255/0), not read from code.
6. `Locale::Languages` enum values: English = 0 is assumed. The values 0x10, 0x18, 0x19, 0x1b,
   0x1c and 0x1e appear as special cases. The string tables are in liblocale.so if needed.
