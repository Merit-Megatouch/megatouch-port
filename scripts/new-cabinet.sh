#!/bin/bash
# Make another cabinet on this PC, for linked play (MegaLink) with the main one.
#
#   scripts/new-cabinet.sh NAME [--from-image]
#
# Creates build/loader/cabinets/NAME: a copy of the main cabinet's state (or, with --from-image,
# of the image's original state), with its own identity (serial, MegaNet ID: a MegaNet server and
# the other cabinets see a different machine) and linked games switched on. Then prints how to
# run both on one cabinet network:
#
#   MEGA_LOADER_NET=lan make loader-run                                        the main cabinet
#   MEGA_LOADER_NET=lan MEGA_LOADER_VAR=$PWD/build/loader/cabinets/NAME \
#     MEGA_LOADER_DISPLAY=56 make loader-run                                   this one
#
# Copy the main cabinet's state while it is stopped.
set -euo pipefail
P=$(cd "$(dirname "$0")/.." && pwd)
B="$P/build/loader"
NAME="${1:-}"
[[ "$NAME" =~ ^[A-Za-z0-9_-]+$ ]] || { sed -n 2,16p "$0" | sed 's/^# \{0,1\}//'; exit 1; }
DEST="$B/cabinets/$NAME"
[ -e "$DEST" ] && { echo "$DEST exists" >&2; exit 1; }
SRC="$B/var"
[ "${2:-}" = --from-image ] && SRC="$B/var.orig"
[ -d "$SRC/merit" ] || { echo "no cabinet state at $SRC (make loader-setup)" >&2; exit 1; }
[ "$SRC" = "$B/var" ] && pgrep -x start >/dev/null && echo "warning: copying while a cabinet runs; stop it for a clean copy" >&2

mkdir -p "$B/cabinets"
cp -a "$SRC" "$DEST"
rm -rf "$DEST/merit/logging/logs"/* "$DEST/merit/fakeio/key.bin" 2>/dev/null || true
export MEGA_LOADER_VAR="$DEST"
if [ -f "$DEST/merit/fakeio/eeprom.bin" ]; then
  "$P/scripts/loader-identity.sh" --new < /dev/null | grep -v "^(stored" || true   # a copy: new identity
else
  "$P/scripts/loader-identity.sh" | grep -v "^(stored"                             # from the image
fi
"$P/scripts/loader-option.sh" LINKED_GAMES_ENABLED 1 >/dev/null
echo
echo "Cabinet '$NAME' is ready ($DEST). Run both on one cabinet network:"
echo "  MEGA_LOADER_NET=lan make loader-run"
echo "  MEGA_LOADER_NET=lan MEGA_LOADER_VAR=$DEST MEGA_LOADER_DISPLAY=56 make loader-run"
echo "Turn linked games on in the main cabinet too: scripts/loader-option.sh LINKED_GAMES_ENABLED 1"
