#!/bin/bash
# Copy everything porting needs out of the cabinet disk image into ./cabinet/, so the
# 60 GB image is no longer required (scripts use the snapshot automatically when it exists).
#
#   scripts/snapshot-cabinet.sh          engine + all game code + cabinet data + game assets
#                                        (skips the attract videos)
#   scripts/snapshot-cabinet.sh --all    also the attract videos (everything on the games partition, ~6.7 GB)
set -euo pipefail
R=$(cd "$(dirname "$0")/.." && pwd)
. "$R/cabinet.conf"
SNAP=$CABINET_SNAPSHOT
unset CABINET_SNAPSHOT            # read from the image while building the snapshot
. "$R/scripts/lib/cabinet.sh"
cab_check
ALL=${1:-}
say() { printf '\033[1m==>\033[0m %s\n' "$*"; }
mkdir -p "$SNAP"/{root,ion,var,home}

say "cabinet programs (/usr/local/bin: loader, daemons, helpers)"
cab_rdump root /usr/local/bin "$SNAP/root/usr/local"

say "engine and game libraries (/usr/local/lib)"
cab_rdump root /usr/local/lib "$SNAP/root/usr/local"

say "system libraries (/lib, /usr/lib shared objects, Pango modules)"
cab_rdump root /lib "$SNAP/root"
mkdir -p "$SNAP/root/usr/lib"
for f in $(cab_ls root /usr/lib | grep '\.so'); do
  if [ -n "$(cab_readlink root "/usr/lib/$f")" ]; then
    ln -sfn "$(cab_readlink root "/usr/lib/$f")" "$SNAP/root/usr/lib/$f"
  else
    cab_dump root "/usr/lib/$f" "$SNAP/root/usr/lib/$f" || true
  fi
done
cab_rdump root /usr/lib/pango "$SNAP/root/usr/lib"
cab_rdump root /etc/pango "$SNAP/root/etc"

say "cabinet data (/usr/local/gamedata, /var/merit, fonts.conf)"
cab_rdump root /usr/local/gamedata "$SNAP/root/usr/local"
cab_rdump root /usr/local/games "$SNAP/root/usr/local"
for d in locale settings; do cab_rdump var "/merit/$d" "$SNAP/var/merit"; done
mkdir -p "$SNAP/home/maxx"; cab_dump home /maxx/.fonts.conf "$SNAP/home/maxx/.fonts.conf"

say "game assets (/usr/local/ion_only/games)"
mkdir -p "$SNAP/ion/games"
for g in $(cab_ls ion /games); do
  if [ "$ALL" != "--all" ]; then
    [ "$g" = idle ] && continue                         # attract-mode videos
  fi
  printf '  %s\n' "$g"
  cab_rdump ion "/games/$g" "$SNAP/ion/games"
done

say "snapshot complete: $(du -sh "$SNAP" | cut -f1) in $SNAP"
echo "The scripts now read from it; the disk image can be archived."
