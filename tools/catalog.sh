#!/bin/bash
# Regenerate docs/reference/games.md (the game catalogue). Needs `make setup`.
set -euo pipefail
R=$(cd "$(dirname "$0")/.." && pwd)
. "$R/cabinet.conf"; . "$R/scripts/lib/cabinet.sh"; cab_check
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT
LD_LIBRARY_PATH="$R/shared/engine-sdk:$R/shared/runtime" "$R/shared/runtime/ld-linux.so.2" "$R/shared/bin/gameids" > "$T/ids.tsv"
mkdir -p "$T/gd"
for g in $(cab_ls ion /games); do cab_dump ion "/games/$g/gamedata.xml" "$T/gd/$g.xml" 2>/dev/null || rm -f "$T/gd/$g.xml"; done
python3 -I "$R/tools/catalog.py" "$R" "$T/ids.tsv" "$T"/gd/*.xml
