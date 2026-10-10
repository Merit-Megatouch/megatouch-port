#!/bin/bash
# Kiosk mode: run the cabinet fullscreen and keep it running, for a dedicated (touchscreen) box.
#
#   scripts/cabinet.sh                 run: fullscreen, pointer hidden; restart it when it exits,
#                                      crashes or hangs (also: make kiosk)
#   scripts/cabinet.sh stop            stop the running kiosk (and the cabinet)
#   scripts/cabinet.sh status          is it running, recent restarts
#   scripts/cabinet.sh autostart on    start the kiosk when this user's desktop session starts
#   scripts/cabinet.sh autostart off   (XDG autostart; set up automatic login to boot into it)
#
# To leave the kiosk at the box: Ctrl+Alt+End (megaview quits with status 42 and the kiosk stops).
# Any other end (the window closed, a crash, the cabinet exiting) restarts it after a pause that
# grows while it keeps failing quickly: 5 s, then 1 min, then 5 min.
#
# Hang protection: megaview touches build/loader/kiosk.alive every few seconds while the picture
# changes. If the picture has been frozen for KIOSK_HANG_SECS (default 300) while the cabinet's
# main thread is busy (a game stuck in a loop, as Trix once was), the cabinet is restarted. A quiet
# screen with an idle main thread (Operator Setup waiting for a touch) is left alone.
#
# Settings (environment, or in cabinet.local.conf):
#   KIOSK_HANG_SECS=300     frozen-picture time before a busy cabinet counts as hung (0 = off)
#   KIOSK_FULLSCREEN=1      start fullscreen (0: a normal window; F11 still toggles)
#   KIOSK_HIDE_CURSOR=1     hide the mouse pointer (0 to show it, e.g. without a touchscreen)
#   KIOSK_BACKUP=daily      settings snapshots: daily (one a day, 14 kept), auto (every start), none
#   KIOSK_UPDATE=nightly    look for a new version once a night (off: never); see below
#   KIOSK_UPDATE_HOUR=4     the hour (0-23) to look
#
# Updates: at KIOSK_UPDATE_HOUR, if GitHub has a newer version, the kiosk stops the cabinet,
# runs scripts/update.sh (settings backup, update, rebuild; it rolls back by itself if the build
# fails) and starts again on the new version. If the cabinet then fails three times quickly or
# hangs within its first 10 minutes, the kiosk rolls the update back and that version is skipped.
# Log: build/loader/kiosk.log (the cabinet's own output goes there too).
set -uo pipefail
P=$(cd "$(dirname "$0")/.." && pwd)
B="$P/build/loader"
[ -f "$P/cabinet.local.conf" ] && . "$P/cabinet.local.conf"
STOP="$B/kiosk.stop"
PIDF="$B/kiosk.pid"
LOG="$B/kiosk.log"
ALIVE="$B/kiosk.alive"
mkdir -p "$B"
log() { echo "$(date '+%F %T') $*" | tee -a "$LOG"; }

running() { [ -f "$PIDF" ] && kill -0 "$(cat "$PIDF")" 2>/dev/null; }

case "${1:-run}" in
  stop)
    touch "$STOP"
    if running; then
      kill "$(cat "$PIDF")" 2>/dev/null
      echo "kiosk stopping"
    else
      echo "kiosk not running"
    fi
    exit 0 ;;
  status)
    if running; then echo "kiosk running (pid $(cat "$PIDF"))"; else echo "kiosk not running"; fi
    [ -f "$LOG" ] && { echo "recent:"; grep -E "^[0-9]{4}-" "$LOG" | tail -8; }
    exit 0 ;;
  autostart)
    dir="${XDG_CONFIG_HOME:-$HOME/.config}/autostart"
    f="$dir/megatouch-cabinet.desktop"
    case "${2:-}" in
      on)
        mkdir -p "$dir"
        cat > "$f" <<EOF
[Desktop Entry]
Type=Application
Name=Megatouch ION cabinet
Comment=Runs the Megatouch cabinet fullscreen (kiosk mode)
Exec=$P/scripts/cabinet.sh
Terminal=false
X-GNOME-Autostart-enabled=true
X-GNOME-Autostart-Delay=5
EOF
        echo "autostart on: $f"
        echo "For a dedicated box, also turn on automatic login for this user (see the operator guide)." ;;
      off) rm -f "$f"; echo "autostart off" ;;
      *) echo "usage: scripts/cabinet.sh autostart on|off" >&2; exit 1 ;;
    esac
    exit 0 ;;
  run) ;;
  *) sed -n 2,11p "$0" | sed 's/^# \{0,1\}//'; exit 1 ;;
esac

if running && [ "$(cat "$PIDF")" != $$ ]; then echo "kiosk already running (pid $(cat "$PIDF"))" >&2; exit 1; fi
echo $$ > "$PIDF"
rm -f "$STOP"
child=
cleanup() { [ -n "$child" ] && kill -- -"$child" 2>/dev/null; rm -f "$PIDF"; }
trap 'touch "$STOP"; log "kiosk stop"; cleanup; exit 0' INT TERM
trap cleanup EXIT

export MEGAVIEW_FULLSCREEN="${KIOSK_FULLSCREEN:-1}" MEGAVIEW_HIDE_CURSOR="${KIOSK_HIDE_CURSOR:-1}" MEGAVIEW_ALIVE="$ALIVE"
export MEGA_LOADER_BACKUP="${KIOSK_BACKUP:-daily}"
HANG="${KIOSK_HANG_SECS:-300}"

# keep the screen on (best effort: X11 screensaver/DPMS, GNOME idle and lock). SDL also
# inhibits the screensaver while the window is open.
command -v xset >/dev/null && xset s off -dpms 2>/dev/null
if command -v gsettings >/dev/null; then
  gsettings set org.gnome.desktop.session idle-delay 0 2>/dev/null
  gsettings set org.gnome.desktop.screensaver lock-enabled false 2>/dev/null
fi

# CPU ticks of the cabinet's main thread (the `start` process in our process group)
main_ticks() {
  local pid
  pid=$(ps -eo pid=,pgid=,comm= | awk -v g="$child" '$2 == g && $3 == "start" {print $1; exit}')
  [ -n "$pid" ] && awk '{print $14 + $15}' "/proc/$pid/task/$pid/stat" 2>/dev/null
}

UPDATE="${KIOSK_UPDATE:-nightly}"
UPDATE_HOUR="${KIOSK_UPDATE_HOUR:-4}"
pending() { grep -q '^PENDING=1$' "$B/update.state" 2>/dev/null; }
restart_kiosk() { rm -f "$PIDF"; exec "$P/scripts/cabinet.sh"; }   # picks up an updated kiosk script

fast=0
log "kiosk start ($P)$(pending && echo ', checking the last update')"
while [ ! -f "$STOP" ]; do
  started=$(date +%s)
  : > "$ALIVE"
  setsid ${KIOSK_CMD:-"$P/scripts/loader.sh"} >> "$LOG" 2>&1 < /dev/null &   # KIOSK_CMD: tests only
  child=$!
  log "cabinet started (pgid $child)"
  hung=0 busy=0 last="" updating=0
  while kill -0 "$child" 2>/dev/null; do
    sleep 15 & wait $!                       # interruptible: `stop` takes effect at once
    now=$(date +%s)
    # an update that has run 10 minutes without trouble is kept
    if pending && [ $((now - started)) -ge 600 ]; then
      "$P/scripts/update.sh" --verified >> "$LOG" 2>&1; log "update verified"
    fi
    # once a night: a newer version?
    if [ "$UPDATE" = nightly ] && [ "$((10#$(date +%H)))" -eq "$UPDATE_HOUR" ] \
       && [ "$(cat "$B/kiosk.update-day" 2>/dev/null)" != "$(date +%F)" ]; then
      date +%F > "$B/kiosk.update-day"
      if "$P/scripts/update.sh" --check >> "$LOG" 2>&1; then
        log "a new version is available: stopping the cabinet to update"
        updating=1
        kill -- -"$child" 2>/dev/null
        break
      fi
    fi
    [ "$HANG" -gt 0 ] || continue
    now=$(date +%s)
    age=$(( now - $(stat -c %Y "$ALIVE" 2>/dev/null || echo "$now") ))
    t=$(main_ticks); t=${t:-0}
    # busy: the main thread used more than 80% of a CPU over the last 15 s (100 ticks/s)
    if [ -n "$last" ] && [ $(( t - last )) -gt 1200 ]; then busy=$((busy + 1)); else busy=0; fi
    last=$t
    if [ "$age" -ge "$HANG" ] && [ $((busy * 15)) -ge 60 ]; then
      log "cabinet hung: picture frozen ${age}s, main thread busy; restarting"
      hung=1
      kill -- -"$child" 2>/dev/null
      break
    fi
  done
  wait "$child"; rc=$?
  child=
  [ -f "$STOP" ] && break
  if [ "$updating" = 1 ]; then
    "$P/scripts/update.sh" >> "$LOG" 2>&1 && log "updated; restarting on the new version" || log "update not applied (see above)"
    restart_kiosk
  fi
  if [ "$rc" = 42 ]; then log "quit at the box (Ctrl+Alt+End)"; break; fi
  ran=$(( $(date +%s) - started ))
  [ "$hung" = 1 ] || log "cabinet exited (status $rc after ${ran}s)"
  if [ "$ran" -lt 120 ]; then fast=$((fast + 1)); else fast=0; fi
  # a fresh update that keeps failing (or hangs) goes back to the previous version
  if pending && { [ "$fast" -ge 3 ] || [ "$hung" = 1 ]; }; then
    log "the cabinet keeps failing since the last update: rolling it back"
    "$P/scripts/update.sh" --rollback >> "$LOG" 2>&1
    restart_kiosk
  fi
  pause=5; [ "$fast" -ge 3 ] && pause=60; [ "$fast" -ge 6 ] && pause=300
  [ "$pause" -gt 5 ] && log "failing repeatedly ($fast quick exits): back off ${pause}s"
  for _ in $(seq "$pause"); do [ -f "$STOP" ] && break; sleep 1 & wait $!; done
done
log "kiosk stop"
