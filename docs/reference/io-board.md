# I/O board, security key and operator fob

How the cabinet talks to its USB I/O board, and how the two iButton readers on that board are
used: the internal **security key** (the software licence) and the front **login key** reader
(operator fobs and player keys). Everything here was read out of the 2014 image; nothing was
observed on real hardware. Statements marked *inferred* come from firmware/host code whose
purpose was not fully traced.

Sources (all git-ignored, under `reference/loader/`; see [Reproducing](#reproducing)):

| What | Where on the cabinet |
|---|---|
| Main program, encrypted | `/usr/local/bin/loader` (2013, the original `start`) |
| Main program, cracked | `/usr/local/bin/start` (2021, plaintext, two checks patched) |
| I/O board firmware (8051 Intel-HEX) | `/usr/local/gamedata/config/*fx*data.dat` |
| Board ID table used at boot | `/usr/local/gamedata/config/harddetect.xml` → `/var/merit/hardware.xml` |
| Operator setup UI | `opsetup.so`, `sixstars.so`, `volumecontrol.so` (same code) |
| Attract loop (key touch → exit) | `idle.so` |
| Last security keys seen | `/var/merit/.kf` (8-byte ROM IDs appended on every boot) |

## The encrypted loader

`.xinitrc` runs `/usr/local/bin/start`. The 2013 `start` (kept on the image as `loader`) is a
2 KB stub with the real program appended:

- payload starts at file offset `0x1874`: one flag byte (1 = scrambled), then chunks of
  `u32 len, u32 unused, len bytes`;
- each byte at absolute payload position `c` is XORed with
  `c % 20 == 0 ? 0 : ((c & 0xf) + (c % 20) + 0x23 + (c & 0x1f)) & 0xff`;
- the stub writes the result to `/tmp/dstart`, `execv`s it, then a grandchild overwrites the
  file with `rand()` bytes and unlinks it 3 s later. `ptrace(TRACEME)` is the anti-debug.

`reference/loader/unwrap.py <loader> <out>` reverses this offline. The result is a normal
non-PIE i386 ELF that exports its C++ symbols (`USBIO`, `usbioboard::USBIOBoard`,
`KeyManager`, …), which is why the rest of this page has names.

The 2021 `start` is the "Keyless" crack: a plaintext PIE build of the same program where
`KeyManager::Check()` and `USBIO::USBConfirmKeyID(uchar)` are overwritten with
`xor eax,eax; inc eax; ret`. `USBIO::FoundUSB()` is untouched, so **the cracked build still
needs a working I/O board**; only the security key is bypassed.

## The board

An AMI/Merit board built on a Cypress EZ-USB 8051. It has no firmware of its own: every boot the
host halts the CPU, downloads a `.dat` file into RAM and restarts it. Boards the loader accepts
(table at `0x082e52a0` in the decrypted loader; the cabinet in this image had `IOBOARD_FX1`):

| # | VID:PID | Chip | CPUCS | Mailbox | Firmware | harddetect id |
|---|---|---|---|---|---|---|
| 0 | 04b4:6473 | FX1 | e600 | e000 | fx1data.dat | IOBOARD_FX1 |
| 1 | 04b4:8613 | FX2 (blank) | e600 | e000 | fx1data.dat | IOBOARD_FX2 |
| 2 | 0547:2235 | FX (AN21xx) | 7f92 | 1b40 | fxdata.dat | IOBOARD_FX |
| 3 | 0100:0200 | FX | 7f92 | 1b40 | fxdata.dat | IOBOARD_CHAMP_FX |
| 4 | 0101:0200 | FX1 | e600 | e000 | fx1data.dat | IOBOARD_CHAMP_FX1 |
| 5 | 0547:1234 | FX | 7f92 | 1b40 | gtfxdata.dat | IOBOARD_GAMETIME_FX |
| 6 | 04b4:1234 | FX1 | e600 | e000 | gtfx1data.dat | IOBOARD_GAMETIME_FX1 |
| 7 | 04b4:2233 | FX1 | e600 | e000 | fx1data.dat | IOBOARD_ENTERTAINER |
| 8 | 04b4:2234 | FX1 | e600 | e000 | srfx1data.dat | IOBOARD_KIDZPACE |
| 9 | 04b4:2232 | FX1 | e600 | e000 | ff_fx1data.dat | IOBOARD_FIREFLY |

`harddetect` (boot script) matches these against `/proc/bus/usb/devices`, reboots on a miss and,
for devices flagged critical, stops at `critical_device_failure` after three misses.

### Opening it (`USBIOBoard::open_ami_board`)

1. `libusb_open_device_with_vid_pid` for each table row in order, claim interface 0.
2. Firmware download (Cypress built-in vendor request `0xA0`, handled by the chip itself):
   write `01` to CPUCS (halt), send every Intel-HEX record as
   `ctrl(0x40, 0xA0, wValue=addr, wIndex=0, data)`, write `00` to CPUCS (run), sleep 100 ms.
   The board does **not** re-enumerate; the host keeps the same handle.
3. `ctrl(0x0C, 0xA1, wValue=0x0013, wIndex=0)` – init (12 s timeout). `FoundUSB` re-sends it
   with `wIndex=0x100` whenever it has to reopen.

### Transport

Everything after the download uses two control requests:

| Request | Setup | Meaning |
|---|---|---|
| **Command** | `bmRequestType 0x0C, bRequest 0xA1, wValue = arg<<8 \| cmd, wIndex = addr/arg`, no data | Firmware copies `SETUPDAT[2..5]` into a register bank and runs `cmd` from its main loop. Any other `bRequest` is stalled. |
| **RAM access** | `0xC0/0x40, bRequest 0xA0, wValue = mailbox` | The chip's built-in RAM read/write, used to move data in and out of the mailbox. |

Two helpers wrap them:

- `ReadUSBMemory(addr, len, buf)` = command `0x03` (`arg = len`, `wIndex = addr`: firmware
  copies XDATA `addr` → mailbox), then `0xA0` IN `len` bytes from the mailbox.
- `WriteUSBMemory(addr, len, buf)` = `0xA0` OUT to the mailbox, then command `0x0E`
  (mailbox → XDATA `addr`).

So the host mostly treats the board as shared memory: write parameters to fixed XDATA
addresses, send a command, read the results back.

### Commands

| cmd | arg / wIndex | Host function | Effect (XDATA addresses) |
|---|---|---|---|
| 0x01 | — | `USBReadKeyID` | Read security-key ROM ID → 8 bytes at `1C31`, each byte `^ 0x08` |
| 0x02 | — | `USBReadKeyData` | Read the 3 DS1991 secure subkeys; passwords in at `1D53` (24 B); status `1D35`; data out at `1C39` (`0xEC` B); key present `2786` |
| 0x03 | len / addr | `ReadUSBMemory` | XDATA → mailbox |
| 0x04 | — | `GetDecryptVal` | Firmware makes a 4-byte session value from timer 1 → `1D25`, done flag `1D29` |
| 0x05 | len | `SendMDLine` | Send a line (up to 64 B at `1D6B`) to the books printer ("MiniDrucker"); status `1D36`, `1E6C`, `1D75`, `1D7A` ([Books printer](#books-printer-command-0x05)) |
| 0x06 | flags / heartbeat | `USBDoIO` | **The I/O poll** (see below) |
| 0x07 | 0x101 | `ReadPlayerOrRechargeKey` | Read 240 B from the login key → `1C31`; status `1D35` |
| 0x08 | 0x101 | `WritePlayerOrRechargeKey` | Write 240 B from `1C31` to the login key |
| 0x09 | len / eeprom addr | `WriteDataToEEPROM` | Board EEPROM ← `1D6B` |
| 0x0A | len / eeprom addr | `ReadDataFromEEPROM` | Board EEPROM → `1D6B` |
| 0x0B | — | `RT_LoginKeyDetected` | Probe the front reader: `1D35` = 1 if a key is touching; ROM ID → `1C31` |
| 0x0E | len / addr | `WriteUSBMemory` | mailbox → XDATA |
| 0x11 | block 0–7 | `read_block`/`read_header`/`read_footer`/`ReadDS1995KeyData` | Read 1 KB block of a DS1995/1996 security key → `1F2D`…`232C`; key present `2786` |
| 0x12 | — | `USBConfirmKeyID` | "Is it still the same security key?" → `1D37` (1 = yes); 11 failures in a row → `KeyShutDown("SECURITY KEY MISMATCH")` |
| 0x13 | 0 / 0x100 | `common_open`, `FoundUSB` | Initialise |
| 0x14 | — | `~USBIO` | `PREPARE_FOR_RELOAD` before exit |
| 0x16 | seconds | `ChangeWatchdogTimeoutVal` | Watchdog timeout (firmware takes it mod 60) |
| 0x17 | — | `USBReadBogusKeyData` | Decoy read into `1C39` (56 B); result unused |
| 0x20 / 0x21 | len 15 | `Read/WriteDockSerialNum` | Dock serial number at `2730`/`2731` |
| 0x23 | n | `FireSolenoid` | Fire solenoid `n`; status at `272D` (3 B) |
| 0x24 | — | `RequestPowerCycle` | Firmware clears port C bit 0 (*inferred*: drops the PC power relay) |
| 0x25 | — | `IsOkToFireSolenoid` | |

Present in the firmware but not traced: 0x07/0x08 also run on the "login" bus; 0x14 sets a
10-tick timer; 0x19 stores `wIndex` low at `27BD`.

### The I/O poll (command 0x06)

`io_heartbeat()` calls `USBIO::USBDoIO` from the loader's I/O thread. Every 5th call is replaced
by a login-key probe (`RT_LoginKeyDetected`) while player keys are enabled.

1. Write 6 output bytes to `1D2E`: `00, coin-meter pulses, TournaMAXX-meter pulses, 0, 0, 0`.
2. Command `0x06` with `wValue = flags<<8 | 6`: flags `0x01` and `0x02` come from two lockout
   globals (*inferred*: coin and bill lockout); `wIndex` = watchdog heartbeat (`0x80` when the watchdog is enabled, plus a
   6-bit counter that steps whenever the software watchdog's heartbeat changes).
3. Read 24 status bytes from `1D3B` into `USBIO+0x118`:

| Byte | Use |
|---|---|
| 0–7 | Coin counters for 8 coin/bill channels: low 7 bits = pulses since last poll (host credits them and clears them) |
| 8 | DIP switch bank DS1, active low, bit-reversed: bit 7 = switch 1 … bit 0 = switch 8 (host stores `~byte` as `IOBB[8]`) |
| 9 | bit 0 = **SETUP** button (opens Operator Setup), bit 1 = **CALIBRATE** button (→ `IOBB[2]` bits 0–1); bit 2 = books printer connected (`USBIO+0x121 & 4`, read by `ProcessMiniDruker`); bit 3 → `USBIO+0x39C`; bits 4 and 7 are latched events cleared by the reader (the joystick's left/right buttons) |
| 11 | bits 4–5 → `IOBB[1]`, bits 0–5 → `IOBB[2]` |
| 12–15 | Joystick X/Y (signed 16-bit) |
| 17 | PSoC version (`USBIO+0x129`, `USBIO::GetPSOCVersion`, cached at first use): ≥ 2 allows the light show, ≥ 10 means the improved amplifier, ≠ 0 lets the menu drop joystick-only games when no joystick is found; also read by the joystick code |
| 18 | Light-show status (`USBIO+0x12A`): bit 6 = command taken, bit 5 = done |
| 19 | Light-show answer (`USBIO+0x12B`) to the active/profile/brightness queries |

Confirmed by running the loader with the fake board (`docs/guides/cabinet-loader.md`) and
watching its own *Diagnostics → I/O Test* screen: coin bytes 0–7 count up channels 1–8, byte 9
lights SETUP/CALIBRATE, byte 8 flips DS1. No status byte drives the screen's second DIP bank
(DS2) on this board. Of the outputs, *Coin Lockout* sets both poll flags (`0x3`) and *Coin
Meter* pulses output byte 1.

### Light show

The ION light-show kit is LED lighting run by the board's PSoC. The loader reads `27A0` once at
start-up (`USBIO::Init` → `USBIO+0x928`); `0x80` means a kit was detected
(`USBIO::LightshowWasDetected`). `LightShowManager::IsSupported` also needs PSoC version ≥ 2
(status byte 17), the ION platform, and the loader not having given up on the kit
(`m_LSIsTalking`).

`LightShowManager::SendPacket(packet[32], len)`, len 1–32:

1. **Clear:** write `FF` to `272D`, then poll until status byte 18 bit 5 is clear.
2. **Send:** write the packet (32 bytes) to `272D` and its length (1 byte) to `274D`. Writing the
   length is the signal.
3. **Confirm:** poll until status byte 18 bit 6 is set (command taken), then until bit 5 is set
   (done). A query's answer is then in status byte 19.

The loop polls every 10 ms and gives up after 5 s. After 6 failures in a row the loader stops
using the kit until it restarts. `272D` is the same area `FireSolenoid` (0x23) reports in: the
light show and the solenoid outputs share the board's PSoC mailbox.

| Packet | Meaning |
| --- | --- |
| `01 00` | stop |
| `01 01 seq` / `01 02 seq` | play sequence `seq` (0–5) once / repeating |
| `01 0B r g b x` | set the lights to a colour (`SetLights`) |
| `01 0F on` | on/off (`SetActive`) |
| `01 10` | is it on? → status byte 19 |
| `01 11 p` | set profile (`SetProfile`) |
| `01 12` | which profile? → status byte 19 |
| `01 15 v` | set brightness (`SetBrightness`) |
| `01 16` | which brightness? → status byte 19 |

The fake board (`MEGAIO_LIGHTSHOW=1`) answers each packet at once (status byte 18 = `0x60`) and
logs it as a `lights` event ([events](events.md#lights)).

### Books printer (command 0x05)

`USBIO::SendMDLine(data, len)`:

1. Write `00` to `1D36`.
2. Write the line (64 bytes) to `1D6B`.
3. Command `0x05` with `wIndex = len`.
4. Read the status from `1D36`. If it reads `02`, the host writes `01` to `1D38`.
5. Read the printer's answer from `1E6C`. If it is `0x13` (the printer identifying itself),
   read `1D75` ('L' = printer type 1, otherwise type 2) and `1D7A` ('C' = the printer wants a
   checksum line).
6. When the line was the end marker `0x16`, an answer other than `01` resets the printer type.

A printout starts with an empty line (`WaitForEnq`: length 0) and ends with a line that is just
`0x16`. Lines are plain ASCII with `\n`. Status byte 9 bit 2 says a printer is connected; the
attract loop prints once per connection ([cabinet software → Books printer](cabinet-software.md#books-printer)).
The fake board saves each printout as `printouts/books-YYYYMMDD-HHMMSS.txt`, and `megaio print`
or **F4** plugs the printer in until a printout is done.

### Board EEPROM map (via commands 0x09/0x0A)

| Addr | Len | Content |
|---|---|---|
| `1F0E` | 200 | Optional key-password override; used only if bytes 0x78.. read `wxyz123` (not `wxyz1234`) |
| `1FB9` | 1 | Cleared on GameTime boards |
| `1FD6` | 10 | Magic `100293JKKJ` marks a valid serial number |
| `1FE0` | 16 | Cabinet serial number |
| `1FEF` | 12 | `u32 pin, u32 ~pin, u32 time_set` – the board-level operator PIN (`get/set/clear_operator_pin`) |

## The two 1-Wire buses

The firmware bit-bangs two separate 1-Wire buses on EZ-USB port C (pin masks `0x40` and `0x20`).
ROM IDs come back with each byte `^ 0x08`; the host checks the standard Dallas/Maxim CRC-8
(table at `0x082e3440`).

### Security key (internal, licence)

`KeyManager::Init` picks the format from the key's family code:

- **0x0A, 0x0C, 0x8C** (DS1995 16 kbit / DS1996 64 kbit memory iButtons): the "new" format,
  read with command 0x11, block by block.
- **Anything else**: the legacy DS1991 MultiKey format, three password-protected subkeys read
  with command 0x02.

This cabinet's `/var/merit/.kf` lists two keys over its lifetime, `8c694a020040000f` and later
`8c14fc020040000e`. Both are family 0x8C, both pass the CRC, so it ran the new format.

**New format (DS1995/1996).** Seven 1 KB data blocks (0–6) and a footer in block 7:

- *Footer* (180 B): XORed with three bytes made from the ROM ID (`rol(id[1],1)`,
  `rol(id[2],6)`, `rol(id[3],3)`, cycled). Layout: `Version[8]` (byte 7 = 0),
  `u16 BlockInUse` (0–6), per-block entries of 24 bytes with a `u16 previous block` at
  `+10 + 24*i`, `u16 CheckSum` at `0xB2` = sum of bytes `0..0xB1`.
- *Block in use* (`ReadDS1995KeyData`): XORed with 6 bytes `id[1..6] ^ 60 62 6e 72 6f 75` cycled,
  except bytes `0x12–0x15`, which get only the first 4. Checksum: u16 at `0x355` = sum of all
  bytes except `0x12–0x15` and `0x355–0x356`.
- Contents used by `KeyManager`: `+0x00` part number (8 chars, must start with `SA3`; this
  cabinet's is `SA362801`, kept in `nvram.dat`), `+0x09` key date (time_t), `+0x0D` revision
  (4), `+0x16` 10-char field, `+0x2A` server URL (200), `+0x35B` web-portal URL (100),
  `+0xF5` 8-byte cabinet code: for a DS1995 key `KeyManager::Check` accepts only
  `93e41c6e20911b9b` or `cfb4a9611bb72003` (else result 6/8; the legacy DS1991 path wants
  `0123456789abcdef` or `…cdff`), `+0x172..0x1AB` floats (coin and price values),
  **`+0x1B0 + game ID` (300 bytes) per-game price: low nibble = default price in credits, high
  nibble = continue cost; 0 = the game is not offered** (`KeyManager::DefaultGamePrice`,
  `DefaultGameContinueCost`), `+0x2DD..` 120 game-option bytes, `+0x357`/`+0x359` unit times
  for coinless time play, `+0x3BF..` a bit field (*not* the game licence; a genuine key has 200
  of 300 bits set), `+0x3EF` one more flag.
- *Option bytes* (`ParseOptionData`; `KeyManager::Option(i)`): 0 = locked off, 1 = locked on,
  2 / 3 = the operator's choice, default off / on. `NVRAMMap::IsOptionOn` uses the NVRAM value only
  for 2/3; Operator Setup shows an option only if `IsOptionSelectable` (2 or 3); a settings reset
  (`InitVars`) sets NVRAM to `value & 1`. A few are read straight from the key: 0x37 Hi-Res (game
  buttons for Hi-Res-only games are skipped when 0), 0x43 MindSpark and 0x72 download selector
  (platform/hardware checks: 0x72 licensed without the matching amplifier/board stops the loader
  with "invalid key with the current hardware configuration"), 0x16/0x51 languages, 0x5C coin
  table editing. With the per-game price table all 0, the unpatched loader deactivates every game (and saves
that in `settings.xml`); the keyless build doesn't read it.
- At boot the key's part number and revision are compared with the ones stored in NVRAM
  (`nvram.dat` +0x01, +0x38); a different key resets NVRAM.
- **The keyless build:** it replaces five key functions (`KeyManager::Check`,
  `USBIO::USBReadKeyID`, `ReadDS1995KeyData`, `USBReadKeyData`, `USBConfirmKeyID`). It returns a
  fixed ROM ID (`8c14fc020040000e`), and its key block is a genuine one embedded at file offset
  `0x1AE408`. The factory loader from the installer discs is identical apart from those
  1,114 bytes, and runs on the fake board with a key image that has a valid cabinet code and
  per-game prices.
- `scripts/loader-key.sh` writes such an image for the fake board (`key.bin`: data in block 5,
  footer in block 7) from the cabinet's own NVRAM; see `docs/guides/cabinet-loader.md`.
- `read_header` also tries a 22-byte header XORed with one of six ROM-ID-derived masks and
  accepts it when it decodes to `SA3…`.

**Legacy format (DS1991).** The host derives three 8-byte subkey passwords from the ROM ID
(`(id[i] + i + 3/0x11/0x99) % 0xFF`, then `merit_encrypt`, then `^ 0x18/0x19/0x1A`), writes
them to `1D53`, runs command 0x02 and gets back 3 × (8-byte ID + 48-byte secret) + 64-byte
scratchpad. That data is additionally scrambled with the 4-byte session value from command 0x04.

**Where the key is enforced.**

- `InitVars()` (boot): breakpoint scans (`findint3`) on the key code → tamper trap at
  `0x081D7FD0` (cycles video modes, clears high scores, loops forever); then
  `KeyManager::ConfirmKey`; then hardware checks with messages such as
  "No ES Key Found! Machine will reboot.", "ES Key is out of range!" and
  "%s is an invalid key with the current hardware configuration.", which `exit(0)` after 30 s.
  On a fresh NVRAM, the key's part number, revision and 120 option bytes are copied into NVRAM
  and the game menu is rebuilt from them.
- `KeyManager::Check()`: returns 1 (OK), 2 (no board), 3 (no key), 6/8 (wrong cabinet code),
  7 (bad checksum).
- During play: `USBIO::USBConfirmKeyID` is called by games (`tritowers.so`, `elevenup.so`,
  `taiplay_new.so`, `symboltritowers.so`), and ~25 others call `USBIO::FoundUSB`. Both need a
  board answering.
- `KeyShutDown()` copies hidden code from `t_i_l` into a heap buffer and jumps to it, and some
  error strings are decoded at run time with `u_m_m_s(…, m_m_k)`. That obfuscated code was not
  followed.

## Operator fob and player keys (front reader)

The front reader is the second 1-Wire bus. It accepts only family **0x02** (DS1991 MultiKey) or
**0x82** iButtons (`RT_LoginKeyDetected`, command 0x0B).

- **Attract loop** (`idle.so`, `AttractClass::DoCurrentEvent`): on ION, if
  `LoginKeyDetected` → attract exits with reason `0x12`.
- **`USBIO::TryReadingLoginKey`** reads the key's 240-byte record (command 0x07). The 8 bytes at
  `+8` hold the key type:
  - `m8b4uc3d` – player key (Megatouch Nation / TournaMAXX login; text fields at `+0x10` (12)
    and `+0x40` (4) are validated). A valid record with an unknown tag is reset to an empty
    player key.
  - `y2ger1b0` – **operator key**, returned as 4.
- **Records must be pre-programmed**: bytes 0–7 of the record must equal the key's own ROM ID
  and the u16 at `0xAE` must be the sum of all other bytes, or the read fails ("Reading Failed").
  Merit shipped its keys programmed; the loader never formats a blank iButton.
- **Operator fob** = a key programmed with the `y2ger1b0` tag **and** registered by ROM ID.
  Registering: Operator Setup → System → *Setup Operator Keys* → *Set Key* ("Touch new key to
  machine…", `STM_SetupOperatorKeys::SetKeyIdPin`), then a 6-Star PIN. The ROM ID and PIN are
  stored as a `db_config::OperatorKeyEntry` (`<KeyId>`, `<PIN>`) in `/var/merit/settings.xml`;
  four slots. Using it: touch the key at the menu or attract screen →
  `PlayerKeyClass::EnterStateStart` sees type 4 and opens `OperatorKeyEntry` ("Enter your pin to
  open Operator's Setup") → the PIN opens Operator Setup. A key with the player tag opens the
  My Merit *Player Key Setup* instead. This image has no operator keys stored.

## Running the original program

Done: `docs/guides/cabinet-loader.md` runs the cabinet's loader (the 2021 `start`, which needs the
board but not the security key) unmodified, with a fake board built as `libusb-1.0.so.0` that
implements everything above, a fake MicroTouch driver, OSS-to-PulseAudio sound and a nested X
server. The original 2013 `loader` would additionally need a security-key image (`key.bin` for
the fake board): `.kf` has the ROM IDs and `nvram.dat` the part number, but the 1 KB record with
its option bytes and checksums is not on the image.

## Reproducing

```
reference/loader/unwrap.py      decrypt /usr/local/bin/loader          (python3 -I unwrap.py in out)
reference/loader/hex2bin.py     Intel-HEX firmware → flat binary
reference/loader/rd.py          dump the board table / bytes at a VA
reference/loader/run-ghidra.sh  import + decompile functions matching a regex
reference/loader/run-post.sh    run DecompileMatching/DecompileCallers/DecompileRefs on the
                                already-analysed loader
reference/loader/Seed8051.java  pre-script so Ghidra finds the 8051 vectors
reference/loader/decomp/        usbioboard.c, USBIO.c, KeyManager.c, initvars.c, fx1data.c,
                                callers-*.c (opsetup, idle, sixstars, volumecontrol)
```

Firmware: import `fw/fx1data.bin` as raw `8051:BE:16:default` with `Seed8051.java`. Its main
loop dispatches on `BANK1_R1` (the command byte copied from `SETUPDAT[2]` when
`SETUPDAT[1] == 0xA1`, function `3132`). 1-Wire reset/presence is `16e2`.
