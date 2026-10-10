# The cabinet's software: what we know

How the Megatouch ION 2014 loader works, as far as this project has learned it. It's written for
preservation, and for anyone who reimplements the cabinet, its hardware or its servers.

**Sources:**

- **Code:** the loader binary `start` (the 2021 "Keyless" build, `reference/loader/bin/start.2021`)
  read in Ghidra. Function names are the loader's own, from its exported symbols.
- **Runs:** observations of the unmodified software running under this project's stand-ins
  ([cabinet loader](../guides/cabinet-loader.md)).

Anything marked *inferred* comes from code reading and wasn't confirmed by a run.

**Related pages:**

| Page | Covers |
| --- | --- |
| [cabinet](cabinet.md) | disk image, partitions, directory map |
| [io-board](io-board.md) | the USB I/O board, security key and key readers |
| [events](events.md) | the event log our stand-ins write |
| [options](options.md) | the 120 game options |
| [cabinet state](cabinet-state.md) | NVRAM map, databases and settings files |
| [meganet](meganet.md), [megalink](megalink.md) | the MegaNet server exchange, the MegaLink wire protocol |
| [games](games.md) | every game's ID and library |

- [Start-up and processes](#start-up-and-processes)
- [Running games](#running-games)
- [Hardware, from the loader's side](#hardware-from-the-loaders-side)
- [Light show](#light-show)
- [Books printer](#books-printer)
- [NVRAM and game options](#nvram-and-game-options) (details: [cabinet state](cabinet-state.md))
- [Identity: serial number and MegaNet ID](#identity-serial-number-and-meganet-id)
- [Network, MegaNet and MegaLink](#network-meganet-and-megalink)
- [Debug flags and logging](#debug-flags-and-logging)
- [Open questions](#open-questions)

## Start-up and processes

On the cabinet, X starts `/home/maxx/.xinitrc`. Our `src/fakeio/xinit.sh` does the same steps:

1. Log the version to `/var/merit/log`; start `network_manager` (`/etc/init.d/merit-networkmanager`).
2. Set the screen to 768×480 (widescreen, `IsWidescreen` from `hardware_detect_utils.sh`) or
   640×480; apply `/etc/X11/modmap`.
3. `db_state` (state database daemon), then `layout_daemon`, which owns the screen layout.
   `layout_client` pushes `start_layout`, on widescreen also `sidebar_layout` (the ad sidebar and
   its left/right switcher), and `loading_layout`.
4. `exec /usr/local/bin/start -name merit-start --videomode F`: the loader.

Programs seen running on a cabinet, by process name:

| Process | Does |
| --- | --- |
| `start` | the loader: attract loop, menus, Operator Setup, credits, I/O, runs every game |
| `layout_daemon`, `layout_client`, `layout` | window layout (game area, sidebar, switcher) |
| `sidebar`, `window_switcher`, `widescreen_in_game_ads`, `widescreen_high_scores` | the widescreen sidebar and what it shows |
| `loading` | the loading screen |
| `db_state` | shared state over IPC (`/dev/merit_ipc/…`) |
| `network_manager`, `network_ui`, `mim` | network configuration and the Network menu's helpers |
| `system_logger`, `remote_diagnostics` | logging, remote diagnostics |
| `credit_card_reader` | the card reader service (exits without a reader) |
| `LinuxPlayer` | Unity games, started per game |

Each has a "non-game" ID in the `GameIds` enum (below); the logger tags log lines with it.

## Running games

Every game, and every full-screen program of the cabinet itself, is started through one function:

```
ExecuteGameID(GameIds id, const char *module, const char *entry)
  ├─ remember the current ID; MegacGlobals+0x2038 = id; Logger::SetGameID(id)
  ├─ special IDs: OPSETUP, CALIBRATE, IDLE, … (some update the minidrucker coin counts first)
  ├─ games (0–299): minidrucker::UpdateCoinDrops(); on machines with more than 256 MB (by
  │                 system_info::GetMemoryDetails) a running merit-firefox is paused (SIGSTOP);
  │                 resolve the game's resources; run the module (below); resume it (SIGCONT)
  └─ restore the previous ID; Logger::SetGameID(previous)
```

Running a module (`FUN_001c956e`, `dll.cpp`):

1. `FileLoc` is set to `/usr/local/gamedata/gamegraphics/<dir>/`, and
   `<FileLoc>AllGraphics.ini` is read if present.
2. The module is `dlopen("/usr/local/lib/<module>.so", RTLD_NOW)`. A name starting with `.` or
   `/` is used as a path.
3. `dlsym(entry)` (the game's `main`) is called, and returns when the player leaves the game.
4. The module is `dlclose`d.

Games therefore run inside the `start` process and share its I/O, sound and credits. That is why
a crash in a game can take the loader down, and why `gameevents.so` can see every game start and
end.

**Unity games** (the newer titles) run as a separate process:

- `ExecuteModule` loads `launcher.so` (game ID `LAUNCHER`, 10062).
- `launcher.so` starts `launcher.sh`, which `exec`s
  `/usr/local/ion_only/games/launcher/LinuxPlayer` for the game's folder under
  `/usr/local/ion_only/games/<game>/`.

See [unity](unity.md).

**Game IDs** (`xml_gameinfo::GameIds`, names from `libenums.so`):

| Range | What |
| --- | --- |
| 0–299 | games (`MAX_GAMES` = 300; 255 = `NO_GAME`); the list is in [games](games.md) |
| 10000–10063 | the cabinet's own programs and screens (`MIN_NON_GAME_ID` … `MAX_NON_GAME_ID`) |

| ID | Tag | ID | Tag |
| --- | --- | --- | --- |
| 10001 | OPSETUP (Operator Setup) | 10033 | ENTERTAINER_SONG |
| 10002 | IDLE (attract loop) | 10034 | ENTERTAINER_REFUND |
| 10003 | CALIBRATE | 10035 | WINDOW_SWITCHER |
| 10004 | TTUNES (TouchTunes) | 10036 | SIDEBAR |
| 10005 | SIXSTAR | 10037 | WIDESCREEN_IN_GAME_ADS |
| 10006 | DRINKTANK_REMOVED | 10038 | WIDESCREEN_HIGH_SCORES |
| 10007 | ECHANNEL_REMOVED | 10039 | LAYOUT |
| 10008 | BROWSER | 10040 | CREDIT_CARD_READER |
| 10009 | INTPATCH_REMOVED | 10041 | LAYOUT_DAEMON |
| 10010 | PRIZENET_REMOVED | 10042 | LAYOUT_CLIENT |
| 10011 | OPERATOR_WEBSITE | 10043 | DISABLE_LOGGING |
| 10013 | COINLESS_COINOP_PLAYS | 10044 | ADS |
| 10014 | COINLESS_COINOP_TIME | 10045 | SYSTEM_LOGGER |
| 10015 | COINLESS_COINOP_RENTAL | 10046 | INSTALLER |
| 10016 | G_SHOW_TENDERS_ONLINE | 10047 | NETWORK_UI |
| 10017 | G_FANTASYSPORTS | 10048 | NETWORK_MANAGER |
| 10018 | FUNZONEPRIZES_REMOVED | 10049 | AMI_CATALOG |
| 10019 | NATION_REMOVED | 10050 | UPGRADE |
| 10020 | CALIBRATE_JOYSTICK | 10051 | REMOTE_DIAGNOSTICS |
| 10021 | GAMETIME_UPDATE_SITE | 10052 | G_AD_CATEGORY |
| 10022 | MEGATOUCH_NATION_ORDER_KEY | 10053 | DB_STATE |
| 10023 | ROWELINK_SONG | 10054 | FIREFLY_UNLOCK |
| 10024 | ROWELINK_REFUND | 10055 | BATTERY_MONITOR |
| 10025 | VOLUME | 10056 | FIREFLY_NAVIGATION |
| 10026 | G_MENU | 10057 | SIDEBAR_VOLUME |
| 10027 | TRACKBALLTEST | 10058 | ENTER_OPERATOR_PIN_FIRST_BOOT |
| 10028 | G_BREAKAGE | 10059 | ENTER_OPERATOR_PIN_RESET_BOOT |
| 10029 | BROWSER_INTERACTIVE_AD | 10060 | RESET_OPERATOR_PIN |
| 10030 | G_PROTOTYPE | 10061 | FIREFLY_IOTEST |
| 10031 | VIDEOSALES | 10062 | LAUNCHER (Unity games) |
| 10032 | VIDEO_AD_REPLAY | | |

`MegaLinkPoll::RequestLink(GameIds, …)`, `Tournamaxx::IsRunning(GameIds)`,
`ChampEditionI::GetGameCost(…, GameIds)` and others take the same IDs. The game IDs are stable
across software versions: MegaNet servers and TournaMAXX use them.

## Hardware, from the loader's side

The loader's I/O thread polls the board (`USBIO::USBDoIO`) continuously; the protocol is in
[io-board](io-board.md#the-io-poll-command-0x06).

| Hardware | Loader side |
| --- | --- |
| Coin/bill inputs (8 channels) | pulses → `CreditsManagerI::AddCredits`; per-channel value from *Credits/Pricing*; `minidrucker::NotifyCoinDrop` counts for the books printer |
| Coin meter, TournaMAXX meter | `AddMoneyToHardMeter` → output bytes 1, 2 (one pulse per count) |
| Coin / bill lockout | two flags in the poll command; set when the cabinet refuses money (*inferred*: out of service, tilt, Operator Setup) |
| SETUP, CALIBRATE buttons | status byte 9 bits 0, 1 → Operator Setup (`OPSETUP`), touchscreen calibration |
| DIP switches DS1 | status byte 8 |
| Front key reader (iButton) | operator keys (PIN → Operator Setup) and My Merit player keys; records on the key's memory |
| Security key (iButton inside) | licence: part number, revision, option licences (*Security key* in io-board) |
| Joystick accessory | status bytes 12–15 and byte 9 bits 4, 7; `USBIO::JoystickFound` |
| PSoC microcontroller | its version is status byte 17 (`USBIO::GetPSOCVersion`, read once and cached) |
| Light-show kit | 0x27A0 = 0x80 when detected; packets through board RAM (below) |
| Books printer | status byte 9 bit 2 = connected; lines through board command 5 (below) |
| Watchdog | the poll carries a heartbeat counter; `USBIO::EnableWatchdog`/`DisableWatchdog` around calibration |

The PSoC version changes the loader's behaviour in three places:

| Where | Condition | Effect |
| --- | --- | --- |
| `LightShowManager::IsSupported` | version ≥ 2 (and ≠ 0xFF), ION platform, kit detected | light show on |
| `SystemInfo::HasImprovedAmp` | version ≥ 10 | the board has the improved amplifier |
| `MenuClass::StartMenu` | version ≠ 0, or not ION | without a joystick, joystick-only games (game flag `0x21b & 2`) are switched off |

## Light show

`LightShowManager` (`lightshow.cpp` in `start`, also used by `idle.so`, `opsetup.so`,
`sixstars.so` and `volumecontrol.so`) drives an LED lighting kit through the board's PSoC.
Debug logging: the flag `lightshow/debugls`.

| Method | Packet (byte 0 is always 1) |
| --- | --- |
| `Play(Sequences seq, bool repeat, bool stop)` | `01 01 seq` (once), `01 02 seq` (repeat), `01 00` (stop); `System_Init` calls `Play(5, true, false)` |
| `SetLights(r, g, b, x)` | `01 0B r g b x` |
| `SetActive(bool)` / `IsActive()` | `01 0F on` / `01 10` → answer |
| `SetProfile(Profiles)` / `GetProfile()` | `01 11 p` / `01 12` → answer |
| `SetBrightness(u8)` / `GetBrightness()` | `01 15 v` / `01 16` → answer |
| `Help()`, `HelpProfile(p)`, `HelpBrightness()` | text for Operator Setup's help screens |

`SendPacket` uses a handshake over board RAM and the status bytes; the protocol is in
[io-board → Light show](io-board.md#light-show). After 5 s without an answer a send fails, and
after 6 failures `m_LSIsTalking` (starts at 1) is cleared, which stops all light-show traffic
until the next start.

Sequences played by the loader: 0 attract loop, 2/3 special high-score animation, 4 high-score
entry, 5 power-on (repeating) and coin-in. Six sequences exist (`seq < 6`).

## Books printer

The German market read the books with a small printer plugged into the cabinet ("Minidrucker",
`minidruk.cpp`). It's checked on every pass of the attract loop (`idle.so` calls
`ProcessMiniDruker`, unless `MegacGlobals+0x2094` is set):

1. **Detect:** status byte 9 bit 2 set, and nothing printed since it was plugged in
   (`DataDownloaded`).
2. **Hello:** `WaitForEnq` sends an empty line. Any board answer counts; with the USB board it
   always succeeds.
3. **Screen:** "Printer recognized / Printing… / Please wait until printout is complete, then
   disconnect printer" (`prntrp1`, or `prntrp1ts` when the key's country is 0x0B).
4. **Print:** the books go out line by line (`SendDataLine` → `USBIO::SendMDLine`, board
   command 5).
5. **Finish:** six line feeds; then, if the printer asked for it, `ESC C <4-hex-digit sum of all
   bytes sent> LF`; then a single 0x16.
6. **Clear?** "Do you want to clear current books? Yes / No". *Yes* clears the current-period
   stats (`MiniDruckerStats::Clear`). Country 0x0B only shows *Done*, and clears automatically
   with printer type 1.
7. **Re-arm:** printing happens again only after the printer is unplugged (bit 2 cleared).

The printer's answer to a line (board RAM 0x1E6C = 0x13) identifies it: 'L' at 0x1D75 = type 1,
otherwise type 2; 'C' at 0x1D7A = wants the checksum line.

Printout layout (`SendAllData`, country ≠ 0x0B; `NewSendAllData` for 0x0B differs):

```
AMI ENTERTAINMENT
MT XL
PROG: PG3002-01 V40
GAME NR: <operator game number>
LAST READING: dd.mm.yy      CURRENT CLEAR: dd.mm.yy      CURRENT READ: dd.mm.yy
TIME: hh:mm:ss
CURRENT STATS:
TOT. MTR PULSES: n   TOTAL CREDITS: n   FREE CREDITS: n
<short game name>  <count>          (one line per game)
… player-count lines " nP=n", "L=n", "S=mm:ss A=mm:ss L=mm:ss" (session times)
TOURN SEQ n: PLAYED / W/O PIN / UNCLAIMED / CLAIMED
LIFE STATS: (the same, since the machine was new)
ENDE NC
```

The operator game number and location number are settings (`minidrucker::SetOperatorGameNumber`,
`SetLocationNumber`). With the global `Dest_Print_File` set, lines go to `Books_Dest_File` (a
`FILE*`) instead of the printer: a print-to-file path inside the loader.

## NVRAM and game options

`/var/merit/nvram.dat`, 6276 bytes (0x1884), is the loader's `NVRAMData` written out whole. It
holds the key's part number and revision, the 120 game options at `0x40 + index`, the menu
layout, the coinless/rental-mode state and a few flags, with a 16-bit checksum of bytes 1–0x1881
at 0x1882. The full map is in [cabinet state](cabinet-state.md#nvram-nvramdat). Most other state
(books, credits, high scores, tournaments, players) is in encrypted SQLite databases, also
described there.

The running loader keeps NVRAM in memory and rewrites the whole file, so edits only stick while it
is stopped. If the key's part number or revision doesn't match NVRAM, the loader clears NVRAM at
boot (keeping a few preserved fields).

The security key decides per option what Operator Setup may change:

| Key value | Meaning |
| --- | --- |
| 0 | locked off |
| 1 | locked on |
| 2 | operator's choice, default off |
| 3 | operator's choice, default on |

A settings reset sets NVRAM to `value & 1`. Without a key everything reads 0: every key-gated menu
disappears.

## Identity: serial number and MegaNet ID

| What | Where | Format |
| --- | --- | --- |
| Hardware serial number | I/O board EEPROM 0x1FE0 (16 bytes), valid when 0x1FD6 holds `100293JKKJ` (`USBIO::ReadSNFromEEPROM`) | this image: `043010MRE60023` |
| MegaNet machine ID | `/var/merit/settings/twobutton/network_config.xml`, encrypted | 14 digits, this image `1023…` |
| MegaNet server name | same file | e.g. `us.oerinet.net` |
| MegaLink ID | same file; in use, the last octet of the IP address | 1–254 |

The encrypted network settings are read and written through the cabinet's own `libnetwork.so`
(`network::settings::Interface`): `get_/set_meganet_machine_id`, `get_/set_meganet_server_name`,
`get_/set_megalink_id`, `save_settings`. Code calling it must use the pre-C++11 `std::string`
ABI (`-D_GLIBCXX_USE_CXX11_ABI=0`); `src/fakeio/netcfg.cpp` is a working example.

## Network, MegaNet and MegaLink

**Configuration:**

- **Managers:** `network_manager` configures `eth0` (wired) or `wlan0` (wireless) and runs
  ISC `dhclient` and its `dhclient-script` (in an empty environment). Dial-up is also supported
  (`/var/merit/tournamaxx/dialinfo.*` per country).
- **Wizard:** the *Connection Wizard* writes the method into `network_config.xml`.
- **Status:** *Network Summary* shows the method, MegaNet ID, connection status ("OK – PINGED
  MEGANET SERVER") and last connection time.

**MegaNet:** HTTPS (libcurl, OpenSSL 0.9.8) to `https://tng<server>/go`, after a registration check
at `https://tng<server>/check_registration?meganetid=<id>`. Each step of the session is one
multipart POST: the cabinet sends `## <Table> field|…` records (first `Login`), and the server
answers with `## LINE_HEADER|<Table>|…` tables, including `Request*` tables for what it wants
next, until `Logout`. The tables carry books, logs, crash dumps, settings, menu layout, game
options, tournaments, players, file downloads and the clock. The full exchange, every table and
the error codes are in [meganet](meganet.md).

**TournaMAXX:** the *Competition!* button appears with *Tournament Mode: ON-LINE*
(`TOURNAMAXX_ENABLED`), a MegaNet connection, and free play off (or `TMAXX_OK_IN_FREEPLAY`). It
lists the tournaments the server sent and greys out when there are none.

**MegaLink (linked games, "Wired Game-to-Game"):** option `LINKED_GAMES_ENABLED` (and not
rental mode). Three layers:

- **Discovery:** UDP broadcasts on port 4700 (ID request/response every 10 s).
- **Challenges:** UDP broadcasts on 4703 (game ID, language, session ID that also seeds the
  shared random deal).
- **The game:** TCP on 4704, one connection per direction, with heartbeats, NetSprite lobby
  objects and the game's own packets.

A cabinet's ID is the last octet of its IP address, and linked cabinets must share one /24 and
the same major software version (40 here). A complete linked game (11 Up) was played between two
cabinets and captured; every packet format is in [megalink](megalink.md).

## Debug flags and logging

`Debug_Flag_Set(category, name)` (`libdebug_shared.so`) is true when the file
`/var/merit/debug/<category>/<name>` exists. The flags below are the ones the loader and its
helpers asked for in a run (boot, attract loop, coins, a game, Operator Setup, the Network menu),
recorded by wrapping `Debug_Flag_Set`. More are checked in screens not visited. The game
engine's flags are in [commands](commands.md#engine-debug-flags).

| Category | Flags |
| --- | --- |
| `logging` | `allegro_sdl`, `db_state`, `idle`, `locale`, `megalink`, `memory` (memory stats per game), `mirror_stderr` (log lines also to stderr, all programs), `network`, `settings`, `sqlite3`, `touchtunes`, `video` |
| `idle` | `CleanExit`, `Freeze`, `IncCredit`, `SegFault`, `no_reboot` (attract-loop test hooks) |
| `WatchDog` | `Freeze`, `Memory`, `SoftOn`, `Timespans` |
| `opsetup` | `allbuttons` (show every Operator Setup button), `allegro_volume`, `postdeltaboot` |
| `system` | `allowkeyboard`, `dev_debug_enabled` |
| `region` | `africa`, `asia`, `europe`, `middle_east`, `north_america`, `oceania` |
| `rowe` | `FakeWallette`, `mp3DownloadBuffer`, `mp3DownloadFatalCount`, `mp3DownloadRetry`, `mp3DownloadTimeout` |
| `diagnostics` | `Disabled`, `keydatadisplay` |
| `meritthread` / `meritthreads` | `calls`, `state` / `details` |
| others | `lightshow/debugls`, `tournamaxx/tradeshow`, `games/playgames.xml`, `memory/nosync`, `priority_play/show_logging`, `Wedge/WideScreen`, `db_state/no_ipc`, `files/resource_locator`, `LibData/TrackQuery`, `translations/untranslated` |

Names come from the code. What each flag does, beyond the obvious, wasn't tested.

**Logging** (`liblogging.so`):

| File | Content |
| --- | --- |
| `/var/merit/logging/logs/<start time>.<program>.running.log` | one log per program and run; fields separated by `\|`, records by `\x03` |
| `/var/merit/logging/summaries/` | per-run summaries |
| `/var/merit/logging/crashes/` | crash dumps (sent to MegaNet on connect) |
| `/var/merit/log` | the boot log `.xinitrc` appends to |

The run's start time is shared by all programs through `/var/merit/log.start`. Each line carries
the current game ID (`Logger::SetGameID`).

## Open questions

- **NVRAM:** the purpose of a few fields (0x0BC, 0x0C0–0x0D7, 0xB85;
  [cabinet state](cabinet-state.md#nvram-nvramdat)).
- **Light show:** profile names, the brightness range, and what sequence 1 is for.
- **Printer:** the country 0x0B printout layout (`NewSendAllData`) in detail.
- **MegaNet:** a plaintext capture of a full session, to confirm field spellings and table order
  ([meganet](meganet.md#7-open-questions)).
- **MegaLink:** each game's own packet types, and links of more than two cabinets
  ([megalink](megalink.md#open-questions)).
- **Lockout:** which lockout flag is coins and which is bills, confirmed on hardware.
