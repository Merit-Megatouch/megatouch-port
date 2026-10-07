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
  # working directory = the game's /var/merit/games/<game> (the shim maps /var/merit into data/)
  V="$D/data/var/merit/games/$G"
  mkdir -p "$V"
  case "$(conf LANGUAGE)" in english|"") LANG_ID=en-US ;; french) LANG_ID=fr-CA ;; spanish) LANG_ID=es-MX ;; *) LANG_ID=$(conf LANGUAGE) ;; esac
  # Same keys and types as a launcher.xml the cabinet left in /var/merit/games/g_rock_mahjong.
  {
    echo '<?xml version="1.0" encoding="utf-8"?>'
    echo '<settings>'
    s() { printf '  <setting identifier="%s" value="%s" type="%s" />\n' "$1" "$2" "${3:-System.String}"; }
    s menu.MenuApplication.MainMenu.Watchdog.timeout 00:01:00 System.TimeSpan
    s launcher.entry_assembly_name start
    s launcher.version 1.0
    s launcher.gameid "$G"
    s launcher.game_type GAME_TYPE_AMUSEMENT
    s launcher.categoryid "$(conf CATEGORY)"
    s launcher.gamestarttime "$(date -u '+%Y-%m-%d %H:%M:%S.000 Z')" System.DateTime
    s launcher.language "$LANG_ID"
    s launcher.country "${MEGA_COUNTRY:-usa}"
    s launcher.window.width "$(conf WIDTH)" System.Int32
    s launcher.window.height "$(conf HEIGHT)" System.Int32
    s launcher.playercount "${MEGA_PLAYERS:-1}" System.Int32
    s launcher.autocontinue False System.Boolean
    PN="${MEGA_PLAYER_NAME:-PLAYER 1}"
    for i in 1 2 3 4; do      # per-player keys: "player.<n>." + the property's own key (1-based)
      s player.$i.player.guid ""
      s player.$i.player.is_anonymous True System.Boolean
      s player.$i.player.username ""
      [ $i = 1 ] && s player.$i.player.player_name "$PN" || s player.$i.player.player_name ""
      s player.$i.player.avatarlocation ""
    done
    s player.1.player.upgrade ""
    s launcher.processid $$ System.Int32
    s launcher.menuhandle 0 System.Int32
    s launcher.menus CoinJam,Continue,Loading,Quit,Watchdog ami.megatouch.configuration.CSArray
    s launcher.messages menupromptopen,menupromptclosed,quit,cancel_quit,continue ami.megatouch.configuration.CSArray
    s launcher.messaging.port 13572 System.Int32
    s launcher.messaging.server_port 13571 System.Int32
    s launcher.messaging.fifo_path ""
    s launcher.messaging.pipe_name ""
    s launcher.no_challenges_database true System.Boolean
    s player.1.games.*.awardedChallenges "" ami.megatouch.configuration.CSArray
    s games.currentgame.allow_music true
    s allow_music true System.Boolean
    s extended_play false System.Boolean
    s card_fan false System.Boolean
    s launcher.challenges.tcp.ip_address 127.0.0.1
    s launcher.challenges.tcp.port 8192 System.Int32
    s games.$G.extended_play false System.Boolean
    s games.$G.card_fan False System.Boolean
    s leaderboard.local.all.currentgame.highest.name ""
    s leaderboard.local.all.currentgame.highest.avatar_path ""
    s content.filter ""
    s erotic.filter ""
    s erotic.rating ""
    # the game's own cabinet settings (upgrades, challenges...), also under games.currentgame.*
    SG="$P/Data/settings_gamedata.xml"
    if [ -f "$SG" ]; then
      grep '<setting ' "$SG" | sed "s/games\.$G\./games.currentgame./"
      grep '<setting ' "$SG"
      # per-player settings: the loader gave each player a copy under player.<n>.games.<game>.
      # (seeds get a fresh random value, as a real launcher.xml on the cabinet shows)
      grep '<setting .*per_player="true"' "$SG" | sed "s/identifier=\"games\.$G\./identifier=\"player.1.games.$G./" \
        | sed -E "/rand_seed\"/ s/value=\"0\"/value=\"$(( (RANDOM % 30000) + 1 ))\"/"
    fi
    echo '</settings>'
  } > "$V/launcher.xml"
  ln -sfn "data/var/merit/games/$G" "$D/var" 2>/dev/null || true
  export MEGA_DATA="$D/data"
  export LD_LIBRARY_PATH="$P/lib:$R:$R/pulseaudio"
  export LIBGL_DRIVERS_PATH="$R/dri"
  cd "$V" || exit 1
  # -nolog: player log to stdout; -popupwindow: as on the cabinet. The settings file is passed by
  # its cabinet path; libmega_unity.so's filesystem shim maps it into data/.
  exec "$P/ld-linux.so.2" --preload "$P/libmega_unity.so" "$P/LinuxPlayer" "/var/merit/games/$G/launcher.xml" -nolog -popupwindow "$@"
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
