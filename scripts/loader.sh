#!/bin/bash
# Run the cabinet's own loader in a rootless sandbox built from the disk image.
#
#   scripts/loader.sh                  start the loader the way /home/maxx/.xinitrc does
#   scripts/loader.sh shell            a shell inside the sandbox (cabinet userland)
#   scripts/loader.sh run <cmd> ...    run any command inside the sandbox
#
# Layout (made by `make loader-setup`, all under build/loader/, git-ignored):
#   root/  the sideB-root partition      → /
#   var/   the sideB-var partition       → /var       (writable: settings, NVRAM, logs)
#   home/  the sideB-home partition      → /home
#   ion/   the sideB-ion_only partition  → /usr/local/ion_only (falls back to cabinet/ion)
#   shared/runtime (modern i386 glibc, Mesa, X11)        → /opt/rt; its ld-linux.so.2 also
#     replaces /lib/ld-linux.so.2 so every cabinet program runs on the modern glibc
#   bin/   our fake I/O board (libusb-1.0.so.0), megaio  → /opt/fakeio
#   bin/ossfake.so           → OSS /dev/dsp and /dev/mixer (the loader's Allegro, the games'
#                              FMOD), played through PulseAudio
#   bin/pci-devices.ion945gc → /proc/bus/pci/devices: the ION board's chipset, so the loader
#                              identifies the platform as ION instead of the older Force
# Settings (environment):
#   MEGA_LOADER_BIN   program to start (default /usr/local/bin/start)
#   MEGAIO_TRACE=1    log every I/O board command
#   MEGA_LOADER_X=host   draw straight on the desktop's X server instead of a nested Xephyr
#                        (the cabinet changes resolution per game, which only Xephyr allows)
#   MEGA_LOADER_DISPLAY  nested display number (default 55)
#   MEGA_LOADER_VIEW     megaview (default: scalable window, F11 fullscreen) or xephyr (Xephyr's
#                        own window, always the cabinet's exact resolution)
#   MEGA_LOADER_IDENTITY=off  don't apply this cabinet's identity (scripts/loader-identity.sh)
#   MEGA_LOADER_BACKUP   auto (default: a snapshot at every start), daily or none
#   MEGA_LOADER_KEY=none no security-key image (scripts/loader-key.sh makes one when missing)
#   MEGA_LOADER_NET      slirp (default: own network namespace + virtual eth0), host, or
#                        lan[:NAME] (a network shared with other cabinets: linked play)
#   MEGA_LOADER_VAR      directory used as /var (default build/loader/var); a second session
#                        needs its own (e.g. a copy of build/loader/var.orig) and display
set -euo pipefail
P=$(cd "$(dirname "$0")/.." && pwd)
B="$P/build/loader"
[ -d "$B/root/usr/local/bin" ] || { echo "no cabinet root: run make loader-setup" >&2; exit 1; }
[ -f "$B/bin/libusb-1.0.so.0" ] || { echo "no fake I/O board: run make loader" >&2; exit 1; }

ION="$B/ion"                                   # the full ion_only partition (make loader-setup)
[ -d "$ION/games" ] || ION="$P/cabinet/ion"     # else the porting snapshot (no content/ packs)
X11=/tmp/.X11-unix
[ -L "$X11" ] && X11=$(readlink -f "$X11")
DISP="${DISPLAY:-:0}"
XEPHYR_PID=
VIEW_PID=
XVFB_PID=
if [ "${MEGA_LOADER_X:-xephyr}" != host ] && [ "${1:-}" != run ] && [ "${1:-}" != shell ]; then
  # A nested X server: its own socket directory, shared with the sandbox as /tmp/.X11-unix.
  N="${MEGA_LOADER_DISPLAY:-55}"
  X11="$B/x11"
  mkdir -p "$X11"
  rm -f "$X11/X$N"
  VIEW="${MEGA_LOADER_VIEW:-megaview}"
  [ -x "$B/bin/megaview" ] && [ -x "$P/toolchain/debug/Xvfb" ] || VIEW=xephyr
  if [ "$VIEW" = megaview ]; then
    # Xephyr runs as always (own window, RandR resolution changes), but on an invisible display
    # (Xvfb :1NN); megaview shows the nested screen scaled in a window of any size
    H=$((N + 100))
    rm -f "$X11/X$H"
    MEGA_X11_DIR="$X11" setsid "$P/toolchain/debug/Xvfb" ":$H" -screen 0 1920x1200x24 -nolisten tcp \
      > "$B/xvfb.log" 2>&1 &
    XVFB_PID=$!
    for i in $(seq 100); do [ -S "$X11/X$H" ] && break; sleep 0.1; done
    [ -S "$X11/X$H" ] || { echo "Xvfb did not start, see $B/xvfb.log" >&2; exit 1; }
    XHOST=":$H"
  else
    XHOST="$DISP"
  fi
  DISPLAY="$XHOST" MEGA_X11_DIR="$X11" setsid "$P/toolchain/debug/Xephyr" ":$N" -screen 640x480 -resizeable \
    -ac -noreset -title "Megatouch ION (cabinet loader)" > "$B/xephyr.log" 2>&1 &
  XEPHYR_PID=$!
  trap 'kill -- -$XEPHYR_PID 2>/dev/null; [ -n "$VIEW_PID" ] && kill $VIEW_PID 2>/dev/null; [ -n "$XVFB_PID" ] && kill -- -$XVFB_PID 2>/dev/null' EXIT INT TERM
  if [ "$VIEW" = megaview ]; then
    if [ "$(od -An -tx1 -j4 -N1 "$B/bin/megaview" | tr -d ' ')" = 02 ]; then
      # 64-bit build: the desktop's GPU (WSLg d3d12) does the scaling
      TDL="$P/toolchain/debug/root/usr/lib/x86_64-linux-gnu"
      gpu=(); [ -e /dev/dxg ] && [ -d /usr/lib/wsl/lib ] && gpu=(GALLIUM_DRIVER=d3d12)   # WSLg's GPU
      env "${gpu[@]}" LD_LIBRARY_PATH="$TDL:$TDL/pulseaudio:/usr/lib/wsl/lib" setsid "$B/bin/megaview" \
        ":$N" "Megatouch ION (cabinet loader)" > "$B/megaview.log" 2>&1 &
    else
      RTD="$P/shared/runtime"
      LIBGL_DRIVERS_PATH="$RTD/dri" LP_NUM_THREADS=2 MEGAVIEW_XTST="$B/root/usr/lib/libXtst.so.6" setsid \
        "$RTD/ld-linux.so.2" --library-path "$RTD:$RTD/pulseaudio" "$B/bin/megaview" ":$N" \
        "Megatouch ION (cabinet loader)" > "$B/megaview.log" 2>&1 &
    fi
    VIEW_PID=$!
  fi
  for i in $(seq 100); do [ -S "$X11/X$N" ] && break; sleep 0.1; done
  [ -S "$X11/X$N" ] || { echo "Xephyr did not start, see $B/xephyr.log" >&2; exit 1; }
  DISP=":$N"
elif [ "${1:-}" = run ] || [ "${1:-}" = shell ]; then
  # helpers join a running nested server if there is one
  N="${MEGA_LOADER_DISPLAY:-55}"
  [ -S "$B/x11/X$N" ] && { X11="$B/x11"; DISP=":$N"; }
fi
args=(
  --unshare-user --uid 0 --gid 0 --unshare-pid --die-with-parent
  --bind "$B/root" /
  --bind "${MEGA_LOADER_VAR:-$B/var}" /var
  --bind "$B/home" /home
  --ro-bind "$ION" /usr/local/ion_only
  --proc /proc
  --ro-bind "$B/bin/pci-devices.ion945gc" /proc/bus/pci/devices
  --ro-bind /sys /sys
  --dev /dev
  --dir /dev/merit_ipc
  --tmpfs /tmp
  --bind "$X11" /tmp/.X11-unix
  --ro-bind "$P/shared/runtime" /opt/rt
  --ro-bind "$B/bin" /opt/fakeio
  --ro-bind "$P/shared/runtime/ld-linux.so.2" /lib/ld-linux.so.2
  --ro-bind "$P/shared/runtime" /lib32
  --ro-bind "$B/bin/empty" /etc/ld.so.cache
)
# (/lib32 is the modern ld.so's first default library directory, and the cabinet's ld.so.cache,
#  which points libc.so.6 at the 2007 /lib/libc.so.6, is hidden: programs started with a cleared
#  environment, e.g. dhclient's script, still get the modern libc)
# Network. The loader (start) gets its own network namespace with a virtual eth0 from
# slirp4netns (user-mode networking: NAT to the host, DHCP 10.0.2.15, DNS 10.0.2.3), so the
# cabinet's network_manager and DHCP client configure it as on the real machine.
# MEGA_LOADER_NET=host shares the host's network instead (the cabinet then reports it as down);
# MEGA_LOADER_NET=lan[:NAME] puts the cabinet on a virtual cabinet network shared with every
# other cabinet started with the same NAME (megalan: a switch with a router to the internet),
# for linked play; helpers (run/shell) always share the host's network.
NET="${MEGA_LOADER_NET:-slirp}"
SLIRP="$P/toolchain/debug/root/usr/bin/slirp4netns"
case "${1:-}" in run|shell) NET=host ;; esac
LAN=""
case "$NET" in lan|lan:*) LAN="${NET#lan}"; LAN="${LAN#:}"; LAN="${LAN:-lan}"; NET=lan ;; esac
[ "$NET" = slirp ] && [ ! -x "$SLIRP" ] && { echo "no slirp4netns (make loader-setup); network shared with the host" >&2; NET=host; }
if [ "$NET" = host ]; then
  # the cabinet's /etc/resolv.conf links to /var/merit/etc/resolv.conf: give it the host's resolver
  VR="${MEGA_LOADER_VAR:-$B/var}/merit/etc/resolv.conf"
  [ -e "$VR" ] || { mkdir -p "$(dirname "$VR")"; : > "$VR"; }
  args+=(--ro-bind "$(readlink -f /etc/resolv.conf)" /var/merit/etc/resolv.conf)
else
  # network admin rights apply only inside the sandbox's own network namespace
  args+=(--unshare-net --cap-add CAP_NET_ADMIN --cap-add CAP_NET_RAW --cap-add CAP_NET_BIND_SERVICE)
fi
if [ "$NET" = lan ]; then
  # this LAN's switch (one megalan per name, shared by every cabinet on it; it exits on its own
  # a minute after the last cabinet leaves)
  [[ "$LAN" =~ ^[A-Za-z0-9_.-]+$ ]] || { echo "bad LAN name: $LAN" >&2; exit 1; }
  mkdir -p "$B/lan"
  LANSOCK="$B/lan/$LAN.sock"
  if ! { [ -f "$B/lan/$LAN.pid" ] && kill -0 "$(cat "$B/lan/$LAN.pid")" 2>/dev/null && [ -S "$LANSOCK" ]; }; then
    # joining other PCs (docs: operator guide, linked cabinets): MEGA_LAN_LISTEN=[ADDR:]PORT makes
    # this PC the hub, MEGA_LAN_CONNECT=HOST:PORT joins one; MEGA_LAN_PASSWORD is shared by all.
    # In the environment or cabinet.local.conf.
    lan_listen="${MEGA_LAN_LISTEN:-}" lan_connect="${MEGA_LAN_CONNECT:-}" lan_pw="${MEGA_LAN_PASSWORD:-}"
    if [ -f "$P/cabinet.local.conf" ]; then
      eval "$(grep -E '^MEGA_LAN_(LISTEN|CONNECT|PASSWORD)=' "$P/cabinet.local.conf" | sed 's/^/conf_/')"
      lan_listen="${lan_listen:-${conf_MEGA_LAN_LISTEN:-}}"
      lan_connect="${lan_connect:-${conf_MEGA_LAN_CONNECT:-}}"
      lan_pw="${lan_pw:-${conf_MEGA_LAN_PASSWORD:-}}"
    fi
    lan_args=()
    [ -n "$lan_listen" ] && lan_args=(--listen "$lan_listen")
    [ -n "$lan_connect" ] && lan_args=(--connect "$lan_connect")
    TDL="$P/toolchain/debug/root/usr/lib/x86_64-linux-gnu"
    MEGALAN_PASSWORD="$lan_pw" LD_LIBRARY_PATH="$TDL" setsid bash -c '"$@"; echo "megalan exited ($?)"' \
      megalan "$B/bin/megalan" "$LANSOCK" "${lan_args[@]}" >> "$B/lan/$LAN.log" 2>&1 < /dev/null &
    echo $! > "$B/lan/$LAN.pid"
    for i in $(seq 50); do [ -S "$LANSOCK" ] && break; sleep 0.1; done
    [ -S "$LANSOCK" ] || { echo "megalan did not start, see $B/lan/$LAN.log" >&2; exit 1; }
  fi
  args+=(--bind "$LANSOCK" /tmp/megalan.sock --dev-bind /dev/net/tun /dev/net/tun)
fi
for d in /dev/dri /dev/dxg /usr/lib/wsl /mnt/wslg; do [ -e "$d" ] && args+=(--dev-bind "$d" "$d"); done
[ -n "${XDG_RUNTIME_DIR:-}" ] && [ -S "$XDG_RUNTIME_DIR/pulse/native" ] && \
  args+=(--bind "$XDG_RUNTIME_DIR/pulse/native" /tmp/pulse-native)

RT=/opt/rt
env=(
  --clearenv
  --setenv HOME /home/maxx --setenv USER maxx --setenv LOGNAME maxx
  --setenv PATH /opt/fakeio/bin:/usr/local/bin:/usr/bin:/bin:/usr/sbin:/sbin
  --setenv DISPLAY "$DISP"
  --setenv LD_LIBRARY_PATH "/opt/fakeio:$RT:$RT/pulseaudio:/usr/local/lib:/usr/lib:/lib"
  --setenv LIBGL_DRIVERS_PATH "$RT/dri"
  --setenv GCONV_PATH "$RT/gconv"
  --setenv GLIBC_TUNABLES "glibc.rtld.execstack=2:glibc.malloc.tcache_count=0"
  --setenv PULSE_SERVER unix:/tmp/pulse-native
  --setenv MEGAIO_DIR /var/merit/fakeio
  --setenv MEGAIO_TRACE "${MEGAIO_TRACE:-0}"
  --setenv MEGAIO_JOYSTICK "${MEGAIO_JOYSTICK:-0}"
  --setenv TERM "${TERM:-xterm}"
  --setenv LD_PRELOAD "/opt/fakeio/startfix.so /opt/fakeio/crashlog.so /opt/fakeio/ossfake.so /opt/fakeio/zlibcompat.so /opt/fakeio/soundfix.so"
)
# the stand-ins' optional settings, passed in when set (docs/reference/commands.md#cabinet-loader)
for v in MEGAIO_SERIAL MEGAIO_OPERATOR_KEY MEGAIO_PLAYER_KEY MEGAIO_NO_KEYS OSSFAKE_TRACE \
         OSSFAKE_FMOD_OUTPUT MEGA_ZLIB; do
  [ -n "${!v:-}" ] && env+=(--setenv "$v" "${!v}")
done
# MEGA_EXTRA_ENV="A=1 B=2": extra variables for debugging
for kv in ${MEGA_EXTRA_ENV:-}; do env+=(--setenv "${kv%%=*}" "${kv#*=}"); done
[ -n "${WAYLAND_DISPLAY:-}" ] && env+=(--setenv XDG_RUNTIME_DIR /mnt/wslg/runtime-dir)

# wait for the sandbox; closing the viewer window ends it too
wait_sandbox() {
  local rc
  if [ -n "$VIEW_PID" ]; then
    wait -n "$1" "$VIEW_PID"; rc=$?
    kill -0 "$1" 2>/dev/null && { kill "$1"; wait "$1"; }
  else
    wait "$1"; rc=$?
  fi
  exit $rc
}

run() {
  if [ "$NET" = slirp ]; then
    # start the sandbox, then plug slirp4netns into its network namespace as eth0
    local info="$B/bwrap-info.$$"
    rm -f "$info"
    bwrap "${args[@]}" "${env[@]}" --info-fd 9 --chdir /home/maxx "$@" 9>"$info" &
    local bw=$! pid=""
    for i in $(seq 100); do
      pid=$(sed -n 's/.*"child-pid": *\([0-9]*\).*/\1/p' "$info" 2>/dev/null); [ -n "$pid" ] && break; sleep 0.05
    done
    rm -f "$info"
    LD_LIBRARY_PATH="$P/toolchain/debug/root/usr/lib/x86_64-linux-gnu" "$SLIRP" --mtu 1500 \
      --disable-host-loopback "$pid" eth0 > "$B/slirp.log" 2>&1 &
    SLIRP_PID=$!
    # slirp4netns outlives a killed sandbox: stop it (and Xephyr) however this script ends
    trap 'kill $SLIRP_PID 2>/dev/null; [ -n "$XEPHYR_PID" ] && kill -- -$XEPHYR_PID 2>/dev/null; [ -n "$VIEW_PID" ] && kill $VIEW_PID 2>/dev/null; [ -n "$XVFB_PID" ] && kill -- -$XVFB_PID 2>/dev/null' EXIT
    trap 'exit 143' INT TERM
    wait_sandbox "$bw"
  fi
  if [ -n "$XEPHYR_PID" ]; then
    bwrap "${args[@]}" "${env[@]}" --chdir /home/maxx "$@" &
    wait_sandbox $!
  fi
  exec bwrap "${args[@]}" "${env[@]}" --chdir /home/maxx "$@"
}
case "${1:-}" in
  shell) run /bin/bash ;;
  run) shift; run "$@" ;;
  *)
    env+=(--setenv MEGA_LOADER_BIN "${MEGA_LOADER_BIN:-/usr/local/bin/start}")
    # every start keeps a snapshot of the loader's state (build/loader/backups, newest 20);
    # MEGA_LOADER_BACKUP=daily keeps one a day instead (kiosk mode), none skips it
    if [ -z "${MEGA_LOADER_VAR:-}" ]; then
      case "${MEGA_LOADER_BACKUP:-auto}" in
        auto) "$P/scripts/loader-backup.sh" --auto || true ;;
        daily) "$P/scripts/loader-backup.sh" --daily || true ;;
      esac
    fi
    # this cabinet's identity (serial, MegaNet ID: scripts/loader-identity.sh)
    if [ "${MEGA_LOADER_IDENTITY:-on}" != off ]; then
      "$P/scripts/loader-identity.sh" --apply || true
      ser=$(sed -n 's/^SERIAL=//p' "${MEGA_LOADER_VAR:-$B/var}.identity" 2>/dev/null)
      [ -n "$ser" ] && [ -z "${MEGAIO_SERIAL:-}" ] && env+=(--setenv MEGAIO_SERIAL "$ser")
    fi
    # on a LAN: the cabinet's port, with a MAC address of its own that stays the same
    if [ "$NET" = lan ]; then
      idf="${MEGA_LOADER_VAR:-$B/var}.identity"
      h=$( { cat "$idf" 2>/dev/null || echo "${MEGA_LOADER_VAR:-$B/var}"; } | md5sum)
      env+=(--setenv MEGA_LAN "$LAN" --setenv MEGA_LAN_MAC "52:54:00:${h:0:2}:${h:2:2}:${h:4:2}")
    fi
    # the security-key image the fake board serves, made once from this /var's NVRAM
    [ "${MEGA_LOADER_KEY:-make}" = none ] || "$P/scripts/loader-key.sh" >/dev/null || true
    run /opt/fakeio/xinit.sh "$@" ;;
esac
