# Quick start: from a bare PC to a running cabinet

About 20 minutes on a fresh machine, most of it downloads. The repository contains no cabinet
software; you need one of these:

| Source | Files | You get |
| --- | --- | --- |
| **The original installer discs** (recommended) | the two ION 2014 CD images, `…V40_02T130830_1115_1OF2.iso` and `…_2OF2.iso` | the factory software, unmodified, and a factory-fresh cabinet |
| The disk image | `Megatouch ION 2014 HDD Keyless.img` (60 GB) | the same software with a patched loader ("keyless"), and the previous owner's settings, books and registration |

Once it runs, the [Operator guide](operator-guide.md) explains everything else.

**Shortcut:** after step 1, this one command does steps 2 to 5 and offers a desktop shortcut.
With the discs:

```bash
bash <(curl -fsSL https://raw.githubusercontent.com/Merit-Megatouch/megatouch-port/main/scripts/install.sh) \
  --disc "/mnt/e/Images/ION 2014 Disc 1 or 2 V40_02T130830_1115_1OF2.iso" \
  --disc "/mnt/e/Images/ION 2014 Disc 2 of 2 V40_02T130830_1115_2OF2.iso"
```

Without `--disc` it asks for the disk image's path instead.

The steps below are what it does, for doing it by hand or understanding a problem.

## 1. A Linux shell

**Windows 10/11:** install WSL2 with Ubuntu. Open PowerShell **as administrator**:

```powershell
wsl --install -d Ubuntu
```

Reboot, open **Ubuntu** from the Start menu and pick a user name. WSLg (the part that shows
Linux windows on the Windows desktop, with sound) comes with current WSL. Check with
`wsl --version` in PowerShell: it should list a WSLg version. If it doesn't, run `wsl --update`.

**Linux:** a recent Ubuntu. Other distributions need `apt-get` and `dpkg-deb`, because setup
downloads Ubuntu packages.

## 2. Host packages (the only step that needs sudo)

```bash
sudo apt update
sudo apt install git gcc g++ make python3 python3-venv curl e2fsprogs binutils bubblewrap
```

| Package | Used for |
| --- | --- |
| `e2fsprogs` | `debugfs`, which reads the cabinet's partitions without mounting them |
| `bubblewrap` | `bwrap`, the rootless sandbox the cabinet runs in |
| `gcc g++ make binutils` | Building the simulated hardware (32-bit support is added locally, no `gcc-multilib`) |
| `python3 python3-venv` | Helper scripts (including the reader for the installer discs) |
| `git curl` | Cloning, downloads |

Nothing else is installed system-wide: everything setup downloads stays inside the repository.

## 3. Clone

```bash
git clone https://github.com/Merit-Megatouch/megatouch-port ~/megatouch-port
cd ~/megatouch-port
```

Clone it **inside the Linux filesystem** (`~/...`), not under `/mnt/c`: the Windows drives are
much slower for the many small files involved.

## 4. Point it at the cabinet software

Create `cabinet.local.conf` in the project folder with a text editor (`nano cabinet.local.conf`).
For the installer discs:

```bash
DISC1="/mnt/e/Images/ION 2014 Disc 1 or 2 V40_02T130830_1115_1OF2.iso"
DISC2="/mnt/e/Images/ION 2014 Disc 2 of 2 V40_02T130830_1115_2OF2.iso"
```

or for the disk image:

```bash
IMG="/mnt/e/path/to/Megatouch ION 2014 HDD Keyless.img"
```

The files can stay on a Windows drive (`E:\` is `/mnt/e/`); they are only read. Keep the
quotes: the names have spaces. `cabinet.local.conf` is not committed. With both set, the discs
win.

## 5. Set up (once)

```bash
make setup           # 32-bit runtime and compiler support (toolchain/, shared/)
make loader-setup    # the cabinet's partitions → build/loader/; display, sound and network helpers
```

`make loader-setup` unpacks the cabinet's root, home and game partitions (about 11 GB) and sets
up `/var`: from the discs, the factory defaults; from the image, the image's own. It also
downloads Xephyr, Xvfb, SDL2 and slirp4netns from Ubuntu into `toolchain/debug/`. Both commands
are safe to re-run; finished steps are skipped, and the cabinet's settings in `build/loader/var`
are never overwritten once they exist.

## 6. Run

```bash
make loader-run
```

The **Megatouch ION (cabinet loader)** window opens. Click to touch; **F1** opens Operator
Setup, **F5–F8** insert coins, **F11** is fullscreen. Close the window to stop.

- **From the disk image:** the attract loop runs after about 20 seconds.
- **From the discs (factory-fresh):** the first start does what a new cabinet did. Touchscreen
  calibration comes first: touch *EXIT* on each screen. Then the *Connection Wizard*: *Next*,
  *Skip*, then ✕ and *Yes*. After that it starts straight into the attract loop.

## Where next

- Setting the cabinet up and looking after it: the [Operator guide](operator-guide.md).
- A Force cabinet (2011) next to the ION: [operator guide §20](operator-guide.md#20-a-force-cabinet-2011).
- Something went wrong: [troubleshooting](troubleshooting.md#cabinet-loader).
- Every command and setting: [commands reference](../reference/commands.md).

## Appendix: standalone game ports (developers)

The older route runs single games without the cabinet. It needs the game submodules, the disk
image (`IMG`) and one more step:

```bash
git submodule update --init      # or clone with --recurse-submodules
make games                       # extract each game's code and assets from the disk image
make run GAME=g_trix             # one game in a window
```

See [porting a game](porting-a-game.md). Operators don't need this.
