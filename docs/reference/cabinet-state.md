# Where the cabinet keeps its state

Everything a Megatouch ION remembers lives under `/var/merit` (here
`build/loader/var/merit`): books, credits, high scores, the menu, prices, options, players,
tournaments, network settings. It is split over four kinds of storage, from oldest to newest.

| Storage | Files | Format | Read with |
| --- | --- | --- | --- |
| [NVRAM image](#nvram-nvramdat) | `nvram.dat` | 6276-byte binary struct with a checksum | `scripts/loader-option.sh` (options); the map below |
| [Databases](#databases) | `tournamaxx/database/*.db`, `mymerit.db` | encrypted SQLite 2.8 and SQLite 3 | `scripts/loader.sh run /opt/fakeio/dbdump` |
| [Settings files](#settings-files) | `settings.xml`, `settings/<level>/*.xml`, `db_state.xml` | XML; two files encrypted | any text editor (cabinet stopped) |
| Board EEPROM, keys | `fakeio/eeprom.bin`, `fakeio/key.bin`, `fakeio/login/` | the I/O board's EEPROM and iButtons | [io-board](io-board.md), `scripts/loader-key.sh` |

Back up all of it with `make loader-backup` (logs left out). The loader rewrites most of these
files while it runs: change them only while it is stopped, and take a backup first.

## NVRAM (`nvram.dat`)

The old battery-backed RAM of earlier Megatouch models, kept as a file: the loader's global
`NVRAMData` (0x1884 bytes) is read at start (`NVRead`) and written back whole (`NVWrite`,
`PutNVRAM`). Most newer state moved to the databases; NVRAM keeps the licence identity, the
options and a few counters.

The map below comes from the loader's code (every function that touches `NVRAMData`, from a
full decompile of `start`) and from runs. Offsets are in hex; *u32* values are little-endian.

| Offset | Size | Content | Used by |
| --- | --- | --- | --- |
| 0x000 | 1 | 0 = NVRAM empty/invalid: the loader initialises it at boot ("database maintenance") | `InitVars` |
| 0x001 | 8 | security-key part number, e.g. `SA362801` (must match the key, or NVRAM is wiped) | `KeyManager::HasChanged`, `RequestLogin` (MegaNet `KeyPartNumber`) |
| 0x009 | 1 | set to 0x12 when NVRAM is initialised (layout version, *inferred*) | `InitVars` |
| 0x038 | 4 | security-key revision, e.g. `R00` | `KeyManager::HasChanged`, `RequestLogin` |
| 0x03C | 4 | a Six Star (operator six-star menu) value, compared when the menu checks Six Star | `MainMenuScreen::CheckSixStar`, `SixStarSettings` from MegaNet |
| 0x040 | 120 | the 120 game options, one byte each, 0/1 ([options](options.md)); `NVRAMMap::GamesOpt` / `SetGameOption` | everywhere |
| 0x0B8 | 4 | adult-games time window: bits 0–4 start hour, bits 5–9 end hour | `CheckSexTimer`, `EroticSettings` |
| 0x0BC | 4 | set by `InitVars` (purpose not traced) | `InitVars` |
| 0x0C0 | 4 | kept across an NVRAM reset | `PreserveImportantVars` |
| 0x0C4 | 1 | kept across a reset | `PreserveImportantVars` |
| 0x0C5 | 9 | anti-theft PIN (text), kept across a reset | `MenuClass::TheftPINClick` |
| 0x0D0, 0x0D4 | 4 + 4 | kept across a reset | `PreserveImportantVars` |
| 0x0D8 | 17 × 40 × 4 | **menu layout**: per category (stride 0xA0) 40 slots holding a game ID (u32, 255 = empty) | `Menu_Layout::*`, `MenuLayout` from MegaNet |
| 0xB78 | ≤ 12 | coinless coin-op (rental/time mode) string, copied when it is reset | `CB_CCOReset` |
| 0xB84 | 1 | coinless coin-op running | `StartRentalTimer`, `ContinueControl`, `RefreshCredits` |
| 0xB85 | 1 | read by coin insertion and the price pop-up (meaning not traced) | `InsertCoins`, `PricePopUp` |
| 0xB8A, 0xB8B | 1 + 1 | coinless coin-op time-table index and size | `BuyCCOTimeBlock`, `GetIndexCreditValue` |
| 0xB98 | 4 | coinless coin-op rate | `BuyCCOTimeBlock`, MegaNet settings |
| 0xB9C | 1 | coinless coin-op flags: reset to 0x22 or 0x01, bit 0x40 set by the freeze/reset callback, bit 0x20 tested by the credits display | 20 menu and continue functions |
| 0xBA0 | 4 | coinless coin-op time remaining | `ContinueControl`, `CheckCoinlessOp`, `RefreshCredits` |
| 0xBA4 | 4 | float: money paid into the current time block | `BuyCCOTimeBlock`, `RefreshCredits` |
| 0xBA8 | 4 | coinless coin-op mode (`ResetCCOData` treats 3 specially) | `ResetCCOData`, MegaNet settings |
| 0xBAC | 4 | rental/coinless value checked at start and before continues | `megastart`, `ContinueControl::allowed` |
| 0xBB0 | 17 × 40 × 4 | second menu table: category defaults, initialised to 300 (`MAX_GAMES`: no game) | `InitVars`, `Menu_InitCatDefaults` |
| 0x1650 | 1 | a game is running (set around `ExecuteGameID`; *inferred*: crash recovery) | `ExecuteGameID`, `InitVars` |
| 0x1654 | 1 | as above | `ExecuteGameID` |
| 0x16A0 | 4 | coinless coin-op alarm sound timeout (−1 = off) | `SetCCOAlarmSoundTimeout`, `ResetCCOData` |
| 0x1882 | 2 | checksum: 16-bit sum of bytes 0x001–0x1881 | `NVWrite` |

Unlisted ranges weren't seen used by this build. Bytes 0x187F–0x1881 are only touched by the
checksum.

**Resets:** when the key's part number or revision differs from NVRAM, or NVRAM is invalid,
`InitVars` keeps the preserved fields (0x0C0–0x0D7, option 56), clears the rest (`NVClear`) and
rebuilds defaults: the menu tables and every option from the key (`value & 1`).

## Databases

The newer state is in SQLite databases, encrypted with a codec built into the cabinet's SQLite
libraries:

| Library | Version | How a database is opened |
| --- | --- | --- |
| `/usr/local/lib/libsqlite.so.0` | SQLite 2.8, with a `block_encrypt` codec | `sqlite_open_crypt(path, 0, &err, 2, key)` |
| `/usr/lib/libsqlite3.so.0` | SQLite 3, with a codec (`sqlite3Codec`) | `sqlite3_open`, then `PRAGMA key=<key>` |

`libgendef_db.so` wraps both (`db_common_open_db`). Each database class supplies its key with a
`Key()` method (`books_db::Key()`, `coinin_settings_db::Key()`, `config_db::Key()`, …).
`abstract_db_class::UseSqlite3()` returns true, so new databases are SQLite 3; older ones stay
SQLite 2 until recreated.

**Warning:** when `db_common_open_db` can't open a file, it moves it aside as `NAME.db.0` and
creates an empty database in its place. A tool must never let the cabinet's library open a
database it isn't sure about. `dbdump` opens files directly with the real SQLite functions, and
only reads them.

```bash
scripts/loader.sh run /opt/fakeio/dbdump --schema /var/merit/tournamaxx/database/books.db
scripts/loader.sh run /opt/fakeio/dbdump /var/merit/tournamaxx/database/*.db > dump.sql   # all rows
```

`dbdump` takes the keys from the cabinet's own libraries at run time; no key is part of this
project. Paths are inside the sandbox (`/var/merit` = `build/loader/var/merit`, or
`MEGA_LOADER_VAR`). The cabinet should be stopped, or work on copies.

| Database | Format | Holds |
| --- | --- | --- |
| `books.db` | SQLite 3 | the books: every play (`Plays`: game, mode, players, credits, money, duration, tournament, player, score), plays per menu category, books clears, clock changes, sweepstakes codes, prize-mode payouts |
| `coinin_settings.db` | SQLite 3 | money in: one `Credits` row per coin/bill/credit event (`Type` such as `COINDROP_CREDITS`, credits, money, time, device) |
| `config.db` | SQLite 3 | high scores, the MegaNet menu entries, the menu layout (`MenuLayout`: category, position, game, active), Merithon rounds |
| `connections.db` | SQLite 3 | the connection log: every MegaNet/TournaMAXX connection, its phases and result (`CE_*` codes, [meganet](meganet.md)) |
| `tournamaxx.db` | SQLite 3 | TournaMAXX: tournaments, players, scores, totals, groups, prize pools and prizes, payouts, locations |
| `player_credit_vault.db` | SQLite 3 | credits stored on player cards (card reader) |
| `remote_alerts.db` | SQLite 3 | remote diagnostics alert rules and their history |
| `ad_data.db` | SQLite 2 | ad play data |
| `mymerit.db` | SQLite 2 | My Merit player keys: players (name, PIN, free games), their high scores, levels and plays |

Every database also has `sqlite_info` (SQLite 2) or `merit_info` (SQLite 3): `Key`, `iValue`,
`sValue` bookkeeping such as `IK_Last_Open_Time`.

**Values:** enumerated columns (declared without a type: `GameId`, `Type`, `Status`, `Phase`,
`CategoryID`) hold the enum's tag name as text (`G_MINIGOLF`, `COINDROP_CREDITS`,
`CAT_TOURNAMENT`, `CE_NO_ERROR`; [games](games.md) lists the game tags). `TIME` columns are Unix
seconds. The MegaNet exchange uses the same table and field names ([meganet](meganet.md)).

### Schemas

As found on this image (generated with `dbdump --schema`):

```sql
-- /var/merit/tournamaxx/database/ad_data.db (SQLite 2, 4 schema entries)
CREATE TABLE AdData ( TypeId  , Value TEXT , Info TEXT , Timestamp TIME);
CREATE TABLE sqlite_info ( Key  , iValue INT4 , sValue VARCHAR(128));
CREATE UNIQUE INDEX AdData_Time ON AdData ( TypeId,Value,Info,Timestamp );
CREATE UNIQUE INDEX sqlite_info_Key ON sqlite_info ( Key );

-- /var/merit/tournamaxx/database/books.db (SQLite 3, 15 schema entries)
CREATE TABLE Categories ( GameId  , Category INT4 , Plays INT4 , Timestamp TIME);
CREATE TABLE ClearBooks ( Span INT4 , Timestamp INT4);
CREATE TABLE PlayerSweepCodes ( PlayerId INT4 , Code VARCHAR(13) , Timestamp TIME);
CREATE TABLE Plays ( GameId  , GameType  , PlayMode INT4 , NumberOfPlayers INT4 , NumberOfCredits INT4 , NumberOfPlays INT4 , Money FLOAT , Duration INT4 , TournId INT4 , PlayerId INT4 , Score INT4 , Timestamp TIME);
CREATE TABLE PrizeModePayouts ( GameId  , Threshold INT4 , Timestamp TIME);
CREATE TABLE TimeChange ( Before TIME , After TIME);
CREATE TABLE merit_info ( Key  , iValue INT4 , sValue VARCHAR(128));
CREATE INDEX Categories_CategoryTime ON Categories ( Timestamp );
CREATE UNIQUE INDEX Categories_PrimaryKey ON Categories ( GameId,Timestamp,Category );
CREATE INDEX ClearBooks_ClearTime ON ClearBooks ( Timestamp );
CREATE INDEX PlayerSweepCodes_PlayerId ON PlayerSweepCodes ( PlayerId );
CREATE INDEX Plays_PlaysTime ON Plays ( GameId,Timestamp );
CREATE UNIQUE INDEX Plays_PrimaryKey ON Plays ( GameId,Timestamp,GameType,PlayMode,NumberOfPlayers,TournId,PlayerId );
CREATE UNIQUE INDEX PrizeModePayouts_PlaysTime ON PrizeModePayouts ( GameId,Timestamp );
CREATE UNIQUE INDEX merit_info_Key ON merit_info ( Key );

-- /var/merit/tournamaxx/database/coinin_settings.db (SQLite 3, 4 schema entries)
CREATE TABLE Credits ( Type  , NumberOfCredits INT4 , Money FLOAT , Timestamp TIME , DeviceId TEXT);
CREATE TABLE merit_info ( Key  , iValue INT4 , sValue VARCHAR(128));
CREATE UNIQUE INDEX Credits_PrimaryKey ON Credits ( Type,Timestamp );
CREATE UNIQUE INDEX merit_info_Key ON merit_info ( Key );

-- /var/merit/tournamaxx/database/config.db (SQLite 3, 15 schema entries)
CREATE TABLE HighScores ( GameId  , Category INT4 , Name TEXT , Score INT4 , Timestamp TIME);
CREATE TABLE MegaNet ( GameId  , Enabled VARCHAR , Rate INT4 , StartURL VARCHAR(201) , IconTitle VARCHAR(31));
CREATE TABLE MenuLayout ( CategoryID  , MenuPosition INT4 , GameID  , Active INT4 , Timestamp INT4);
CREATE TABLE Merithon ( TournId INT4 , Round INT4 , GameId  , TargetScore INT4 , MasterGameId );
CREATE TABLE merit_info ( Key  , iValue INT4 , sValue VARCHAR(128));
CREATE INDEX HighScores_Game ON HighScores ( GameId );
CREATE INDEX HighScores_GameCategory ON HighScores ( GameId,Category );
CREATE INDEX HighScores_GameCategoryTime ON HighScores ( GameId,Category,Timestamp );
CREATE UNIQUE INDEX MegaNet_GameId ON MegaNet ( GameId );
CREATE UNIQUE INDEX MenuLayout_CategoryPosition ON MenuLayout ( CategoryID,MenuPosition );
CREATE UNIQUE INDEX Merithon_Id ON Merithon ( MasterGameId,TournId,Round );
CREATE INDEX Merithon_MasterGameId ON Merithon ( MasterGameId );
CREATE INDEX Merithon_TournGames ON Merithon ( TournId,MasterGameId );
CREATE INDEX Merithon_TournId ON Merithon ( TournId );
CREATE UNIQUE INDEX merit_info_Key ON merit_info ( Key );

-- /var/merit/tournamaxx/database/connections.db (SQLite 3, 4 schema entries)
CREATE TABLE Connections ( Type  , Phase  , Reason  , StartTime TIME , EndTime TIME , Retry INT4 , Priority INT4 , Status  , PhaseErrorCode INT4 , PhaseResultString VARCHAR(50));
CREATE TABLE merit_info ( Key  , iValue INT4 , sValue VARCHAR(128));
CREATE UNIQUE INDEX Connections_Entry ON Connections ( Phase,StartTime );
CREATE UNIQUE INDEX merit_info_Key ON merit_info ( Key );

-- /var/merit/mymerit.db (SQLite 2, 11 schema entries)
CREATE TABLE HighScores ( Id INT4 , GameId  , Date TIME , Score INT4);
CREATE TABLE Level ( Id INT4 , GameId  , Level INT1);
CREATE TABLE Players ( Id INT4 , Name TEXT , PIN VARCHAR(5) , LastEntry INT4 , FreeGames INT4 , CreationDate TIME);
CREATE TABLE Plays ( Id INT4 , GameId  , Date TIME , Count INT4);
CREATE TABLE sqlite_info ( Key  , iValue INT4 , sValue VARCHAR(128));
CREATE UNIQUE INDEX HighScores_IdGame ON HighScores ( Id,GameId );
CREATE UNIQUE INDEX Level_IdGame ON Level ( Id,GameId );
CREATE UNIQUE INDEX Players_Id ON Players ( Id );
CREATE UNIQUE INDEX Players_IdNamePIN ON Players ( Id,Name,PIN );
CREATE  INDEX Players_NamePIN ON Players ( Name,PIN );
CREATE UNIQUE INDEX sqlite_info_Key ON sqlite_info ( Key );

-- /var/merit/tournamaxx/database/player_credit_vault.db (SQLite 3, 5 schema entries)
CREATE TABLE PlayerCreditVault ( CardData VARCHAR , Name VARCHAR , Credits INT4 , FixedPointCollectedMoneyInMachine INT4 , FixedPointOldMoneyInMachine INT4 , FixedPointNewMoneyInMachine INT4 , Expiration TIME , LastLogin TIME , IsOperatorCard VARCHAR , ModificationTime TIME);
CREATE TABLE merit_info ( Key  , iValue INT4 , sValue VARCHAR(128));
CREATE UNIQUE INDEX PlayerCreditVault_CardData ON PlayerCreditVault ( CardData );
CREATE INDEX PlayerCreditVault_IsOperatorCard ON PlayerCreditVault ( IsOperatorCard );
CREATE UNIQUE INDEX merit_info_Key ON merit_info ( Key );

-- /var/merit/tournamaxx/database/remote_alerts.db (SQLite 3, 6 schema entries)
CREATE TABLE History ( Timestamp INT4 , Message VARCHAR(201) , Data VARCHAR(1001));
CREATE TABLE RemoteAlerts ( Type VARCHAR , Message VARCHAR(201) , Filter VARCHAR(201) , SystemName VARCHAR(201) , Description VARCHAR(201) , Units VARCHAR(201) , Enabled VARCHAR , LowAlertEnable VARCHAR , LowAlertLevel FLOAT , HighAlertEnable VARCHAR , HighAlertLevel FLOAT , HistoryEnable VARCHAR , HistoryDelta FLOAT , HistoryLast FLOAT , HistoryDays INT2);
CREATE TABLE merit_info ( Key  , iValue INT4 , sValue VARCHAR(128));
CREATE INDEX History_MessageHistory ON History ( Message );
CREATE UNIQUE INDEX RemoteAlerts_MessageTypeName ON RemoteAlerts ( Message,Type,SystemName );
CREATE UNIQUE INDEX merit_info_Key ON merit_info ( Key );

-- /var/merit/tournamaxx/database/tournamaxx.db (SQLite 3, 44 schema entries)
CREATE TRIGGER Players_timestamp_insert after INSERT on Players begin update Players set Updated=strftime( '%s', 'now' ) where _ROWID_=new._ROWID_; end;
CREATE TRIGGER Players_timestamp_update after UPDATE on Players begin update Players set Updated=strftime( '%s', 'now' ) where _ROWID_=new._ROWID_; end;
CREATE TRIGGER Tournaments_timestamp_insert after INSERT on Tournaments begin update Tournaments set Updated=strftime( '%s', 'now' ) where _ROWID_=new._ROWID_; end;
CREATE TRIGGER Tournaments_timestamp_update after UPDATE on Tournaments begin update Tournaments set Updated=strftime( '%s', 'now' ) where _ROWID_=new._ROWID_; end;
CREATE TABLE Groups ( TournId INT4 , GroupId INT4 , TournLevel INT4 , Name VARCHAR(64) , Prize VARCHAR(64) , Promote INT4 , LocationInRanks VARCHAR);
CREATE TABLE Locations ( Id INT4 , Name VARCHAR(64) , City VARCHAR(30) , State VARCHAR(35) , Country VARCHAR(30) , Phone VARCHAR(20) , Active VARCHAR);
CREATE TABLE Payouts ( TournId INT4 , TournLevel INT1 , TournRank INT4 , PercentOfPool INT4 , PayedPlayerId INT4 , PayedDate TIME , PayedAmount FLOAT , PayedServer INT4);
CREATE TABLE Players ( Id INT4 , UnicodeHandle TEXT , PIN VARCHAR(5) , LocationId INT4 , FirstName VARCHAR(20) , LastName VARCHAR(20) , Address VARCHAR(30) , UnicodeCity TEXT , UnicodeState TEXT , PostalCode VARCHAR(16) , Country VARCHAR(30) , Phone VARCHAR(20) , Birthday VARCHAR(12) , DateAdded TIME , Gender VARCHAR(2) , Email VARCHAR(50) , Updated TIMESTAMP);
CREATE TABLE PositionPrizes ( TournId INT4 , TournLevel INT4 , TournRank INT4 , Prize VARCHAR(64));
CREATE TABLE PrizePools ( TournId INT4 , TournLevel INT1 , SeedCredits INT4 , PoolCredits INT4 , PrizeRate INT4 , PayoutPIN INT4 , NumScoresRequired INT4);
CREATE TABLE ScoreTotals ( TournId INT4 , GroupId INT4 , TournLevel INT4 , PlayerId INT4 , Score INT4 , Rank INT4);
CREATE TABLE Scores ( TournId INT4 , GroupId INT4 , TournLevel INT4 , PlayerId INT4 , Score INT4 , Time TIME);
CREATE TABLE Tournaments ( TournId INT4 , GameId  , Name VARCHAR(50) , Description VARCHAR(750) , Status  , StartDate TIME , EndDate TIME , ShowScoresDate TIME , CreditsPerPlay INT4 , GameOptions INT4 , RandomSeed INT4 , SeedIncrement INT4 , ScoresToKeep INT4 , AllowContinues INT4 , Updated TIMESTAMP , StartLevel INT4);
CREATE TABLE merit_info ( Key  , iValue INT4 , sValue VARCHAR(128));
CREATE UNIQUE INDEX Groups_IdLevel ON Groups ( TournId,TournLevel );
CREATE INDEX Groups_Tourn ON Groups ( TournId );
CREATE UNIQUE INDEX Locations_Id ON Locations ( Id );
CREATE INDEX Payouts_TournId ON Payouts ( TournId );
CREATE INDEX Payouts_TournLevel ON Payouts ( TournId,TournLevel );
CREATE UNIQUE INDEX Payouts_TournLevelRank ON Payouts ( TournId,TournLevel,TournRank );
CREATE INDEX Players_Handle ON Players ( UnicodeHandle );
CREATE UNIQUE INDEX Players_HandlePIN ON Players ( UnicodeHandle,PIN );
CREATE UNIQUE INDEX Players_Id ON Players ( Id );
CREATE UNIQUE INDEX PositionPrizes_TournLevelRank ON PositionPrizes ( TournId,TournLevel,TournRank );
CREATE INDEX PrizePools_TournId ON PrizePools ( TournId );
CREATE UNIQUE INDEX PrizePools_TournIdLevel ON PrizePools ( TournId,TournLevel );
CREATE INDEX ScoreTotals_PlayerId ON ScoreTotals ( PlayerId );
CREATE INDEX ScoreTotals_Rank ON ScoreTotals ( Rank );
CREATE UNIQUE INDEX ScoreTotals_TPL ON ScoreTotals ( TournId,PlayerId,TournLevel );
CREATE INDEX ScoreTotals_TRL ON ScoreTotals ( TournId,Rank,TournLevel );
CREATE INDEX ScoreTotals_TournGroup ON ScoreTotals ( TournId,GroupId );
CREATE INDEX ScoreTotals_TournId ON ScoreTotals ( TournId );
CREATE INDEX ScoreTotals_TournLevel ON ScoreTotals ( TournId,TournLevel );
CREATE INDEX ScoreTotals_TournPlayer ON ScoreTotals ( TournId,PlayerId );
CREATE UNIQUE INDEX Scores_All ON Scores ( TournId,TournLevel,PlayerId,Score,Time );
CREATE INDEX Scores_PlayerId ON Scores ( PlayerId );
CREATE INDEX Scores_TPG ON Scores ( TournId,PlayerId,GroupId );
CREATE INDEX Scores_TPL ON Scores ( TournId,PlayerId,TournLevel );
CREATE INDEX Scores_TournId ON Scores ( TournId );
CREATE INDEX Tournaments_GameId ON Tournaments ( GameId );
CREATE UNIQUE INDEX Tournaments_Id ON Tournaments ( TournId );
CREATE INDEX Tournaments_IdStatus ON Tournaments ( GameId,Status );
CREATE INDEX Tournaments_Status ON Tournaments ( Status );
CREATE UNIQUE INDEX merit_info_Key ON merit_info ( Key );
```

## Settings files

| File | Content |
| --- | --- |
| `settings.xml` | `Initialised`, `OperatorKeyEntry` ×4 (registered operator keys), `UAEPrizeMode`, `BWOperatorInfo`, and one `GameInfo` per game (264 here): enabled, price, menu flags (`SettingsTable`) |
| `db_state.xml` | the state of every database (`DbStateMap`), kept by the `db_state` daemon |
| `settings/never/*.xml` | settings by subsystem: `money.xml` (pricing scheme, coin values), `credits.xml` (free credits, meter pulses), `volume.xml`, `language.xml`, `joystick.xml`, `meganet.xml`, `promocredits.xml`, `meritmoney.xml`, `minidrucker_stats.xml` (books printer counts), `championship_edition.xml`, `cash_settlement.xml`, `idles.xml` and `idlesequence.xml` (attract loop), `key_settings.xml`, and per-game files (random seeds, levels, content packs) |
| `settings/twobutton/network_config.xml` | network settings: method, addresses, MegaNet ID and server, MegaLink ID; **encrypted** (tag names too); read and write it through `libnetwork.so` (`netcfg`) |
| `settings/twobutton/rowelink_settings.xml` | Rowe jukebox link settings; encrypted |
| `settings/twobutton/tournamaxx_p.xml`, `rowelink_reports.xml` | TournaMAXX player-ID counter, Rowe reports |
| `settings/install/`, `settings/restore/` | empty on this image |

The folder is the file's reset level (`SettingsManager::Clear(path, level)`; *inferred* from the
names): `never` survives every reset, `twobutton` is cleared by the two-button settings reset,
`install` by a software installation. Each file has a `.bak.xml` copy written before changes.

