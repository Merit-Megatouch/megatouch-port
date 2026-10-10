# megatouch-port documentation

This project runs a Megatouch ION (2014) cabinet's own software, unmodified, on a PC (Linux or
Windows with WSL2), with the cabinet's hardware simulated: menus, every game, Operator Setup,
coins, keys, MegaNet and TournaMAXX. An older route, standalone ports of single games, is kept
for development.

## Start here

| I want to… | Read |
| --- | --- |
| **Run a cabinet on my PC and look after it** | [Operator guide](guides/operator-guide.md) |
| Install from scratch, step by step | [Quick start](guides/quick-start.md) |
| Fix something that doesn't work | [Troubleshooting](guides/troubleshooting.md) |
| Know how the cabinet is simulated | [Cabinet loader](guides/cabinet-loader.md), [I/O board](reference/io-board.md) |
| Connect real lights, coin acceptors, buttons, keys, home automation | [Connectors](guides/connectors.md) |
| Understand or reimplement the cabinet's software | [Cabinet software](reference/cabinet-software.md), [cabinet state](reference/cabinet-state.md), [MegaNet](reference/meganet.md), [MegaLink](reference/megalink.md), [events](reference/events.md), [options](reference/options.md) |
| Look up a command or setting | [Commands](reference/commands.md) |
| Know what's next for the project | [Roadmap](roadmap.md) |
| Port a single game (developers) | [Porting a game](guides/porting-a-game.md) |
| Push my work | [Contributing](guides/contributing.md) |

## Guides (task-oriented)

| Guide | Contents |
| --- | --- |
| [operator-guide](guides/operator-guide.md) | Install, start/stop, the window, controls, first-time setup, Operator Setup map, coins and free play, operator and player keys, the game menu, network/MegaNet/TournaMAXX, game options, backups, updating, problems |
| [quick-start](guides/quick-start.md) | WSL, packages, clone, image path, `make setup`, `make loader-setup`, first run |
| [troubleshooting](guides/troubleshooting.md) | Setup and cabinet problems with fixes; then the standalone ports |
| [cabinet-loader](guides/cabinet-loader.md) | How the cabinet runs: every stand-in and compatibility fix, network, licence key and options, backups, settings |
| [connectors](guides/connectors.md) | The hardware bridge: event feed, commands, MQTT, webhooks, WLED, input devices, GPIO, iButton reader, light show, books printer |
| [porting-a-game](guides/porting-a-game.md) | Standalone ports: scaffold → stand-ins → first run → play-through → commit, worked through on Word Dojo 2 |
| [debugging](guides/debugging.md) | Standalone ports: symptom → tool table; crash traces, screenshots, sound, profiler |
| [contributing](guides/contributing.md) | Main repo vs game repos, everyday git, publishing, what to update, testing |

## Reference (look things up)

| Reference | Contents |
| --- | --- |
| [commands](reference/commands.md) | Every make target and script, the cabinet loader's settings, `game.conf` keys, `MEGA_*` variables, engine debug flags and tools |
| [architecture](reference/architecture.md) | Components, startup sequence, frame loop, file, image and sound data flow, repo layout |
| [engine-abi](reference/engine-abi.md) | Classes, sizes, field offsets, vtable slots; which slots our backend fills; adding a class |
| [loader-services](reference/loader-services.md) | Every loader stand-in with behaviour and the game that needed it; what unported games still need |
| [cabinet](reference/cabinet.md) | Disk image partitions, directory map, gamedata.xml, settings.xml, GameIds, engine libraries |
| [legacy](reference/legacy.md) | The legacy route: reconstructed loader API, `.dlt` delta animations, touch zones, next games |
| [unity](reference/unity.md) | The Unity route: shared player, game folder layout, launcher.xml keys, sound shim, cabinet .NET code |
| [file-formats](reference/file-formats.md) | `.spr` (fully decoded), images, sound, layouts, fonts, translations |
| [known-bugs](reference/known-bugs.md) | Open issues; every bug fixed so far by category; diagnosing a new crash |
| [games](reference/games.md) | All 194 cabinet games: GameId, library, family, resolution, port status *(generated: `make docs`)* |
| [gamedevice-survey](reference/gamedevice-survey.md) | Each GameDevice game's missing loader symbols, easiest first *(generated: `make survey`)* |
| [io-board](reference/io-board.md) | USB I/O board protocol (including the light show and books printer), encrypted loader, security key (licence, option values, the key image) and operator fob (iButton) formats |
| [cabinet-software](reference/cabinet-software.md) | The loader's internals: start-up and processes, how games run, game and screen IDs, hardware from the loader's side, light show, books printer, NVRAM, identity, MegaNet, MegaLink, debug flags, logging, open questions |
| [events](reference/events.md) | The event log (`events.jsonl`): every event type and field |
| [options](reference/options.md) | The 120 game options: index, name, the image's value, our key's licence |
| [cabinet-state](reference/cabinet-state.md) | Where the cabinet keeps its state: the NVRAM map, the encrypted databases (schemas, `dbdump`), the settings files |
| [meganet](reference/meganet.md) | The MegaNet exchange: hosts, registration check, the POST session, every table, error codes; enough to write a server |
| [megalink](reference/megalink.md) | The MegaLink wire protocol: discovery, challenges, the TCP game link, packet formats, a linked game step by step |
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

## The standalone ports in one screen

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
