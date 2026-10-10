# Event log reference

`<var>/merit/fakeio/events.jsonl` (main cabinet: `build/loader/var/merit/fakeio/events.jsonl`)
records what the cabinet does with its hardware, plus games starting and ending. One JSON object
per line, appended as things happen. The operator's view is in
[connectors](../guides/connectors.md); this page is the format.

**Writers:**

| Writer | Writes | Where it runs |
| --- | --- | --- |
| The fake I/O board (`src/fakeio/fakeio.c`) | everything hardware | inside the cabinet's `start` process, as its libusb |
| `gameevents.so` (`src/fakeio/gameevents.c`) | `game` and `screen` | preloaded into the cabinet's programs |

**Files:**

- **Writes:** both writers append with `O_APPEND`, one `write` per line, so lines never interleave.
- **Rotation:** at board start-up, a log over 5 MB is renamed to `events.jsonl.1` (one
  generation).
- **Readers:** follow the file by name, not by an open handle (`scripts/hwbridge.py` does).

## Common fields

| Field | Type | Meaning |
| --- | --- | --- |
| `t` | number | Unix time in seconds, millisecond precision |
| `type` | string | event type, below |

Unknown fields must be ignored: later versions may add some.

## Types

### `board`

The cabinet opened the I/O board (loader start-up; once per run).

| Field | |
| --- | --- |
| `state` | `"ready"` |
| `lightshow` | `true` if the board reports the light-show kit (`MEGAIO_LIGHTSHOW=1`) |

### `coin`

Pulses the cabinet received on a coin/bill input, reported at the I/O poll that delivered them.

| Field | |
| --- | --- |
| `channel` | 1–8 (status bytes 0–7) |
| `pulses` | 1–127 |

What a pulse is worth (coins, bills, credits) is the cabinet's *Credits/Pricing* setting; the
board doesn't know.

### `meter`

The cabinet pulsed an electromechanical meter (output byte 1 or 2 of an I/O poll).

| Field | |
| --- | --- |
| `meter` | `"coin"` (output byte 1) or `"tournamaxx"` (output byte 2) |
| `pulses` | pulses in this poll |
| `total` | pulses counted since the board's control file was created (`megaio status` shows the same) |

### `lockout`

The lockout flags of the I/O poll changed (not reported for the first poll).

| Field | |
| --- | --- |
| `flags` | raw poll flags |
| `coins_locked` | bit 0: coin lockout on |
| `bills_locked` | bit 1: bill lockout on |

The two flags come from two lockout globals in the loader. *Diagnostics → I/O Test → Coin Lockout*
sets both. Which one drives coins and which bills is inferred from their use, not confirmed on
hardware.

### `outputs`

Output byte 0 or 3–5 changed. The loader always sends zeros there, so this would be news; meter
bytes 1–2 are reported as `meter` instead.

| Field | |
| --- | --- |
| `bytes` | the six output bytes |

### `button`

| Field | |
| --- | --- |
| `button` | `"setup"` (status byte 9 bit 0) or `"calibrate"` (bit 1) |
| `pressed` | `true` / `false` |

### `key`

A key touched or left the front reader (operator fob or My Merit player key).

| Field | |
| --- | --- |
| `state` | `"on"` / `"off"` |
| `kind` | `"operator"` or `"player"` (on only): how the board provisions an unknown key's record |
| `id` | the 8-byte ROM ID, 16 hex digits, family byte first (on only) |

### `lights`

The cabinet sent the light-show kit a packet (`LightShowManager`, see
[I/O board → Light show](io-board.md#light-show)). Only with `MEGAIO_LIGHTSHOW=1`.

| `cmd` | Fields | Packet |
| --- | --- | --- |
| `"play"` | `sequence` (0–5), `repeat` | 0x01 once / 0x02 repeating |
| `"stop"` | – | 0x00 |
| `"set"` | `values`: `[r, g, b, x]` | 0x0B `SetLights(r, g, b, x)` |
| `"active"` | `on` | 0x0F `SetActive` |
| `"profile"` | `profile` | 0x11 `SetProfile` |
| `"brightness"` | `value` | 0x15 `SetBrightness` |

Queries (active? 0x10, profile? 0x12, brightness? 0x16) are answered by the board and not
logged.

| `sequence` | Where the loader plays it |
| --- | --- |
| 0 | the attract loop (`idle.so`) |
| 2 | the special high-score animation (`PlaySpecialFlc`) when a score table still has an empty (0) slot |
| 3 | the same animation otherwise |
| 4 | high-score entry (`HiScore`) |
| 5 | `System_Init` (power-on, repeating) and coins in |
| 1 | not seen |

### `printer` and `print`

| Event | Fields | |
| --- | --- | --- |
| `printer` | `plugged` | the books printer was plugged in or out (status byte 9 bit 2) |
| `print` | `file` | a printout ended (0x16 received); `file` is relative to the board's directory: `printouts/books-YYYYMMDD-HHMMSS.txt` |

### `game` and `screen`

From `ExecuteGameID` in the loader, which runs every game and every one of the cabinet's own
full-screen programs ([cabinet software → Running games](cabinet-software.md#running-games)).

| Field | |
| --- | --- |
| `state` | `"start"` / `"end"` |
| `id` | `GameIds` value: 0–299 games (`type: "game"`); 10000–10063 the cabinet's own screens (`type: "screen"`) |
| `tag` | its `GameIds` name, e.g. `G_HIDDEN_OBJECT_3`, `IDLE`, `OPSETUP` |

The `end` belongs to the most recent `start` that has no `end`. A game can start another, so
starts and ends nest.

## Example

A coin, the menu, a game of Photo Hunt Expedition III, and the high-score lights:

```json
{"t":1791651731.726,"type":"screen","state":"start","id":10002,"tag":"IDLE"}
{"t":1791651731.884,"type":"lights","cmd":"play","sequence":0,"repeat":true}
{"t":1791651745.176,"type":"coin","channel":1,"pulses":2}
{"t":1791651745.190,"type":"lights","cmd":"play","sequence":5,"repeat":true}
{"t":1791651745.200,"type":"meter","meter":"coin","pulses":2,"total":208}
{"t":1791651745.408,"type":"screen","state":"end","id":10002,"tag":"IDLE"}
{"t":1791651758.337,"type":"game","state":"start","id":280,"tag":"G_HIDDEN_OBJECT_3"}
{"t":1791652087.103,"type":"game","state":"end","id":280,"tag":"G_HIDDEN_OBJECT_3"}
{"t":1791652087.518,"type":"lights","cmd":"play","sequence":4,"repeat":false}
```
