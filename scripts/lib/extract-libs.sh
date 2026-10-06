#!/bin/bash
# Copy 32-bit libraries and everything they depend on (recursively, via ELF NEEDED) out of
# the cabinet into <outdir>. Libraries the modern 32-bit runtime supplies are skipped:
# glibc, libstdc++, libgcc_s, and zlib (the cabinet's 2008 zlib breaks modern libpng).
#
#   scripts/lib/extract-libs.sh <outdir> <libname>...
#   e.g. extract-libs.sh games/g_trix/lib g_trix.so
set -uo pipefail
ROOTDIR=$(cd "$(dirname "$0")/../.." && pwd)
. "$ROOTDIR/cabinet.conf"
. "$ROOTDIR/scripts/lib/cabinet.sh"
cab_check || exit 1

OUT=$1; shift
mkdir -p "$OUT"
SKIP='^(libz|libc|libm|libdl|libpthread|librt|libstdc\+\+|libgcc_s|ld-linux|libresolv|libnsl|libutil|libcrypt)\.so'
SEARCH="/usr/local/lib /lib /usr/lib /usr/X11R6/lib"

declare -A seen
queue=("$@")
while [ ${#queue[@]} -gt 0 ]; do
  name=${queue[0]}; queue=("${queue[@]:1}")
  [ -n "${seen[$name]:-}" ] && continue
  seen[$name]=1
  [[ $name =~ $SKIP ]] && continue
  if [ ! -s "$OUT/$name" ]; then
    for d in $SEARCH; do
      if cab_exists root "$d/$name"; then cab_dump root "$d/$name" "$OUT/$name"; break; fi
    done
  fi
  if [ -s "$OUT/$name" ]; then
    for dep in $(readelf -d "$OUT/$name" 2>/dev/null | grep NEEDED | grep -o '\[.*\]' | tr -d '[]'); do queue+=("$dep"); done
  else
    echo "not found in cabinet: $name" >&2
  fi
done
