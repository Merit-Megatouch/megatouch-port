#!/bin/bash
# Push repositories to the GitHub org in repos.conf, creating them on first use.
#
#   scripts/publish.sh <game>    push games/<game> to <org>/<game>; on first publish it is
#                                registered in the main repo as a submodule at games/<game>
#   scripts/publish.sh --main    push the main repo to <org>/<MAIN_REPO>
#   scripts/publish.sh --all     every game under games/, then the main repo
#
# Uncommitted changes are never committed for you — commit first, then publish.
set -euo pipefail
R=$(cd "$(dirname "$0")/.." && pwd)
. "$R/repos.conf"
. "$R/scripts/lib/github.sh"
. "$R/scripts/lib/game-repo.sh"
say() { printf '\033[1m==>\033[0m %s\n' "$*"; }

ensure_remote() {   # ensure_remote <repo dir> <github name>
  local url; url=$(gh_url "$2")
  if git -C "$1" remote get-url origin >/dev/null 2>&1; then git -C "$1" remote set-url origin "$url"
  else git -C "$1" remote add origin "$url"; fi
}

publish_game() {
  local g=$1 gd="$R/games/$1" title
  [ -f "$gd/game.conf" ] || { echo "games/$g has no game.conf"; exit 1; }
  title=$(sed -n 's/^TITLE=Megatouch //p' "$gd/game.conf"); title=${title:-$g}
  game_repo_init "$gd" "$g" "$title"
  if [ -n "$(git -C "$gd" status --porcelain)" ]; then
    echo "games/$g has uncommitted changes — commit them first:"; git -C "$gd" status --short; exit 1
  fi
  say "games/$g → github.com/$GITHUB_ORG/$g"
  gh_repo_create "$g" "Megatouch $title — port configuration and notes (part of $MAIN_REPO)"
  ensure_remote "$gd" "$g"
  git -C "$gd" push -q -u origin main

  # Register as a submodule of the main repo (first time) or stage the new commit pointer.
  if ! git -C "$R" config -f .gitmodules --get "submodule.games/$g.path" >/dev/null 2>&1; then
    git -C "$R" rm -r -q --cached "games/$g" 2>/dev/null || true
    git -C "$R" submodule add -q "$(gh_url "$g")" "games/$g"
    say "games/$g is now a submodule of the main repo (staged — commit it)"
  else
    git -C "$R" add "games/$g"
  fi
}

publish_main() {
  if [ -n "$(git -C "$R" status --porcelain --ignore-submodules=dirty)" ]; then
    echo "main repo has uncommitted changes — commit them first:"; git -C "$R" status --short; exit 1
  fi
  say "main repo → github.com/$GITHUB_ORG/$MAIN_REPO"
  gh_repo_create "$MAIN_REPO" "Run original Megatouch ION cabinet games on Linux / WSL2 (SDL2 backend, tooling, docs)"
  ensure_remote "$R" "$MAIN_REPO"
  git -C "$R" push -q -u origin main
}

case "${1:-}" in
  --main) publish_main ;;
  --all)
    for gd in "$R"/games/*/; do [ -f "$gd/game.conf" ] && publish_game "$(basename "$gd")"; done
    if [ -n "$(git -C "$R" diff --cached --name-only)" ]; then
      git -C "$R" commit -q -m "Update game submodules" && say "committed submodule changes in the main repo"
    fi
    publish_main ;;
  ""|-h|--help) sed -n '2,10p' "$0" | sed 's/^# \{0,1\}//' ;;
  *) publish_game "$1" ;;
esac
