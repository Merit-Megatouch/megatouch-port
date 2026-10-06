# Loader services

On the cabinet, games are `dlopen`ed into the loader process, so any symbol the loader exports
is available to them. These are the ones the GameDevice games use, what we know about each, and
what our stand-in in `src/host/loader_services.cpp` does.

`make analyze GAME=<name>` lists what a game still needs. [gamedevice-survey.md](gamedevice-survey.md)
lists it for every GameDevice game at once.

## Hardware and key checks

Some cabinet games are said to check the I/O board (coin mech, key switch) or the security key
mid-game, and to crash or stop when the check fails. Which games do this isn't known yet. A
GameDevice game can't reach hardware directly; a check has to go through one of:

| Route | How to see it |
| --- | --- |
| A loader function (`NVRAMData`, credits, `MegacGlobals`…) | `make analyze`: it shows as an unresolved symbol, so we choose its answer |
| A file or device (`/dev/...`, `/var/merit/...`, `/proc`) | `DEBUG=files` |
| IPC to the loader (`libipc_new`, unix sockets in `/dev/merit_ipc/`) | `[ipc]` lines in every run's output (`MEGA_TRACE_IPC=1` for every attempt) |

When a port crashes or freezes mid-game for no visible reason, check those three first and note
the result in the game's `NOTES.md`. Results so far: Trix, Word Dojo 2 and Boxxi Blitz make no
IPC connections and read no hardware paths during play.

## Rules for writing a stand-in

1. **Get the exact mangled name** from `notes/unresolved.txt` or `nm -D --undefined-only`.
   Overloads are different symbols: `HighEnough(int,int)` is not
   `HighEnough(xml_gameinfo::GameIds,int,int,int)`.
2. **Static or member?** The name doesn't say. Disassemble one caller
   (`objdump -d -C -M intel lib.so | grep -B12 'call.*Name'`): a member call pushes the object
   pointer as an extra first argument. Getting this wrong shifts every argument
   (`Translator::Translate` bug, [known bugs](known-bugs.md) #9).
3. **Return values matter.** Find what the caller does with the result in Ghidra output
   (`make decompile`) before picking "true", "0" or "".
4. **Data symbols** (`PlrScore`) must exist before the game loads, and their size must be big
   enough for every index used.
5. Singletons can return a pointer to a static buffer when the class has no fields we know of.
   Engine code never looks inside them.
6. Use `extern "C"` for C functions (Allegro helpers), and plain C++ declarations for classes,
   so the compiler produces the same mangled names.
7. Make a stand-in work for every game, and keep it small. Note in this file which game needed
   it.

## Implemented

| Symbol | Kind | Our behaviour | Cabinet behaviour | Needed by |
| --- | --- | --- | --- | --- |
| `PlrScore` | `int[8]` | Plain array | Player scores, read by the loader for high scores and network play | g_trix, g_word_dojo_2 |
| `ef_fp_filename` | `char[256]` | Plain buffer | Effect loader scratch filename | libeffect_loader |
| `Translator::GetInstance()` | static | Static dummy object | Singleton | g_trix |
| `Translator::LoadTranslations(const char*, bool)` | **static** | Loads `SupportFiles.utf8` once, then `translations/<name>.utf8` | Same tables | libtranslate_start |
| `Translator::Translate(const char*)` | **static** | Key or numeric id → English column; unknown → itself | Language from operator settings | g_trix, layouts |
| `ContinueControl::ContinueControl()`, `~ContinueControl()` | member | Nothing | Credit-based "continue?" prompt | g_trix |
| `ContinueControl::display(unsigned, unsigned)` | member | `true` (always continue, free play) | Shows the prompt and charges credits | g_trix |
| `HighScoresManager::Instance()` | static | Static dummy object | Singleton | g_trix |
| `HighScoresManager::HighEnough(int player, int)` | member | If `PlrScore[player]` beats the saved best, save it. Always returns `false`. | `true` opened the loader's name-entry screen | g_trix, g_word_dojo_2 |
| `HighScoresManager::Winner() const` | member | `0` | Index of the winning player | g_trix |
| `HighScoresManager::HighestScore(int)` | member | Saved best score | Top of the high-score table | g_word_dojo_2 |
| `HighScoresManager::HighestName(int)` | member | Saved best name | Its name | g_word_dojo_2 |
| `Locale::LanguageManager::GetInstance()` | static | Static dummy object | Singleton | g_word_dojo_2 |
| `Locale::LanguageManager::Active() const` | member | `0` = English | Operator language. Order as in `LanguagesSupported`: 0 English, 3 French, 4 Spanish, … | g_word_dojo_2 (dictionary folder) |
| `ustrlen(const char*)` | C | UTF-8 code point count | Allegro 4 | g_word_dojo_2 |
| `ugetat(const char*, int)` | C | Code point at index; negative counts from the end | Allegro 4 | g_word_dojo_2 |
| `ustrcmp(const char*, const char*)` | C | Compare by code point | Allegro 4 | g_word_dojo_2 |
| `ustrupr(char*)` | C | ASCII uppercase in place | Allegro 4 (full Unicode) | g_word_dojo_2 |
| `Logger::SetApplicationID(GameIds)` | static | Nothing | Tags log lines | g_trix |

### High scores in detail

Games keep the running score in `PlrScore[player]`. At game over they call
`Winner()` and then `HighEnough(winner, …)`. The cabinet's `true` opened a name-entry screen
that lives in the loader, which we don't have. So the stand-in records the score itself and
returns `false`, and the game plays its normal game-over sequence.

The file is `/var/merit/highscores/<GAME_ID>.txt` (per game:
`games/<name>/data/var/merit/highscores/`) and holds one line, `<score> <NAME>`. The name is
`MEGA_PLAYER_NAME`, else `$USER`, in upper case. Delete the file to reset it.

### Translations in detail

Files: `/usr/local/gamedata/translations/<game>.utf8` and `SupportFiles.utf8` (shared). Format,
one entry per line, optional UTF-8 BOM, header line containing `KEYSTRING`:

```
KEYSTRINGID|KEYSTRING|ENGLISH|GERMAN|FRENCH|…
1234|HELP_TEXT|Bid on how many tricks…\nThe player with…|…
```

Both the id and the key map to the English column. A literal `\n` becomes a newline. An empty
English column falls back to the key. Help text comes through the same tables, so placeholder
help ("HELP_TEXT", "Text line 1") means the translator isn't finding them.

To support another language, pick a different column (the header row names them) and set
`LANGUAGE` in `game.conf`. Both are untested.

## Still needed by unported GameDevice games

These came from [the survey](gamedevice-survey.md). Mangled names are from the game libraries.
The "probable" behaviour is a guess from the name and must be checked in the caller before it is
implemented.

| Symbol (mangled) | Demangled | Games | Probable stand-in |
| --- | --- | --- | --- |
| `_ZN17HighScoresManager10HighEnoughEN12xml_gameinfo7GameIdsEiii` | `HighEnough(GameIds, int, int, int)` | brain_in_gear, fight_the_landlord, text_twist2, tug_of_words, photo_hunt_hd, … | Newer overload with the game id first. Same logic as ours: save the best score and return false. Check which argument is the score. |
| `_ZN17HighScoresManager12HighestScoreEN12xml_gameinfo7GameIdsEi` | `HighestScore(GameIds, int)` | most | Saved best score (the id selects the file) |
| `_ZN17HighScoresManager11HighestNameEN12xml_gameinfo7GameIdsEi` | `HighestName(GameIds, int)` | most | Saved best name |
| `_Z9LightShowv` | `LightShow()` | fight_the_landlord, stunt_squirrel, funky_monkey_2, touchdown_poker, jackpot_corner | Cabinet marquee lights: no-op. Check the return type. |
| `_ZN16LightShowManager4PlayENS_9SequencesEbb` | `LightShowManager::Play(Sequences, bool, bool)` | same | No-op. Possibly static; check the caller. |
| `_ZN3ads11in_game_ads11get_ad_nameEN12xml_gameinfo7GameIdsE` | `ads::in_game_ads::get_ad_name(GameIds)` | tug_of_words, stunt_squirrel, touchdown_poker, jackpot_corner | In-game advert image name. Returns `std::string` by value (hidden result pointer): return an empty string. |
| `_ZN13ChampEditionI…` | `ChampEditionI::DisplayMadeIt(int,int)`, `DisplayPrizePool(int,int)`, `GetCurrentLeaderScore(int)`, `GetCurrentNumberOfRounds()` | funky_monkey_2, jackpot_corner | "Champion Edition" tournament mode: not running, so 0 and no-ops |
| `_ZN8Profiler11GetInstanceEv`, `_ZN8Profiler11DumpResultsEv` | `Profiler::GetInstance()`, `DumpResults()` | touchdown_poker | Static dummy object, no-op |
| `_ZN12MegacGlobals11GetInstanceEv` | `MegacGlobals::GetInstance()` | photo_hunt_hd, g_phillies_hottest_phunt | Loader globals object. Its fields are read inline, so the layout must be recovered first. Harder. |
| `_ZN8Settings…`, `_ZTI8Settings` | `Settings::Settings(const char*, unsigned)`, `Init`, `Load`, `Key`, `Restore`, `SaveNow`, `Cleanup`, `~Settings`, typeinfo | little_shop_of_treasures, g_little_shop_road_trip, g_space_farmer, photo_hunt_hd, g_phillies_hottest_phunt, g_mystery_phraze_hd | **Not a loader service**: these are defined in the cabinet's `/usr/local/lib/libsettings.so`, which the loader had already loaded. Ship that library and load it before the game (see below) instead of writing stand-ins. |
| `DBFClass::*`, `RandomizedArrayClass::*`, `xml_gamerandom::MystPICRAND_record::*`, `ugetx`, `uwidth` | — | g_mystery_phraze_hd | Same approach: look for a cabinet library that defines them first. This game also uses the Irrlicht backend, a different seam. |

### Libraries the loader preloaded

Some symbols are missing only because the loader process had already loaded a library the game
never lists as NEEDED. Before you write a stand-in, check whether a cabinet library defines it:

```bash
. ./cabinet.conf; . scripts/lib/cabinet.sh
mkdir -p /tmp/cablibs
for l in $(cab_ls root /usr/local/lib | grep '\.so$'); do cab_dump root /usr/local/lib/$l /tmp/cablibs/$l; done
for f in /tmp/cablibs/*.so; do nm -D --defined-only "$f" 2>/dev/null | grep -q ' T _ZN8Settings4InitEv' && echo "$f"; done
```

Known so far: `libsettings.so` defines `Settings` (it needs `libgendef_xml.so` and
`libmerit_threads.so`; the second is on the drop list in `new-game.sh`, so check whether it is
actually loader-dependent before you ship it). To use such a library, extract it with its closure
into the game's `lib/` and load it ahead of the game. A host option that `dlopen`s an extra list
from `game.conf` (for example `PRELOAD=libsettings.so`) would be the clean way to do this. It
doesn't exist yet.
