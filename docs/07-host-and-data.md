# 07 — Host shim, filesystem & data layout

## The data tree

The engine hard-codes cabinet paths. Rather than patch binaries, the launcher exports libc
wrappers that rewrite these prefixes to `$MEGA_HOME/data/...`:

```
/usr/local/games   /usr/local/gamedata   /usr/local/ion_only   /var/merit   /dev/merit_ipc
```

Wrapped: `open open64 fopen fopen64 access opendir mkdir rmdir unlink remove rename symlink
realpath stat64 lstat64 glob` and the old-ABI `__xstat __lxstat __fxstat __xstat64 readdir`.
Symbols defined in the main executable win symbol lookup for every library — including versioned
references like `glob@GLIBC_2.0` (verified with `LD_DEBUG=bindings`). `glob` results are
re-prefixed back to cabinet paths so callers can keep composing them.

`game/data/` layout:

```
data/
├── usr/local/ion_only/games/g_trix/   game assets (copy of the extracted folder)
├── usr/local/games/                   empty base dir (the locator checks it)
├── usr/local/gamedata/{config,translations,help,ttf,fonts}/   from partition 9
├── var/merit/{locale,settings}/       from partition 11 (/merit/...)
├── etc/fonts.conf                     /home/maxx/.fonts.conf from partition 8
└── pango/{modules/*.so, pango.modules.in}
```

**The game must start with CWD = its asset dir.** The loader did that; layouts, effect XMLs and
images are opened relative to it (`gfx/player_cards/...`).

## How the resource locator searches

`resources::ResourceLocator::get_file(subdir, name, type)` composes
`base / game / resolution / language / country / subdir / name.ext`, each level a fallback list:
* base: `/var/merit/games`, `/usr/local/ion_only/games/`, `/usr/local/games`
* game: `g_trix`, `default`
* resolution: `1280x800`, `default`, (none)

Each candidate is tested with `DirExists()` (`__xstat`) then `SymlinkExists()` (`__lxstat`). If
nothing matches you get `found [] for [...]` and an empty filename — which is how the 64-bit
inode bug (chapter 10) showed up. Turn on the engine's own trace with
`data/var/merit/debug/files/resource_locator` (chapter 09).

## Old glibc ABI

The 2008 libraries call `__xstat(3, path, struct stat*)` with the **32-bit `struct stat`** (88
bytes; `st_ino` 32-bit, `st_mode` at offset 16) and `readdir@GLIBC_2.0` (32-bit `d_ino`). On
filesystems with 64-bit inode numbers (WSL `/mnt/*`, often ext4 too) glibc returns `EOVERFLOW`.
The shim implements them on `stat64`/`lstat64`/`fstat64`/`readdir64` and converts. `readdir` is
defined with an asm label (`void* readdir_old(DIR*) __asm__("readdir")`) to avoid clashing with
`<dirent.h>`.

## Loader services (`src/host/loader_services.cpp`)

```cpp
extern "C" { int PlrScore[8]; char ef_fp_filename[256]; }

class Translator { public:
  static Translator* GetInstance();
  static bool LoadTranslations(char const*, bool);   // STATIC — callers push only the args
  static char const* Translate(char const*);         // STATIC
};
class ContinueControl { public: ContinueControl(); ~ContinueControl(); bool display(unsigned, unsigned); };
class HighScoresManager { public: static HighScoresManager* Instance(); bool HighEnough(int,int); int Winner() const; };
class Logger { public: static void SetApplicationID(xml_gameinfo::GameIds); };
```

Static vs member doesn't change the mangled name, only the calling convention — so check the
caller's disassembly (count the pushed arguments) before declaring.

**Translations**: `translations/<game>.utf8` (plus `SupportFiles.utf8`) is
`KEYSTRINGID|KEYSTRING|ENGLISH|GERMAN|...`. Map both the key and the numeric id to the English
column, converting literal `\n` to newlines. Layout text like `HELP_TEXT`, `Text line 1` are
keys — with a pass-through translator you get placeholder text.

## Fonts and text

The text system is Pango 1.14 + FreeType + fontconfig 2.4 from the image.
1. `FONTCONFIG_FILE=data/etc/fonts.conf` — the cabinet's own file; it lists
   `/usr/local/gamedata/ttf`, which the shim redirects. Fonts are EuroFONT families with
   scrambled filenames (`torrey3.ttf` = "EFN TorreyaC Bold", which layouts request as
   `EFNTORREYAC-BOLD`).
2. **Pango shaping modules**: without them every glyph renders as a box. Ship
   `/usr/lib/pango/1.5.0/modules/*.so` (minus `pango-basic-x.so`) and the image's
   `/etc/pango/i386-redhat-linux-gnu/pango.modules`; at launch, rewrite module paths to the
   install dir into a temp file and point `PANGO_RC_FILE` at a pangorc containing
   `[Pango]\nModuleFiles = <that file>`. (Modules are `dlopen`ed — the path shim can't redirect them.)

## Configuration defaults chosen

| Setting | Value | Why |
|---|---|---|
| Game ID | 245 | G_TRIX |
| Window | 1280×800 logical, resizable | Trix is `RESOLUTION_1280x800` widescreen-only |
| Language | english | |
| Card fanning | off | operator option; art not shipped |
| Continue prompt | always yes | free play |
| High scores | never high enough | skips name entry |
| Rate | 30 updates / 30 fps | the cabinet default; 60 breaks game timing |
