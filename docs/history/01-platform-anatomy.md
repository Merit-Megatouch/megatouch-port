# 01 — Platform anatomy

## The disk image

`Megatouch ION 2014 HDD Keyless.img` — 60 GB raw disk, MBR, GRUB legacy, CentOS 5
(kernel 2.6.18-92.1.6.el5), 32-bit i386. "Keyless" = security-key check patched out.

```
$ fdisk -l "Megatouch ION 2014 HDD Keyless.img"
  #   start sector   size    label            mount point
  1         63       55M     common-boot      /bootloader   (GRUB menu.lst)
  2     112455      157M     (not ext)
  3     433755      518M     common-swap      swap
  4    (extended)
  5    1494108      4.6G     sideA-*          ┐
  6   11229498        8G                      ├ A side — zeroed in this image
  7   28017423      5.4G                      ┘
  8   39262923      157M     sideB-home       /home   (/home/maxx = cabinet user)
  9   39584223      4.6G     sideB-root       /       (OS + engine + loader)
 10   49319613        8G     sideB-ion_only   /usr/local/ion_only   (games + content)
 11   66107538      5.4G     sideB-var        /var    (/var/merit = settings, logs, state)
```

A/B partition scheme: the cabinet updates into the inactive side. Here only side B exists.
Partition labels come from `/etc/fstab` on partition 9; the byte offset for tools is
`start_sector * 512`.

## Hardware it expects

From `/var/merit/hardware.xml`: Intel 945GC graphics ("i845" driver), FX1 I/O board (coin mech,
key), MicroTouch USB touch screen, Realtek r8168 NIC. X11 drivers present: `intel`, `i810`, `fbdev`.
None of this matters for a single-game port — we never boot the OS.

## Where things live

| Path (cabinet) | Partition | Contents |
|---|---|---|
| `/usr/local/bin/loader` | 9 | The "loader": main cabinet program. 2.9 MB, packed/obfuscated. Hosts the legacy sprite engine, Allegro, coin handling, security |
| `/usr/local/lib/*.so` | 9 | Engine libraries (`libgraphics.so`, `libgame_device.so`, ...) **and game code** (`g_trix.so`, `boxxi.so`, ...) |
| `/usr/local/gamedata/config/` | 9 | `gamedata.xml` (master game table), `languagedata.xml`, settings schemas |
| `/usr/local/gamedata/translations/` | 9 | `<game>.utf8` translation tables, `SupportFiles.utf8` |
| `/usr/local/gamedata/help/<lang>/` | 9 | Help texts |
| `/usr/local/gamedata/ttf/` | 9 | Fonts (EuroFONT EFN* family with scrambled filenames) |
| `/usr/local/games/` | 9 | A few game dirs + symlinks |
| `/usr/local/ion_only/games/g_<name>/` | 10 | **Per-game assets** for ION-era games |
| `/usr/local/ion_only/games/idle/` | 10 | Attract-mode videos (`g_<name>.mov`) for every game |
| `/usr/local/ion_only/games/launcher/` | 10 | Unity 3.2 `LinuxPlayer` (shared by the Unity games) |
| `/var/merit/settings.xml` | 11 | Operator settings — per-game `GameInfo` with `Active` flag and pricing |
| `/var/merit/locale/`, `/var/merit/settings/` | 11 | Locale state read by `liblocale.so` |
| `/var/merit/debug/<category>/<flag>` | 11 | Engine debug flags (touch a file to enable — see chapter 09) |
| `/home/maxx/.fonts.conf` | 8 | The fontconfig file the games actually used |
| `/etc/pango/i386-redhat-linux-gnu/pango.modules` | 9 | Pango 1.14 shaping-engine registry |

## Game IDs and names

`/usr/local/gamedata/config/gamedata.xml` has one `<game>` per title:

```xml
<game>
  <GameId>G_TRIX</GameId>            <!-- xml_gameinfo::GameIds enum; G_TRIX = 245 -->
  <DLLName>g_trix</DLLName>          <!-- /usr/local/lib/g_trix.so -->
  <Description>TRIX</Description>
  <Directory>g_trix</Directory>      <!-- asset dir -->
  <UseResolution>RESOLUTION_1280x800</UseResolution>
  ...
</game>
```

Newer games (added after the base build) carry their own `gamedata.xml` inside their asset folder.
`/var/merit/settings.xml` lists which are enabled (`GAME_ACTIVE`) — 188 on this image.

## How a game runs on the cabinet

Every game `.so` exports `main`. The loader `dlopen`s the game library into its own process,
so the game's unresolved symbols bind to whatever the loader exports (Allegro, the legacy
"sprite engine", `PlrScore`, `NVRAMData`, `Translator`, ...). Before calling `main`, the loader
`chdir`s into the game's asset directory — games open their layout files with relative paths.

## Four engine families

Classify a game library by its `NEEDED` entries (`readelf -d lib.so | grep NEEDED`):

| Family | Marker | Count here | Examples | Porting difficulty |
|---|---|---|---|---|
| **GameDevice** (2009+) | needs `libgame_device_sprite.so` | 22 | g_trix, g_word_dojo_2, photo_hunt_hd, text_twist2 | **Done** — this cookbook |
| **Unity 3.2** | asset dir has `Data/` (Managed DLLs, `*.assets`), no `.so` | ~30 | g_ice_rift, g_tri_towers_2, g_rock_mahjong | Different route — see chapter 11 |
| **Merit3D** | needs `libmerit3d.so` (+ODE, OpenGL) | 13 | luxor2, chainz2, beer_pong | Hard — legacy loader services |
| **Legacy sprite** | none of the above; imports `Sprite`, `WorldClass`, `Bitmap` | 143 | boxxi, elevenup, checkerz | Hardest — reimplement the loader's 2D engine |

The GameDevice family is the sweet spot: its graphics/sound/input go through clean C++
interfaces whose *base classes live in normal shared libraries*, and only a thin "sprite"
adapter talks to the loader.
