# Operator guide

Everything you need to run a Megatouch ION (2014) cabinet on a PC: installing it, starting it,
the controls, Operator Setup, coins and keys, the game menu, MegaNet and TournaMAXX, settings
and backups, and what to do when something goes wrong.

What you get is the cabinet's own software, unmodified, from the cabinet's disk image: the
attract loop, the game menus, Operator Setup, coins and credits, operator and player keys,
every game, MegaNet and TournaMAXX. The cabinet hardware it expects (I/O board, touchscreen,
security key, sound card, network port, monitor) is simulated. For how that works, see
[cabinet-loader](cabinet-loader.md).

- [1. What you need](#1-what-you-need)
- [2. Install (once)](#2-install-once)
- [3. Starting and stopping](#3-starting-and-stopping)
- [4. The window](#4-the-window)
- [5. Controls](#5-controls)
- [6. First-time setup checklist](#6-first-time-setup-checklist)
- [7. Operator Setup at a glance](#7-operator-setup-at-a-glance)
- [8. Coins, credits and free play](#8-coins-credits-and-free-play)
- [9. Operator and player keys](#9-operator-and-player-keys)
- [10. The game menu](#10-the-game-menu)
- [11. Network, MegaNet and TournaMAXX](#11-network-meganet-and-tournamaxx)
- [12. Game options and the licence](#12-game-options-and-the-licence)
- [13. Settings, backups and resets](#13-settings-backups-and-resets)
- [14. Updating](#14-updating)
- [15. When something goes wrong](#15-when-something-goes-wrong)
- [16. Dedicated touchscreen box (kiosk mode)](#16-dedicated-touchscreen-box-kiosk-mode)
- [17. Linked cabinets (MegaLink, experimental)](#17-linked-cabinets-megalink-experimental)
- [18. Where things are](#18-where-things-are)

## 1. What you need

| | |
| --- | --- |
| **A PC** | Windows 10/11 with WSL2 (Windows Subsystem for Linux), or Ubuntu Linux. 64-bit Intel/AMD. A graphics card helps but is not required. |
| **The cabinet disk image** | `Megatouch ION 2014 HDD Keyless.img` (60 GB). This project contains no cabinet software; everything is read from your image. It is only read, never changed. Other images need other partition offsets (see [troubleshooting](troubleshooting.md#setup)). |
| **Disk space** | About 20 GB free inside Linux, besides the image. The image itself can stay on a Windows drive. |
| **Internet** | During installation (Ubuntu packages are downloaded). Afterwards only for MegaNet. |

## 2. Install (once)

### 2.1 Linux (Windows only)

In PowerShell **as administrator**:

```powershell
wsl --install -d Ubuntu
```

Reboot, open **Ubuntu** from the Start menu and choose a user name and password. Linux windows
and sound on the Windows desktop come from WSLg, which is part of current WSL: `wsl --version`
should list a WSLg version (if not, run `wsl --update`).

All commands below are typed in the Ubuntu window (or a terminal on a Linux box).

### 2.2 The installer

```bash
bash <(curl -fsSL https://raw.githubusercontent.com/Merit-Megatouch/megatouch-port/main/scripts/install.sh)
```

It installs the Ubuntu packages it needs (asks for your password), downloads the project to
`~/megatouch-port`, asks where the disk image is and checks it, sets everything up (10–20
minutes the first time, mostly downloads), and offers a shortcut: on the Windows desktop under
WSL, in the applications menu on Linux. On a dedicated touchscreen box, add `--kiosk`
([section 16](#16-dedicated-touchscreen-box-kiosk-mode)).

Windows drives appear under `/mnt/`: `E:\Images\x.img` is `/mnt/e/Images/x.img`.

Already have the project? `scripts/install.sh` from inside it does the same. Running it again is
safe: whatever is already done is skipped, and the cabinet's settings are never overwritten.

### 2.3 By hand (what the installer does)

```bash
sudo apt install git gcc g++ make python3 python3-venv curl e2fsprogs binutils bubblewrap
git clone https://github.com/Merit-Megatouch/megatouch-port ~/megatouch-port
cd ~/megatouch-port
echo 'IMG="/mnt/e/path/to/Megatouch ION 2014 HDD Keyless.img"' > cabinet.local.conf
make setup           # 32-bit runtime and compiler support
make loader-setup    # copies the cabinet's software out of the image, downloads the display
                     # and network helpers
```

Keep the project in your Linux home folder (`~`), not under `/mnt/c` or `/mnt/e`: those are much
slower. Keep the quotes around the image path: the file name has spaces.

## 3. Starting and stopping

```bash
cd ~/megatouch-port
make loader-run
```

A window titled **Megatouch ION (cabinet loader)** opens. The cabinet boots in about 20
seconds (a little faster than the real one), shows the region warning, and then runs its attract
loop. **Touch the screen** (click) to get the menu.

- **To stop**, close the window (or press Ctrl+C in the terminal). Everything shuts down with it.
- The first start after an unclean stop may show *"Performing database maintenance"*: wait, it
  finishes on its own.
- Every start saves a backup of the cabinet's settings first ([section 13](#13-settings-backups-and-resets)).

**Windows shortcut**: the installer offers one. By hand, a desktop shortcut with this target
(a console window opens alongside; closing the cabinet window closes it):

```
C:\Windows\System32\wsl.exe -e bash -lc "cd ~/megatouch-port && make loader-run"
```

## 4. The window

| | |
| --- | --- |
| Resize, maximise | Drag the edges, or use the window's maximise button. The picture scales. |
| **F11** or **Alt+Enter** | Fullscreen on / off |
| **Ctrl+Alt+S** | Keep the cabinet's shape (black bars) / stretch to fill |

The cabinet changes resolution on its own: 640×480 for the menus, 768×480 when the widescreen
sidebar is shown, 1280×800 for widescreen games such as Trix. The window follows, and touches land
where you click at any size.

The narrow strip beside the menu is the **widescreen sidebar** (rotating ads). The two buttons
under it move it to the left or right.

## 5. Controls

| Cabinet | On the PC |
| --- | --- |
| Touchscreen | Mouse, left button, in the window |
| **SETUP** button (inside the cabinet) | **F1**: opens Operator Setup |
| **CALIBRATE** button | **F2**: touchscreen calibration (finishes by itself here) |
| Coin into channel 1 / 2 / 3 / 4 | **F5 / F6 / F7 / F8** |
| Operator key on the key reader (held while the key is down) | **F9** |
| Player key (My Merit) on the key reader | **F10** |
| Joystick accessory | arrow keys, **Space** / **Enter**; only when started with `MEGAIO_JOYSTICK=1 make loader-run` |

The keys work while the cabinet's window has focus (click into it first).

> If SETUP doesn't respond during the attract loop, touch the screen once, then press F1.

## 6. First-time setup checklist

Your cabinet starts with the settings that were on the image. A sensible first round:

1. **Operator key and PIN**: so setup isn't open to everyone ([section 9](#9-operator-and-player-keys)).
2. **Free play or paid play**: *Credits/Pricing* ([section 8](#8-coins-credits-and-free-play)).
3. **Clock**: *System → Set Time*. The cabinet keeps its own clock; MegaNet also corrects it.
4. **Volume**: *System → Volume Control*.
5. **Which games are in the menu**: *Games → Game Setup* ([section 10](#10-the-game-menu)).
6. **MegaNet** if you want tournaments and updates ([section 11](#11-network-meganet-and-tournamaxx)).

## 7. Operator Setup at a glance

Press **F1** (or touch with the operator key and enter the PIN). The main menu:

| Button | What's there |
| --- | --- |
| **Credits/Pricing** | Coin values, credits per coin, game prices, free play (*Options → Freeplay Enabled*) |
| **Games** | *Game Setup*: which games are in each menu category, their price and continue cost. *Options*: game behaviour options |
| **Hi Scores** | High-score tables (clear, settings) |
| **Books** | The cabinet's accounting: coins in, plays per game and so on |
| **System** | *Set Time*, *Security Setup*, *Volume Control*, *Set 6 Star PIN*, *Data Transfer*, *Options*, *Setup Operator Keys*, *AMI Setup* |
| **Diagnostics** | *I/O Test* shows every coin channel, button and DIP switch reacting (F1/F2/F5–F8 here) |
| **Network** | Network summary, *Connection Wizard*, *Network Options* (MegaNet server), *Connect to MegaNet/Update from Server* |
| **Tournament** | The cabinet's own local tournaments |
| **Credit Card** | Card reader settings (no reader is simulated) |
| **Presentation**, **Promotion**, **MegaNet** | As on the cabinet; see Merit's operator manual |

Leave with the red **X** at the top right. Merit's own operator manual describes every screen
in detail; the menus here are exactly the cabinet's.

## 8. Coins, credits and free play

- **F5–F8** drop a coin into channels 1–4. What a coin is worth is set in *Credits/Pricing*;
  the default here is $1 = 6 credits.
- **Free play**: *Credits/Pricing → Options → Freeplay Enabled*. With free play on, games start
  without credits and the top of the menu shows *FREEPLAY*.
- Game prices per game: *Games → Game Setup* (the *Credits* and *cost to continue* columns).
- Everything counted goes into *Books*, as on the cabinet.

## 9. Operator and player keys

The cabinet has a key reader for small iButton keys. Here, **F9** is an operator key and
**F10** a player key.

**Register F9 as your operator key:**

1. **F1** → *System* → *Setup Operator Keys* → *Set Key* (slot 1).
2. Hold **F9** while it says "Touch new key to machine…", release when asked.
3. Enter a PIN on the six stars, *Set New PIN*. The key shows as `3001104147454d02`.
4. Leave setup. From now on **F9** at the menu asks for the PIN and opens Operator Setup.

**F10** is a My Merit player key; touching it at the menu opens *Player Key Setup*.

Need different key IDs (for example a second operator key)? See `MEGAIO_OPERATOR_KEY` /
`MEGAIO_PLAYER_KEY` in the [commands reference](../reference/commands.md#cabinet-loader).

## 10. The game menu

The menu shows categories (Puzzles, Cards, Quiz & Word, Strategy, Kids Club, Action, New Games…)
and each category's games. Many installed games are **not in any category** by default; Trix
is one.

**Add a game to a category:**

1. **F1** → *Games* → *Game Setup* → the category (e.g. *Cards*).
2. The upper list is the category's games, page by page (arrow on its right). The lower list is
   every game that can go in it: **red = already in the menu**, white/dark = not yet.
3. Tap an **empty slot** in the upper list (page right to find one), then tap the game in the
   lower list. Tapping a filled slot replaces that game.
4. *Done* → *Yes* to save.

*Enable All Games* puts everything available into the category; *Factory Default Games* restores
the original lists.

Hi-Res-only games (Trix and other 1280×800 widescreen games) only appear in the menu when the
High-Res option is licensed and on, which it is with this project's key image
([section 12](#12-game-options-and-the-licence)).

## 11. Network, MegaNet and TournaMAXX

The cabinet has its own virtual **wired** network with internet access through your PC. The
cabinet's settings from the image are for **wireless**, so set it up once:

1. **F1** → *Network* → *Connection Wizard* → *Next* (time zone) → *Skip* (MegaNet Core) →
   *Skip* (registration) → **Wired Ethernet** → **Wired Ethernet Network** →
   *Accept Settings and Connect* → *Save Settings*.

**MegaNet** was Merit's online service (updates, tournaments, statistics). Merit's own server is
gone; community servers exist. To use one:

2. *Network* → *Network Options* → *MegaNet Server* → *Set* → type the server name (for
   example `us.oerinet.net`) → *Enter*.
3. Back on *Network Summary*: **Connect to MegaNet/Update from Server**. The server's operator
   usually has to activate your machine first; tell them your **MegaNet ID** (shown on
   *Network Summary*, or `scripts/loader-identity.sh`).

**Each install is its own machine.** A new install gets its own hardware serial number and
MegaNet ID (an install that already had a cabinet keeps the one it had, so its registration
stays valid). `scripts/loader-identity.sh` shows them; `--new` picks new random ones and
`--set SERIAL MEGANET_ID` sets them (cabinet stopped). They are kept in
`build/loader/var.identity`, which `make loader-reset` doesn't touch; keep a copy with your
backups if the server knows your machine.

Good to know before connecting: a MegaNet connection sends the server your cabinet's books,
logs and crash reports (as the real cabinet did), and the server can change settings, menus and
tournaments.

**TournaMAXX** (online tournaments):

- Needs *System → Options → Tournament Mode: ON-LINE*, a MegaNet connection, and free play
  **off** (or the *TMAXX_OK_IN_FREEPLAY* option, [section 12](#12-game-options-and-the-licence)).
- The start screen then shows **Competition!** (MegaNet / TournaMAXX) with the server's running
  tournaments, and the main menu gets a TournaMAXX category.
- No tournaments listed? The server isn't offering any for your machine right now, or you
  haven't connected since they were added: press *Connect to MegaNet/Update from Server*.

## 12. Game options and the licence

The cabinet's behaviour is driven by 120 **game options** (free play, TournaMAXX, Six Stars,
languages, attract-mode sound…). On a real cabinet the security key (the licence) decides which
ones the operator may change. This project gives the cabinet a key image of its own licence,
made automatically on first start, which leaves every option to you except those for hardware we
don't simulate (MindSpark mode, TouchTunes, credit card, the Rowe download selector).

Change options in Operator Setup (*System → Options*, *Games → Options*, *Credits/Pricing*).
For options the menus don't show, from a terminal **with the cabinet stopped**:

```bash
scripts/loader-option.sh --list                          # every option, its value, what the licence allows
scripts/loader-option.sh ENABLE_SOUND_IN_IDLE_MODE 1     # set one (a backup is taken first)
```

## 13. Settings, backups and resets

Everything the cabinet remembers (settings, books, high scores, keys, MegaNet registration,
tournaments) is in `build/loader/var/merit`.

| | |
| --- | --- |
| Automatic backups | Every start saves one to `build/loader/backups/` (the newest 20 are kept) |
| Make one yourself | `make loader-backup` (or `scripts/loader-backup.sh before-changes` with a label) |
| List them | `scripts/loader-backup.sh --list` |
| Restore one | Stop the cabinet, then `make loader-restore BACKUP=<file name>`. The current state is kept aside, not deleted. |
| Back to the image's original state | `make loader-reset` (takes a backup first) |

Copy `build/loader/backups/` somewhere safe from time to time; it is not in git.

## 14. Updating

With the cabinet stopped:

```bash
cd ~/megatouch-port
make update
```

It backs up the cabinet's settings, downloads the new version, rebuilds what changed, and goes
back to the previous version by itself if the new one doesn't build. `scripts/update.sh --check`
only says whether there is an update; `scripts/update.sh --rollback` returns to the version
before the last update (that version is then skipped until a newer one appears). Your settings,
`cabinet.local.conf` and the cabinet's state are never touched by an update. Kiosk boxes update
themselves at night ([section 16](#16-dedicated-touchscreen-box-kiosk-mode)).

## 15. When something goes wrong

| Problem | What to do |
| --- | --- |
| No sound at all (also in other Linux apps) | WSLg's sound server has hung: in PowerShell run `wsl --shutdown`, reopen Ubuntu, start again |
| The window doesn't open / `megaview did not start` | Look at `build/loader/megaview.log`. `MEGA_LOADER_VIEW=xephyr make loader-run` uses the plain fixed-size window instead |
| F1 does nothing | Click into the window first. In the attract loop, touch once, then F1 |
| A game freezes | Note the game and what happened, and report it (with `build/loader/var/merit/logging/logs/*.running.log`) |
| "Performing database maintenance" at boot | Normal after an unclean stop; wait |
| Network says *No Internet* | Run the Connection Wizard for **Wired Ethernet** ([section 11](#11-network-meganet-and-tournamaxx)) |
| Settings got messed up | Restore a backup ([section 13](#13-settings-backups-and-resets)) |
| Kiosk keeps restarting | `scripts/cabinet.sh status` and `build/loader/kiosk.log` show why; `make kiosk-stop`, then `make loader-run` to watch it in a window |
| Something else | [Troubleshooting](troubleshooting.md#cabinet-loader) |

## 16. Dedicated touchscreen box (kiosk mode)

For a box that should be nothing but the cabinet, typically a small PC with a touchscreen
running Ubuntu Desktop:

```bash
make kiosk                          # run it now, kiosk-style
scripts/cabinet.sh autostart on     # and from now on at every login
```

(`scripts/install.sh --kiosk` does the autostart part during installation.)

What kiosk mode does:

| | |
| --- | --- |
| **Fullscreen**, pointer hidden | The touchscreen acts as the cabinet's touchscreen (touches arrive as clicks) |
| **Auto-run** | Starts when the user's desktop session starts (XDG autostart) |
| **Crash protection** | If the cabinet exits or crashes, it is started again after 5 seconds. If it keeps failing within two minutes of starting, the pause grows to 1 minute, then 5, so a broken setup doesn't spin |
| **Hang protection** | If the picture hasn't changed for 5 minutes *and* the cabinet's main program is busy all that time (stuck in a loop), it is restarted. A quiet screen waiting for a touch is left alone |
| **Screen stays on** | Screen blanking and the lock screen are turned off while it runs |
| **Daily backups** | One settings backup a day (the last 14 kept) instead of one per start, so restarts can't push good backups out |
| **Nightly updates** | At 4 am, if there is a new version: stop the cabinet, update (settings backed up), start again. If the cabinet then fails three times quickly or hangs within its first 10 minutes, the update is rolled back on its own and that version is skipped |

To boot straight into the cabinet, also turn on **automatic login** for the user: on Ubuntu
Desktop, *Settings → Users → Automatic Login* (or `autologin-user=` in LightDM's
configuration on lighter desktops). Then power on → desktop → cabinet, no keyboard needed.

**Getting out** at the box: **Ctrl+Alt+End** quits and stops the kiosk (closing the window any
other way counts as a crash and it comes back). From another terminal or over SSH:
`make kiosk-stop`. `scripts/cabinet.sh status` shows whether it runs and the latest restarts;
the log is `build/loader/kiosk.log`. Turn the autostart off with `scripts/cabinet.sh autostart off`.

Settings (in `cabinet.local.conf` or the environment): `KIOSK_HANG_SECS` (300; 0 turns hang
protection off), `KIOSK_FULLSCREEN` (1), `KIOSK_HIDE_CURSOR` (1; 0 without a touchscreen),
`KIOSK_BACKUP` (`daily`, `auto` or `none`), `KIOSK_UPDATE` (`nightly` or `off`), `KIOSK_UPDATE_HOUR` (4).

Under WSL the Windows side starts and stops WSL, so use the desktop shortcut there; kiosk mode is
meant for Linux boxes.

## 17. Linked cabinets (MegaLink, experimental)

Real cabinets in one venue could link: players on different machines play linkable games
against each other (MegaLink). Here, cabinets started on the same **cabinet network** see each
other as if plugged into one switch, with internet access through it as usual.

Two cabinets on one PC:

```bash
scripts/new-cabinet.sh second                    # a copy of your cabinet, with its own identity
scripts/loader-option.sh LINKED_GAMES_ENABLED 1  # linked games on in the main cabinet too
MEGA_LOADER_NET=lan make loader-run              # the main cabinet, on the cabinet network
MEGA_LOADER_NET=lan MEGA_LOADER_VAR=$PWD/build/loader/cabinets/second MEGA_LOADER_DISPLAY=56 make loader-run
```

Each cabinet gets its own address on the network (10.0.2.15, .16, …); the last number is its
MegaLink ID. Cabinets find each other on their own. Then start a linkable game on one; the others
are invited to join. `new-cabinet.sh NAME --from-image` starts from the image's original state
instead, which needs the Connection Wizard (wired) once, like any fresh cabinet. Several separate
networks: `MEGA_LOADER_NET=lan:NAME`.

**Cabinets on different PCs** (a LAN, or the internet). One PC is the *hub*, the others join
it; all their cabinets end up on one cabinet network. Put the same password on every PC, in
`cabinet.local.conf`:

```bash
# on the hub PC
MEGA_LAN_LISTEN=4790
MEGA_LAN_PASSWORD="choose-a-long-password"

# on every other PC
MEGA_LAN_CONNECT=hub-pc-name-or-address:4790
MEGA_LAN_PASSWORD="choose-a-long-password"
```

Then start the cabinets with `MEGA_LOADER_NET=lan make loader-run` on each PC (in that order is
easiest, but a PC that starts first keeps trying to reach the hub). Each PC keeps its own
internet access; the hub gives each PC its own range of addresses (hub 10.0.2.16–31, the next
PC .32–47, … up to 14 PCs with up to 16 cabinets each), so every cabinet has a distinct MegaLink
ID. `build/loader/lan/lan.log` shows who joined.

- The hub must be reachable on its port (4790/TCP): open it in the hub's firewall, and forward it
  on the router for joining over the internet. Under **WSL**, other PCs can't reach a hub inside
  WSL by default: turn on WSL's mirrored networking (`networkingMode=mirrored` in `.wslconfig`),
  or let a Linux PC be the hub; WSL PCs can always join a hub.
- The password is checked both ways and never sent; the cabinet traffic between PCs is not
  encrypted. Over the internet, a VPN (Tailscale, ZeroTier, WireGuard) avoids opening a port
  and encrypts it: use the hub's VPN address in `MEGA_LAN_CONNECT`.

Status: the network side works, on one PC and between joined switches (addresses, the internet,
MegaLink finding the other cabinets). Playing a linked game through to the end hasn't been
tried yet; neither has a link over the real internet.

## 18. Where things are

| Path | What |
| --- | --- |
| `cabinet.local.conf` | Your image path |
| `build/loader/var.identity` | This cabinet's serial number and MegaNet ID |
| `build/loader/cabinets/` | Extra cabinets (`scripts/new-cabinet.sh`), each with its own `.identity` |
| `build/loader/lan/` | Cabinet networks: their switches' sockets and logs |
| `build/loader/var/merit/` | The cabinet's state (settings, books, high scores, keys) |
| `build/loader/backups/` | Backups of that state |
| `build/loader/var/merit/logging/logs/` | The cabinet's own logs (`*.running.log` is the current run) |
| `build/loader/root`, `ion`, `home` | The cabinet's software, copied from the image (read-only use) |
| `build/loader/*.log` | Logs of the display and network helpers; `kiosk.log` in kiosk mode |

Technical details (how the simulation works, every setting, environment variables):
[cabinet-loader](cabinet-loader.md) and the [commands reference](../reference/commands.md#cabinet-loader).
