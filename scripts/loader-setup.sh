#!/bin/bash
# Prepare the cabinet's own loader to run (make loader-setup). Nothing is installed system-wide.
#
#   1. build/loader/{root,var,home,ion}: the cabinet's root, var, home and ion_only partitions,
#      plus var.orig, a pristine copy of /var for `make loader-reset`. From the installer discs
#      (DISC1, DISC2 … in cabinet.local.conf: factory software, /var from the factory defaults)
#      or else from the disk image (IMG: the image's sideB partitions).
#      build/force/{root,var,home}: a Force cabinet (2007.5-2011 software) from its disc
#      (FORCE_DISC1 …), the same way; run it with `make loader-run CABINET=force`
#      build/maxx/{root,var,home}: a Maxx cabinet (2005 Crown software) from its three discs
#      (MAXX_DISC1 … MAXX_DISC3); run it with `make loader-run CABINET=maxx`
#   2. toolchain/debug: Xephyr (+ xkbcomp), slirp4netns and SDL2 downloaded with apt-get download; Xephyr's RandR size table
#      patched to offer the cabinet's widescreen modes (768x480, 1280x800); pactl for checking sound
#   3. build/loader/bin: our stand-ins (make loader builds them)
# Needs: cabinet.conf / cabinet.local.conf with DISC1… or IMG (and/or FORCE_DISC1…, MAXX_DISC1…), debugfs, apt-get, dpkg-deb, bwrap, setsid.
#
#   scripts/loader-setup.sh [--replace-root]
#   --replace-root  with discs set: replace an existing build/loader/root (e.g. from the keyless
#                   image) with the factory one from the discs; the old one is kept as root.image.
#                   /var and /home are kept, so the cabinet keeps its settings, books and identity.
set -euo pipefail
P=$(cd "$(dirname "$0")/.." && pwd)
. "$P/cabinet.conf"
B="$P/build/loader"
D="$P/toolchain/debug"
step() { printf '\n== %s\n' "$*"; }

for t in debugfs apt-get dpkg-deb bwrap setsid python3; do
  command -v "$t" >/dev/null || { echo "missing tool: $t" >&2; exit 1; }
done

REPLACE_ROOT=0
[ "${1:-}" = --replace-root ] && REPLACE_ROOT=1
disc_list() {   # disc_list <prefix>: the files in <prefix>1..4 (DISC1…, FORCE_DISC1…, MAXX_DISC1…)
  local v
  for v in "$1"1 "$1"2 "$1"3 "$1"4; do
    if [ -n "${!v:-}" ]; then printf '%s\n' "${!v}"; fi
  done
}
from_disc() {   # from_disc <partition image number> <dir>: never touches an existing directory
  if [ -d "$C/$2" ] && [ -n "$(ls -A "$C/$2")" ]; then echo "  $2: present"; return; fi
  local img="$C/disc-part$1.img"
  python3 "$P/scripts/partimage.py" extract "$1" "$img" "${discs[@]}"
  rm -rf "${C:?}/$2.partial"; mkdir -p "$C/$2.partial"
  debugfs -R "rdump / $C/$2.partial" "$img" 2>&1 | grep -v "changing ownership\|^debugfs" || true
  rm -f "$img"
  rm -rf "${C:?}/$2" && mv "$C/$2.partial" "$C/$2"
  if [ "$2" = root ]; then printf '%s\n' "${discs[@]}" > "$C/root.source"; fi
  echo "  $2: $(du -sh "$C/$2" | cut -f1)"
}
factory_var() {   # /var as a fresh install has it after its first boot
  if [ -d "$C/var" ]; then echo "  var: present"; return; fi
  # a fresh install's /var is empty; the first boot restores the factory defaults from
  # /factorybackup/var.tar.gz (merit-checkfs on ION; a Force does the same)
  [ -f "$C/root/factorybackup/var.tar.gz" ] || { echo "  no /factorybackup/var.tar.gz on these discs: a release this setup can't read yet" >&2; exit 1; }
  rm -rf "${C:?}/var.partial"; mkdir -p "$C/var.partial"
  tar -xzf "$C/root/factorybackup/var.tar.gz" -C "$C/var.partial" --strip-components=1 --no-same-owner
  mkdir -p "$C/var.partial/log"
  mv "$C/var.partial" "$C/var"
  echo "  var: factory defaults"
}
cabinet_from_discs() {   # cabinet_from_discs <dir> <ion|force|maxx> <disc>…
  local C="$1" want="$2"; shift 2
  local discs=("$@") parts is
  mkdir -p "$C"
  step "Cabinet partitions from the installer discs → ${C#$P/}"
  parts=$(python3 "$P/scripts/partimage.py" info "${discs[@]}" | tee /dev/stderr | sed -n 's/.*images: //p' | tr '\n' ' ')
  case " $parts " in
    *" part5 "*) ;;
    *) echo "  these discs have no root partition image (part5): an older release this setup can't read yet" >&2; exit 1 ;;
  esac
  if [ "$want" = ion ] && [ $REPLACE_ROOT = 1 ] && [ -d "$C/root" ] && [ ! -f "$C/root.source" ]; then
    [ -e "$C/root.image" ] && { echo "${C#$P/}/root.image exists already; move it away first" >&2; exit 1; }
    pgrep -x start >/dev/null && { echo "stop the cabinet first" >&2; exit 1; }
    mv "$C/root" "$C/root.image"
    echo "  the image's root kept as ${C#$P/}/root.image"
  fi
  from_disc 5 root
  # the software decides, not the setting the discs came from: only an ION has the layout daemon,
  # only the Maxx (2005) software has the /usr/local/share/forceonly mount point
  is=force
  if [ -e "$C/root/usr/local/bin/layout_daemon" ]; then is=ion; fi
  if [ -d "$C/root/usr/local/share/forceonly" ]; then is=maxx; fi
  if [ "$is" != "$want" ]; then
    local var=DISC1…
    case "$is" in force) var=FORCE_DISC1… ;; maxx) var=MAXX_DISC1… ;; esac
    echo "  these discs hold $is software, not $want: set them as $var instead" >&2
    echo "  (delete ${C#$P/}/root, extracted from them, before trying again)" >&2
    exit 1
  fi
  from_disc 2 home
  if [ "$is" = ion ]; then from_disc 6 ion; fi    # /usr/local/ion_only: the ION's game assets
  # the Maxx discs keep some directories without read permission (debugfs copies the modes)
  if [ "$is" = maxx ]; then chmod -R u+rwX "$C/root" "$C/home"; fi
  factory_var
  rm -f "$C/var/merit/swapfile"
  [ -d "$C/var.orig" ] || cp -a "$C/var" "$C/var.orig"
}

mapfile -t discs < <(disc_list DISC)
mapfile -t force_discs < <(disc_list FORCE_DISC)
mapfile -t maxx_discs < <(disc_list MAXX_DISC)
for f in "${discs[@]}" "${force_discs[@]}" "${maxx_discs[@]}"; do
  [ -f "$f" ] || { echo "no such disc image: $f (cabinet.local.conf)" >&2; exit 1; }
done
mkdir -p "$B"
# a Force cabinet (2007-2011 software) from its disc, next to the ION's: make loader-run CABINET=force
if [ ${#force_discs[@]} -gt 0 ]; then cabinet_from_discs "$P/build/force" force "${force_discs[@]}"; fi
# a Maxx cabinet (2005 Crown software, all three discs): make loader-run CABINET=maxx
if [ ${#maxx_discs[@]} -gt 0 ]; then cabinet_from_discs "$P/build/maxx" maxx "${maxx_discs[@]}"; fi
if [ ${#discs[@]} -gt 0 ]; then
cabinet_from_discs "$B" ion "${discs[@]}"
elif [ -z "$IMG" ] && [ $((${#force_discs[@]} + ${#maxx_discs[@]})) -gt 0 ]; then
  :   # only a Force and/or Maxx cabinet
else
step "Cabinet partitions → build/loader"
[ -n "$IMG" ] && [ -f "$IMG" ] || { echo "set IMG (the disk image) or DISC1/DISC2 (the installer discs) in cabinet.local.conf" >&2; exit 1; }
dump() {   # dump <partition offset> <dir>; an existing directory is never touched (var holds
           # the loader's settings, NVRAM and books: `make loader-reset` restores it on purpose)
  if [ -d "$B/$2" ] && [ -n "$(ls -A "$B/$2")" ]; then echo "  $2: present"; return; fi
  mkdir -p "$B/$2.partial"
  debugfs -R "rdump / $B/$2.partial" "$IMG?offset=$1" 2>&1 | grep -v "changing ownership\|^debugfs" || true
  rm -rf "${B:?}/$2" && mv "$B/$2.partial" "$B/$2"
  echo "  $2: $(du -sh "$B/$2" | cut -f1)"
}
dump "$ROOT_OFF" root
dump "$VAR_OFF" var
dump "$HOME_OFF" home
dump "$ION_OFF" ion      # /usr/local/ion_only, including content/ (the snapshot leaves that out)
rm -f "$B/var/merit/swapfile"
[ -d "$B/var.orig" ] || cp -a "$B/var" "$B/var.orig"
fi

step "Xephyr, xkbcomp, pactl, slirp4netns and SDL2 (for megaview) → toolchain/debug"
mkdir -p "$D/debs"
PKGS="xserver-xephyr libxfont2 libfontenc1 libxcb-shape0 libxcb-render-util0 libxcb-util1
      libxcb-image0 libxcb-icccm4 libxcb-keysyms1 libxcb-xkb1 libxcb-xv0 x11-xkb-utils libxkbfile1
      pulseaudio-utils libpulse0 libsndfile1 libflac14 libvorbisenc2 libvorbis0a libogg0 libopus0
      libmpg123-0t64 libmp3lame0 libasyncns0 libapparmor1 libpulse-dev slirp4netns libslirp0
      libsdl2-dev libsdl2-classic libasound2t64 libsamplerate0 libxss1 libdecor-0-0 xvfb"
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
for server in Xephyr Xvfb; do
  # $server from the downloaded Ubuntu package (toolchain/debug/root). The server runs
  # /usr/bin/xkbcomp, which the host lacks, so inside a bwrap /usr/bin is replaced by a symlink
  # copy of itself (pointing at the real one, mounted at $D/hostbin) plus our xkbcomp.
  #   MEGA_X11_DIR  directory to use as /tmp/.X11-unix (the loader sandbox shares it)
  { echo '#!/bin/sh'
    echo "# $server from toolchain/debug/root, run with our xkbcomp (see scripts/loader-setup.sh)."
    echo '#   MEGA_X11_DIR  directory to use as /tmp/.X11-unix (the loader sandbox shares it)'
    cat <<'WRAP'
D=$(cd "$(dirname "$0")" && pwd -P)
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
  --setenv LD_LIBRARY_PATH "$D/root/usr/lib/x86_64-linux-gnu" "$D/root/usr/bin/SERVER" "$@"
WRAP
  } | sed "s#/usr/bin/SERVER\"#/usr/bin/$server\"#" > "$D/$server"
done
cat > "$D/pactl" <<'EOF'
#!/bin/sh
D=$(cd "$(dirname "$0")" && pwd -P)
LD_LIBRARY_PATH="$D/root/usr/lib/x86_64-linux-gnu:$D/root/usr/lib/x86_64-linux-gnu/pulseaudio" exec "$D/root/usr/bin/pactl" "$@"
EOF
chmod +x "$D/Xephyr" "$D/Xvfb" "$D/pactl"

step "Sandbox network check"
. "$P/scripts/lib/sandbox-check.sh"
if sandbox_net_check full; then echo "  ok: the cabinet's sandbox gets its own network"; else echo "  the cabinets will run, but without a network until this is fixed" >&2; fi
step "Done. Next: make loader && make loader-run"
