# Quick start: from a bare PC to a running cabinet

About 20 minutes on a fresh machine, most of it downloads. You need the cabinet disk image
(`Megatouch ION 2014 HDD Keyless.img`, 60 GB). The repository contains no cabinet software.

Once it runs, the [Operator guide](operator-guide.md) explains everything else.

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
| `e2fsprogs` | `debugfs`, which reads the image's partitions without mounting them |
| `bubblewrap` | `bwrap`, the rootless sandbox the cabinet runs in |
| `gcc g++ make binutils` | Building the simulated hardware (32-bit support is added locally, no `gcc-multilib`) |
| `python3 python3-venv` | Helper scripts |
| `git curl` | Cloning, downloads |

Nothing else is installed system-wide: everything setup downloads stays inside the repository.

## 3. Clone

```bash
git clone https://github.com/Merit-Megatouch/megatouch-port ~/megatouch-port
cd ~/megatouch-port
```

Clone it **inside the Linux filesystem** (`~/...`), not under `/mnt/c`: the Windows drives are
much slower for the many small files involved.

## 4. Point it at the cabinet image

```bash
echo 'IMG="/mnt/e/path/to/Megatouch ION 2014 HDD Keyless.img"' > cabinet.local.conf
```

The image can stay on a Windows drive (`E:\` is `/mnt/e/`); it is only read. Keep the quotes,
the name has spaces. `cabinet.local.conf` is not committed.

## 5. Set up (once)

```bash
make setup           # 32-bit runtime and compiler support (toolchain/, shared/)
make loader-setup    # the cabinet's partitions → build/loader/; display, sound and network helpers
```

`make loader-setup` copies the cabinet's root, var, home and game partitions out of the image
(about 11 GB) and downloads Xephyr, Xvfb, SDL2 and slirp4netns from Ubuntu into `toolchain/debug/`.
Both commands are safe to re-run; finished steps are skipped, and the cabinet's settings in
`build/loader/var` are never overwritten once they exist.

## 6. Run

```bash
make loader-run
```

The **Megatouch ION (cabinet loader)** window opens; after about 20 seconds the attract loop
runs. Click to touch. **F1** opens Operator Setup, **F5–F8** insert coins, **F11** is fullscreen.
Close the window to stop.

## Where next

- Setting the cabinet up and looking after it: the [Operator guide](operator-guide.md).
- Something went wrong: [troubleshooting](troubleshooting.md#cabinet-loader).
- Every command and setting: [commands reference](../reference/commands.md).

## Appendix: standalone game ports (developers)

The older route runs single games without the cabinet. It needs the game submodules and one more
step:

```bash
git submodule update --init      # or clone with --recurse-submodules
make games                       # extract each game's code and assets from the image
make run GAME=g_trix             # one game in a window
```

See [porting a game](porting-a-game.md). Operators don't need this.
