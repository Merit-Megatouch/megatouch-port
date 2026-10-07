#!/bin/bash
# Scaffold a game port from the cabinet into games/<dll>/.
#
#   scripts/new-game.sh <name> [--force]        (or: make new GAME=<name>)
#
# <name>: DLLName (g_word_dojo_2), asset folder, or GameId (G_WORD_DOJO_2).
# --force re-extracts code and assets and rewrites game.conf; NOTES.md is never touched.
#
# Creates:  game.conf  NOTES.md  README.md  run  lib/  data/  notes/scaffold.md  notes/unresolved.txt
# and makes games/<dll> its own git repo (see scripts/lib/game-repo.sh).
set -euo pipefail
R=$(cd "$(dirname "$0")/.." && pwd)
. "$R/cabinet.conf"
. "$R/scripts/lib/cabinet.sh"
. "$R/repos.conf"
. "$R/scripts/lib/game-repo.sh"

NAME=${1:?usage: new-game.sh <name> [--force]}
FORCE=${2:-}
say()  { printf '\033[1m==>\033[0m %s\n' "$*"; }
warn() { printf '\033[33m!!\033[0m %s\n' "$*"; }
TMP=$(mktemp -d); trap 'rm -rf "$TMP"' EXIT
[ -x "$R/shared/bin/gameids" ] || { echo "run 'make setup' first"; exit 1; }
cab_check

# Sets GAMEID_NAME DLL DIR DESC RES from gamedata.xml (the master list, else the game's own copy).
identify_game() {
  local master="$R/shared/data-common/usr/local/gamedata/config/gamedata.xml" info d
  if ! info=$(python3 -I "$R/tools/gameinfo.py" "$NAME" "$master"); then
    for d in "$NAME" "$(echo "$NAME" | tr 'A-Z' 'a-z')"; do
      cab_dump ion "/games/$d/gamedata.xml" "$TMP/gamedata.xml" 2>/dev/null && break || true
    done
    info=$(python3 -I "$R/tools/gameinfo.py" "$NAME" "$TMP/gamedata.xml" 2>/dev/null) || { echo "'$NAME' not found in gamedata.xml"; exit 1; }
  fi
  eval "$info"
  [ -n "$DLL" ] || { echo "no DLLName for $NAME"; exit 1; }
}

# Game library + its dependency closure; sets FAMILY.
extract_code() {
  rm -rf "$GD/lib"; mkdir -p "$GD/lib" "$GD/notes"
  cab_exists root "/usr/local/lib/$DLL.so" || { echo "/usr/local/lib/$DLL.so is not in the cabinet"; exit 1; }
  say "extracting $DLL.so and the libraries it needs"
  "$R/scripts/lib/extract-libs.sh" "$GD/lib" "$DLL.so" 2> "$GD/notes/missing-libs.txt" || true
  local f; for f in "$GD"/lib/*; do [ -e "$R/shared/runtime/$(basename "$f")" ] && rm -f "$f"; done

  FAMILY=legacy
  if readelf -d "$GD/lib/$DLL.so" | grep -q libgame_device_sprite.so; then FAMILY=gamedevice
  elif readelf -d "$GD/lib/$DLL.so" | grep -q libmerit3d.so; then FAMILY=merit3d; fi
  cab_exists ion "/games/$DIR/Data" && FAMILY=unity
  say "engine family: $FAMILY"
  if [ "$FAMILY" = gamedevice ]; then
    # the cabinet backend and the legacy engine it called are replaced by our SDL2 backend
    rm -f "$GD"/lib/{libgame_device_sprite,libgraphics_sprite,libinput_sprite,libsound_sprite,libmerit2d,libmerit3d,libmeritbasegame,libmerit_threads}.so
    ln -sf ../../../shared/bin/libgame_device_sprite.so "$GD/lib/libgame_device_sprite.so"
    # some games list the other backend libraries without using them: empty stand-ins
    local s; for s in libgraphics_sprite libinput_sprite libsound_sprite; do
      ln -sf "../../../shared/bin/stubs/$s.so" "$GD/lib/$s.so"; done
  elif [ "$FAMILY" = legacy ] || [ "$FAMILY" = merit3d ]; then
    # the loader's legacy 2D engine: our stand-in, preloaded by megatouch-host (PRELOAD=)
    ln -sf ../../../shared/bin/libmerit_legacy.so "$GD/lib/libmerit_legacy.so"
    # Merit3D draws with OpenGL + GLU (GLU from the cabinet, shared with the Unity player)
    [ "$FAMILY" = merit3d ] && ln -sf ../../../shared/unity/lib/libGLU.so.1 "$GD/lib/libGLU.so.1"
  else
    warn "$FAMILY games are not supported yet (docs/roadmap.md)"
  fi
}

# Assets + links to shared data; sets ASSET_DIR.
extract_assets() {
  local part="" src=""
  if { [ "$FAMILY" = legacy ] || [ "$FAMILY" = merit3d ]; } && cab_exists root "/usr/local/gamedata/gamegraphics/$DIR"; then
    ASSET_DIR="usr/local/gamedata/gamegraphics/$DIR"; part=root; src="/usr/local/gamedata/gamegraphics/$DIR"
  elif cab_exists ion "/games/$DIR"; then ASSET_DIR="usr/local/ion_only/games/$DIR"; part=ion; src="/games/$DIR"
  elif cab_exists root "/usr/local/games/$DIR"; then ASSET_DIR="usr/local/games/$DIR"; part=root; src="/usr/local/games/$DIR"
  else ASSET_DIR="usr/local/ion_only/games/$DIR"; warn "no asset folder '$DIR' found — set ASSET_DIR in game.conf"; fi
  [ -L "$GD/data/usr/local/gamedata" ] && [ "$FAMILY" != gamedevice ] && rm "$GD/data/usr/local/gamedata"
  mkdir -p "$GD/data/$(dirname "$ASSET_DIR")" "$GD/data/usr/local/games" "$GD/data/usr/local/ion_only/games"
  if [ -n "$part" ]; then
    say "extracting assets from $src"
    rm -rf "$GD/data/$ASSET_DIR"
    local link=""
    [ "$part" = root ] && link=$(cab_readlink root "$src" 2>/dev/null || true)
    case "$link" in
      ../../ion_only/*)   # newer games keep their art on the games partition, linked from gamedata
        local rel=${link#../../ion_only/}
        mkdir -p "$GD/data/usr/local/ion_only/$(dirname "$rel")"
        cab_rdump ion "/$rel" "$GD/data/usr/local/ion_only/$(dirname "$rel")"
        ln -sfn "$link" "$GD/data/$ASSET_DIR" ;;
      *) cab_rdump "$part" "$src" "$GD/data/$(dirname "$ASSET_DIR")" ;;
    esac
  fi
  # shared cabinet data (read-only links) + a private, writable /var/merit
  if [ "$FAMILY" = legacy ] || [ "$FAMILY" = merit3d ]; then
    # legacy assets live inside /usr/local/gamedata, so that folder is real: shared parts linked
    local sub; for sub in config translations help ttf fonts; do
      ln -sfn "../../../../../../shared/data-common/usr/local/gamedata/$sub" "$GD/data/usr/local/gamedata/$sub"; done
    mkdir -p "$GD/data/usr/local/gamedata/gamegraphics"
    ln -sfn ../../../../../../../shared/data-common/usr/local/gamedata/gamegraphics/misc "$GD/data/usr/local/gamedata/gamegraphics/misc"
  else
    ln -sfn ../../../../../shared/data-common/usr/local/gamedata "$GD/data/usr/local/gamedata"
  fi
  ln -sfn ../../../../../../shared/data-common/usr/local/games/default "$GD/data/usr/local/games/default"
  ln -sfn ../../../shared/data-common/etc   "$GD/data/etc"
  ln -sfn ../../../shared/data-common/pango "$GD/data/pango"
  [ -d "$GD/data/var/merit" ] || { mkdir -p "$GD/data/var"; cp -r "$R/shared/data-common/var-template/merit" "$GD/data/var/"; }
  # runtime, host and launcher are shared
  ln -sfn ../../shared/runtime "$GD/runtime"
  ln -sf ../../shared/bin/megatouch-host "$GD/megatouch-host"
  ln -sf ../../scripts/launch.sh "$GD/run"
}

# game.conf: numeric GameId and window size; sets ID W H RES_SRC BIGGEST.
write_config() {
  ID=$(LD_LIBRARY_PATH="$R/shared/engine-sdk:$R/shared/runtime" "$R/shared/runtime/ld-linux.so.2" \
       "$R/shared/bin/gameids" "$GAMEID_NAME" 2>/dev/null || echo -1)
  case "$RES" in
    RESOLUTION_*x*)        W=${RES#RESOLUTION_}; H=${W#*x}; W=${W%x*} ;;
    SUPER_HIGH_RESOLUTION) W=1280; H=800 ;;   # widescreen (the launcher pairs it with FULLSCREEN_WIDE)
    HIGH_RESOLUTION)       W=1024; H=768 ;;   # unverified
    *)                     W=800;  H=600 ;;   # unverified default
  esac
  [ "$FAMILY" = legacy ] && { W=640; H=480; RES="legacy 640x480"; }
  [ "$FAMILY" = merit3d ] && [ "$W" = 800 ] && { W=640; H=480; }
  RES_SRC="declared ${RES:-none}"
  BIGGEST=$(python3 -I "$R/tools/largest-png.py" "$GD/data/$ASSET_DIR")
  case "${BIGGEST%% *}" in   # a full-screen background beats the declaration
    1280x800|1024x768|800x600|640x480)
      [ "${BIGGEST%% *}" != "${W}x${H}" ] && RES_SRC="largest PNG (declared ${RES:-none} → ${W}x${H})"
      W=${BIGGEST%%x*}; H=${BIGGEST#*x}; H=${H%% *} ;;
  esac
  if [ ! -f "$GD/game.conf" ] || [ "$FORCE" = --force ]; then
    cat > "$GD/game.conf" <<EOF
# $DESC — generated by new-game.sh $(date +%F). Keys become MEGA_<KEY>; the environment wins.
LIB=$DLL.so
ASSET_DIR=$ASSET_DIR
GAME_ID=$ID
WIDTH=$W
HEIGHT=$H
LANGUAGE=english
TITLE=Megatouch $DESC
EOF
    if [ "$FAMILY" = merit3d ]; then
      printf 'PRELOAD=libGL.so.1 libGLU.so.1 libmerit_legacy.so\nPLAYERS=1\n' >> "$GD/game.conf"
    elif [ "$FAMILY" = legacy ]; then
      printf 'PRELOAD=libmerit_legacy.so\nPLAYERS=1\n' >> "$GD/game.conf"
    else
      echo "# CARD_FANNING=1" >> "$GD/game.conf"
    fi
  fi
  [ "$ID" -ge 0 ] 2>/dev/null || warn "no numeric GameId for $GAMEID_NAME — set GAME_ID in game.conf"
}

# notes/scaffold.md (regenerated) and NOTES.md (created once, yours to edit).
write_notes() {
  local unres="(not analysed: $FAMILY game)" missing newlibs
  case "$FAMILY" in gamedevice|legacy|merit3d) unres=$("$R/tools/analyze.sh" "$GD") ;; esac
  missing=$(grep -c . "$GD/notes/missing-libs.txt" 2>/dev/null || true)
  newlibs=$(comm -23 <(ls "$GD/lib" | sort) <(ls "$R/shared/engine-sdk" | sort) \
            | grep -vx "$DLL.so" | grep -vx libgame_device_sprite.so | tr '\n' ' ' || true)
  cat > "$GD/notes/scaffold.md" <<EOF
# $DESC — scaffold facts (regenerated by new-game.sh)

| Fact | Value |
| --- | --- |
| GameId | $GAMEID_NAME = $ID |
| Code | /usr/local/lib/$DLL.so |
| Engine family | $FAMILY |
| Assets | /$ASSET_DIR |
| Window size | ${W}x${H} — from $RES_SRC |
| Largest PNG | $BIGGEST |
| Engine libraries beyond the shared SDK | ${newlibs:-none} |
| Libraries not found in the cabinet | $missing (missing-libs.txt) |
| Unresolved symbols | ${unres%% →*} (unresolved.txt) |
EOF
  [ -f "$GD/NOTES.md" ] || cat > "$GD/NOTES.md" <<EOF
# $DESC ($DLL)

Status: scaffolded $(date +%F). Facts: [notes/scaffold.md](notes/scaffold.md).

## Checklist
- [ ] Window size in game.conf matches the largest PNG (notes/scaffold.md)
- [ ] Every symbol in notes/unresolved.txt has a stand-in in src/host/loader_services.cpp
      (\`make analyze GAME=$DLL\` until it reports 0)
- [ ] First run: \`make run GAME=$DLL DEBUG=shots\` — crash trace + screenshots in notes/shots
- [ ] Paths: \`make run GAME=$DLL DEBUG=files\`; engine trace: \`mkdir -p data/var/merit/debug/files && touch data/var/merit/debug/files/resource_locator\`
- [ ] Reference code: \`make decompile GAME=$DLL\`
- [ ] Translations + help text appear (gamedata/translations/$DLL.utf8)
- [ ] Sound and music play (\`DEBUG=sound\`)
- [ ] A full game plays through (\`DEBUG=profile\` to catch stalls and old-malloc bugs)

## Log
<!-- dated notes: what broke, what fixed it -->
EOF
  echo "$unres"
}

# ---------------------------------------------------------------- Unity-family games
# All Unity games share the cabinet's LinuxPlayer (shared/unity). A game folder holds its
# Data/ (the asset folder), a player/ dir the player runs from (Unity finds Data/ next to the
# executable, which is player/ld-linux.so.2 because we start it through our loader), and
# var/, the writable working directory where launch.sh writes launcher.xml.
scaffold_unity() {
  FAMILY=unity
  [ -f "$R/shared/unity/.done" ] || { echo "shared/unity missing — rerun make setup"; exit 1; }
  ASSET_DIR="usr/local/ion_only/games/$DIR"
  mkdir -p "$GD/notes" "$GD/data/usr/local/ion_only/games" "$GD/player" "$GD/var"
  say "extracting /games/$DIR (Unity Data/)"
  rm -rf "$GD/data/$ASSET_DIR"
  cab_rdump ion "/games/$DIR" "$GD/data/usr/local/ion_only/games"
  [ -d "$GD/data/$ASSET_DIR/Data/Managed" ] || { echo "no Data/Managed in /games/$DIR"; exit 1; }
  ln -sfn ../../shared/runtime "$GD/runtime"
  ln -sf ../../scripts/launch.sh "$GD/run"
  # real files, not symlinks: Unity and Mono locate Data/ from the executable's resolved path
  rm -f "$GD/player/LinuxPlayer"
  ln "$R/shared/unity/LinuxPlayer" "$GD/player/LinuxPlayer" 2>/dev/null || cp "$R/shared/unity/LinuxPlayer" "$GD/player/LinuxPlayer"
  ln -sfn ../../../shared/unity/lib "$GD/player/lib"
  ln -sf ../../../shared/bin/libmega_unity.so "$GD/player/libmega_unity.so"
  ln -sfn "../data/$ASSET_DIR/Data" "$GD/player/Data"
  cp "$R/shared/runtime/ld-linux.so.2" "$GD/player/ld-linux.so.2"
  ID=$(LD_LIBRARY_PATH="$R/shared/engine-sdk:$R/shared/runtime" "$R/shared/runtime/ld-linux.so.2" \
       "$R/shared/bin/gameids" "$GAMEID_NAME" 2>/dev/null || echo -1)
  case "$RES" in
    RESOLUTION_*x*)        W=${RES#RESOLUTION_}; H=${W#*x}; W=${W%x*} ;;
    SUPER_HIGH_RESOLUTION) W=1280; H=800 ;;
    *)                     W=1024; H=768 ;;   # the launcher's own default for Unity games
  esac
  if [ ! -f "$GD/game.conf" ] || [ "$FORCE" = --force ]; then
    cat > "$GD/game.conf" <<EOF
# $DESC — generated by new-game.sh $(date +%F). Keys become MEGA_<KEY>; the environment wins.
ENGINE=unity
GAME=$DIR
GAME_ID=$ID
WIDTH=$W
HEIGHT=$H
LANGUAGE=english
TITLE=Megatouch $DESC
EOF
  fi
  local unity; unity=$(cat "$GD/data/$ASSET_DIR/Data/unityversion.txt" 2>/dev/null || echo "?")
  cat > "$GD/notes/scaffold.md" <<EOF
# $DESC — scaffold facts (regenerated by new-game.sh)

| Fact | Value |
| --- | --- |
| GameId | $GAMEID_NAME = $ID |
| Engine family | unity (shared player: /usr/local/ion_only/games/launcher/LinuxPlayer) |
| Data | /$ASSET_DIR/Data (unityversion.txt: $unity) |
| Managed code | $(ls "$GD/data/$ASSET_DIR/Data/Managed" | grep -vE '^(System|Mono|mscorlib|Boo|UnityEngine)' | grep '\.dll$' | tr '\n' ' ') |
| Window size | ${W}x${H} — from declared ${RES:-none} |
| Cabinet settings | Data/settings_gamedata.xml, merged into var/launcher.xml at launch |
EOF
  [ -f "$GD/NOTES.md" ] || cat > "$GD/NOTES.md" <<EOF
# $DESC ($DIR)

Status: scaffolded $(date +%F). Unity family. Facts: [notes/scaffold.md](notes/scaffold.md).

## Checklist
- [ ] Starts: \`make run GAME=$DIR\` (player log on stdout)
- [ ] Graphics and text look right; window size
- [ ] Sound and music (FMOD → PulseAudio through libmega_unity.so)
- [ ] Settings it can't find: \`make run GAME=$DIR 2>&1 | grep "Couldn't pull"\` — add them to scripts/launch.sh
- [ ] A full game plays through
- [ ] Launcher messaging (FIFO/TCP to the cabinet) failing doesn't break play

## Log
<!-- dated notes: what broke, what fixed it -->
EOF
}

identify_game
if [ "$DLL" = launcher ]; then   # Unity games all name the shared launcher as their library
  GD="$R/games/$DIR"
  if [ -d "$GD/player" ] && [ "$FORCE" != --force ]; then echo "games/$DIR already exists (use --force to redo)"; exit 1; fi
  say "$DESC  ($GAMEID_NAME, Unity, Data in '$DIR')"
  scaffold_unity
  game_repo_init "$GD" "$DIR" "$DESC"
  say "games/$DIR ready (its own git repo; publish with: make publish GAME=$DIR)"
  echo "    next: make run GAME=$DIR     (notes: games/$DIR/NOTES.md)"
  exit 0
fi
GD="$R/games/$DLL"
if [ -d "$GD/lib" ] && [ "$FORCE" != --force ]; then echo "games/$DLL already exists (use --force to redo)"; exit 1; fi
say "$DESC  ($GAMEID_NAME → $DLL.so, assets '$DIR')"
extract_code
extract_assets
write_config
UNRES=$(write_notes)
game_repo_init "$GD" "$DLL" "$DESC"
say "games/$DLL ready (its own git repo; publish with: make publish GAME=$DLL)"
echo "    ${UNRES}"
echo "    next: make run GAME=$DLL DEBUG=shots     (notes: games/$DLL/NOTES.md)"
