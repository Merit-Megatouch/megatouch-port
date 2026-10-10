# MegaLink: the wire protocol

MegaLink is how Megatouch cabinets in one venue play each other ("Wired Game-to-Game"). This is
what the 2014 ION loader (`start`, build 2021, program `PG3002-01 V40.02`) sends. It's taken
from the code (Ghidra) and confirmed by a capture of a complete linked game of **11 Up** between
two cabinets on one megalan (2026-10-10).

**Status of the claims:**

- **confirmed:** seen in the capture.
- **code:** read in the decompiled loader, not seen on the wire.

All multi-byte fields are little-endian (x86). Byte 0 of every packet is the destination
cabinet ID, byte 1 the source ID.

## Overview

| Layer | Transport | Port | Function in `start` |
| --- | --- | --- | --- |
| Discovery ("who is linked") | UDP broadcast to the subnet broadcast address | **4700** (0x125C) | `CSystemSocket` (thread `MegaLinkPoll::ML_System`) |
| Challenges (invite to a game, end of session) | UDP broadcast | **4703** (0x125F) | `MegaLinkPoll::RequestLink` / `RequestEnd`, received by thread `ML_Challenge` |
| The linked game | TCP, one connection per direction per peer | **4704** (0x1260) | `NetGlobals` (`heartbeat.cpp`), `MachineInfo` (`netglob_acc.cpp`), `CConnections` |

**Cabinet ID = last octet of the cabinet's IPv4 address** (`MegaLinkPoll::IfUp`:
`SetMyId(ip >> 24)`). The "MegaLink ID" in the network settings is not what is used here.
Peers are reached at *(own /24 prefix).ID*, so all linked cabinets must share one /24 subnet
and have distinct last octets (`CSystemSocket::Check_TCP_Link_Info`, `MachineInfo::TCP_SendFxn`:
`connect(own_ip & 0x00FFFFFF | id << 24, 4704)`) **(code; confirmed: 10.0.2.16 ↔ 10.0.2.17)**.

**Requirements:**

- **Option:** MegaLink runs only when game option 52 `LINKED_GAMES_ENABLED` is on and option 2
  `ENABLE_RENTAL_MODE` is off (`MegaLinkPoll::MegaLinkAllowed`).
- **Interface:** the threads start once the network interface has an IPv4 address
  (`MegaLinkPoll::IfUp` → `InitThreads`, which starts `ML_System`, `ML_Challenge` and
  `TT_Link_Ping`; the last one is a TournaMAXX ping, not MegaLink).

**Link revision:** `LINK_REVISION` is the major version read from
`/usr/local/gamedata/config/version.dat` with the format on its second line (`PG3002-01
V%d.%d`), so **40** for V40.02 (`InitNetwork`). It is in every UDP packet. A discovery packet
with another revision from another ID increments `BadRevFlag`, which leads to the "revision
mismatch" screen (`FatalRevNumMismatch`). Challenge packets with another revision are ignored.
Only cabinets on the same major version can link.

## Common header

Every MegaLink packet, UDP or TCP, starts with this 8-byte header (`DummyPacket`, see
`PrintPacket`, which prints `Pid`, `Dest`, `Src`, `Size`, type and subtype):

| Offset | Size | Field |
| --- | --- | --- |
| 0 | 1 | destination ID; `0xFF` = everyone |
| 1 | 1 | source ID |
| 2 | 1 | type |
| 3 | 1 | subtype |
| 4 | 2 | total length of the packet, header included |
| 6 | 2 | UDP: magic `0xF00F` (bytes `0F F0`). TCP: per-peer sequence number ("Pid") |

The UDP packets use bytes 2–3 differently (below): discovery leaves them 0 and keeps its
message type at offset 8; challenges keep it in byte 3.

## Discovery: UDP 4700 (`CSystemSocket`)

Every cabinet binds UDP 4700 on its address (`SO_REUSEADDR`, `SO_BROADCAST`,
`SO_BINDTODEVICE` to the network interface) and broadcasts to the subnet broadcast address.

**Packet, 36 bytes (0x24):**

| Offset | Size | Value |
| --- | --- | --- |
| 0 | 1 | `FF` |
| 1 | 1 | sender ID |
| 2 | 2 | `00 00` |
| 4 | 2 | `24 00` (36) |
| 6 | 2 | `0F F0` |
| 8 | 2 | message: `21 00` = **ID request**, `22 00` = **ID response** |
| 10 | 2 | unused (stack garbage) |
| 12 | 4 | `LINK_REVISION` (`28 00 00 00` = 40) |
| 16 | 4 | sender's `SystemTimer()` (ms) at send time; also kept as `m_my_send_time` |
| 20 | 16 | unused (stack garbage) |

**Captured (confirmed):**

```
10.0.2.16 → 10.0.2.255:4700  ff 10 00 00 24 00 0f f0 21 00 00 00 28 00 00 00 57 7d 00 00 28 02 7c 56 41 e1 33 00 00 …
10.0.2.17 → 10.0.2.255:4700  ff 11 00 00 24 00 0f f0 22 00 03 e0 28 00 00 00 bb 71 00 00 28 02 95 56 50 04 70 de …
```

**Behaviour** (`CSystemSocket::Process`, run by `ML_System` with a 2.5 s select timeout):

- **Request timer:** each cabinet keeps a deadline, initially 10 s after start. When it passes,
  the cabinet forgets its peer table (`Clear_TCP_Link_Info` ages entries out), broadcasts an
  **ID request** and sets the deadline 10 s ahead.
- **Answering a request:** a cabinet that receives a request from another ID records the
  sender as linked (`Check_TCP_Link_Info`). After draining its socket it answers with one
  **ID response** broadcast, then pushes its own deadline 15 s ahead. So while one cabinet keeps
  requesting every 10 s, the others only answer.
- **Recording responses:** a received response is recorded the same way.
- **Own packets:** loop-back copies of the cabinet's own packets are recognised by source ID and
  `send time`.
- **Address conflict:** a packet from another cabinet using this cabinet's own IP logs
  "CSystemSocket::Process - IP Address Conflict!".

**Confirmed:** cabinet .16 requested at exactly 10.0 s intervals (49 times), and .17 answered
each one within 1 ms and never requested.

The peer table holds `(/24 prefix | ID << 24)` per linked cabinet. `GetNumUnitsLinked` counts
it; the menu and the lobby use that count.

## Challenges: UDP 4703 (`MegaLinkPoll`)

**Packet, 28 bytes (0x1C):**

| Offset | Size | Value |
| --- | --- | --- |
| 0 | 1 | `FF` |
| 1 | 1 | sender ID |
| 2 | 1 | `00` |
| 3 | 1 | **type**: `02` = challenge (`RequestLink`), `05` = session end (`RequestEnd`); read as u16 `0x0200` / `0x0500` |
| 4 | 2 | `1C 00` (28) |
| 6 | 2 | `0F F0` |
| 8 | 4 | unused (0 in challenges, garbage in ends) |
| 12 | 4 | `LINK_REVISION` (40) |
| 16 | 1 | game ID (`GameIds`, e.g. `0A` = `G_11UP`); 0 in ends |
| 17 | 1 | the sender's active language (index in its language table) |
| 18 | 2 | unused |
| 20 | 4 | **session ID** = `SaveTime`: the master's `SystemTimer()` when it set up the game (`Link_SyncSeedValue`, `MyLinkInit`). It also seeds the shared random generator (`Randomize(SaveTime)`), so every cabinet deals the same cards |
| 24 | 1 | challenge: the master's chosen face ("head") index in the lobby (`MasterHead`); end: 0 |
| 25 | 3 | unused |

**Captured (confirmed):**

```
challenge  ff 10 00 02 1c 00 0f f0 00 00 00 00 28 00 00 00 0a 00 7e 56 60 76 04 00 00 c4 39 58
end        ff 10 00 05 1c 00 0f f0 44 dd e4 ff 28 00 00 00 00 00 00 00 ee 42 02 00 00 00 00 00
```

**Receiving** (`ML_Challenge`, blocking on the socket):

1. Drop packets with another revision, or from this cabinet's own ID, or while MegaLink isn't
   allowed.
2. **Challenge** for a game this cabinet has, has enabled and is linkable (gamedata
   `<Linkable>`, byte `0x1D1` of the game record, > 0), while no link game is running
   (`NetGlob == 0`): remember the challenger ID, session ID, game ID, the face byte and the time.
   A repeat of the same challenge just refreshes the time.
3. **End** with the remembered challenger and session: mark the challenge closed (game ID
   `0xFF`).
4. `CheckForChallenge` (polled from the attract loop and menus) accepts a challenge for up to
   20 s after it was last refreshed. If the game is installed and has languages, the cabinet
   opens that game's MegaLink lobby as a **slave**: `SetMaster(challenger ID)`.

**Sending:**

- **Challenges** are sent by the master's lobby on every lobby tick while it has fewer TCP
  peers than discovered cabinets, no more than once per 500 ms after a new face joins.
  *Confirmed:* 345 challenges over 50 s (about every 0.14 s) while the lobby was open.
- **Ends** are sent when the master leaves the lobby or the game. *Confirmed:* 3 at a time.

## The linked game: TCP 4704 (`NetGlobals`)

When a cabinet enters a link game's lobby (master or slave), `NetGlobals` starts:

- **Receive thread** (`NetGlobals::TCP_RecvFxn`): listens on TCP 4704 (backlog 120) and
  accepts peers. A peer is identified by the last octet of its source address. Each accepted
  peer gets a `MachineInfo`.
- **One send thread per peer** (`MachineInfo::TCP_SendFxn`, thread name `Send_NNN`): connects
  to *prefix.ID*:4704 (30 s timeout) and writes that peer's queue. If a send fails with
  `ECONNRESET` (104) it reconnects, up to 5 times.

**Each direction is its own TCP connection.** A cabinet writes only on the connection it
opened, and reads only on connections it accepted. *Confirmed:* .17 opened .17:54324 → .16:4704,
then .16 opened .16:40856 → .17:4704 0.3 s later. Each stream carried only its opener's
packets.

**Framing:** packets are concatenated on the stream with no separator; the length at offset 4
delimits them. The receiver peeks up to 448 bytes, needs at least 16, and reads exactly
`length` bytes. Maximum packet size: 448 (0x1C0). *Confirmed:* segments carrying two packets
back-to-back split correctly by the length field, across 702 packets.

**Ordering and duplicates:**

- **Sequence:** byte 6 (u16) is a per-destination sequence number (`IncLastSendPacket`),
  starting at 1.
- **Duplicates:** the receiver drops any game or NetSprite packet whose sequence isn't greater
  than the last one from that sender (`ProcPacket`). There are no acks at this level; TCP gives
  reliability.
- **Destination `FF`:** a broadcast is turned into one copy per peer, each with that peer's ID in
  byte 0.

**Session field:** every TCP packet's bytes 12–15 are set by `NetGlobals::SendPacket` to
`NetGlob+0x40`. *Confirmed:* it held the master's session ID (`0x00047660`, equal to the
challenge's `SaveTime`) on the master's packets at the start of a game. Later it took other
values: the field follows the cabinet's link-game state and isn't checked by receivers.

### Packet types

| Type (byte 2) | Name in `PrintPacket` | Who handles it |
| --- | --- | --- |
| `0A` | `HTBT` (heartbeat) | `NetGlobals::TCP_RecvProc` |
| `0B` | `NSPR` (NetSprite: shared screen objects) | `NetGlobals::ProcPacket` → `NetSprite` objects |
| anything else | game-defined | queued for the game, read by its `GetPackets()` |

### Heartbeat (`0A`), 28 bytes

| Offset | Size | Field |
| --- | --- | --- |
| 0 | 1 | destination (`FF`, expanded per peer) |
| 1 | 1 | source ID |
| 2 | 1 | `0A` |
| 3 | 1 | subtype: `00` PING, `01` AC, `03` **BEAT**, `04` **EXIT**, `05` MAST |
| 4 | 2 | `1C 00` |
| 6 | 2 | sequence |
| 8 | 4 | unused |
| 12 | 4 | session field (above) |
| 16 | 1 | the `GameIds` that was current when the receive thread started (stale: seen as `2A` = Look Out, the attract-loop game, during an 11 Up game) |
| 20 | 4 | the sender's score (`PlrScore`) |
| 24 | 4 | the sender's game state: a counter the game raises as it moves on (rounds, phases); the receiver keeps the highest value seen per peer |

- **Timing:** each `TCP_RecvFxn` loop (500 ms select) ends with a **BEAT** to all peers, and
  every received packet refreshes the sender's "last update" time.
- **Timeouts:** the lobby treats the master as gone when nothing arrives for 7.5 s
  (`Heartbeat_Check(master, 7500)`, "Master Timed Out").
- **Leaving:** sends **EXIT**, which zeroes the peer's last-update time.

*Confirmed:*

- **Counts:** 606 BEATs and 4 EXITs in the capture.
- **State values:** the state climbed 0, 1, 3, 7, 8 … 36 on the slave and 0, 3, 5 … 36 on the
  master over the three rounds.
- **Score:** 0 throughout, since the test hands scored nothing.
- **Sequence:** the PING/AC/MAST subtypes weren't seen; `SendPacket` gives them the fixed
  sequence values `0x0AC0`/`0xBEAF`/`0xDEAD`/`0xF00D`, but the per-peer sequence overwrites
  those on broadcast.

### NetSprite (`0B`): shared lobby and game objects

Lobby buttons and faces (`MegaLinkMenuNameSpace::StartButton`, `AcceptButton`,
`DeclineButton`, `PayButton`, heads) are `NetSprite`s. A click or state change on one cabinet is
mirrored on the others.

| Offset | Size | Field |
| --- | --- | --- |
| 0–7 | | header; type `0B`; subtype `00` NULL (update/click), `01` WAIT (lock request), `02` ACK, `03` NACK |
| 8 | 4 | unused |
| 12 | 4 | session field |
| 16 | 2 | NetSprite ID |
| 18 | 1 | flags: bit 0 = state update (apply the data at once and call the sprite's handler), else queue it as a click for the UI thread; bit 1 = data present; bit 2 = lock protocol (subtypes 1–3) |
| 19 | 1 | unused |
| 20 | n | the sprite's shared data (`n` per sprite; length = 20 + n) |

**Locks:** `NetSpriteLock::SendPacketLock` sends WAIT to the master and waits up to 30 ticks
for ACK or NACK. The master answers ACK, or NACK when the game is full or the face is taken
(`AcceptButton::NetClick`). This is how a slave claims a seat.

*Confirmed* (slave .17 accepting with face 3, master .16 granting):

```
.17 → .16  10 11 0b 01 15 00 26 00 80 b5 f7 57 00 00 00 00 03 00 06 00 01           WAIT sprite 3, flags 06, data 01
.16 → .17  11 10 0b 02 15 00 59 00 00 00 00 00 e9 35 05 00 03 00 06 00 00           ACK  sprite 3
.16 → .17  11 10 0b 00 26 00 5a 00 00 00 00 00 e9 35 05 00 04 00 02 00 00 01 11 00 00 00 00 00 00 00 01 00 00 00 00 00 00 00
           NULL sprite 4 (the Start button), 18 data bytes: the master's slave list (SlaveList, FF-terminated per StartButton::SpriteClick)
```

The other NSPR traffic was 87 NULL updates of sprite 1 (flags `0B`, data
`1a 00 00 00 01 00 00 00`, and `3c 00 …`): the lobby timer and heads.

### Game-defined packets

Packet types below `0A` belong to the game. 11 Up used type `06`:

- **Master:** subtypes `02` and `03`, at each round end.
- **Slave:** `03`.
- **Length:** 24 bytes each.
- **Carrier:** these go through the same `SendPacket` / `GetPackets` API that every link game
  uses.

The classic card games' helper `Card_LinkSlaveSendPacket` builds type `05` subtype `02`
packets for the master, 24 bytes: offset 16 = u32 value, offset 20 = u16 message, offset 22 =
the game-over flag. It skips repeats of the same message within a given interval.

The old file-based transport `DevLink_*` (`/tmp/DevLink.P<n>.F<from>.T<to>` files) is still in
the binary, for development on one machine; the shipping code uses TCP.

## Sequence of a linked game (confirmed, times from the capture)

| t (s) | Event |
| --- | --- |
| 32.5 → | .16 broadcasts ID requests every 10 s, .17 answers each: both count one linked unit |
| 297.3 | Player on .16 picks 11 Up → **MegaLink** → a face. .16 becomes master (`SetMaster(own ID)`) and starts sending challenges (game `0A`, session `0x47660`, face 0) on 4703 |
| ~297–307 | .17 (in its attract loop) accepts the challenge (`CheckForChallenge`) and opens the 11 Up lobby as slave ("Select a face to accept the challenge.") |
| 307.6 | .17 connects to .16:4704 and starts beating; .16 connects back 0.3 s later |
| 308 | NSPR sprite-1 updates both ways (lobby timer and heads) |
| 317.1 | Nobody accepted in time: both send EXIT; .16 sends 3 session-end packets on 4703 |
| 336 → | Second try: new session `0x535E9`, new TCP connections |
| 364.46 | Player on .17 picks a face: WAIT → ACK → master's Start sprite carries the slave list |
| 364.5 | .16 sends 3 session ends (the challenge is closed); the game starts on both with the same deal (shared `SaveTime` seed) |
| 366 → | Each cabinet plays its own board; BEATs carry score and state; type `06` packets at round ends; rounds 2 and 3 start in step |
| end | Both send EXIT, close the connections, and return to the menu; 2 credits charged on each |

## How this was captured

With two cabinets on one PC (`MEGA_LOADER_NET=lan`, see the
[operator guide](../guides/operator-guide.md#17-linked-cabinets-megalink-experimental)), the
cabinet network's switch can record everything it carries: `MEGALAN_PCAP=<file>` makes `megalan`
append every frame (cabinets, router, joined PCs) to a standard pcap file (Ethernet link type),
readable with Wireshark or `tcpdump -r`. Set it for the first cabinet started on the network,
since that one starts the switch:

```bash
MEGALAN_PCAP=/tmp/megalink.pcap MEGA_LOADER_NET=lan:test MEGA_LOADER_VAR=… make loader-run
```

Wireshark shows the UDP and TCP structure. The MegaLink payloads need the tables above (no
dissector exists). The `logging/megalink` debug flag
([cabinet software](cabinet-software.md#debug-flags-and-logging)) adds the loader's own MegaLink
log lines.

The capture above:

- **Cabinets:** two test cabinets, 10.0.2.16 and 10.0.2.17, each with its own identity.
- **Options:** `LINKED_GAMES_ENABLED` = 1 on both, and 4 coins each.
- **Play:** a game of 11 Up started from one cabinet and accepted on the other.

## Open questions

- **Other games' packets:** the meaning of each game's own packet types (only 11 Up's type `06`
  was seen; each game module defines its own). Fast games (Air Hockey, Tennis, Driving) will
  send far more.
- **Heartbeat subtypes:** PING, AC and MAST (`00`, `01`, `05`) weren't observed, and their
  senders weren't traced.
- **NSPR flag bit 3:** set on sprite 1's updates (`0B`); its meaning is unknown.
- **Data layouts:** the NetSprite data of each lobby sprite (timer, heads, pay button)
  beyond the slave list.
- **Bigger links:** behaviour with more than 2 cabinets (up to `<Linkable>` = 8), and the
  "game full" rules.
- **Session field:** how it moves during a game, and whether anything reads it.
- **TT_Link_Ping:** it belongs to TournaMAXX (`TT_Ping`, every 10–60 s), not MegaLink; not
  analysed.

See also: [cabinet software](cabinet-software.md#network-meganet-and-megalink).
