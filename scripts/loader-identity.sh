#!/bin/bash
# This install's cabinet identity: its hardware serial number and MegaNet ID. Every install from
# the same disk image would otherwise be the same machine (serial 043010MRE60023, MegaNet ID
# 10230600310157): a MegaNet server would see them as one cabinet, and linked cabinets need to
# tell each other apart.
#
#   scripts/loader-identity.sh            show it (created on first use)
#   scripts/loader-identity.sh --apply    make the cabinet use it (scripts/loader.sh does this at
#                                         every start; the cabinet must be stopped)
#   scripts/loader-identity.sh --new      give this install a new random identity (asks first;
#                                         a MegaNet server then sees a different machine)
#   scripts/loader-identity.sh --set SERIAL MEGANET_ID   set it explicitly
#
# Stored next to the cabinet's state, outside it: build/loader/var.identity for the main cabinet,
# <dir>.identity for MEGA_LOADER_VAR=<dir>. `make loader-reset` keeps it.
# An install that already has a cabinet (its fake board's EEPROM exists) keeps the identity it
# has, so a machine registered on a MegaNet server stays registered. New installs and new
# cabinets (for example a second one for linking) get a random serial (043010MRE + 5 digits) and
# MegaNet ID (1023 + 10 digits).
#
# Where they live in the cabinet: the serial in the I/O board's EEPROM (written when the fake
# board first creates it: MEGAIO_SERIAL), the MegaNet ID in the cabinet's encrypted network
# settings (changed through its own network library: build/loader/bin/netcfg).
set -euo pipefail
P=$(cd "$(dirname "$0")/.." && pwd)
B="$P/build/loader"
VAR="${MEGA_LOADER_VAR:-$B/var}"
VAR="${VAR%/}"
IDF="$VAR.identity"
EEPROM="$VAR/merit/fakeio/eeprom.bin"

netcfg() { MEGA_LOADER_DISPLAY=none "$P/scripts/loader.sh" run /opt/fakeio/netcfg "$@" 2>/dev/null; }
eeprom_serial() { [ -f "$EEPROM" ] && dd if="$EEPROM" bs=1 skip=$((0x1FE0)) count=16 2>/dev/null | tr -d '\0\377' || true; }
digits() { local n=$1 s=""; while [ ${#s} -lt "$n" ]; do s="$s$(od -An -N4 -tu4 /dev/urandom | tr -d ' ')"; done; echo "${s:0:$n}"; }
running() { [ -z "${MEGA_LOADER_VAR:-}" ] && pgrep -x start >/dev/null; }

write_identity() {   # write_identity SERIAL MEGANET_ID ORIGIN
  cat > "$IDF.new" <<EOF
# This cabinet's identity (scripts/loader-identity.sh). Keep it with the cabinet's backups.
SERIAL=$1
MEGANET_ID=$2
# origin: $3, $(date +%F)
EOF
  mv "$IDF.new" "$IDF"
}

ensure() {
  [ -f "$IDF" ] && return
  [ -d "$VAR/merit" ] || { echo "no cabinet state at $VAR (make loader-setup)" >&2; exit 1; }
  if [ -f "$EEPROM" ]; then
    # an existing cabinet: keep what it already is
    local s m
    s=$(eeprom_serial); m=$(netcfg | sed -n 's/^meganet-id //p')
    write_identity "${s:-043010MRE60023}" "${m:-10230600310157}" "kept from the existing cabinet"
  else
    write_identity "043010MRE$(digits 5)" "1023$(digits 10)" "new install"
  fi
}

load() { SERIAL=$(sed -n 's/^SERIAL=//p' "$IDF"); MEGANET_ID=$(sed -n 's/^MEGANET_ID=//p' "$IDF"); }

case "${1:-}" in
  ""|--show)
    ensure; load
    echo "serial      $SERIAL"
    echo "MegaNet ID  $MEGANET_ID"
    echo "(stored in $IDF)" ;;
  --apply)
    ensure; load
    running && { echo "stop the cabinet first" >&2; exit 1; }
    # the serial goes in when the fake board creates its EEPROM; an existing EEPROM keeps its own
    s=$(eeprom_serial)
    if [ -n "$s" ] && [ "$s" != "$SERIAL" ]; then
      echo "note: the board's EEPROM has serial $s, the identity says $SERIAL (--new/--set rewrite it)" >&2
    fi
    cur=$(netcfg | sed -n 's/^meganet-id //p')
    if [ -n "$cur" ] && [ "$cur" != "$MEGANET_ID" ]; then
      netcfg meganet-id "$MEGANET_ID" >/dev/null && echo "MegaNet ID set to $MEGANET_ID"
    fi ;;
  --new|--set)
    running && { echo "stop the cabinet first" >&2; exit 1; }
    if [ "$1" = --set ]; then
      [ $# -eq 3 ] || { echo "usage: loader-identity.sh --set SERIAL MEGANET_ID" >&2; exit 1; }
      ns=$2; nm=$3; origin="set by hand"
    else
      ns="043010MRE$(digits 5)"; nm="1023$(digits 10)"; origin="new random identity"
      if [ -f "$IDF" ] && [ -t 0 ]; then
        read -r -p "Give this cabinet a new identity? A MegaNet server will see a different machine. [y/N] " a
        case "$a" in y|Y|yes) ;; *) exit 1 ;; esac
      fi
    fi
    [[ "$nm" =~ ^[0-9]{6,20}$ ]] || { echo "MegaNet ID: digits only" >&2; exit 1; }
    [ ${#ns} -le 15 ] || { echo "serial: at most 15 characters" >&2; exit 1; }
    write_identity "$ns" "$nm" "$origin"
    # the board's EEPROM: rewrite the serial in place (the marker stays)
    if [ -f "$EEPROM" ]; then
      python3 - "$EEPROM" "$ns" <<'PY'
import sys
p, s = sys.argv[1], sys.argv[2].encode()
d = bytearray(open(p, 'rb').read())
d[0x1FE0:0x1FF0] = s.ljust(16, b'\0')
open(p, 'wb').write(d)
PY
    fi
    "$0" --apply
    "$0" --show ;;
  *) sed -n 2,13p "$0" | sed 's/^# \{0,1\}//'; exit 1 ;;
esac
