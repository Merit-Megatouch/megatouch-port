# The cabinet: discs, image, partitions, files

Everything known about the ION 2014 software's two forms, the original installer discs and the
`Megatouch ION 2014 HDD Keyless` disk image, and where things live in them. The Force (2004–2011
software) is in [force](force.md); every disc of the collection is listed below. Software version
**PG3002-01 V40.02** (build `V40_02T130830_1115`, 2013-08-30).

## The installer discs

Two bootable CD images, `V40_02T130830_1115_1OF2.iso` and `…_2OF2.iso` (3.6 + 4.3 GB, ISO 9660
with Joliet). Booting them runs a small Linux (`isolinux/vmlinuz`, `initrd` with the installer
scripts in `/usr/local/bin/install_*` and the partition table in `install.xml`). The installer
partitions the drive, formats swap, ion_only and var, and restores each other partition from a
partimage image.

| Disc path | Restored to | Size |
| --- | --- | --- |
| `lib/images/part1.gz.000` (disc 2) | bootloader | 10 MB |
| `lib/images/part2.gz.000` (disc 1) | `/home` | 150 MB |
| `lib/images/part5.gz.000–005` (disc 1) | `/` (root) | 4.75 GB |
| `lib/images/part6.gz.000` (disc 1), `.001–.008` (disc 2) | `/usr/local/ion_only` | 8 GB |
| `share/` (disc 1) | installer screens, `version.dat`, `upgrade.map` (which versions upgrade to which), install sizes | |

`/var` starts empty. On the first boot `merit-checkfs` restores it from root's
`/factorybackup/var.tar.gz` (and `/home` from `home.tar.gz`). An upgrade from an older version
also creates `/var/merit/games/upgrade`, which runs `upgrade_manager` on the next boot.

`scripts/partimage.py` reads the discs without booting them (format in the script);
`make loader-setup` uses it when `DISC1`/`DISC2` are set
([cabinet-loader → From the installer discs](../guides/cabinet-loader.md#from-the-installer-discs)).
Other releases ship the same way (inventory below); the Force's system is described in
[force](force.md).

### Every installer disc (as seen 2026-10-11)

What `scripts/partimage.py info` reports for each disc of the collection: the volume label (the
build: `V<version>T<yymmdd>_<hhmm>`), and the partition images on it. The file names aren't always
right; the volume label is.

| Release (file name) | Build on the disc | Images | Notes |
| --- | --- | --- | --- |
| Force 2004 (3 discs) | "disc 1–3" | `part1_gz.000`, `part2_gz.000–002`, `part6_gz.000` | older multi-file naming; no `part5` |
| Force 2005.5 (4 discs) | V15.10 (`050614_1709`) | `part1`, `part2`, `part5` (over all four), `part6` | |
| Force 2006.5 | V21.51 (`070522_1743`) | `part1`, `part2`, `part5`, `part6` | |
| Force 2007.5 | V23.00 (`070420_1642`), disc 1 of 2 | `part1`, `part2`, `part5` | disc 2 (`V23_00…_2OF2`) has `part6` |
| Force 2008.5 / ION 2008.5 | V25.00 (`080320_1114`) | disc 1: `part1`, `part2`, `part5`; disc 2: `part6` | the Force and ION disc 1 files are byte-identical |
| "Force 2009.5 V29_11…" | **V27.03** (`090811_1443`) | `part1`, `part2`, `part5` | mislabelled: same file as `V27_03…iso`; the real V29.11 (`081203_0708`) is another disc |
| Force 2010.5 | V28.20 (`100329_1757`) | `part1`, `part2`, `part5` | |
| Force 2011 "1 of 2" | V28.40 (`101109_1217`) | `part1`, `part2`, `part5` | complete on one disc; **runs** ([guide](../guides/cabinet-loader.md#a-force-cabinet)) |
| ION 2009 | V30.23 (`090216_1459`) | disc 1: `part1`, `part2`, `part5`; disc 2: `part6` | |
| ION 2009.5 update | V31.02 | disc 1: `part2`, `part5`; disc 2: `part1`, `part6` | product label PG3002-01 from here on |
| ION 2010.5 | V33.00 | as 2009.5 | |
| ION 2011 | V34.10 | as 2009.5 | |
| ION 2011.5 | V35.00 | as 2009.5 | |
| ION 2012 | V36.10 | as 2009.5 | also V36.01 (`110826_1325`) |
| ION 2013 | V38.01 | disc 1: `part2`, `part5`, `part6`; disc 2: `part1`, `part6` | |
| ION 2014 | V40.02 | as 2013 | **runs** |
| Unlabelled: V24.04, V28.10 | `070917_1718`, `091204_0300` | disc 1: `part1`, `part2`, `part5`; disc 2: `part6` | platform not checked |
| Unlabelled: V27.04, V29.11 | `090805_1537`, `081203_0708` | `part1`, `part2`, `part5` | |
| Unlabelled: V32.03, V32.11 | `090928_1617`, `091207_1021` | ION layout (PG3002-01) | |
| Maxx Ruby (2 discs), `maxx_ruby_d1/d2` | **V10.07** (volume "10.07") | `part1`, `part2`, `part3`, `part6`, `part7`; disc 2 `dospart1`, `dospart2` | Linux (`/bin/version`) |
| Maxx Ruby 2 (2 discs) | V10.0 (volume "10.0") | as Ruby | **raw CD images** (2352-byte sectors): convert to ISO first (2048 bytes of each sector from offset 16) |
| Maxx Ruby Update | — | none: replacement files (`START`, `LIBGFXOLD_SO_1_0.1`, `LIBTMAXX_SO_1_0.1`, `OPSETUP.SO` …) | raw CD image |
| Maxx Sapphire (3 discs), `maxx_sapphire_v12-01` | **PG3002-01 V12.01** | `part1`, `part2`, `part6`; discs 2–3 `part2` | Linux, isolinux |
| Maxx Jade 2 (4 discs) | **PG3002-01 V15.10** | `part1`, `part2`, `part5` (over all four), `part6` | Linux; the same version number as Force 2005.5, but different discs |
| Maxx Crown (3 discs), two copies | **PG3002-01 V16.10** | `part1`, `part2`, `part5` (over all three), `part6` | `Megatouch Maxx Crown …` and `maxx_crown_v16-10_reva_…` have the same version but different checksums. **Runs:** `MAXX_DISC1`–`MAXX_DISC3`, `make loader-run CABINET=maxx` ([maxx](maxx.md)) |
| `vips40` | **PG4000-01 V4.00** | `part1`, `part2`, `part5` | another product line (PG4000); not identified |
| Maxx Diamond Rev-B v6.03, Double Diamond, Emerald Rev F (2 discs each) | 2000–2001 (`001107`, `010327`, `011226`) | none | DOS: a `DISKIMGC.001` disk image, `D2000.EXE`, DOS4GW, a boot floppy image |
| MAXX 2000 upgrade V4 (2 discs) | 1999 (`991213`, `991210`) | none | DOS: `.UDF` upgrade files (`301TO302.UDF` …) |
| Megatouch XL 5000, Super 5000, Extreme, XL 6000; "2k plus update" | 1998 (`980121` …); XL 6000 `121227_1616`; 2k plus `000502` | none | DOS: game libraries (`*DLL.LIB`) and Smacker folders |
| `ruby_force_2003_v10-02_update` | — | — | **not a disc**: an HTML page saved from a file-sharing site |

## The image

60 GB raw disk, MBR, GRUB legacy, **CentOS 5** (kernel 2.6.18-92.1.6.el5), **32-bit i386**.

**Keyless:** `/usr/local/bin/start` has the security-key reads patched out (1,114 bytes in five
functions, dated 2021-07-25). Apart from that file, root, home and ion_only are the factory
software from the discs, plus the previous owner's `/var` and leftovers of use.

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
key switch), MicroTouch USB touch screen, Realtek r8168 NIC, CML-190W widescreen monitor. X
drivers: `intel`, `i810`, `fbdev`. The cabinet's `harddetect` writes this file at every boot; we
never boot the OS, so `scripts/loader.sh` writes one describing the simulated hardware when it is
missing (a factory-fresh `/var`). The cabinet reads the monitor from it: without it, it isn't
widescreen.

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
