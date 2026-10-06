# Unity games

30 cabinet games are Unity 3.2 games. They all run on one shared player,
`/usr/local/ion_only/games/launcher/LinuxPlayer` (Unity 3.2.0f4, 32-bit, 40 MB, Mono built in).
Each game is only a `Data/` folder: scenes, assets and .NET code. We run the cabinet's player
unchanged on our 32-bit runtime. First game: Tri Towers 2 (2026-10-07), with graphics, sound and
input working.

```bash
make new GAME=g_tri_towers_2     # name = the game's folder (all Unity games have DLLName "launcher")
make run GAME=g_tri_towers_2
```

## How the cabinet started them

`/usr/local/bin/launcher.sh <game>`:

```sh
ln -vsfT ../../../../usr/local/ion_only/games/$GAMEID/Data /var/merit/games/launcher/Data   # point the player at the game
cd /var/merit/games/$GAMEID                                                                # writable cwd
LD_LIBRARY_PATH=...:/usr/local/ion_only/games/launcher/lib \
  /usr/local/ion_only/games/launcher/LinuxPlayer /var/merit/games/$GAMEID/launcher.xml \
  -FMOD_OUTPUTTYPE_OSS -DSP_BUFFER 2048 4 -popupwindow -nolog > output_log.txt 2>&1
```

The loader wrote `launcher.xml` (the game's settings) first, then talked to the running game
over a FIFO or TCP socket (`launcher.messaging.*`) for menus, pause and challenges.

## What a port does

| Problem | Fix |
| --- | --- |
| Five player libraries missing from a modern runtime | `setup.sh` copies them from the cabinet into `shared/unity/lib`: libGLU.so.1, libcurl.so.3 (+ libcares, libidn), libfmodex.so, libtheora.so.0, libgthread-2.0.so.0. Everything else (X11, Mesa GL, glib, freetype, ogg/vorbis, OpenSSL 0.9.8) is already in `shared/runtime`. |
| The player finds `Data/` next to its own executable, with symlinks resolved | `games/<g>/player/` holds a **hard link** of `LinuxPlayer`, a copy of `ld-linux.so.2`, and `Data → ../data/usr/local/ion_only/games/<g>/Data` |
| `launcher.xml` comes from the loader | `launch.sh` writes `var/launcher.xml` at every start (see below) |
| No sound: the player always selects FMOD's OSS output (`setOutput(10)`) | `libmega_unity.so` (`src/unity/fmod_output.cpp`), preloaded, switches it to PulseAudio (13). `MEGA_FMOD_OUTPUT=<n>` overrides: 0 auto, 10 OSS, 11 ALSA, 12 ESD, 13 PulseAudio. |
| Writable working directory | `games/<g>/var/` |

Game folder:

```
games/<g>/
├── game.conf      ENGINE=unity GAME GAME_ID WIDTH HEIGHT LANGUAGE TITLE
├── NOTES.md  README.md  notes/scaffold.md
├── data/usr/local/ion_only/games/<g>/Data/      the game (cabinet files)
├── player/        LinuxPlayer (hard link)  ld-linux.so.2 (copy)  lib → shared/unity/lib
│                  libmega_unity.so → shared/bin   Data → ../data/…/Data
├── var/           cwd; launcher.xml written here at each start
└── run → scripts/launch.sh     runtime → shared/runtime
```

## launcher.xml

A flat list of settings, the same format as the game's own `Data/settings_gamedata.xml`:

```xml
<settings>
  <setting identifier="launcher.gameid" value="g_tri_towers_2" />
  <setting identifier="launcher.window.width" value="1024" type="System.Int32" />
  …
</settings>
```

Keys we write (from `game.conf`, so `MEGA_*` variables override them):

| Key | Value | Read by |
| --- | --- | --- |
| `launcher.gameid` | `GAME` (the folder name) | LauncherFile, LauncherUnity |
| `launcher.language` | `LANGUAGE` | translations (default english) |
| `launcher.window.width`, `.height` | `WIDTH`, `HEIGHT` | layout (defaults 1024×768) |
| `launcher.playercount` | 1 | |
| `launcher.game_type`, `launcher.rule_set` | empty | |
| `launcher.no_challenges_database` | true | skips the challenges server |
| `extended_play`, `allow_music`, `card_fan` | false, true, false | game options |
| `content.filter` | empty | |
| `player.0.player_name`, `player.0.is_anonymous` | `$MEGA_PLAYER_NAME`/`$USER`, true | |
| everything in `Data/settings_gamedata.xml` | as is, and again with `games.<g>.` → `games.currentgame.` | upgrades, challenges, categories |

Other keys the cabinet code asks for (from strings in `AMIUnityExtensions_Plugins.dll` and
`LauncherFile.dll`): `AllPlayersLoggedIn`, `erotic.filter`, `erotic.rating`,
`leaderboard.local.all.currentgame.highest.name`/`.avatar_path`, `launcher.categoryid`,
`launcher.processid`, `launcher.menus`, `launcher.messaging.fifo_path`/`pipe_name`/`port`/`server_port`,
`launcher.challenges.tcp.ip_address`/`port`, `player.guid`, `player.username`,
`player.avatarlocation`, `player.upgrade`. Run a game and grep `Couldn't pull` to see which
ones it misses.

## The cabinet code inside a Unity game

| DLL | Role |
| --- | --- |
| `Assembly-CSharp.dll` | The game itself |
| `AMIUnityExtensions_Plugins.dll` | AMI's shared Unity layer: `CrossPlatformData` (reads launcher settings), translations, `AMIAudioManager`, input |
| `LauncherFile.dll` | Loads and watches `launcher.xml`; challenges, upgrades, awards; FIFO/TCP messaging with the loader |
| `LauncherUnity.dll` | Glue between the two |

These are .NET assemblies. Their string literals are UTF-16 (`strings -el file.dll`). For
real decompilation use ILSpy or dnSpy on a Windows or .NET machine, or Mono.Cecil to patch them.

## Known issues

| Issue | Notes |
| --- | --- |
| `Couldn't pull AllPlayersLoggedIn from launcher` every frame | The key name or type the code wants isn't known yet; log spam only (sign-in text) |
| `DllNotFoundException: MonoPosixHelper` | Native helper for the launcher's FIFO messaging. Not on the cabinet either, so its messaging presumably used TCP. Harmless here. |
| `Could not load any suitable font: Arial` | Unity's built-in GUI font; the game's own text renders |
| A few `Has no translation for this keystring` | Strings missing from the game's translation table |
| No high scores, challenges or upgrades persistence | Those went through the loader's messaging and database |
