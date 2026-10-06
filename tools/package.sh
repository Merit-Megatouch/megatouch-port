#!/bin/bash
# Make a self-contained copy of a game folder (all symlinks to shared/ resolved).
#   tools/package.sh games/<name> <destination-dir>
set -euo pipefail
SRC=$(cd "$1" && pwd); DEST=$2
[ -e "$DEST" ] && { echo "$DEST exists"; exit 1; }
mkdir -p "$DEST"
cp -rL "$SRC"/. "$DEST"/
rm -rf "$DEST/decomp" "$DEST/notes/"*.prof 2>/dev/null || true
echo "packaged $(du -sh "$DEST" | cut -f1) → $DEST   (run: $DEST/run)"
