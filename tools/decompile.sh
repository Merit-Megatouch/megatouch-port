#!/bin/bash
# Decompile a game's own library with Ghidra (headless) into <game>/decomp/<lib>.c
#   tools/decompile.sh games/<name> [extra .so names from its lib/ ...]
set -euo pipefail
P=$(cd "$(dirname "$0")/.." && pwd)
GD=$(cd "$1" && pwd); shift
LIB=$(sed -n 's/^LIB=//p' "$GD/game.conf")
[ -f "$P/toolchain/ghidra/.done" ] || { echo "Ghidra not installed: run scripts/setup.sh --ghidra"; exit 1; }
export JAVA_HOME=$(ls -d "$P"/toolchain/ghidra/jdk-*/ | head -1); export PATH="$JAVA_HOME/bin:$PATH"
G=$(ls -d "$P"/toolchain/ghidra/ghidra_*_PUBLIC | head -1)
mkdir -p "$GD/decomp" "$GD/notes/ghproj"
files=("$GD/lib/$LIB"); for x in "$@"; do files+=("$GD/lib/$x"); done
"$G/support/analyzeHeadless" "$GD/notes/ghproj" game -import "${files[@]}" -overwrite \
   -scriptPath "$P/tools" -postScript DecompileAll.java "$GD/decomp" > "$GD/notes/ghidra.log" 2>&1
ls -la "$GD/decomp"
echo "note: Ghidra rebases libraries to 0x10000 — subtract it from addresses before using objdump/ELF offsets"
