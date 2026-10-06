# The cabinet: image, partitions, files

Everything known about the `Megatouch ION 2014 HDD Keyless` disk image and where things live on
it. Software version **PG3002-01 V40.02**.

## The image

60 GB raw disk, MBR, GRUB legacy, **CentOS 5** (kernel 2.6.18-92.1.6.el5), **32-bit i386**.
"Keyless" means the security-key check is patched out.

| # | Start sector | Size | Label | Mounted at | Contents |
| --- | ---: | --- | --- | --- | --- |
| 1 | 63 | 55 MB | common-boot | /bootloader | GRUB `menu.lst` |
| 2 | 112455 | 157 MB | — | — | not ext |
| 3 | 433755 | 518 MB | common-swap | swap | |
| 5–7 | 1494108… | 18 GB | sideA-* | — | A side: **zeroed** in this image |
| 8 | 39262923 | 157 MB | sideB-home | /home | `/home/maxx` = the cabinet user |
| 9 | 39584223 | 4.6 GB | sideB-root | / | OS, engine, game code, loader, gamedata |
| 10 | 49319613 | 8 GB | sideB-ion_only | /usr/local/ion_only | game assets, attract videos, Unity player |
| 11 | 66107538 | 5.4 GB | sideB-var | /var | `/var/merit`: settings, logs, state |

The cabinet updates the inactive side (A/B scheme); here only side B is present. Labels come from
`/etc/fstab` on partition 9. Byte offset = start sector × 512. These are the `*_OFF` values in
`cabinet.conf`.

Reading without root: `debugfs -R "<cmd>" "$IMG?offset=<bytes>"` with `ls -l`, `cat`, `dump`,
`rdump`, `stat` (a symlink target is `Fast link dest`). `scripts/lib/cabinet.sh` wraps these.

## Expected hardware

From `/var/merit/hardware.xml`: Intel 945GC graphics (`i845` driver), FX1 I/O board (coin mech,
key switch), MicroTouch USB touch screen, Realtek r8168 NIC. X drivers: `intel`, `i810`,
`fbdev`. None of it is needed for a port: we never boot the OS.

## Directory map

| Cabinet path | Partition | Contents |
| --- | --- | --- |
| `/usr/local/bin/loader` | 9 | The loader: the main cabinet program. 2.9 MB, packed and obfuscated. Allegro, the legacy sprite engine, credits, security, menu. |
| `/usr/local/bin/` | 9 | Also `launcher.sh`, `layout*`, `downloadman`, `credit_card_reader`, `hardware_detect_utils.sh`, log tools… |
| `/usr/local/lib/*.so` | 9 | 282 libraries: the engine (`libgraphics.so`, `libgame_device.so`, …) **and game code** (`g_trix.so`, `boxxi.so`, …) |
| `/usr/local/gamedata/config/` | 9 | `gamedata.xml` (master game table), `languagedata.xml`, settings schemas |
| `/usr/local/gamedata/translations/` | 9 | `<game>.utf8` translation tables, `SupportFiles.utf8` |
| `/usr/local/gamedata/help/<language>/` | 9 | Help texts |
| `/usr/local/gamedata/ttf/` | 9 | Fonts: the EuroFONT `EFN*` families with scrambled file names |
| `/usr/local/gamedata/fonts/` | 9 | Bitmap fonts (`.dlt.gz`, `.tga`) for legacy games |
| `/usr/local/games/<dir>/` | 9 | Assets of older games, and symlinks |
| `/usr/local/games/default/` | 9 | Fallback "game": game-over, winner/loser, quit and exit prompts per resolution |
| `/usr/local/ion_only/games/<dir>/` | 10 | Assets of ION-era games |
| `/usr/local/ion_only/games/idle/` | 10 | Attract-mode videos, `<game>.mov` |
| `/usr/local/ion_only/games/launcher/` | 10 | Unity 3.2 `LinuxPlayer`, shared by the Unity games |
| `/var/merit/settings.xml` | 11 | Operator settings: per-game `GameInfo` with `Active` flag and pricing |
| `/var/merit/locale/`, `/var/merit/settings/` | 11 | Locale state read by `liblocale.so` |
| `/var/merit/debug/<category>/<flag>` | 11 | Engine debug flags ([list](commands.md#engine-debug-flags)) |
| `/var/merit/hardware.xml` | 11 | Detected hardware |
| `/var/merit/games/` | 11 | First place the resource locator looks (runtime-selected game data) |
| `/home/maxx/.fonts.conf` | 8 | The fontconfig file the games used |
| `/usr/lib/pango/1.5.0/modules/` | 9 | Pango 1.14 shaping modules |
| `/etc/pango/i386-redhat-linux-gnu/pango.modules` | 9 | Their registry |
| `/dev/merit_ipc/` | — | IPC sockets between the loader and helpers (created at runtime) |

## gamedata.xml

The master table, `/usr/local/gamedata/config/gamedata.xml`, has one `<game>` per title. Games
added after the base build carry their own `gamedata.xml` in their asset folder.

```xml
<game>
  <GameId>G_TRIX</GameId>                         <!-- xml_gameinfo::GameIds name; number via shared/bin/gameids -->
  <DLLName>g_trix</DLLName>                       <!-- /usr/local/lib/g_trix.so; "launcher" = Unity; empty = service -->
  <Description>TRIX</Description>
  <Directory>g_trix</Directory>                   <!-- asset folder name -->
  <UseResolution>RESOLUTION_1280x800</UseResolution>
  …                                                <!-- categories, languages supported, pricing defaults -->
</game>
```

`UseResolution` values: `RESOLUTION_<W>x<H>`; `SUPER_HIGH_RESOLUTION` (1280×800, confirmed with
Word Dojo 2); `HIGH_RESOLUTION` (unreliable: Boxxi Blitz declares it and is 800×600, so trust the art); absent (probably 800×600).
Several games can share one library: `photo_hunt_hd.so` serves Photo Hunt HD, Penthouse
Photohunt HD and Photo Hunt Hunks, chosen by GameId.

## settings.xml

`/var/merit/settings.xml` holds the operator configuration. For each game there is a `GameInfo`
with `GameId` and `Active` (`GAME_ACTIVE` or not). 188 games are active on this image. The
extracted list is in `docs/data/games-catalogue.tsv`.

## Game IDs

`xml_gameinfo::GameIds` is an enum compiled into `libenums.so`. `shared/bin/gameids` prints it
(`gameids G_TRIX` → `245`). The GameId number:

- picks the asset folder for the resource locator (`ResourceLocator(id)`);
- is the high-score file name;
- goes into `GameConfig::SetGameID`.

## A game's pieces

For `DLLName=g_foo`, `Directory=g_foo`:

| Piece | Location |
| --- | --- |
| Code | `/usr/local/lib/g_foo.so` plus its NEEDED closure |
| Assets | `/usr/local/ion_only/games/g_foo/` (ION era) or `/usr/local/games/g_foo/` (older) |
| Own gamedata | `<asset dir>/gamedata.xml` (newer games) |
| Translations | `/usr/local/gamedata/translations/g_foo.utf8` |
| Help | `/usr/local/gamedata/help/<language>/g_foo.utf8` |
| Attract video | `/usr/local/ion_only/games/idle/g_foo.mov` |

Typical GameDevice asset folder (Trix, 26 MB): `fx/` (effect XMLs), `gfx/` (layout XMLs,
184 PNG, 10 `.spr.gz`), `scripts/` (`MessageTypes.dat`, `NetMessageTypes.dat`), `sfx/sounds/`
(18 WAV, 5 OGG), `gamedata.xml`. Some games keep assets under `media/` or in
`/usr/local/gamedata` instead.

## Engine families

Classify a library by its NEEDED entries (`readelf -d lib.so | grep NEEDED`):

| Family | Marker | Count |
| --- | --- | ---: |
| GameDevice | needs `libgame_device_sprite.so` (or `_irrlicht`) | 22 |
| Unity 3.2 | `DLLName` is `launcher`; asset dir has `Data/` with `Managed/*.dll` | 30 |
| Merit3D | needs `libmerit3d.so` | 13 |
| Legacy sprite | none of the above; imports `Sprite`, `WorldClass`, `Bitmap` | 143 |

The family of each library is in `docs/data/engine-families.tsv`.

## Engine libraries (Trix's closure)

| Library | Size | Role |
| --- | ---: | --- |
| `libgame_device.so` | 57 KB | `BaseGameDevice`, `GameConfig`, frame loop, net interfaces |
| `libgraphics.so` | 182 KB | Scenes, sprites, renderables, textures, resource manager, layouts |
| `libeffects.so`, `libeffect_loader.so` | 121, 562 KB | Effect sequences (`fx/*.xml`) |
| `libobject_loader.so` | 78 KB | Builds objects from layout XML |
| `libtext_system.so` | 11 KB | Pango text system |
| `libinput.so` | 15 KB | `BaseInputManager` |
| `libmerit_sound.so` | 25 KB | `BaseSoundManager` |
| `libresources.so` | 65 KB | `ResourceLocator`: finds files by game, resolution, language |
| `libmessaging.so` | 44 KB | Message manager, net messages |
| `libstate_machine.so` | 23 KB | Game state machines |
| `libcore.so` | 23 KB | `Rect`, `Color`, `Vector2D` |
| `libenums.so` | 69 KB | Enums, including `GameIds` |
| `libtranslate.so`, `libtranslate_start.so` | 66, 4 KB | Translation front end (calls the loader's `Translator`) |
| `liblocale.so` | 85 KB | Locale state |
| `libgendef_common/db/xml.so` | 19–130 KB | Generated data definitions; SQLite and XML records |
| `libipc_new.so` | 62 KB | IPC with the loader (dormant in a port) |
| `libapp_common.so`, `libmerit_game_utils.so` | 23, 28 KB | Helpers |
| `libdebug_shared.so`, `libdebug_mock.so` | 32, 4 KB | `Debug_Flag_Set` and friends |

Third-party libraries from 2008 that must come from the image: `libexpat.so.0`,
`libsqlite.so.0` (SQLite 2), `libsqlite3.so.0`, `libssl.so.6`/`libcrypto.so.6` (OpenSSL 0.9.8),
Pango 1.14 with its glib, fontconfig and freetype. **Never** the image's `libz.so.1`: modern
libpng needs `inflateReset2`. glibc, libstdc++ and libgcc come from the modern 32-bit runtime,
which still has the old symbol versions.
