# The Maxx line (Crown, 2005 software)

The Megatouch Maxx was the cabinet line before the Force. Its last Linux release, **Crown**
(`PG3002-01 V16.10`, three discs), is being brought up in the cabinet loader; this page records
what is known so far. The discs of the whole line are listed in
[cabinet → every installer disc](cabinet.md#every-installer-disc-as-seen-2026-10-11). Status: the
loader starts, finds and starts the I/O board, passes the touchscreen check and stops at the
security key (a DS1991 key image doesn't exist yet).

## The discs and the system

| | Crown V16.10 |
| --- | --- |
| Discs | 3: disc 1 `part1` (/boot), `part2` (/home), `part5` (/, continued on discs 2–3 as `part5_gz.001`, `.002`), `part6` |
| `part6` | mounted at `/usr/local/share/forceonly`; nearly empty on this release |
| Linux | kernel modules for 2.4.17 and 2.4.26; glibc 2.2.5; the loader built with GCC 2.95 (old C++ name mangling, `Foo__5USBIO`) |
| `/var` | empty after install; the first boot restores `/factorybackup/var.tar.gz` (as Force and ION) |
| `/tmp` | a symlink to `/var/tmp` |
| `.xinitrc` | `/var/bin/start` if present, else `/usr/local/bin/start`; an xterm when `/var/config/commandline` exists |
| Loader | `/usr/local/bin/start`, packed like the others (payload at file offset 0x1964) |

Discs 2 and 3 use plain ISO 9660 names (`part5_gz.001`); `scripts/partimage.py` reads both
spellings. The extracted tree has directories without read permission (debugfs keeps the modes);
`chmod -R u+rwX` on our copy fixes that. Ruby 2 and Ruby Update are raw CD images (2352-byte
sectors): take 2048 bytes from offset 16 of each sector to get an ISO.

## Platforms

`FUN_0823e340` reads Linux 2.4's text `/proc/pci` (not `/proc/bus/pci/devices`) and looks for
chipset names, which are scrambled with `u_m_m_s` like the Force's, but with the key `"14:14:44"`
(the build's compile time; the Force 2011 key differs):

| Platform | Needs | Board |
| --- | --- | --- |
| 1 | `82439TX` + (`Rage IIC` or `264VT3`) + `82371` | Maxx: Intel 430TX, ATI Rage |
| 2 | `82437VX` + `Trio 64` + `82371` | older Maxx: Intel 430VX, S3 Trio64 |
| 3 | `VT8501` + `VT82C686` + `CyberBlade` | VIA |
| 4 | `82801` + `82810` + `CM8738` | Force: Intel 810, CMI8738 |

The cabinet loader presents platform 4 (`forcefix.so` answers `/proc/pci` with
`src/fakeio/proc-pci.force810`). On the Maxx boards the I/O board sits on ISA ports
0x228–0x22D; on a Force it is USB.

## The I/O board

On USB the loader looks for **`0100:0200`** (`usb_open(0x100, 0x200)`; same usbfs code as the
Force), downloads its firmware into RAM and starts it through **CPUCS 0x7F92**: an original
EZ-USB (AN2131), older than the ION's FX1. Command numbers are the ION's (vendor request 0xA1),
but results go to other RAM addresses and the host waits for a done flag; see
[io-board → the older board](io-board.md#the-older-board-ez-usb-an2131). The CMOS bytes it
reads (`GetCMOSByte`) come from the PC's RTC through ports 0x70/0x71.

## Touchscreen

`Open_MicroTouch` opens `/dev/twscreen0` (`fopen`, the touch data) and `/dev/twscreen1` (the
control port). On platform 4 `InitVars` requires Merit's own controller: `Is_H2H_Microtouch`
sends `SOH NM CR` (name) and the answer must start `Merit SPR`; otherwise "Touch Screen
controller not responding" and the tamper loop.

## Tamper loop

Failed checks show a message and then run `t_i_l` after clearing the stack up to `0xBFFFFFFF`
(the 2.4 kernel's stack top). On today's systems that address is past the stack, so the clearing
itself crashes (SIGSEGV in `memset`): a crash in libc's `memset` with a return address in the
loader is this loop.

## Databases

The loader's encrypted SQLite 2 is its own `libsqlite.so.0`; the ION's copy in the runtime writes
another format ("write bad cypher version [252]", then "database is full" on every
`CREATE TABLE`). Started with `MEGA_LOADER_PRELOAD=/usr/local/lib/libsqlite.so.0`, the databases
work.

## The security key (DS1991)

Only the legacy DS1991 format: three password-protected subkeys (8-byte ID + 48 bytes each) and a
64-byte scratchpad, read through the board (`USBReadKeyData`, command 2, done flag `0x1C48`, data
from `0x1B48`) or, without USB, bit-banged directly (`ReadKeyData`).

- **Passwords:** from the key's ROM ID, `(id[i] + i + 3 / 0x11 / 0x99) % 0xFF` XOR the ID, then
  `merit_encrypt` (state `0x12 0x78 0x37 0xF3`, one per subkey), then XOR 0x18 / 0x19 / 0x1A.
- **Transport scrambling:** IDs and data are mixed with the board's 4-byte session value
  (command 4, at `0x1C34`) and two more bytes after the scratchpad (`+0xE8`, `+0xE9`).
- **Cipher** (`decrypt`): a 4-byte key from ROM ID bytes 1–3 (and a 2-byte global that is
  always 0); each byte `out = (in − c) XOR key[i % 4]`, `c` starting at the subkey's state and
  growing by 0x16 per byte, through ID, data and scratchpad.
- **Checked at start** (`FUN_0823ff40`): ID 1 starts `SA`; ID 2's last two characters are `0`/`9`
  and a digit `0`–`5` or `9`; ID 3 starts `R`; subkey 1's data bytes `0x28–0x2F` are the cabinet
  code `87 23 87 9F E2 43 24 98` (+1 on the first byte if a hardware flag in subkey 3 `+0x2C`
  disagrees with the platform, or for high-bit characters in a subkey 2 string; −0x11 on the
  last byte is also accepted).
- **Read elsewhere:** subkey 1 = `DallasKeyP1` (coin defaults at `+0x17`, 6 × 4 bytes),
  subkey 2 = `DallasKeyP2` (prices: a list at `+0x10`, defaults at `+0x2F`/`+0x3A`), subkey 3 =
  `DallasKeyP3`, scratchpad = `DallasKeyScratch` (six 8-byte currency entries: name, value at
  `+5`). About 155 functions read the pages; the rest of the layout is still to be mapped.

## Open questions

- The rest of the DS1991 layout (options, languages, country, MegaNet and TournaMAXX settings).
- What `DAT_087a5694` (the platform test next to the hardware flag) returns on a Force.
- The ISA board's protocol (ports 0x228–0x22D), for running Crown as a Maxx.
