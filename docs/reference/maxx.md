# The Maxx line (Crown, 2005 software)

The Megatouch Maxx was the cabinet line before the Force. Its last Linux release, **Crown**
(`PG3002-01 V16.10`, three discs), is being brought up in the cabinet loader; this page records
what is known so far. The discs of the whole line are listed in
[cabinet → every installer disc](cabinet.md#every-installer-disc-as-seen-2026-10-11).

**Status (2026-10-11):** it runs as a Force (`MEGA_LOADER_CABINET=build/maxx`, a tree unpacked by
hand; `make loader-setup` doesn't build Maxx yet) to its main menu, with a DS1991 key image the
fake board serves, with correct colours and sound, in English, with the categories Puzzles, Cards,
Quiz & Word, Strategy, Kids Club, Mega Sports and New Games 2006, and it dials in for TournaMAXX
through a fake modem (the server it reaches no longer speaks its protocol; see
[TournaMAXX](#tournamaxx-dial-up)). Not checked yet: playing games, coins. On the main screen "General Help" and the credits label
overlap at the top right.

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

## Libraries

The runtime's copies of libraries the cabinet also ships (the ION's SQLite 2, glib, Pango,
FreeType, Fontconfig, Expat) don't suit this release: SQLite writes another encryption format
("write bad cypher version [252]"), and with the newer Pango the menu spins on
`pango_font_description_set_size: assertion 'size >= 0'` (millions of log lines). `loader.sh`
preloads Crown's own copies of those (not GL or X).

## Colours

Merit built this release's Allegro for a BGR screen: `set_gfx_mode` sets the colour layout to red
shift 0 and blue shift 11 (stock Allegro: 11 and 0), and its colour conversion is built around that
too (the `_colorconv_*` code is linked into the loader). On a 24-bit X screen Allegro converts its
16-bit screen as BGR, while the loader's sprites (`.spr`, RGB565, like every release's) are copied in
as they are, so red and blue came out swapped in all menu graphics (PCX images, loaded through
`makecol`, were right). Changing the layout values isn't enough, because the conversion keeps BGR.
On a 16-bit X screen Allegro takes its "matching visual" path instead: it reads the layout from
the X server (RGB565) and draws without any conversion. `loader.sh` therefore runs Xephyr at
depth 16 for this release, and `megaview` shows 16-bit screens (an RGB565 texture).

## Menu background

On a Force the menu wants `main/2k3/forcebkg`, which these Maxx discs don't have (only `maxxbkg`,
`pzbkg`); `BuildMainScreen` first looks for an operator background in `/var/merit/menu/`, so
`loader.sh` puts the Maxx one there as `forcebkg.spr`. Without it the menu crashes in
`SetBackgroundBmp` (a null bitmap) right after the first-boot calibration.

## TournaMAXX (dial-up)

*Operator Setup → TournaMAXX → Internet Setup* offers only **DIAL-UP** on this release: access
phone number, log-in, password, server (`us.accessmerit.com`), DNS. *Update From Server* (or the
automatic call after a real-time tournament game, `NT_DoTMaxxUpdate_FromIdle`) runs:

1. `PPP_Config`: `stat("/dev/modem")` (fails → "NO MODEM DETECTED"); writes
   `/var/merit/ppp/wvdial.conf` (sections `Dialer Defaults` and `Dialer TMaxx<n>`, Stupid Mode on),
   `pap-secrets`/`chap-secrets`, and the configured DNS servers into `/var/config/resolv.conf`
   (Crown's `/etc/resolv.conf`). Init string 2 depends on the modem named in
   `/var/config/hardware.cfg` (`AEIGEX560LKTF1` gets extra S-registers); `MODEM_INIT` overrides it.
2. `PPP_Start`: WvDial (`libwvdial.so` on the disc, linked to the loader) sends `ATZ`, the init
   strings and `ATM0L0` (speaker off), retrying with simpler init strings twice ("MODEM BROKEN"
   after the third), then dials (`ATDT…`).
3. On CONNECT WvDial starts `/usr/sbin/pppd` and reports Online; `PPP_Is_Up` then needs an IPv4
   address on **`ppp0`**.
4. The server name gets a 0 or 1 after its first label (`us.accessmerit.com` → `us0.…`,
   `us1.…`, alternating per attempt), looked up with c-ares, then a TCP connection to port
   **17751** (`0x4557`, fixed in `NT_Idle_Connect_To_Server`); refused → "SERVER NOT RUNNING".

In the loader this runs on stand-ins (see the
[loader guide](../guides/cabinet-loader.md)): `modemfake.so` is the modem, `pppd-fake.sh` stands in
for `pppd`, and slirp's interface is named `ppp0`. Seen 2026-10-11: init, dial (`ATDT654376`),
CONNECT, DNS through slirp, `us0.accessmerit.com` → 198.46.150.234 (today the community
`us.oerinet.net` host), then **connection refused on 17751**: that server only has ports 80 and 443,
for the ION/Force MegaNet exchange (HTTPS, text tables; [meganet](meganet.md)). The 2005 protocol
is a binary packet exchange (`CTournamaxxSocket::Process_*_Request`, `CPointToPointSocket`), so
TournaMAXX for this release needs a server that speaks it. None is known to be running today; the community server's operator, asked on 2026-10-11, knows nothing about the Maxx protocol.

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
- **Cipher** (`decrypt`): a 4-byte key from ROM ID bytes 1–5: `((id3 − id4) − id5) ^ id1 ^ 0x7F`,
  `(((id1 + id2) − id3) − id4) ^ 0xF3`, `(id4 + 0xBC) ^ (2·id2 + id1)`, `2·id4 + id5 + id1 + 0x7F`
  (the "global" next to `k_laser` is ROM ID bytes 4–5). Each byte `out = (in − c) XOR key[i % 4]`,
  `i` restarting at each ID / data / scratchpad part, `c` running on: it starts at 0xC2, 0x28, 0xE7
  for the three subkeys (0x12, 0x78, 0x37 advanced by 8 × 0x16 over the passwords) and 0xF3 for
  the scratchpad, and grows by 0x16 per byte. Verified: the loader decodes the served key byte
  for byte as written.
- **Transport scrambling** (session value S): ID 1 `(S[i&3] ^ r) + S0`, ID 2 `(S3 ^ 0x41) + r`,
  ID 3 `r − (S2 ^ S[i&3])`; data 1 is chained (`d[i] = r[i] ^ d[i−1]`, `d[0] = r[0] ^ (((S1 + S0)
  − S2) ^ S3)`, because `k_data1` follows `k_id3` in memory); data 2 and 3 XOR the bytes after the
  scratchpad (+0xE8, +0xE9); scratchpad bytes 0x3D–0x3F XOR S0–S2. S is 0x29292929
  (`USBIO::FixKey`) until command 4 hands out a new one.
- **ROM ID** (`FUN_0823e140`, "KEY RANGE MISMATCH"): family 0x82, bytes 6–5 = `00 40`, bytes 4–1
  between 0x31380 and 0x53FFF. Ours: `82 00 50 04 00 40 00 ed`.
- **The Force check** (`ForceChk1`, which picks the cabinet-code variant): platform 4, a USB board,
  two flags, and Allegro's `cpu_family == 6` (the Force's Celeron). `forcefix.so` reports family 6
  on any PC, so the key's Force flag (subkey 3 data `+0x1C` bit 3) always matches.
- **Pages:** each 64-byte page holds its subkey's ID at +0 and data at +0x10; the ION 2014
  `KeyManager` reads legacy keys the same way (P1/P2/P3 at `+0x3F0`/`+0x430`/`+0x470`), so its legacy
  branches name fields: NJ exception P1+0x10, Champion Edition cost P1+0x11, language mask P1+0x13
  (Crown: big-endian, `mask = v·2 + 1`), lockout P1+0x37, cabinet code P1+0x38, ID 2 = date
  `MM/DD/YY`, default language P2+0x2E, prices P2+0x2F/0x3A/0x3B, locale P2+0x30 (10 chars; "NJ…" New
  Jersey rules, "KIDZPACE" the kids' version), country P2+0x3E (0 USA; many places test it), ID 3 = revision; options: option *i* at legacy position `LEGACY[i]` (2-bit codes in
  subkey 3 data bytes 0–15, single bits from data byte 0x10; table in `scripts/lib/ds1991key.py`).
- **Checked at start** (`FUN_0823ff40`): ID 1 starts `SA`; ID 2's last two characters are `0`/`9`
  and a digit `0`–`5` or `9`; ID 3 starts `R`; subkey 1's data bytes `0x28–0x2F` are the cabinet
  code `87 23 87 9F E2 43 24 98` (+1 on the first byte if a hardware flag in subkey 3 `+0x2C`
  disagrees with the platform, or for high-bit characters in a subkey 2 string; −0x11 on the
  last byte is also accepted).
- **Read elsewhere:** subkey 1 = `DallasKeyP1` (coin defaults at `+0x17`, 6 × 4 bytes),
  subkey 2 = `DallasKeyP2` (prices: a list at `+0x10`, defaults at `+0x2F`/`+0x3A`), subkey 3 =
  `DallasKeyP3`, scratchpad = `DallasKeyScratch` (six 8-byte currency entries: name, value at
  `+5`). About 155 functions read the pages; the rest of the layout is still to be mapped.
- **Games:** game *N* is licensed by bit `0x80 >> N % 8` of P3 `+0x24 + N / 8`
  (`SetGamePriceAndStatusFromKey`; status 3 = on, 0 = off; games flagged 0x40 in their game data
  also need `ForceChk1`). This release has 195 game IDs (0 `sol` … 193 `sportsmerithon`, 194 the
  menu), so the bits run to P3+0x3C; the Force flag at P3+0x2C bit 3 is game 68's bit as well. With
  no game bits only Puzzles, Quiz & Word and Kids Club had anything to show.
- **Default language:** P2+0x2E is a language index for `InxToLang` (0 = English; the ION reads the
  same byte minus 1).

## Open questions

- The rest of the DS1991 layout (which categories and games it offers; options; languages).
- How the real Maxx hardware showed RGB565 sprites with this BGR Allegro (presumably a 16-bit
  framebuffer, where no conversion happens).
- The 2005 TournaMAXX packet protocol on port 17751 (for a server of our own).
- The ISA board's protocol (ports 0x228–0x22D), for running Crown as a Maxx.
