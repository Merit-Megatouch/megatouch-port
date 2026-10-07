#!/bin/bash
# Smoke-test every legacy game (stubs regenerated first), 6 at a time, and summarise.
#   tools/legacy-batch.sh [secs] [game...]    -> build/smoke/legacy-batch.txt
R=$(cd "$(dirname "$0")/.." && pwd)
SECS=${1:-20}; shift || true
cd "$R"
games=("$@")
[ ${#games[@]} -gt 0 ] || games=($(for c in games/*/game.conf; do grep -q libmerit_legacy "$c" && basename "$(dirname "$c")"; done))
for g in "${games[@]}"; do make -s stubs GAME=$g >/dev/null 2>&1; done
printf '%s\n' "${games[@]}" | xargs -P 6 -I{} tools/smoke.sh {} "$SECS" > build/smoke/legacy-batch.raw 2>&1
grep -E '^[a-z0-9_]+ +rc=' build/smoke/legacy-batch.raw | sort > build/smoke/legacy-batch.txt
echo "$(grep -c 'crash=0' build/smoke/legacy-batch.txt) without a crash, $(grep -vc 'crash=0' build/smoke/legacy-batch.txt) crashed, of ${#games[@]}"
