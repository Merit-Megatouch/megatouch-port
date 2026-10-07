#!/bin/bash
# Headless smoke test: run a game without a window or sound, tap the screen centre now and
# then, save screenshots, and summarise problems from the log.
#
#   tools/smoke.sh <game> [seconds]        → build/smoke/<game>/{log.txt,frame*.png|bmp}
#
# Works for GameDevice and legacy games (SDL offscreen). Unity and Merit3D need a display.
set -uo pipefail
R=$(cd "$(dirname "$0")/.." && pwd)
G=$1; SECS=${2:-30}
GD="$R/games/$G"; O="$R/build/smoke/$G"
[ -f "$GD/game.conf" ] || { echo "no games/$G"; exit 1; }
rm -rf "$O"; mkdir -p "$O"
W=$(sed -n 's/^WIDTH=//p' "$GD/game.conf"); H=$(sed -n 's/^HEIGHT=//p' "$GD/game.conf")
cx=$(( ${W:-800} / 2 )); cy=$(( ${H:-600} / 2 ))
if grep -q '^ENGINE=unity' "$GD/game.conf"; then
  # Unity: needs the real display; screenshots come from libmega_unity.so's glXSwapBuffers hook
  MEGA_SHOT_DIR="$O" MEGA_SHOT_EVERY=180 timeout "$SECS" "$GD/run" > "$O/log.txt" 2>&1
  rc=$?
  shots=$(ls "$O" | grep -c '^frame')
  printf '%-28s rc=%-3s shots=%-3s crash=%s\n' "$G" "$rc" "$shots" "$(grep -cE 'Segmentation|SIGSEGV|Crash!!!' "$O/log.txt")"
  grep -E "Couldn't pull|Exception|Couldn't load|not found|failed" "$O/log.txt" | sed 's/[0-9]\{3,\}/N/g' | sort | uniq -c | sort -rn | head -6 | sed 's/^/    /'
  exit 0
elif grep -q '^PRELOAD=.*libmerit_legacy' "$GD/game.conf"; then
  # legacy: milliseconds, every 1.5 s
  clicks=$(for t in $(seq 2000 1500 $((SECS * 1000))); do printf '%d:%d,%d;' $t $cx $cy; done)
  every=60
else
  # GameDevice: update ticks (30/s), every 2 s
  clicks=$(for t in $(seq 60 60 $((SECS * 30))); do printf '%d:%d,%d;' $t $cx $cy; done)
  every=150
fi
SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy MEGA_SHOT_DIR="$O" MEGA_SHOT_EVERY=$every \
  MEGA_AUTOCLICK="$clicks" timeout "$SECS" "$GD/run" > "$O/log.txt" 2>&1
rc=$?
shots=$(ls "$O" | grep -c '^frame')
crash=$(grep -c '^\*\*\* signal' "$O/log.txt")
printf '%-28s rc=%-3s shots=%-3s crash=%s' "$G" "$rc" "$shots" "$crash"
grep -q '^\*\*\* signal' "$O/log.txt" && printf '  %s' "$(grep -m1 -A3 '^\*\*\* signal' "$O/log.txt" | grep -o '(_Z[^)]*)' | head -1 | tr -d '()' | c++filt)"
echo
# most frequent complaints
grep -vE '^\[(frames|hitch|profile|mega\] renderer)' "$O/log.txt" | grep -iE 'error|fail|missing|unable|not found|\[stub\]|exception' \
  | sed 's/[0-9]\{3,\}/N/g' | sort | uniq -c | sort -rn | head -5 | sed 's/^/    /'
exit 0
