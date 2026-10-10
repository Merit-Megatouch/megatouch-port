#!/bin/sh
# Keep a simple play log: one line per game played, with how long it ran.
#   cabinet.local.conf:  HW_ON_GAME="scripts/examples/log-games.sh"
# Writes build/loader/plays.log (tab-separated: start time, seconds, game tag, game ID).
LOG=${PLAYLOG:-build/loader/plays.log}
STATE="$LOG.start"
case "$MEGA_EVENT_STATE" in
  start) echo "$MEGA_EVENT_T" > "$STATE" ;;
  end)
    start=$(cat "$STATE" 2>/dev/null || echo "$MEGA_EVENT_T")
    secs=$(awk -v a="$start" -v b="$MEGA_EVENT_T" 'BEGIN { printf "%d", b - a }')
    printf '%s\t%s\t%s\t%s\n' "$(date -d "@${start%.*}" '+%F %T')" "$secs" "$MEGA_EVENT_TAG" "$MEGA_EVENT_ID" >> "$LOG" ;;
esac
