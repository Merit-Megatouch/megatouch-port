#!/bin/sh
# Launch one ported Megatouch game. Symlinked into every game folder as `run`;
# the folder it is invoked from is the game (game.conf, lib/ or player/, data/, runtime/).
D=$(cd "$(dirname "$0")" && pwd)
R="$D/runtime"
[ -f "$D/game.conf" ] || { echo "no game.conf in $D" >&2; exit 1; }
# conf KEY: MEGA_KEY from the environment, else KEY= from game.conf
conf() { eval "v=\${MEGA_$1:-}"; [ -n "$v" ] || v=$(sed -n "s/^$1=//p" "$D/game.conf" | head -1); printf '%s' "$v"; }

if [ "$(conf ENGINE)" = unity ]; then
  # Unity 3.2 games: the cabinet's LinuxPlayer, started the way /usr/local/bin/launcher.sh did,
  # with the launcher.xml settings file the cabinet loader used to write.
  P="$D/player"
  G=$(conf GAME)
  # the loader copy must match the runtime's libc (refreshed after `make setup`)
  cmp -s "$R/ld-linux.so.2" "$P/ld-linux.so.2" || cp "$R/ld-linux.so.2" "$P/ld-linux.so.2"
  [ -L "$P/LinuxPlayer" ] && { echo "player/LinuxPlayer must be a real file: make new GAME=$G FORCE=1" >&2; exit 1; }
  mkdir -p "$D/var"
  {
    echo '<?xml version="1.0" encoding="utf-8" ?>'
    echo '<settings>'
    s() { printf '  <setting identifier="%s" value="%s"%s />\n' "$1" "$2" "${3:+ type=\"$3\"}"; }
    s launcher.gameid "$G"
    s launcher.language "$(conf LANGUAGE)"
    s launcher.window.width "$(conf WIDTH)" System.Int32
    s launcher.window.height "$(conf HEIGHT)" System.Int32
    s launcher.playercount 1 System.Int32
    s launcher.game_type ""
    s launcher.rule_set ""
    s launcher.no_challenges_database true System.Boolean
    s extended_play false System.Boolean
    s allow_music true System.Boolean
    s card_fan false System.Boolean
    s content.filter ""
    s player.0.player_name "${MEGA_PLAYER_NAME:-${USER:-PLAYER}}"
    s player.0.is_anonymous true System.Boolean
    # the game's own cabinet settings (upgrades, challenges...), also under games.currentgame.*
    SG="$P/Data/settings_gamedata.xml"
    if [ -f "$SG" ]; then
      grep '<setting ' "$SG" | sed "s/games\.$G\./games.currentgame./"
      grep '<setting ' "$SG"
    fi
    echo '</settings>'
  } > "$D/var/launcher.xml"
  export LD_LIBRARY_PATH="$P/lib:$R:$R/pulseaudio"
  export LIBGL_DRIVERS_PATH="$R/dri"
  cd "$D/var" || exit 1
  # -nolog: player log to stdout; -popupwindow: as on the cabinet
  exec "$P/ld-linux.so.2" --preload "$P/libmega_unity.so" "$P/LinuxPlayer" "$D/var/launcher.xml" -nolog -popupwindow "$@"
fi

export LD_LIBRARY_PATH="$D/lib:$R:$R/pulseaudio"
export LIBGL_DRIVERS_PATH="$R/dri"
export FONTCONFIG_FILE="$D/data/etc/fonts.conf"
# Pango 1.14 loads its shaping engines from absolute paths listed in pango.modules.
PR="${XDG_RUNTIME_DIR:-/tmp}/megatouch-$(id -u)"
mkdir -p "$PR"
sed "s|/usr/lib/pango/1.5.0/modules|$D/data/pango/modules|" "$D/data/pango/pango.modules.in" > "$PR/pango.modules"
printf '[Pango]\nModuleFiles = %s\n' "$PR/pango.modules" > "$PR/pangorc"
export PANGO_RC_FILE="$PR/pangorc"
# The 2008 engine has a few use-after-free patterns that the old allocator tolerated.
export GLIBC_TUNABLES=glibc.malloc.tcache_count=0
export MEGA_HOME="$D"
exec "$R/ld-linux.so.2" "$D/megatouch-host" "$@"
