#!/bin/bash
# Update this install to the latest version from GitHub, safely.
#
#   scripts/update.sh              update now (also: make update): back up the cabinet's settings,
#                                  pull the new version, rebuild; roll back if the build fails
#   scripts/update.sh --check      say whether an update is available (exit status 0 if so)
#   scripts/update.sh --rollback   go back to the version before the last update
#   scripts/update.sh --verified   mark the last update as good (kiosk mode does this once the
#                                  cabinet has run 10 minutes on it)
#
# The cabinet must be stopped. Local changes to tracked files block the update (your
# cabinet.local.conf and the cabinet's state in build/ are not tracked and are never touched).
# A version that was rolled back is skipped until a newer one appears.
# State: build/loader/update.state (PREV, NEW, PENDING, BAD).
set -uo pipefail
P=$(cd "$(dirname "$0")/.." && pwd)
B="$P/build/loader"
STATE="$B/update.state"
BRANCH="${MEGA_UPDATE_BRANCH:-main}"
cd "$P"
mkdir -p "$B"
say() { echo "[update] $*"; }
state_get() { sed -n "s/^$1=//p" "$STATE" 2>/dev/null | tail -1; }
state_set() {   # state_set KEY=VALUE ...
  local f="$STATE.new" kv k
  cp "$STATE" "$f" 2>/dev/null || : > "$f"
  for kv in "$@"; do k=${kv%%=*}; grep -v "^$k=" "$f" > "$f.2"; echo "$kv" >> "$f.2"; mv "$f.2" "$f"; done
  mv "$f" "$STATE"
}
cabinet_running() { [ -z "${MEGA_UPDATE_IGNORE_RUNNING:-}" ] && pgrep -x start >/dev/null; }
rebuild() {
  [ -n "${MEGA_UPDATE_TEST_BUILD:-}" ] && { $MEGA_UPDATE_TEST_BUILD; return; }   # tests only
  make setup >> "$B/update.log" 2>&1 && make loader-setup >> "$B/update.log" 2>&1 && make loader >> "$B/update.log" 2>&1
}

[ -d .git ] || { say "not a git checkout: update by downloading the project again"; exit 1; }

case "${1:-}" in
  --verified)
    state_set PENDING=0
    say "update to $(state_get NEW | cut -c1-7) verified"
    exit 0 ;;
  --rollback)
    prev=$(state_get PREV); new=$(git rev-parse HEAD)
    [ -n "$prev" ] || { say "nothing to roll back to"; exit 1; }
    cabinet_running && { say "stop the cabinet first"; exit 1; }
    say "rolling back $(echo "$new" | cut -c1-7) → $(echo "$prev" | cut -c1-7)"
    git reset -q --hard "$prev" || { say "git reset failed"; exit 1; }
    rebuild || say "warning: rebuilding the previous version failed (see build/loader/update.log)"
    state_set BAD="$new" PENDING=0 NEW="$prev"
    say "rolled back; $(echo "$new" | cut -c1-7) will be skipped until a newer version appears"
    exit 0 ;;
esac

if ! timeout 120 git fetch -q origin "$BRANCH" 2>/dev/null; then say "can't reach GitHub"; exit 2; fi
target=$(git rev-parse "origin/$BRANCH")
behind=$(git rev-list --count "HEAD..$target")
if [ "$behind" = 0 ]; then say "up to date ($(git rev-parse --short HEAD))"; exit 1; fi
if [ "$target" = "$(state_get BAD)" ]; then
  say "the latest version ($(echo "$target" | cut -c1-7)) was rolled back before: skipping it"; exit 1
fi
if [ "${1:-}" = --check ]; then
  say "$behind new change(s) available:"
  git log --oneline "HEAD..$target" | head -10 | sed 's/^/  /'
  exit 0
fi

cabinet_running && { say "stop the cabinet first"; exit 1; }
if [ -n "$(git status --porcelain --untracked-files=no --ignore-submodules)" ]; then
  say "local changes to tracked files; not updating (git status shows them)"; exit 1
fi
prev=$(git rev-parse HEAD)
say "updating $(git rev-parse --short HEAD) → $(echo "$target" | cut -c1-7) ($behind change(s))"
"$P/scripts/loader-backup.sh" before-update >/dev/null && say "settings backed up"
if ! git merge -q --ff-only "$target"; then say "can't fast-forward (local commits?)"; exit 1; fi
: > "$B/update.log"
if ! rebuild; then
  say "the new version didn't build (build/loader/update.log): rolling back"
  git reset -q --hard "$prev"
  rebuild
  state_set BAD="$target" PENDING=0 PREV="$prev" NEW="$prev"
  exit 1
fi
state_set PREV="$prev" NEW="$target" PENDING=1 TIME="$(date '+%F %T')"
say "updated to $(git rev-parse --short HEAD)"
exit 0
