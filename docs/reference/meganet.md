# MegaNet: the cabinet ↔ server exchange

How the ION 2014 loader talks to a MegaNet server, as far as needed to write a compatible
server. **Source:** static analysis of `start` (2021 Keyless build; class `TournamaxxUpdate`,
`update.cpp`), `network_ui` (`connection_base.cpp`), `libgendef_common.so` (record encoding),
`libtournamaxx.so` (table registry) and `libnetwork.so` (result codes, read by calling the
cabinet's own enum functions).

**Confirmation:** a live plaintext capture of this build's exchange hasn't been made for this
write-up. Statements marked
*seen* come from the 2026-10-07 connection to `us.oerinet.net` (the session log and an earlier
look at that traffic). Everything else is from the code. Field names below are the records' own
field names; that the wire uses exactly these spellings is *inferred* (the code matches columns
by the field-definition name, which the generated `Get_<name>` accessors share).

## 1. Hosts and transport

| | |
| --- | --- |
| Server name | *Network → Network Options → MegaNet Server*, stored encrypted in `network_config.xml` (`network::settings::Interface::get_meganet_server_name`) |
| Session URL | `https://tng<server>/go` if the name starts with a letter (`us.oerinet.net` → `https://tngus.oerinet.net/go`), else `https://<server>/go` (an IP address) (`TournamaxxUpdate::SetServerURL`) |
| Registration check | `GET https://tng<server>/check_registration?meganetid=<MegaNet ID>` (`network_ui`) |
| TLS | libcurl + OpenSSL 0.9.8. `CURLOPT_SSL_VERIFYPEER = 0` (any certificate is accepted; `VERIFYHOST = 1` has no effect without peer verification). Old protocol/cipher support only: the server must still offer something OpenSSL 0.9.8 can negotiate (TLS 1.0, SSLv3-era ciphers) |
| Method | `POST`, `multipart/form-data` (curl `formadd`) |
| User-Agent | `merit tmaxx ng; version test1` (session), `merit tmaxx ng; registration checker` (check) |
| Cookies | curl's cookie engine is on (`COOKIEFILE ""`): a cookie set by the server in one response is sent back on the following POSTs of the same session. `TournamaxxUpdate::sSessionCookie` exists; how it is used beyond that was not traced |
| Encoding | `Accept-Encoding: identity, deflate, gzip`; redirects followed |
| Timeouts | connect 30 s, whole transfer 3600 s (registration check: 60 s); DNS cache 600 s |
| Other web pages | `http://www.accessmerit.com/meganet/meganetcore/login1.aspx?…` and `enroll1.aspx?MeganetID=…&CabinetIDType=…&HardwareSerialNumber=…&Language=…` (test server `http://testweb.meritind.com/meganet/meganetcore/`) are opened in the cabinet's browser for MegaNet enrolment; not part of the data exchange. GameTime's `http://www.megatouchgametime.com/update/?mach_id=%s&serial=%s` likewise |

### Registration check

`GET https://tng<server>/check_registration?meganetid=<id>`; the body is ignored.

| HTTP status | Result |
| --- | --- |
| 200 | registered (`CE_NO_ERROR`) |
| 404 | not registered (result 205) |
| anything else | `CE_SERVER_ERROR` (13); transport failures map through `MapLibcurlError` |

*Seen:* `us.oerinet.net` answered 200 for the image's MegaNet ID.

## 2. Session structure

"Connect to MegaNet/Update from Server" runs a state machine (`State<InternetConnectivity>`:
`ConnectState` → `TournamaxxState` → `DisconnectState`, with `RebootState`, `QuitState`,
`ErrorState`). `TournamaxxState` drives `Tournamaxx::UpdateSetup` → repeated
`Tournamaxx::Update` → `UpdateCleanup`. The UI's steps (connection, MegaNet/registration check,
exchange, disconnect) are reported per phase in the connection log; the UI texts include
"Establishing connection….", "Checking registration….", "Connecting to server...",
"Disconnecting". *Inferred:* the four steps shown as "Success" are these phases.

Each `Update()` call is **one HTTP POST and its response**:

1. `UpdateSetup` (once): backs up the local tournament tables (`Scores, ScoreTotals,
   Tournaments, PrizePools, Payouts, PositionPrizes, Groups`); for every connection except
   `NETWORK_UI_INITIAL_CONNECTION` (9) and `NETWORK_UI_TEST_CONNECTION` (11) marks every local
   tournament `Complete` (the server re-sends the active ones); queues a **`Login`** record
   (and every local player with a negative id, i.e. created on the machine).
2. `Update`: performs pending downloads, then `SendData` = POST of everything queued.
3. The response is parsed line by line; handlers apply tables and may queue more records or files
   (answers to `Request*` tables).
4. The caller loops while the result is `CE_TMAXX_EXCHANGE` (59).
5. When the cabinet has nothing queued (no records, no files), it adds a **`Logout`** record
   itself. The session ends when the server's response contains a `Logout` table line
   (`Abort` = 1 → aborted, `CE_MANUAL_EXIT` 39); otherwise result `CE_NO_ERROR` (0), or
   `CE_REBOOT_REQUESTED` (48) when a handler asked for a reboot (file download/delete or
   machine-option change with `Reboot`/`RebootRequired` set).

So a minimal server: answer the first POST (Login) with whatever tables it wants to send plus
`Request*` tables for what it wants back; answer each following POST (the requested data) with
`Logout` when done.

## 3. The request (cabinet → server)

`multipart/form-data` with:

| Part | Content |
| --- | --- |
| `catalog` | text, the queued records (format below) |
| one part per uploaded file | the part **name is the file's full path** on the cabinet, the content is the file (curl `CURLFORM_FILE`, so its filename is the basename). Used for crash dumps (`TournamaxxUpdateI::SendCrashDumps`, from `/var/merit/logging/crashes`) and other `AddFileToUpload` files |

### Catalog text

A sequence of tables. A table starts with a header line and is followed by one line per record:

```
## <Table> <Field1>|<Field2>|…|<FieldN>
<value1>|<value2>|…|<valueN>|
<value1>|<value2>|…|<valueN>|
```

- **Header:** `## `, the table name, a space, and the field names joined by `|` (no trailing
  `|`). Fields whose type is `ignore` are left out.
- **Records** (`abstract_record::PipeDelimited`): every field's text followed by `|` (so a line
  ends with `|`). Inside a value, `|` becomes `~`, newline becomes the two characters `\n`, and
  CR becomes `\r`. Lines end with LF.
- **Encryption:** a value can be Blowfish-encrypted per field when the record is written with a
  key (`gendef_common::EncryptString`); the exchange code calls it without a key.
- **Values:** text forms of the record's fields. Times are Unix seconds (`time_t`). Enum
  fields are their tag names (*seen*: `CAT_TOURNAMENT`, `G_FOURPLAY` in `MenuLayout`).

### The Login record (first POST)

`Login` fields: `MachineId` (MegaNet ID), `MeritSerialNumber` (I/O-board serial,
`USBIO::SerialNumber`), `MajorVersion`, `MinorVersion` (from `GAME_VERS`), `NetworkVersion`
(20), `Time` (now), `LastSuccessfulConnection`, `Platform` (`SystemInfo::GetPlatformType`),
`CabinetIdString`, `CabinetIdType`, `KeyPartNumber`, `KeyId` (security key), `ModemType` (dial-up)
or the network interface, `MACAddress`, `Sequence` (the connection reason, below),
`CreditCardReaderConnected`, `Database`.

Connection reasons (`network::ConnectionReason`): 0 `TMAXX_IDLE_CONNECTION`, 1
`TMAXX_CONNECTION`, 2 `TMAXX_INITIAL_CONNECTION`, 3 `TMAXX_INITIAL_CONNECTION_OWN_ISP`, 8
`TMAXX_POST_GAMETIME_MERITLINK`, 9 `NETWORK_UI_INITIAL_CONNECTION`, 10
`NETWORK_UI_CONNECTION` (the *Connect to MegaNet/Update from Server* button), 11
`NETWORK_UI_TEST_CONNECTION`, 12 `BROWSER_CONNECTION`.

## 4. The response (server → cabinet)

Plain text (gzip/deflate allowed), processed line by line:

| Line | Meaning |
| --- | --- |
| `## LINE_HEADER\|<Table>\|<col>\|<col>…` | starts a table. The first word must be `LINE_HEADER`; columns are matched to the record's fields **by name**, in any order; unknown column names are skipped (logged). An unknown table name: its lines are ignored (*seen*: `XXXMenuLayout`, `XXXGameEnableOverride`, `XXXGameTimeEnable` skipped) |
| `#!<n>` | an error code, handled as an `Errorcode: <n>` header |
| anything else | a record of the current table, `\|`-separated, in header column order; parsing stops at LF |

An HTTP header `Errorcode: <n>` (or a `500 Internal Server Error` status) also ends the exchange
with an error:

| Errorcode | Result (`network::ERROR_CODES`) |
| --- | --- |
| 2601, 2602 | 14 `CE_BAD_SERIAL_NUMBER` |
| 2600 and other values below 2601 (except 2512) | 15 `CE_NO_SERIAL_NUMBER` |
| 2603, 2604 | 33 `CE_VERSION_MISMATCH` |
| 2605 | 16 `CE_INVALID_KEY` |
| 2611 | 44 `CE_OTHER_ERROR` |
| 2650 | 61 `CE_BROADBAND_ONLY` |
| 2701, 2702 | 60 `CE_DATABASE_SCREWUP` |
| 2512, 2999 | 38 `CE_DATABASE_ERROR` |
| 2995 | 46 `CE_SOCKET_ERROR` |
| 2998 | 12 `CE_SERVER_NOT_RUNNING` |
| any other / HTTP 500 | 13 `CE_SERVER_ERROR` |

HTTP 404 → 11 `CE_NO_SERVER_AT_ADDRESS`; other HTTP status above 200 → 13. A transport error →
`MapLibcurlError` (52 `CE_LIBCURL_ERROR` and others). The log line is `RESULT: <name> (<n>)`.

## 5. Tables the cabinet accepts

Handlers registered by `TournamaxxUpdate` (constructor) and, through
`tournamaxx::Register(name, metadata, parse, catalog)`, by libraries. A library table with a
catalog function is also one the cabinet sends back when asked for settings.

### Commands and requests (the server asks, the cabinet answers in its next POST)

| Table | Fields | What the cabinet does |
| --- | --- | --- |
| `RequestSettings` | `Dummy` | sends its settings changed since the last successful connection: `MenuLayout` (rows with `Timestamp` newer), the registered settings tables (`TournamaxxSettings`, `InterfaceSettings`, `DialUpAccount`, `WirelessSettings`, `MoneySettings`, `MeritMoney`, `PlayerCreditVault`, `CashSettlementSettings`, `CashSettlementFeesTimestamped`, `CreditCardSettings`), `EroticSettings`, `SixStarSettings`, `HiScoreSettings`, `MachineOptions` (every game option modified since then: `OptionId`, `Value`; option `0xFE` = volume), `PromoCredits` (7 rows, one per weekday), `MeganetFeatures`, `CoinlessCoinOpSettings`, `OSASettings` |
| `RequestBooks` | `Start` (time; −1 = since last success), `All`, `Plays`, `Credits`, `Clears`, `Categories`, `Connections`, `TimeChanges`, `SweepCodes`, `Ads` (flags) | sends the books database rows since `Start`: `Plays`, `TimeChange`, `Categories`, `Credits`, `Clearbooks`, … |
| `RequestSystemLog` | `Start`, `SendData`, `Filter` | with `SendData`: log lines (`LogFileLine`: `m_Time`, `m_Type`, `m_Level`, `m_Module`, `m_GameId`, `m_Thread`, `m_Message`, …) since `Start`, selected by `Filter` (rule lines, `~` for `\|`; default `-\|ALL\|ALL\|*` `+\|WRN\|ALL\|*` `+\|ALL\|ALL\|*:*:*:MyMerit` `+\|INF\|DEV\|*:*:*:tracking`). Always: uploads the crash dumps since `Start` as file parts |
| `RequestTournaments` | `KnownTournaments` | the cabinet answers with the IDs of the tournaments it has (comma-separated) |
| `RequestPlayerDb` | `StartAt` | sends player records from that ID |
| `RequestLocations` | `StartAt` | sends locations |
| `RequestPayouts` | `Dummy` | sends payouts |
| `RequestAdData` | `Start`, `Limit` | sends ad play data |
| `FileExistsRequest` | `GameDirectory`, `GameFileName` (in), `Result`, `FileSize`, `FileDateStamp`, `FileCRC` (out) | `stat`s `/<GameDirectory>/<GameFileName>`, computes a CRC, and sends the record back filled in |
| `DownloadFileRequest` | `URL`, `GameDirectory`, `GameFileName`, `Reboot`, `AckServer`, `Status` | queued; before the next POST the cabinet downloads `URL` (to `/var/tmp/` first, resuming when a part exists) and moves it to `/<GameDirectory>/<GameFileName>`; with `AckServer` it reports `Status` back; `Reboot` → reboot after the session |
| `DownloadMissingFileRequest` | `URL`, `GameDirectory`, `GameFileName`, `CheckIfExist`, `MissingFile`, `Reboot` | the same, only if the file is missing |
| `DeleteFileRequest` | `GameDirectory`, `GameFileName`, `Reboot`, `AckServer`, `Status` | deletes `/<dir>/<name>` (wildcards allowed, `unlink_wild`); acknowledges |
| `SetTime` | `Time` | sets the cabinet clock |
| `Timeout` | `TimeToWait` | how long the cabinet waits for the server (*inferred*) |
| `Timebomb` | `TimeToLive` | `TimeBomb::SetTimebomb`: the cabinet stops working after this unless it connects again (*inferred*) |
| `Logout` | `Abort` | ends the session (after this response) |

### Settings and data the server sets

| Table | Fields |
| --- | --- |
| `MachineOptions` | `OptionId` (0–119 game option, 0xFE volume), `Value`, `Modified`, `RebootRequired`: sets an NVRAM game option ([options](options.md)) |
| `MenuLayout` | `CategoryID`, `GameID`, `MenuPosition`, `Active`, `Timestamp` (*seen*: `CAT_TOURNAMENT\|0\|G_FOURPLAY`) |
| `GameEnableOverride` | `Id`, `OverrideId`, `Categories`, `Expire`, `KeyRevision`, `RequiresTournamaxx` |
| `GameTimeEnable` | `GamesEnabled`, `Checksum` |
| `AmusementPricing`, `CostToContinue` | `GameId`, `Credits`, `Timestamp` |
| `HiScoreSettings` | `GameId`, `CategoryId`, `Index`, `AutoClear`, `Weeks` |
| `SixStarSettings` | `Enabled`, `Volume`, `HighScore`, `MusicTimer`, `TournaMAXX`, `Calibration`, `NetworkOptions`, `VideoBillboard`, `PIN` |
| `EroticSettings` | `Enabled`, `Level`, `Nudity`, `TimeOn`, `TimeOff` |
| `PromoCredits` | `DayOfWeek`, `Enabled`, `StartHour`, `StartMinute`, `EndHour`, `EndMinute`, `MaxCredits`, `IdleTime` |
| `MeritMoney` | `DayOfWeek`, `Enabled`, `StartHour`, `StartMinute`, `EndHour`, `EndMinute`, `MaxGames`, `PIN` |
| `MeganetFeatures` | `FeatureId`, `Enabled`, `Credits`, `UnitTime` |
| `CoinlessCoinOpSettings` | `Enabled`, `Mode`, `TimeModeRate`, `PIN` |
| `OSASettings` | `ButtonText`, `URL` (the operator website button; *seen*: set to a test text) |
| `BillingInfo` | `Content` |
| `Tournamaxx` | `LocationId`, `MinimumAge`, `LoginRequirements`, `LastPlayerIdFromServer` |
| `LoginRequirements` | `Field`, `Skip` |
| `NewPlayer` | `OldId`, `NewId`, `PIN` (renumbers a player created on the machine) |
| `RoweLinkInfo` | (Rowe jukebox link) |
| settings tables registered by libraries | `TournamaxxSettings` (`call_in_hours`), `InterfaceSettings` (`type`, `ip`, `automatic`, `interface_active`), `DialUpAccount` (`account`, `login`, `password`, `phone_number`), `WirelessSettings` (`ssid`, `mode`, `privacy`, `key`), `MoneySettings` (`Currency`, `Credits`, `Money`, `ChannelValues`, `PricingScheme`, …), `CreditValues`, `Credits`, `PlayerCreditVault`, `CashSettlementSettings`, `CashSettlementFeesTimestamped`, `CreditCardSettings`; each with a `ModificationTime`/`LastChanged` |
| `FileUploadRequest` | `RequestId`, `Filename`, `FileDescription`, `FileSize`, `Status`, `Details` (server asks for a file to be uploaded) |

### TournaMAXX tables

| Table | Fields |
| --- | --- |
| `Tournaments` | `TournId`, `GameId`, `Name`, `Description`, `Status`, `StartDate`, `EndDate`, `ShowScoresDate`, `Updated`, `CreditsPerPlay`, `AllowContinues`, `GameOptions`, `RandomSeed`, `SeedIncrement`, `StartLevel`, `ScoresToKeep` |
| `Players` | `Id`, `PIN`, `FirstName`, `LastName`, `UnicodeHandle`, `Email`, `Phone`, `Address`, `UnicodeCity`, `UnicodeState`, `Country`, `PostalCode`, `Gender`, `Birthday`, `LocationId`, `DateAdded`, `Updated` |
| `Scores` | `TournId`, `TournLevel`, `GroupId`, `PlayerId`, `Score`, `Time` |
| `ScoreTotals` | `TournId`, `TournLevel`, `GroupId`, `PlayerId`, `Score`, `Rank` |
| `Groups` | `GroupId`, `TournId`, `TournLevel`, `Name`, `Prize`, `Promote`, `LocationInRanks` |
| `PrizePools` | `TournId`, `TournLevel`, `PoolCredits`, `SeedCredits`, `PrizeRate`, `NumScoresRequired`, `PayoutPIN` |
| `PositionPrizes` | `TournId`, `TournLevel`, `TournRank`, `Prize` |
| `Payouts` | `TournId`, `TournLevel`, `TournRank`, `PayedPlayerId`, `PayedAmount`, `PayedDate`, `PayedServer`, `PercentOfPool` |
| `Locations` | `Id`, `Name`, `City`, `State`, `Country`, `Phone`, `Active` |
| `Merithon` | `TournId`, `GameId`, `MasterGameId`, `Round`, `TargetScore` |

The cabinet keeps these in its TournaMAXX database (`/var/merit/tournamaxx/database`); the
*Competition!* button lists `Tournaments` rows whose status is active.

## 6. Seen on 2026-10-07 (us.oerinet.net)

- Registration check 200; the session ended `RESULT: NO ERROR`; MegaLink started afterwards.
- The server asked for the system log, books and crash dumps (three dumps were uploaded).
- It sent `DownloadFileRequest`s: an update `.tgz` from a plain-HTTP address → `/var/merit/tournamaxx/updates/update004.tgz`, and a test file → `/var/merit/orizzle_was_here`. The log showed them not carried out; why (URL/directory rules, the `Reboot`/`AckServer` handling) wasn't traced.
- `OSASettings` set the operator website text.
- `Tournaments`: no active tournament for the machine.

## 7. Open questions

- A plaintext capture of a full session with this build, to confirm the exact header spellings,
  the order of tables, cookie use, and the `Login` values. A capture holds the server's data and
  the machine's uploads, so it shouldn't be published as is.
- Which `Errorcode` the server sends for an unregistered machine during the session (vs the 404 of
  the check).
- `Timeout` and `Timebomb` semantics in detail; `CallInDisable` (a record type that exists but
  isn't registered as a handler).
- What `ConnectState` does before the exchange (ping/route checks) and which phase the UI calls
  each "step".
- Why the seen `DownloadFileRequest`s were skipped.

See also: [cabinet software](cabinet-software.md#network-meganet-and-megalink),
[cabinet state](cabinet-state.md) (the databases these tables come from).
