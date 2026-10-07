#!/bin/bash
# Run the cabinet's own loader in a rootless sandbox built from the disk image.
#
#   scripts/loader.sh                  start the loader the way /home/maxx/.xinitrc does
#   scripts/loader.sh shell            a shell inside the sandbox (cabinet userland)
#   scripts/loader.sh run <cmd> ...    run any command inside the sandbox
#
# Layout (made by `make loader-root`, all under build/loader/, git-ignored):
#   root/  the sideB-root partition      → /
#   var/   the sideB-var partition       → /var       (writable: settings, NVRAM, logs)
#   home/  the sideB-home partition      → /home
#   cabinet/ion (snapshot)               → /usr/local/ion_only
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
#   MEGA_LOADER_VAR      directory used as /var (default build/loader/var); a second session
#                        needs its own (e.g. a copy of build/loader/var.orig) and display
set -euo pipefail
P=$(cd "$(dirname "$0")/.." && pwd)
B="$P/build/loader"
[ -d "$B/root/usr/local/bin" ] || { echo "no cabinet root: run make loader-root" >&2; exit 1; }
[ -f "$B/bin/libusb-1.0.so.0" ] || { echo "no fake I/O board: run make loader" >&2; exit 1; }

X11=/tmp/.X11-unix
[ -L "$X11" ] && X11=$(readlink -f "$X11")
DISP="${DISPLAY:-:0}"
XEPHYR_PID=
if [ "${MEGA_LOADER_X:-xephyr}" != host ] && [ "${1:-}" != run ] && [ "${1:-}" != shell ]; then
  # A nested X server: its own socket directory, shared with the sandbox as /tmp/.X11-unix.
  N="${MEGA_LOADER_DISPLAY:-55}"
  X11="$B/x11"
  mkdir -p "$X11"
  rm -f "$X11/X$N"
  MEGA_X11_DIR="$X11" setsid "$P/toolchain/debug/Xephyr" ":$N" -screen 640x480 -resizeable -ac -noreset \
    -title "Megatouch ION (cabinet loader)" > "$B/xephyr.log" 2>&1 &
  XEPHYR_PID=$!
  trap 'kill -- -$XEPHYR_PID 2>/dev/null' EXIT INT TERM
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
  --ro-bind "$P/cabinet/ion" /usr/local/ion_only
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
)
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
  --setenv GLIBC_TUNABLES "glibc.rtld.execstack=2:glibc.malloc.tcache_count=0"
  --setenv PULSE_SERVER unix:/tmp/pulse-native
  --setenv MEGAIO_DIR /var/merit/fakeio
  --setenv MEGAIO_TRACE "${MEGAIO_TRACE:-0}"
  --setenv MEGAIO_JOYSTICK "${MEGAIO_JOYSTICK:-0}"
  --setenv TERM "${TERM:-xterm}"
  --setenv LD_PRELOAD "/opt/fakeio/startfix.so /opt/fakeio/crashlog.so /opt/fakeio/ossfake.so"
)
# MEGA_EXTRA_ENV="A=1 B=2": extra variables for debugging
for kv in ${MEGA_EXTRA_ENV:-}; do env+=(--setenv "${kv%%=*}" "${kv#*=}"); done
[ -n "${WAYLAND_DISPLAY:-}" ] && env+=(--setenv XDG_RUNTIME_DIR /mnt/wslg/runtime-dir)

run() {
  if [ -n "$XEPHYR_PID" ]; then
    bwrap "${args[@]}" "${env[@]}" --chdir /home/maxx "$@"
    local rc=$?
    kill -- -"$XEPHYR_PID" 2>/dev/null
    exit $rc
  fi
  exec bwrap "${args[@]}" "${env[@]}" --chdir /home/maxx "$@"
}
case "${1:-}" in
  shell) run /bin/bash ;;
  run) shift; run "$@" ;;
  *)
    env+=(--setenv MEGA_LOADER_BIN "${MEGA_LOADER_BIN:-/usr/local/bin/start}")
    run /opt/fakeio/xinit.sh "$@" ;;
esac
