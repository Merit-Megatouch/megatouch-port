#!/bin/bash
# Set a legacy game's PRELOAD to libmerit_legacy.so + the cabinet service libraries it needs (in
# dependency order, extracted into games/<g>/lib) + libmega_stubs.so if the game uses stubs.
#   scripts/lib/legacy-preload.sh <game>
set -uo pipefail
R=$(cd "$(dirname "$0")/../.." && pwd)
g=$1; c="$R/games/$g/game.conf"
[ -s "$R/build/index/cabinet-syms.tsv" ] || "$R/tools/cabinet-providers.py" --index
libs=$("$R/tools/cabinet-providers.py" "$g" | tr '\n' ' ')
for l in $libs; do [ -s "$R/games/$g/lib/$l" ] || "$R/scripts/lib/extract-libs.sh" "$R/games/$g/lib" "$l" 2>/dev/null; done
for f in "$R/games/$g"/lib/*; do [ -e "$R/shared/runtime/$(basename "$f")" ] && rm -f "$f"; done
stubs=""; grep -q '^PRELOAD=.*libmega_stubs.so' "$c" && stubs=" libmega_stubs.so"
line="PRELOAD=libmerit_legacy.so ${libs}${stubs}"; line=$(echo "$line" | tr -s ' ' | sed 's/ $//')
if grep -q '^PRELOAD=' "$c"; then sed -i "s|^PRELOAD=.*|$line|" "$c"; else echo "$line" >> "$c"; fi
echo "$g: $line"
