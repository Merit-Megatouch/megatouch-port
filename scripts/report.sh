#!/bin/bash
# Collect what's needed to look into a problem into one file to attach to a report (make report).
#
#   scripts/report.sh            writes build/report-<date>.tar.gz and prints its path
#
# In it: system.txt (Linux/WSL version, kernel, AppArmor, bwrap, display and sound, what's running),
# network.txt (the PC's network, DNS, both sandbox network checks), and per cabinet (ION, Force,
# Maxx, extra cabinets) its logs (kiosk, console, update, slirp, window, X server; long ones cut to
# their last 20000 lines) and the cabinet's own view of its network (net.txt).
# Not in it: the cabinet's databases, NVRAM, books, key images or disc images. cabinet.local.conf is
# included with passwords, tokens and secrets blanked. Look inside before sending it on: logs can
# hold IP addresses and the names of disc files.
set -uo pipefail
P=$(cd "$(dirname "$0")/.." && pwd)
stamp=$(date +%Y%m%d-%H%M%S)
R="$P/build/report-$stamp"
mkdir -p "$R"

section() { printf '\n== %s\n' "$*"; }
run() { printf '$ %s\n' "$*"; "$@" 2>&1 | head -200; }
log_copy() {   # log_copy <file> <name in the report>
  [ -s "$1" ] || return 0
  mkdir -p "$(dirname "$R/$2")"
  local n; n=$(wc -l < "$1")
  if [ "$n" -gt 20000 ]; then
    { echo "[report: last 20000 of $n lines]"; tail -n 20000 "$1"; } > "$R/$2"
  else
    cp "$1" "$R/$2"
  fi
}

{
  section "megatouch-port"
  run git -C "$P" log -1 --format='%h %ci %s'
  run git -C "$P" status --short --untracked-files=no
  section "System"
  run date
  [ -r /etc/os-release ] && run sed -n 's/^PRETTY_NAME=//p' /etc/os-release
  run uname -a
  if grep -qi microsoft /proc/version 2>/dev/null; then echo "WSL: yes"; else echo "WSL: no"; fi
  run sh -c 'nproc; free -m | head -2; df -h "'"$P"'" | tail -1'
  section "Sandbox"
  run bwrap --version
  for f in kernel/apparmor_restrict_unprivileged_userns kernel/unprivileged_userns_clone user/max_user_namespaces; do
    [ -r /proc/sys/$f ] && echo "$f = $(cat /proc/sys/$f)"
  done
  [ -d /etc/apparmor.d ] && run sh -c 'ls /etc/apparmor.d | grep -iE "bwrap|userns|megatouch" || echo "(no bwrap/userns profiles)"'
  [ -r /sys/module/apparmor/parameters/enabled ] && echo "AppArmor enabled: $(cat /sys/module/apparmor/parameters/enabled)"
  section "Display and sound"
  echo "DISPLAY=${DISPLAY:-} WAYLAND_DISPLAY=${WAYLAND_DISPLAY:-} XDG_SESSION_TYPE=${XDG_SESSION_TYPE:-} XDG_RUNTIME_DIR=${XDG_RUNTIME_DIR:-}"
  [ -n "${XDG_RUNTIME_DIR:-}" ] && { [ -S "$XDG_RUNTIME_DIR/pulse/native" ] && echo "PulseAudio socket: yes" || echo "PulseAudio socket: no"; }
  section "Running"
  for p in start dstart Xephyr Xvfb megaview slirp4netns megalan bwrap; do
    n=$(pgrep -x "$p" | wc -l); [ "$n" -gt 0 ] && echo "$p: $n"
  done
  [ -x "$P/scripts/cabinet.sh" ] && run "$P/scripts/cabinet.sh" status
  section "Cabinets"
  for c in "$P"/build/loader "$P"/build/force "$P"/build/maxx "$P"/build/loader/cabinets/*; do
    [ -d "$c/root" ] || continue
    echo "${c#$P/}: $(cat "$c/root.source" 2>/dev/null | head -1 | sed 's|.*/||' || true)$( [ -d "$c/var" ] && echo ' (var present)')"
  done
} > "$R/system.txt" 2>&1

{
  section "The PC's network"
  run ip -brief addr
  run ip route
  run cat /etc/resolv.conf
  run getent hosts us.oerinet.net
  section "Sandbox network (quick)"
  . "$P/scripts/lib/sandbox-check.sh"
  if sandbox_net_check 2>&1; then echo "ok"; fi
  section "Sandbox network with slirp4netns (as the cabinet gets it)"
  if sandbox_net_check full 2>&1; then echo "ok"; fi
} > "$R/network.txt" 2>&1

for c in "$P"/build/loader "$P"/build/force "$P"/build/maxx "$P"/build/loader/cabinets/*; do
  [ -d "$c" ] || continue
  n=${c#$P/build/}; n=${n//\//-}
  for f in kiosk.log update.log console.log console.log.1 var.console.log var.console.log.1 slirp.log \
           megaview.log xephyr.log xvfb.log; do
    log_copy "$c/$f" "$n/$f"
  done
  log_copy "$c/var/merit/fakeio/net.txt" "$n/net.txt"
done

if [ -f "$P/cabinet.local.conf" ]; then
  sed -E 's/^([A-Za-z_]*(PASS|PASSWORD|TOKEN|SECRET|PSK|APIKEY|API_KEY)[A-Za-z_]*=).*/\1<removed>/I' \
    "$P/cabinet.local.conf" > "$R/cabinet.local.conf"
fi

tar -C "$P/build" -czf "$R.tar.gz" "report-$stamp" && rm -rf "$R"
echo "$R.tar.gz"
echo "Attach this file to the report. It has logs and system details, no game data or licence;"
echo "open it first if you want to check what it holds."
