#!/bin/bash
# Read or set the loader's NVRAM game options (/var/merit/nvram.dat). Normally you change them in
# Operator Setup; this is for checking them from a terminal and for options the menus never show.
#
#   scripts/loader-option.sh --list                 every option: index, name, value, key licence
#   scripts/loader-option.sh NAME|INDEX             one option (NAME with or without _INX)
#   scripts/loader-option.sh NAME|INDEX 0|1         set it (loader stopped; a backup is taken first)
#   scripts/loader-option.sh tournamaxx             shortcut: TOURNAMAXX_ENABLED = 1
#
# Key licence (from the fake board's key.bin, scripts/loader-key.sh): off/on = locked by the key,
# choice = the operator's (shown in Operator Setup), "-" = no key image.
# nvram.dat: options are bytes at 0x40 + index (0..0x77); the last two bytes are a 16-bit sum of
# bytes 1..0x1881 (NVWrite in start). The running loader keeps NVRAM in memory and rewrites the
# file, so changes are only made while it is stopped.
set -euo pipefail
P=$(cd "$(dirname "$0")/.." && pwd)
VAR="${MEGA_LOADER_VAR:-$P/build/loader/var}"
NV="$VAR/merit/nvram.dat"
KEY="$VAR/merit/fakeio/key.bin"
[ -f "$NV" ] || { echo "no $NV" >&2; exit 1; }
[ $# -ge 1 ] || { sed -n 2,14p "$0"; exit 1; }
[ -x "$P/build/loader/bin/enumtag" ] || { echo "run make loader first" >&2; exit 1; }
names=$("$P/scripts/loader.sh" run /opt/fakeio/enumtag option $(seq 0 119) 2>/dev/null)
opt=$1; val=${2:-}
[ "$opt" = tournamaxx ] && { opt=20; val=1; }
if [ "$opt" != --list ] && ! [[ "$opt" =~ ^[0-9]+$ ]]; then
  want=${opt^^}; want=${want%_INX}
  opt=$(awk -v w="$want" '{n=$2; sub(/_INX$/, "", n)} n == w {print $1}' <<< "$names")
  [ -n "$opt" ] || { echo "unknown option: $1 (see --list)" >&2; exit 1; }
fi
if [ -n "$val" ]; then
  [[ "$val" =~ ^[01]$ ]] || { echo "value must be 0 or 1" >&2; exit 1; }
  [ -z "${MEGA_LOADER_VAR:-}" ] && pgrep -x start >/dev/null && { echo "stop the loader first (it rewrites nvram.dat)" >&2; exit 1; }
  "$P/scripts/loader-backup.sh" "before-option-$opt" >/dev/null
fi
python3 - "$NV" "$KEY" "$opt" "$val" "${MEGAIO_KEY_ID:-8c14fc020040000e}" <<EOF
import os, struct, sys
nvpath, keypath, which, val, idhex = sys.argv[1:6]
names = {int(l.split()[0]): l.split()[1].removesuffix('_INX') for l in """$names""".splitlines() if l.strip()}
lic = {}
if os.path.exists(keypath):                       # decode the key's option bytes (loader-key.sh)
    kid = bytes.fromhex(idhex)
    rol = lambda b, n: ((b << n) | (b >> (8 - n))) & 0xff
    fm = (rol(kid[1], 1), rol(kid[2], 6), rol(kid[3], 3))
    img = open(keypath, 'rb').read()
    blk = struct.unpack_from('<H', bytes(c ^ fm[i % 3] for i, c in enumerate(img[7168:7168 + 16])), 8)[0]
    mask = bytes(kid[1 + k] ^ b'\`bnrou'[k] for k in range(6))
    d = img[blk * 1024:blk * 1024 + 0x400]
    for i in range(120):
        lic[i] = ('off', 'on', 'choice', 'choice')[(d[0x2DD + i] ^ mask[(0x2DD + i) % 6]) & 3]
d = bytearray(open(nvpath, 'rb').read())
show = lambda i: print(f'{i:3} {names.get(i, "?"):42} {d[0x40 + i]}   {lic.get(i, "-")}')
if which == '--list':
    print('idx name                                       val key')
    for i in range(120): show(i)
    sys.exit()
i = int(which)
if val:
    d[0x40 + i] = int(val)
    d[-2:] = struct.pack('<H', sum(d[1:0x1882]) & 0xffff)
    open(nvpath, 'wb').write(d)
show(i)
EOF
