#!/bin/bash
# Prepare the cabinet's own loader to run (make loader-setup). Nothing is installed system-wide.
#
#   1. build/loader/{root,var,home}: the image's sideB root, var and home partitions (debugfs),
#      plus var.orig, a pristine copy of /var for `make loader-reset`
#   2. toolchain/debug: Xephyr (+ xkbcomp) downloaded with apt-get download, its RandR size table
#      patched to offer the cabinet's widescreen modes (768x480, 1280x800); pactl for checking sound
#   3. build/loader/bin: our stand-ins (make loader builds them)
# Needs: cabinet.conf / cabinet.local.conf with IMG, debugfs, apt-get, dpkg-deb, bwrap, setsid.
set -euo pipefail
P=$(cd "$(dirname "$0")/.." && pwd)
. "$P/cabinet.conf"
B="$P/build/loader"
D="$P/toolchain/debug"
step() { printf '\n== %s\n' "$*"; }

for t in debugfs apt-get dpkg-deb bwrap setsid python3; do
  command -v "$t" >/dev/null || { echo "missing tool: $t" >&2; exit 1; }
done

step "Cabinet partitions → build/loader"
[ -n "$IMG" ] && [ -f "$IMG" ] || { echo "set IMG in cabinet.local.conf" >&2; exit 1; }
mkdir -p "$B"
dump() {   # dump <partition offset> <dir>
  [ -e "$B/$2/.done" ] && { echo "  $2: present"; return; }
  rm -rf "${B:?}/$2"; mkdir -p "$B/$2"
  debugfs -R "rdump / $B/$2" "$IMG?offset=$1" 2>&1 | grep -v "changing ownership\|^debugfs" || true
  touch "$B/$2/.done"
  echo "  $2: $(du -sh "$B/$2" | cut -f1)"
}
dump "$ROOT_OFF" root
dump "$VAR_OFF" var
dump "$HOME_OFF" home
rm -f "$B/var/merit/swapfile"
[ -d "$B/var.orig" ] || cp -a "$B/var" "$B/var.orig"

step "Xephyr, xkbcomp and pactl → toolchain/debug"
mkdir -p "$D/debs"
PKGS="xserver-xephyr libxfont2 libfontenc1 libxcb-shape0 libxcb-render-util0 libxcb-util1
      libxcb-image0 libxcb-icccm4 libxcb-keysyms1 libxcb-xkb1 libxcb-xv0 x11-xkb-utils libxkbfile1
      pulseaudio-utils libpulse0 libsndfile1 libflac14 libvorbisenc2 libvorbis0a libogg0 libopus0
      libmpg123-0t64 libmp3lame0 libasyncns0 libapparmor1 libpulse-dev"
(cd "$D/debs" && for p in $PKGS; do
   ls "${p}"_*.deb >/dev/null 2>&1 || apt-get download "$p" >/dev/null 2>&1 || echo "  (skip $p)"; done)
for d in "$D"/debs/*.deb; do dpkg-deb -x "$d" "$D/root"; done
X="$D/root/usr/bin/Xephyr"
python3 - "$X" <<'EOF'
# Xephyr's RandR only offers a fixed size list; the cabinet switches between 640x480, 768x480,
# 800x600 and 1280x800. Replace 832x624 with 1280x800 and 720x400 with 768x480 in that table.
import struct, sys
p = sys.argv[1]; d = bytearray(open(p, 'rb').read())
table = struct.pack('<6i', 832, 624, 800, 600, 720, 400)
done = struct.pack('<6i', 1280, 800, 800, 600, 768, 480)
if d.find(done) >= 0:
    print('  Xephyr size table: already patched')
else:
    i = d.find(table)
    if i < 0: sys.exit('  Xephyr size table not found: unknown Xephyr build')
    d[i:i + 24] = done
    open(p + '.new', 'wb').write(d)
    import os; os.chmod(p + '.new', 0o755); os.replace(p + '.new', p)
    print('  Xephyr size table patched at', hex(i))
EOF
cat > "$D/Xephyr" <<'EOF'
#!/bin/sh
# Xephyr from the downloaded Ubuntu package (toolchain/debug/root). The server runs
# /usr/bin/xkbcomp, which the host lacks, so inside a bwrap /usr/bin is replaced by a symlink
# copy of itself (pointing at the real one, mounted at $D/hostbin) plus our xkbcomp.
#   MEGA_X11_DIR  directory to use as /tmp/.X11-unix (the loader sandbox shares it)
D=$(cd "$(dirname "$0")" && pwd)
B="$D/usrbin"
mkdir -p "$D/hostbin"
if [ ! -e "$B/.done" ]; then
  rm -rf "$B"; mkdir -p "$B"
  for f in /usr/bin/*; do ln -s "$D/hostbin/$(basename "$f")" "$B/"; done
  ln -sf "$D/root/usr/bin/xkbcomp" "$B/xkbcomp"
  touch "$B/.done"
fi
X11=${MEGA_X11_DIR:?set MEGA_X11_DIR to the socket directory}
exec bwrap --die-with-parent --dev-bind / / --ro-bind /usr/bin "$D/hostbin" --ro-bind "$B" /usr/bin \
  --tmpfs /tmp --bind "$X11" /tmp/.X11-unix \
  --bind "$(readlink -f /tmp/.X11-unix)/X0" /tmp/.X11-unix/X0 \
  --setenv LD_LIBRARY_PATH "$D/root/usr/lib/x86_64-linux-gnu" "$D/root/usr/bin/Xephyr" "$@"
EOF
cat > "$D/pactl" <<'EOF'
#!/bin/sh
D=$(cd "$(dirname "$0")" && pwd)
LD_LIBRARY_PATH="$D/root/usr/lib/x86_64-linux-gnu:$D/root/usr/lib/x86_64-linux-gnu/pulseaudio" exec "$D/root/usr/bin/pactl" "$@"
EOF
chmod +x "$D/Xephyr" "$D/pactl"

step "Done. Next: make loader && make loader-run"
