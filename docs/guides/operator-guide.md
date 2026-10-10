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
- [16. Where things are](#16-where-things-are)

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

All commands below are typed in the Ubuntu window.

### 2.2 Packages (the only step that needs your password)

```bash
sudo apt update
sudo apt install git gcc g++ make python3 python3-venv curl e2fsprogs binutils bubblewrap
```

### 2.3 Get the project

```bash
git clone https://github.com/Merit-Megatouch/megatouch-port ~/megatouch-port
cd ~/megatouch-port
```

Keep it in your Linux home folder (`~`), not under `/mnt/c` or `/mnt/e`: those are much slower.

### 2.4 Tell it where the image is

```bash
echo 'IMG="/mnt/e/path/to/Megatouch ION 2014 HDD Keyless.img"' > cabinet.local.conf
```

Windows drives appear under `/mnt/` (`E:\Images\x.img` is `/mnt/e/Images/x.img`). Keep the
quotes: the file name has spaces.

### 2.5 Build and extract (once, 10–20 minutes, mostly downloads)

```bash
make setup           # 32-bit runtime and compiler support
make loader-setup    # copies the cabinet's software out of the image, downloads the display
                     # and network helpers
```

Both are safe to run again; finished steps are skipped. `make loader-setup` never overwrites
your cabinet's settings once they exist.

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

**Windows shortcut** (optional): a desktop shortcut with this target starts it with one click
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
   *Network Summary*).

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

```bash
cd ~/megatouch-port
git pull
make loader-setup    # only fetches what's new (if anything); your settings are kept
make loader-run
```

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
| Something else | [Troubleshooting](troubleshooting.md#cabinet-loader) |

## 16. Where things are

| Path | What |
| --- | --- |
| `cabinet.local.conf` | Your image path |
| `build/loader/var/merit/` | The cabinet's state (settings, books, high scores, keys) |
| `build/loader/backups/` | Backups of that state |
| `build/loader/var/merit/logging/logs/` | The cabinet's own logs (`*.running.log` is the current run) |
| `build/loader/root`, `ion`, `home` | The cabinet's software, copied from the image (read-only use) |
| `build/loader/*.log` | Logs of the display and network helpers |

Technical details (how the simulation works, every setting, environment variables):
[cabinet-loader](cabinet-loader.md) and the [commands reference](../reference/commands.md#cabinet-loader).
