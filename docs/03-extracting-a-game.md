# 03 — Extracting a game

## 1. Build the game catalogue

```bash
debugfs -R "dump /merit/settings.xml settings.xml" "$VAR"
debugfs -R "dump /usr/local/gamedata/config/gamedata.xml gamedata.xml" "$ROOT"
```

Join `GameInfo/GameId` + `Active` (settings) with `game/GameId` → `Description`, `DLLName`,
`Directory` (gamedata). Games added after the base build have a `gamedata.xml` in their own asset
folder instead. The Python used is in the session history; the result for this image was 188
active games (`docs/data/games-catalogue.tsv`; engine family per library in `docs/data/engine-families.tsv`).

## 2. Locate the pieces of one game

For a game with `DLLName=g_foo`, `Directory=g_foo`:

| Piece | Location in image |
|---|---|
| Code | `/usr/local/lib/g_foo.so` (partition 9) |
| Assets | `/games/g_foo/` on partition 10 = `/usr/local/ion_only/games/g_foo/` |
| Attract video | `/games/idle/g_foo.mov` (partition 10) |
| Translations | `/usr/local/gamedata/translations/g_foo.utf8` |
| Help | `/usr/local/gamedata/help/<language>/g_foo.utf8` |

Trix assets: `fx/` (effect XMLs), `gfx/` (layout XMLs + PNG + `.spr.gz`), `scripts/`
(`MessageTypes.dat`, `NetMessageTypes.dat`), `sfx/sounds/` (WAV + OGG), `gamedata.xml`.
184 PNGs, 10 `.spr.gz`, 5 OGG, 18 WAV — 26 MB.

## 3. Close over library dependencies

```bash
# breadth-first over NEEDED, pulling each from the image (see scripts/lib/extract-libs.sh)
readelf -d g_foo.so | grep NEEDED
```

Trix's closure: 33 Megatouch libraries (`libgraphics`, `libgame_device`, `libeffects`,
`libstate_machine`, `libmessaging`, `libresources`, `libtext_system`, `libgendef_*`, ...).
Then the third-party ones that are too old to substitute:

| From the image (`syslibs/`) | Why not modern |
|---|---|
| `libexpat.so.0` | soname changed to .1 |
| `libsqlite.so.0` (SQLite **2**) | gone from distros |
| `libsqlite3.so.0` | could be modern, image one is fine |
| `libssl.so.6`, `libcrypto.so.6` (+ krb5, keyutils, selinux deps) | OpenSSL 0.9.8 ABI |
| `libpango-1.0.so.0`, `libpangoft2-1.0.so.0`, `libglib-2.0`, `libgobject-2.0`, `libgmodule-2.0`, `libfontconfig.so.1`, `libfreetype.so.6` | the engine's text system was written against Pango 1.14 |

**Do not** take the image's `libz.so.1` — modern libpng needs `inflateReset2`; modern zlib is
backward compatible. Glibc/libstdc++/libgcc come from the modern 32-bit runtime (glibc keeps the
old versioned symbols like `__xstat@GLIBC_2.0`; libstdc++ keeps the GCC 4.1 ABI).

## 4. Smoke-test loading

A tiny host that `dlopen(g_foo.so, RTLD_LAZY|RTLD_GLOBAL)` with
`LD_LIBRARY_PATH=lib:syslibs:sysroot/usr/lib32` tells you which symbols are missing. With the
original backend libraries you'll get something like
`libmeritbasegame.so: undefined symbol: _ZN6Sprite6SignalEP12SpriteSignal` — a symbol the
**loader** normally provides. That's the signal to map the runtime (chapter 04).
