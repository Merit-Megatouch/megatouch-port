# File formats

Formats the engine and games use, how the backend handles each one, and tools to inspect them.

| Format | Where | Handled by | Notes |
| --- | --- | --- | --- |
| PNG | `gfx/**` | backend (SDL_image) | Most art. 32-bit with alpha. |
| TGA | `gfx/**` | backend (SDL_image, type given explicitly) | SDL_image can't detect TGA from content, so the type comes from the extension |
| `.spr`, `.spr.gz` | `gfx/**` | backend (`src/backend/spr.cpp`) | Megatouch RLE sprite animation, below |
| WAV | `sfx/**` | backend (SDL) | Converted to 44.1 kHz stereo S16 |
| OGG Vorbis | `sfx/**` (music) | backend (stb_vorbis) | Whole-file decode |
| Layout XML | `gfx/<group>/<group>_layout.xml` | engine | Sprites, buttons, text |
| Effect XML | `fx/*.xml` | engine | Animation sequences |
| `MessageTypes.dat`, `NetMessageTypes.dat` | `scripts/` | engine | Message id tables |
| `gamedata.xml` | gamedata/config, asset dirs | engine, `new-game.sh` | [cabinet.md](cabinet.md#gamedataxml) |
| `.utf8` translation tables | gamedata/translations, help | host Translator | [loader-services.md](loader-services.md#translations-in-detail) |
| TTF | gamedata/ttf | fontconfig, Pango | Scrambled names |
| `.dlt.gz`, bitmap `.tga` fonts | gamedata/fonts | — | Legacy games |
| `.mov` | ion_only/games/idle | — | Attract videos; unused so far |
| Unity `Data/` | Unity games | — | `mainData`, `level0`, `*.assets`, `Managed/*.dll` |

Rule for every file the backend reads: **read the whole file into memory first**, then decode
from memory. SDL's own file opening rejects files on WSL drvfs mounts ("not a regular file or pipe").

## `.spr` / `.spr.gz`: Megatouch sprite animation

Fully decoded. Gzip-compressed; little-endian throughout.

```
u32 version = 2
repeat:                       // one per animation frame
    u32 dataBytes
    u32 width
    u32 height
    u8  data[dataBytes]
u8  trailer[8] = 0            // files end 8 bytes after the last frame
```

Frame data is a stream of 16-bit words: **top 6 bits = opcode, low 10 bits = count n**.

| Op | Meaning |
| --- | --- |
| 0 | Skip n rows (y += n). Every frame starts with one (leading blank rows). |
| 1 | Skip n transparent pixels (x += n) |
| 2 | n opaque pixels follow, one RGB565 word each |
| 3 | End of row (y++, x = 0) |
| 4 | End of frame |
| 5 | n translucent pixels follow, two words each: RGB565 colour, then a word whose **low byte is a 5-bit alpha (0–31)** |

Validated on all 10 Trix files (426 frames) with no inconsistencies. The high byte of the alpha
word is unknown; low-byte alpha gives visually correct glows. The original engine converted
images into this format at load time (`MyDitherAndProcessAlpha` → `CompressBitmap`), so legacy
games use it too.

Decoding to ARGB8888: RGB565 → 8-bit channels by bit replication; alpha a5 → `a5 * 255 / 31`.

```bash
python3 tools/sprdump.py games/g_trix/data/usr/local/ion_only/games/g_trix/gfx/…/deal.spr.gz /tmp/deal 0 1 2
```

## Images in the engine

Texture pixel formats (`TEXTURE_PIXEL_FORMAT`): 0/1 → 16 bpp RGB565, 2 → 24 bpp, 3 → 32 bpp ARGB.
For 16-bit sources, magic pink `0xF81F` means transparent. The original sprite backend loaded
`.png` and `.tga` as 32-bit, streamed names containing `_DSK`/`_DYN` from disk, and sent anything
else (e.g. `.spr`) to the loader's `WorldClass::LoadBmp`.

## Sound

- **WAV:** `SDL_LoadWAV_RW` (from memory) then `SDL_AudioCVT` to 44.1 kHz stereo S16. When no
  conversion is needed, `SDL_BuildAudioCVT` returns 0 and leaves `len_cvt` unset: use `len`.
- **OGG:** `stb_vorbis_decode_filename`, then the same conversion. A music track takes about
  90 ms to decode; streaming would remove the hitch.
- Games play music as a playlist: when `IsPlaying(track)` turns false they start the next track.
  A zero-length decode therefore looks like tracks cycling every frame with no sound.

## Layouts

`gfx/<group>/<group>_layout.xml`, consumed by `libgraphics`/`libobject_loader`. Elements seen:
`<sprite2d>`, `<button2d>`, text objects via `<createFrom>TEXT</createFrom>` with font names like
`EFNTORREYAC-BOLD`. `bitmap_file` paths are relative to the layout's folder. Text keys
(`HELP_TEXT`) go through the Translator.

## Fonts

The cabinet's `.fonts.conf` points fontconfig at `/usr/local/gamedata/ttf`, which the shim maps
into `data/`. Files have scrambled names: `torrey3.ttf` is "EFN TorreyaC Bold". To see what a
file is: `fc-scan --format '%{family} %{style}\n' file.ttf`.

Pango 1.14 needs its shaping modules. Without them every glyph is a box. The launcher writes a
`pango.modules` with absolute paths and points `PANGO_RC_FILE` at a `pangorc` that names it.
Modules are `dlopen`ed by absolute path, so the shim can't redirect them.
