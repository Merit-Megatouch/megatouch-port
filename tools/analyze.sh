#!/bin/bash
# List the symbols a game's libraries still need that nothing provides:
# not the game's lib/, not the shared 32-bit runtime, not megatouch-host.
# These are cabinet-loader services you must stand in for in src/host/loader_services.cpp.
#
# usage: tools/analyze.sh games/<name>      → writes games/<name>/notes/unresolved.txt
set -euo pipefail
export LC_ALL=C
P=$(cd "$(dirname "$0")/.." && pwd)
GD=$(cd "$1" && pwd)
mkdir -p "$GD/notes"
T=$(mktemp -d)
trap 'rm -rf "$T"' EXIT

defined() { { nm -D --defined-only "$@" 2>/dev/null || true; } | awk 'NF>=3 {print $3}' | sed 's/@.*//'; }

# Everything that is provided.
{
  for f in "$GD"/lib/*.so*; do defined "$f"; done
  for f in "$P"/shared/runtime/*.so* "$P"/shared/runtime/pulseaudio/*.so*; do [ -f "$f" ] && defined "$f"; done
  defined "$P/shared/bin/megatouch-host"
} | sort -u > "$T/def"

# Strong undefined references, with the library that needs each.
for f in "$GD"/lib/*.so; do
  nm -D --undefined-only "$f" 2>/dev/null | awk -v lib="$(basename "$f")" '$1=="U" {print $2" "lib}'
done | sed 's/@[^ ]*//' | sort -u > "$T/undef"

join -v1 "$T/undef" "$T/def" > "$T/missing"
{
  echo "# Symbols no library, runtime or megatouch-host provides ($(date +%F))"
  echo "# megatouch-host loads the game with RTLD_NOW, so each of these must be defined"
  echo "# (usually in src/host/loader_services.cpp) before the game will start."
  echo "# symbol | needed by"
  while read -r sym lib; do printf '%s | %s\n' "$(echo "$sym" | c++filt)" "$lib"; done < "$T/missing"
} > "$GD/notes/unresolved.txt"
n=$(wc -l < "$T/missing")
echo "$n unresolved symbol(s) → $GD/notes/unresolved.txt"
