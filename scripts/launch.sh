#!/bin/sh
# Launch one ported Megatouch game. Symlinked into every game folder as `run`;
# the folder it is invoked from is the game (game.conf, lib/, data/, runtime/).
D=$(cd "$(dirname "$0")" && pwd)
R="$D/runtime"
[ -f "$D/game.conf" ] || { echo "no game.conf in $D" >&2; exit 1; }
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
