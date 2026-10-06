# megatouch-port documentation

The goal is every game from the Megatouch ION cabinet running on a normal PC (Linux or WSL2),
using the original game and engine binaries, and in the end a replacement for the cabinet's
loader itself. Two GameDevice games are fully playable today; the [roadmap](roadmap.md) covers
the remaining 200-odd.

## Start here

| I want to… | Read |
| --- | --- |
| Play Trix or Word Dojo 2 | [Quick start](guides/quick-start.md) |
| Port another game | [Porting a game](guides/porting-a-game.md), then the [survey](reference/gamedevice-survey.md) to pick one |
| Fix something that doesn't work | [Troubleshooting](guides/troubleshooting.md) |
| Find out why a game crashes or lags | [Debugging](guides/debugging.md) |
| Push my work | [Contributing](guides/contributing.md) |
| Know what's next for the project | [Roadmap](roadmap.md) |
| Understand how it works | [Architecture](reference/architecture.md) |

## Guides (task-oriented)

| Guide | Contents |
| --- | --- |
| [quick-start](guides/quick-start.md) | WSL install, packages, clone, image, `make setup`, play, Windows shortcut, standalone copy |
| [porting-a-game](guides/porting-a-game.md) | Scaffold → stand-ins → first run → play-through → commit, worked through on Word Dojo 2 |
| [debugging](guides/debugging.md) | Symptom → tool table; crash traces, screenshots, autoclick, paths, sound, profiler, ABI questions |
| [troubleshooting](guides/troubleshooting.md) | Setup, launch, gameplay and git problems with fixes |
| [contributing](guides/contributing.md) | Main repo vs game repos, everyday git, publishing, credentials, what to update, testing |

## Reference (look things up)

| Reference | Contents |
| --- | --- |
| [commands](reference/commands.md) | Every make target, `game.conf` key, `MEGA_*` variable, engine debug flag and tool |
| [architecture](reference/architecture.md) | Components, startup sequence, frame loop, file, image and sound data flow, repo layout |
| [engine-abi](reference/engine-abi.md) | Classes, sizes, field offsets, vtable slots; which slots our backend fills; adding a class |
| [loader-services](reference/loader-services.md) | Every loader stand-in with behaviour and the game that needed it; what unported games still need |
| [cabinet](reference/cabinet.md) | Disk image partitions, directory map, gamedata.xml, settings.xml, GameIds, engine libraries |
| [file-formats](reference/file-formats.md) | `.spr` (fully decoded), images, sound, layouts, fonts, translations |
| [known-bugs](reference/known-bugs.md) | Open issues; every bug fixed so far by category; diagnosing a new crash |
| [games](reference/games.md) | All 194 cabinet games: GameId, library, family, resolution, port status *(generated: `make docs`)* |
| [gamedevice-survey](reference/gamedevice-survey.md) | Each GameDevice game's missing loader symbols, easiest first *(generated: `make survey`)* |
| [glossary](reference/glossary.md) | Terms used throughout |

## History

[history/](history/README.md): the twelve-chapter journal of the first port (Trix), written as
it happened. It shows how the seam was found, how the backend was reverse-engineered and why each
fix exists. Paths in it are from before the reorganisation.

## Data

| File | Contents |
| --- | --- |
| [data/games-catalogue.tsv](data/games-catalogue.tsv) | Name, GameId and Active flag for each game in `/var/merit/settings.xml` |
| [data/engine-families.tsv](data/engine-families.tsv) | Engine family of each of the 178 game libraries |

## In one screen

1. **Read the image** with `debugfs` at partition offsets: no mounting, no root.
2. **Catalogue games** from `settings.xml` and `gamedata.xml`.
3. **Extract** a game's library plus its dependency closure and its asset folder.
4. **The seam:** GameDevice games import only `GameDevice::CreateNewGameDevice()` from the
   cabinet backend. Everything else is virtual calls on engine base classes.
5. **Our backend** builds engine objects with their own constructors and points them at copies of
   their vtables with our SDL2 functions in the slots.
6. **The host** stands in for a dozen loader functions and maps cabinet paths into a per-game
   `data/` folder, with old-glibc `stat` fixed for 64-bit inodes.
7. **The environment:** working directory = asset dir, the cabinet's fonts, Pango 1.14 modules,
   translation tables, old-malloc behaviour.
8. **Formats:** `.spr` decoded; PNG, TGA, WAV, OGG through SDL and stb_vorbis.
