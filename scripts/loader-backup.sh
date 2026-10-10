#!/bin/bash
# Snapshots of the loader's state: /var/merit (settings, NVRAM, books, databases, high scores,
# registered keys, MegaNet registration, the fake board's EEPROM). Logs are left out.
#
#   scripts/loader-backup.sh [label]          snapshot → build/loader/backups/<date>-<label>.tar.gz
#   scripts/loader-backup.sh --auto           same, labelled "auto", keeping the newest $KEEP autos
#   scripts/loader-backup.sh --daily          one "daily" snapshot per day at most, keeping the newest
#                                             $KEEP_DAILY (kiosk mode: restarts don't rotate them out)
#   scripts/loader-backup.sh --list           list snapshots
#   scripts/loader-backup.sh --restore FILE   restore one; the current state is first moved
#                                             aside to build/loader/var/merit.before-restore-<date>
set -euo pipefail
P=$(cd "$(dirname "$0")/.." && pwd)
B="$P/build/loader"
VAR="${MEGA_LOADER_VAR:-$B/var}"
D="$B/backups"
KEEP=${KEEP:-20}
KEEP_DAILY=${KEEP_DAILY:-14}
mkdir -p "$D"
now=$(date +%Y%m%d-%H%M%S)

snapshot() {   # snapshot <label>
  [ -d "$VAR/merit" ] || { echo "no loader state at $VAR/merit" >&2; exit 1; }
  local f="$D/$now-$1.tar.gz"
  tar -czf "$f.part" -C "$VAR" --exclude=merit/logging --exclude=merit/swapfile merit
  mv "$f.part" "$f"
  echo "$f"
}

case "${1:-}" in
  --list) ls -lh "$D"/*.tar.gz 2>/dev/null | awk '{print $5, $NF}' ;;
  --auto)
    snapshot auto >/dev/null
    ls -1t "$D"/*-auto.tar.gz 2>/dev/null | tail -n +$((KEEP + 1)) | while read -r old; do rm -f -- "$old"; done ;;
  --daily)
    ls "$D"/"$(date +%Y%m%d)"-*-daily.tar.gz >/dev/null 2>&1 || snapshot daily >/dev/null
    ls -1t "$D"/*-daily.tar.gz 2>/dev/null | tail -n +$((KEEP_DAILY + 1)) | while read -r old; do rm -f -- "$old"; done ;;
  --restore)
    f="${2:?usage: loader-backup.sh --restore FILE}"
    [ -f "$f" ] || f="$D/$f"
    [ -f "$f" ] || { echo "no such backup: $2" >&2; exit 1; }
    pgrep -x start >/dev/null && { echo "stop the loader first" >&2; exit 1; }
    if [ -d "$VAR/merit" ]; then mv "$VAR/merit" "$VAR/merit.before-restore-$now"; echo "current state kept in $VAR/merit.before-restore-$now"; fi
    tar -xzf "$f" -C "$VAR"
    echo "restored $f" ;;
  *) snapshot "${1:-manual}" ;;
esac
