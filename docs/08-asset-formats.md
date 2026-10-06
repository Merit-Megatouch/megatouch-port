# 08 — Asset formats

## Images
* **PNG** (most assets) and **TGA** — decode with SDL_image to ARGB8888. Read the file into
  memory first and use `IMG_Load_RW(SDL_RWFromConstMem(...))`: SDL's own file opening rejects files
  on WSL drvfs mounts ("not a regular file or pipe").
* **`.dlt.gz`, `.tga` bitmap fonts** in `gamedata/fonts` — used by legacy games, not Trix.

## `.spr` / `.spr.gz` — Megatouch sprite animation (decoded)

Gzip-compressed. Little-endian throughout.

```
u32 version = 2
repeat:                       // one per animation frame
    u32 dataBytes
    u32 width
    u32 height
    u8  data[dataBytes]
u8  trailer[8] = 0            // files end 8 bytes after the last frame
```

Frame data is a stream of 16-bit words. **Top 6 bits = opcode, low 10 bits = count n.**

| Op | Meaning |
|---|---|
| 0 | skip n rows (y += n) — every frame starts with one (leading blank rows) |
| 1 | skip n transparent pixels (x += n) |
| 2 | n opaque pixels follow, one RGB565 word each |
| 3 | end of row (y++, x = 0) |
| 4 | end of frame |
| 5 | n translucent pixels follow, two words each: RGB565 colour, then a word whose **low byte is a 5-bit alpha (0–31)** |

Validated across all 10 Trix files / 426 frames with zero inconsistencies (row widths add up
exactly: e.g. contract icon 46 px = skip 21 + 4 alpha pixels + skip 21). The meaning of the
alpha word's high byte is unknown; low-byte-as-alpha gives visually correct glows.
`tools/sprdump.py` renders frames to PNG (`--mode 0` low-byte alpha, `--mode 1` high byte).

The original engine compressed images into this same format at load time
(`MyDitherAndProcessAlpha` → `CompressBitmap`), so the format is shared with legacy games.

## Sound
* **WAV** — `SDL_LoadWAV_RW` (from memory, same drvfs reason) + `SDL_AudioCVT` → 44.1 kHz stereo S16.
* **OGG** (music tracks) — `stb_vorbis_decode_filename`. When the source is already 44.1 kHz
  stereo, `SDL_BuildAudioCVT` reports "no conversion" and **leaves `len_cvt` unset** — use `len`.
* The game plays music as a playlist: if `IsPlaying(track)` is false it starts the next one, so
  a zero-length decode looks like rapid track cycling with no sound.

## Layouts and effects
Plain XML, consumed entirely by the engine (`libgraphics`, `libeffect_loader`):
`gfx/<group>/<group>_layout.xml` (`<sprite2d>`, `<button2d>`, text via `<createFrom>TEXT</createFrom>`)
and `fx/*.xml` effect sequences. `bitmap_file` names are resolved relative to the layout's folder.

## Unity games
`Data/` holds Unity 3.2 serialized assets (`mainData`, `level0`, `*.assets`) and `Managed/*.dll`
(.NET). Not needed for GameDevice games.
