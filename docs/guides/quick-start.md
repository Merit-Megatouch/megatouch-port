# Quick start: from nothing to playing Trix

This takes about 15 minutes on a fresh machine, most of it downloads. You need the cabinet disk
image (`Megatouch ION 2014 HDD Keyless.img`, 60 GB) or a snapshot made from it. The repository
contains no cabinet code or assets.

## 1. A Linux shell

**Windows 10/11:** install WSL2 with Ubuntu. Open PowerShell as administrator:

```powershell
wsl --install -d Ubuntu
```

Reboot, open "Ubuntu" from the Start menu and pick a user name. WSLg (the part that shows Linux
windows on the Windows desktop, with sound) comes with current WSL. Check with `wsl --version`
in PowerShell: it should list a WSLg version. If it doesn't, run `wsl --update`.

**Linux:** any recent Ubuntu works. Other distributions need `apt-get` and `dpkg-deb`, because
setup downloads Ubuntu's i386 packages.

## 2. Host packages (the only step that needs sudo)

```bash
sudo apt update
sudo apt install git gcc g++ make python3 python3-venv curl e2fsprogs binutils
```

What each one is for:

| Package | Used for |
| --- | --- |
| `gcc g++` | Building the 32-bit host and backend. Setup adds 32-bit support locally, so `gcc-multilib` is not needed. |
| `make` | Every command |
| `python3 python3-venv` | Helper tools, and a private venv with pyelftools and capstone |
| `curl` | Ghidra download (optional) |
| `e2fsprogs` | `debugfs`, which reads the image's ext3 partitions without mounting them |
| `binutils` | `readelf`, `nm`, `objdump`, `c++filt` |
| `git` | Cloning |

## 3. Clone

```bash
git clone --recurse-submodules https://github.com/Merit-Megatouch/megatouch-port ~/megatouch-port
cd ~/megatouch-port
```

Clone it **inside the Linux filesystem** (`~/...`), not under `/mnt/c`. The Windows drives are
10–50× slower for the many small files involved, and their 64-bit inode numbers are exactly what
the old engine trips over.

If you cloned without `--recurse-submodules`, run `git submodule update --init`.

## 4. Point it at the cabinet image

```bash
echo 'IMG="/mnt/e/path/to/Megatouch ION 2014 HDD Keyless.img"' > cabinet.local.conf
```

The image can stay on a Windows drive: it is only read. `cabinet.local.conf` is gitignored.
If you have a snapshot folder instead (see [make snapshot](../reference/commands.md#make-snapshot)),
put it at `./cabinet/` and skip this step.

## 5. Set up and build (once, about 3 minutes)

```bash
make setup
```

This builds, all inside the repository and with no root:

- `toolchain/`: 32-bit gcc support, i386 SDL2/Mesa/PulseAudio packages and a Python venv.
- `shared/runtime/`: the 32-bit runtime that games run on.
- `shared/data-common/`: the cabinet data every game uses (fonts, translations, config, Pango modules).
- `shared/bin/`: the host program and the SDL2 backend.

It is safe to re-run; finished steps are skipped. Details are in
[reference/commands.md](../reference/commands.md#make-setup).

## 6. Extract the games and play

```bash
make games               # for each games/<name>: extract its code and assets from the cabinet
make run GAME=g_trix     # a 1280×800 window opens
```

Controls: click or tap with the mouse. **F11** toggles fullscreen. Close the window to quit. The
window can be resized, and the picture scales with the aspect ratio kept.

## 7. Optional: Windows shortcut

Make a desktop shortcut with this target:

```
C:\Windows\System32\wsl.exe ~/megatouch-port/games/g_trix/run
```

To have no console window, use `wslg.exe` instead of `wsl.exe` (it ships with current WSL).

## 8. Optional: copy a game somewhere else

```bash
make package GAME=g_trix DEST=~/trix-standalone
```

This makes a folder with every symlink resolved: runtime, libraries, data and a `run` script.
Copy it to any x86-64 Linux or WSL2 machine and start `./run`. It does not need the repository
or the image.

## Where next

- Something went wrong: [troubleshooting](troubleshooting.md).
- Port another game: [porting a game](porting-a-game.md).
- What every command and setting does: [commands reference](../reference/commands.md).
