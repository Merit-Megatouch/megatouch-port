# 02 — Toolchain setup (no root required)

> `make setup` (scripts/setup.sh) now does all of this into `toolchain/` and `shared/`.
> This chapter explains what it does and why, using the original paths.

Everything below was done as a normal user on Ubuntu 26.04 under WSL2 (Windows 10 19045, WSLg
present). Nothing is installed system-wide.

## Reading the image without mounting

`debugfs` (e2fsprogs) accepts an `?offset=` suffix on the device name:

```bash
IMG="Megatouch ION 2014 HDD Keyless.img"
ROOT="$IMG?offset=$((39584223*512))"      # partition 9  /
ION="$IMG?offset=$((49319613*512))"       # partition 10 /usr/local/ion_only
VAR="$IMG?offset=$((66107538*512))"       # partition 11 /var
HOME_="$IMG?offset=$((39262923*512))"     # partition 8  /home

debugfs -R "ls -l /usr/local/lib" "$ROOT"
debugfs -R "cat /etc/fstab" "$ROOT"
debugfs -R "dump /usr/local/lib/g_trix.so out.so" "$ROOT"
debugfs -R "rdump /games/g_trix ." "$ION"        # recursive (ownership warnings are harmless)
debugfs -R "stat /path" "$ROOT" | grep 'Fast link dest'   # read a symlink target
```

`scripts/lib/extract-libs.sh <outdir> <lib>...` extracts libraries from the root partition and follows
their `NEEDED` entries recursively, resolving symlinks inside the image and skipping the
glibc/libstdc++ family (the modern runtime supplies those).

## 32-bit C/C++ compiler

The host gcc is x86-64 only. Download the multilib *packages* (they are amd64 packages, so
`apt-get download` works without root) and unpack them locally:

```bash
mkdir -p ~/trix-port/{debs,sysroot}; cd ~/trix-port/debs
apt-get download libc6-dev-i386 libc6-i386 lib32stdc++-15-dev lib32gcc-15-dev \
                 lib32stdc++6 lib32gcc-s1 lib32asan8 lib32ubsan1 lib32atomic1 \
                 lib32gomp1 lib32itm1 lib32quadmath0
for d in *.deb; do dpkg-deb -x $d ../sysroot; done
```

Fix the absolute paths in the `libc.so` linker script to point into the sysroot:

```bash
SR=~/trix-port/sysroot
sed -i "s#/usr/lib32/#$SR/usr/lib32/#g; s#/lib/ld-linux.so.2#$SR/usr/lib32/ld-linux.so.2#" \
    $SR/usr/lib32/libc.so
```

Wrapper `~/trix-port/g++32`:

```sh
#!/bin/sh
SR=$HOME/trix-port/sysroot
exec g++ -m32 -B$SR/usr/lib32 -B$SR/usr/lib/gcc/x86_64-linux-gnu/15/32 \
  -L$SR/usr/lib32 -L$SR/usr/lib/gcc/x86_64-linux-gnu/15/32 \
  -isystem $SR/usr/include/x86_64-linux-gnu/c++/15/32 \
  -idirafter /usr/include/x86_64-linux-gnu -idirafter $SR/usr/include/x86_64-linux-gnu \
  -Wl,--dynamic-linker=$SR/usr/lib32/ld-linux.so.2 -Wl,-rpath-link,$SR/usr/lib32 "$@"
```

Gotchas found on the way:
* With `-m32`, gcc's multiarch include dir becomes `i386-linux-gnu`; Ubuntu's biarch headers
  live in `x86_64-linux-gnu`, so it must be added with `-idirafter` (after the C++ config dir).
* Don't put the sysroot's `usr/include` before the system one — it shadows `bits/wordsize.h`.
* `/lib/ld-linux.so.2` doesn't exist on the host; run binaries through the sysroot loader
  explicitly (`$SR/usr/lib32/ld-linux.so.2 ./prog`) — that's what the launcher script does.

**C++ ABI:** the 2013 engine was built with GCC 4.1 (copy-on-write `std::string`, list without
size). Compile everything that shares C++ objects with it using
`-D_GLIBCXX_USE_CXX11_ABI=0`. Modern 32-bit `libstdc++.so.6` still runs the old binaries.

## 32-bit SDL2 and friends (private apt)

Ubuntu still publishes i386 SDL2. Use a private apt state directory to resolve and download:

```bash
mkdir -p ~/trix-port/apt32/{lists/partial,cache/archives/partial,etc/preferences.d}
cd ~/trix-port/apt32
cat > etc/sources.list <<'EOF'
deb [arch=i386] http://archive.ubuntu.com/ubuntu resolute main universe
deb [arch=i386] http://archive.ubuntu.com/ubuntu resolute-updates main universe
EOF
cat > apt.conf <<EOF
APT::Architecture "i386";
APT::Architectures { "i386"; };
Dir::State "$PWD";  Dir::State::Lists "$PWD/lists";  Dir::State::status "$PWD/status";
Dir::Cache "$PWD/cache";  Dir::Etc::SourceList "$PWD/etc/sources.list";  Dir::Etc::SourceParts "-";
Dir::Etc::Preferences "$PWD/etc/preferences";  Dir::Etc::PreferencesParts "$PWD/etc/preferences.d";
Dir::Etc::Trusted "/etc/apt/trusted.gpg";  Dir::Etc::TrustedParts "/etc/apt/trusted.gpg.d";
EOF
touch status; export APT_CONFIG=$PWD/apt.conf; apt-get update
apt-cache depends --recurse --no-recommends --no-suggests --no-conflicts --no-breaks \
   --no-replaces --no-enhances libsdl2-2.0-0 libsdl2-image-2.0-0 | grep -v '^ ' | grep -v '<'
# download each name individually (one bad name aborts a multi-package download):
for p in <list>; do apt-get download $p || echo skip $p; done
for d in cache/*.deb; do dpkg-deb -x $d ~/trix-port/i386root; done
```

Notes: there is no i386 SDL2_mixer anymore — sound is mixed by hand, OGG decoded with
single-file `stb_vorbis.c`. Also grab `libsdl2-dev`, `libsdl2-image-dev`, `zlib1g-dev` for headers.
When linking, add `-Wl,--allow-shlib-undefined` and put `.../i386-linux-gnu/pulseaudio` on
`rpath-link` (libpulse's private `libpulsecommon` lives there).

Smoke test: a 32-bit SDL2 window on WSLg reports `video=x11 renderer=opengl audio=pulseaudio`.
OpenGL is Mesa **llvmpipe** (software) — WSLg's GPU path is 64-bit only. That turned out to be
fast enough (≈4 ms/frame at 1280×800).

## Ghidra headless

```bash
cd ~/trix-port/tools
curl -L -o jdk.tgz "https://github.com/adoptium/temurin21-binaries/releases/download/jdk-21.0.12.1%2B1/OpenJDK21U-jdk_x64_linux_hotspot_21.0.12.1_1.tar.gz"
tar xzf jdk.tgz
curl -L -o ghidra.zip https://github.com/NationalSecurityAgency/ghidra/releases/download/Ghidra_12.1.4_build/ghidra_12.1.4_PUBLIC_20260921.zip
python3 -c "import zipfile; zipfile.ZipFile('ghidra.zip').extractall('.')"
# python's zipfile drops exec bits:
chmod +x ghidra_*/support/* ghidra_*/ghidraRun
find ghidra_* -path '*os/linux_x86_64/*' -type f -exec chmod +x {} +

export JAVA_HOME=$PWD/jdk-21.0.12.1+1 PATH=$JAVA_HOME/bin:$PATH
ghidra_12.1.4_PUBLIC/support/analyzeHeadless ~/trix-port/ghproj trix \
   -import ~/trix-port/bin/*.so -scriptPath ~/trix-port/tools \
   -postScript DecompileAll.java ~/trix-port/decomp -overwrite
```

`tools/DecompileAll.java` writes one `<lib>.c` per library with every function decompiled.
**Ghidra loads shared objects at base 0x10000**: subtract 0x10000 from addresses it prints
before looking them up in the ELF.

## Python helpers

```bash
python3 -m venv ~/trix-port/venv && ~/trix-port/venv/bin/pip install pyelftools capstone
```

| Tool | Purpose |
|---|---|
| `tools/vtdump.py lib.so [filter]` | Dumps every exported vtable, resolving each slot via relocations (R_386_32 → symbol, R_386_RELATIVE → local address → dynsym). Shows `__cxa_pure_virtual` slots. |
| `tools/sprdump.py file.spr.gz out [frames] [--mode N]` | Decodes `.spr` frames to PNG over a checkerboard |
| `tools/profreport.py out.prof [game.log]` | Summarizes profiler samples and hitch windows |
| `scripts/lib/extract-libs.sh` | Recursive library extraction from the cabinet (image or snapshot) |

Also handy: `objdump -d -R -C -M intel --no-show-raw-insn lib.so` (keep `-R` to see which
PLT/GOT entries calls go to), `nm -DC --defined-only`, `nm -D --undefined-only`, `readelf -d`.
