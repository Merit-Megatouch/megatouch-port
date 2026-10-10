# The Force platform (2004–2011 software)

The Megatouch Force was the cabinet line before the ION. This page collects what is known about
its software, from Force 2011 (**V2011.40, branch V28.40**, build `V28_40T101109_1217`,
2010-11-09), the release that runs in the cabinet loader
([guide](../guides/cabinet-loader.md#a-force-cabinet)). Addresses are in the unpacked loader
(`/tmp/dstart`, see [io-board → the encrypted loader](io-board.md#the-encrypted-loader)).

## The system

| | Force 2011 |
| --- | --- |
| Disc | one CD: `part1` (/boot), `part2` (/home), `part5` (/, 4.75 GB). No `ion_only` image |
| Linux | kernel modules for 2.4.26, 2.4.31 and 2.6.18-164.6.1.el5 (the ION's CentOS 5 kernel) in `/lib/modules` |
| C library | glibc 2.3.6, code built with GCC 3.3; LinuxThreads (the loader exports its own `pthread_create`) |
| X | XFree86 in `/usr/X11R6` (XF86VidMode and DGA) |
| `/etc/fstab` | `hda5` / (ext2, read-only), `hda1` /boot, `hda2` /home, `hda3` swap, `hda7` /var (ext3), and `hda6` `/usr/local/share/ion_only` |
| `/tmp` | a symlink to `/var/tmp` |
| Libraries | `/usr/local/lib` (the loader's RPATH; 2005 libogg/libvorbis, Loudmouth, glib 2), `/usr/local/lib/merit`, `/usr/X11R6/lib`, `/usr/mozilla/lib` |
| `/var` | empty after install; the first boot restores `/factorybackup/var.tar.gz`, as on ION |

`/home/maxx/.xinitrc` logs `/etc/version` to `/var/merit/log`, sets a black root window and the
keymap, runs `up-disconkey update` (USB-stick updates), then starts the first that exists of:
`/usr/local/bin/delta` (when `/var/merit/delta` exists), `/var/bin/start`,
`/usr/local/bin/start`. There is no layout daemon or sidebar: the loader draws the whole screen.

The 2.6.18 modules and the `ion_only` line in `fstab` suggest this software line also ran on
newer hardware; the loader has ION code paths too (`SystemInfo::IsPlatformION`, ION key checks).
The "Force 2008.5" and "ION 2008.5" disc 1 files are byte-identical (V25.00; the ION set adds
`part6` on disc 2). Not tried: whether one root serves both.

## Files

| Path | What |
| --- | --- |
| `/usr/local/bin/start` | the loader, packed (payload at file offset 0x187c; it unpacks itself to `/tmp/dstart`) |
| `/usr/local/share/merit/config/` | default settings (`network.xml`, `key_settings.xml`, `loglimits.xml` …) |
| `/var/config/nvram.dat` | NVRAM (ION: `/var/merit/nvram.dat`) |
| `/var/config/settings/never/`, `…/twobutton/` | settings files; by the names, kept for good or cleared by the two-button clear (not checked) |
| `/var/config/settings/never/key_settings.xml` | `read_attempts`: failed security-key reads in a row |
| `/var/config/settings/twobutton/network.xml` | network settings (connection type, DHCP, DNS, server) |
| `/var/config/dhcpc`, `/var/config/dhcp_options` | the DHCP client script (`/etc/init.d/dhcpc` links to it) and its options |
| `/var/merit/dhcp/dhcpcd-eth0.info` | the DHCP lease (address, gateway, DNS) |
| `/var/config/hardware.cfg` | optional: a line containing `microtouch` (and `usb` for the USB controller) |
| `/var/config/cal640480.dat`, `cal800600.dat` | the touchscreen calibration (copies of the controller's parameter block 1) |
| `/var/config/forcecalibrate` | if present, the next boot calibrates the touchscreen |
| `/var/merit/tournamaxx/database/` | TournaMAXX databases (`config.db`, `books.db`, `coinin_settings.db` …) |
| `/var/merit/logging/` | logs, summaries, crash dumps (same system as ION) |

## Obfuscated strings

Many of the loader's strings (file names, messages, key checks) are stored scrambled and decoded
at run time by `u_m_m_s(text, m_m_k, 0, 0x400)`: each byte is XORed with the key
`"TheCowGoesBwak"` (repeating), except that a byte equal to its key byte stays as it is. For
example `"\x1f-<c=6\t( S\x0f>2&\x15<&\v"` decodes to `KEY RANGE MISMATCH`. The loader is
position-independent: strings are reached as offsets from the GOT base in `%ebx`
(`0x084e4350` in Force 2011), so search for `lea -0x…(%ebx)` rather than absolute addresses.

## Platform detection

`SystemInfo` reads `/proc/bus/pci/devices` and matches chipsets (the device IDs are themselves
obfuscated strings):

| Mask | Board | PCI IDs (vendor:device) |
| --- | --- | --- |
| 1 | Force: Intel 810 | `8086:2418` (82801AA hub), `8086:712x` (i810), `13f6:0111` (CMI8738 sound) |
| 2 | nForce2 | `10de:01e8`, `10de:006a`, `10de:01f0` |
| 4 | nForce 430 | `10de:026f`, `10de:026c`, `10de:0242` |
| 8 | ATI RS690 | `1002:7912`, `1002:4383`, `1002:791e` |

The I/O board table lists the FX1 (`04b4:6473`) first; GameTime boards are `0547:1234` and
`04b4:1234` (`SystemInfo::IsPlatformGameTime`).

## The I/O board

The Force loader has its own usbfs code instead of libusb: `usb_open(vid, pid)` reads
`/proc/bus/usb/devices` (`T: Bus=… Dev#=…` and `P: Vendor=… ProdID=…` lines), opens
`/proc/bus/usb/BBB/DDD`, reads the 18-byte device descriptor and then sends control transfers
with `ioctl(USBDEVFS_CONTROL = 0xc0105500)` (also `0x80045505`, `0x8004550f`). The protocol on
top is the ION's ([io-board](io-board.md)); the security key differs
([io-board → Force 2011](io-board.md#security-key-internal-licence)).

## Video

`ScreenGraphics` picks the Allegro driver from the `--videomode` argument
(`glob_video_mode`):

| Argument contains | Driver |
| --- | --- |
| `F` or `f` | `DGA2` on a Force, else `XWFS` (full screen) |
| `W` or `w` | `XWIN` (a window) |
| `X` or `x` | `XWFS` |
| none | 0 (Allegro's autodetect) |

`SwitchVideoModeInternal` changes resolution (640×480 menus, 800×600 for calibration and
Hi-Res games). In the full-screen modes it walks `XF86VidModeGetAllModeLines` without checking
that the call worked, then `XF86VidModeSwitchToMode` and `SetViewPort`; `XWFS` also uses
`SwitchVirtualScreen`. Allegro checks `XF86VidModeQueryExtension` itself.

## Touchscreen

`Get_Touch_Screen_Type` reads `/var/config/hardware.cfg`; without it the screen is a serial
MicroTouch. The 3M TouchWare kernel driver exposes the controller's serial protocol on
`/dev/twscreen1…3` (opened `O_RDWR | O_NONBLOCK`); touches reach the loader as pointer events
through the driver's X input. With no control port the loader shows **"Touch Screen Controller
Error"** and stops.

Commands are `SOH <text> CR`; the controller answers `SOH <text> CR` (`0` = OK):

| Command | Use |
| --- | --- |
| `GP1` | get parameter block 1: answer `SOH A CR`, `SOH <52 bytes> CR`, `SOH 0 CR` (`GetBlock1Serial`) |
| `SP1` | set it: answer `SOH A CR`, the loader writes 52 raw bytes, answer `SOH 0 CR` (`SetBlock1Serial`) |
| `CX` | calibrate: the controller answers once for the command and once per touched target (`MT_Calibrate_640x480`, `…800x600`) |

The loader keeps its calibration in parameter block 1 and copies it to
`/var/config/cal640480.dat` / `cal800600.dat`; at a resolution change it writes the matching
file back (`Load_Store_Calib`). The USB variant (`hardware.cfg` with `usb`) uses 8-byte packets
on the same port instead.

**First boot:** `InitVars` sets `MGlob` when NVRAM is missing (or `/var/config/forcecalibrate`
exists); the menu then runs module `0x2713`, `Calibrate_TouchScreen`: 640×480 and 800×600,
each ending in a *MOVE CURSOR* check with an EXIT button, then Operator Setup.

## The security key

Read by `USBIO::ReadDS1995KeyData` and checked by `KeyManager::Check` / `ConfirmKey`; format,
cabinet codes and status values are in [io-board](io-board.md#security-key-internal-licence).
On failure the screen shows **"INVALID KEY FOR VERSION V2011.40."** (or "ION KEY DETECTED IN
FORCE SYSTEM", "NO SECURITY KEY DETECTED") and "Machine will reboot. Attempt *n* out of 5"; after
five, "All attempts to verify the Security key…" and the loader stops at its tamper loop.
`InitVars` also checks the ROM ID against the range of real keys ("KEY RANGE MISMATCH") and the
key's part number and revision against NVRAM ("Key Part No. mismatch").

## Network

Operator Setup → *Network*: *Internet Connection* **Dial-Up** (the default; PPP through the
modem: `PPP_Config`, `PPP_Start`) or **LAN**; *Ethernet Setup*: **AUTOMATIC** (DHCP) or
**MANUAL** (IP, gateway); *Setup*: server name (factory: `us.accessmerit.com`) and two DNS
servers (factory: Merit's `198.6.1.1`, `198.6.1.142`). DNS is always set by hand, also with DHCP.
A factory /var has a static `192.128.5.5`. MegaLink uses an alias `eth0:0` at `192.168.4.<id>`.

The network manager (`general_network_device`) runs a state machine logged as
`Old State [RS_…] New State [RS_…]`: `RS_START_IFUP` → `RS_DHCP_RENEW` (or `RS_SET_STATIC_IP`) →
`RS_SET_DNS` → `RS_UP_END_OK` → `RS_MIM_START` (MegaLink). `dhcp_up` runs
`/etc/init.d/dhcpc start /var/merit/dhcp eth0`, i.e. **dhcpcd 1.3.22-pl4** with
`-t 15 -d -n -Y -N -R -l 86400` (it keeps `resolv.conf`; the loader writes DNS itself). dhcpcd
sets the default route with `SIOCADDRT` on its packet socket.

**Is there Internet?** `SystemInfo::IsInternetAvailable` asks `TestConnectionManager` to open a
TCP connection to **port 53 of each DNS server** (10 s timeout); a success in the last 20 s
counts. Otherwise a connection ends with result **51, `CE_NO_BROADBAND_CONNECTION`**
("NO BROADBAND CONNECTION"); `DialupNetworking::Connect` / `Status` return it for the
broadband account (type 3).

**Connecting** (`InternetConnectivity::Connect(reason)`, logged as `Connect( reason = … )` and
`TMaxx::Connect - Done. (code:text)`): the connection type comes from the network device (LAN or
dial-up), `ISP_Account` picks the dial-up account by priority, then a state machine
(`ConnectState`, `TournamaxxState`, …) talks to the server
([meganet](meganet.md)). After choosing LAN and saving, Operator Setup offers **Initial
TournaMAXX Connection** (reason `TMAXX_INITIAL_CONNECTION_OWN_ISP`): it registers the cabinet,
shows the registration details the server sends ("Is the following information correct?") and
loads tournaments and the catalog.

**After connecting** the loader syncs its clock: `SetHWClock(t)` calls `settimeofday`, then
opens `/dev/rtc` (unchecked) and sets the hardware clock with `RTC_SET_TIME`
(`0x4024700a`).

**Messenger:** `MessengerThread` uses Loudmouth 1.0 (XMPP). Its connect callback accepts only
exactly "error" or exactly "writable"; anything else hits `g_assert_not_reached`
(`lm-connection.c` line 506) and aborts the loader.

## Sound

Allegro's OSS driver (`/dev/dsp`). The loader's RPATH (`/usr/local/lib`) puts the cabinet's 2005
libogg/libvorbis ahead of any others in the process.

## Running it today: what breaks

What the cabinet loader has to cover, and why (details in the
[guide's stand-in table](../guides/cabinet-loader.md#a-force-cabinet)):

- no XF86VidMode in a nested X server → the unchecked mode list crashes at boot;
- no `/dev/twscreen*` → "Touch Screen Controller Error";
- `/proc/bus/usb` doesn't exist on modern kernels → no I/O board;
- 64-bit kernels refuse a 32-bit `SIOCADDRT` on a packet socket → DHCP works but there's no
  default route;
- Windows' DNS proxy (WSL) refuses TCP on port 53 → the Internet test fails with the sandbox's
  DNS; use public DNS servers;
- no `/dev/rtc` → `close(-1)` after the clock sync trips the loader's debug assert (SIGABRT);
- Linux reports a failed connect as writable + error + hang-up → Loudmouth aborts;
- modern PulseAudio libraries next to the 2005 libogg and the loader's `pthread_create`.

## Open questions

- Older Force discs (2004–2006.5) use other partition images (`part6`, multi-file `partN_gz.*`).
- Which XMPP host and port the messenger uses, and what it is for.
- Whether one root serves both Force and ION hardware (2008.5–2011), and what the 2.6.18 modules
  were for.
- The meaning of parameter block 1's 52 bytes.
