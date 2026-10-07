#!/bin/bash
# shots.sh <prefix>: save every mapped top-level window as <prefix>-<id>.png (run inside the sandbox)
/opt/fakeio/xshot -l | while read id geo state name; do
  [ "$state" = mapped ] && /opt/fakeio/xshot "$1-$id.png" "$id" >/dev/null && echo "$1-$id.png $geo $name"
done
