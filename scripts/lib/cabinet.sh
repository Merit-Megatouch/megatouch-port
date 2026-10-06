# Read files from the Megatouch cabinet — either straight from the disk image (debugfs, no
# mounting, no root) or from a snapshot folder made by scripts/snapshot-cabinet.sh.
# Source this after cabinet.conf. Partitions are named root | ion | var | home:
#   root = /   ion = /usr/local/ion_only   var = /var   home = /home
#
#   cab_exists  <part> <path>            true if the file or directory exists
#   cab_dump    <part> <path> <out>      copy one file (symlinks followed)
#   cab_rdump   <part> <path> <outdir>   copy a directory tree into <outdir>/<basename>
#   cab_ls      <part> <path>            names in a directory, one per line
#   cab_readlink <part> <path>           symlink target, empty if not a link

_cab_dev() {
  case $1 in
    root) echo "$IMG?offset=$ROOT_OFF" ;; ion) echo "$IMG?offset=$ION_OFF" ;;
    var) echo "$IMG?offset=$VAR_OFF" ;; home) echo "$IMG?offset=$HOME_OFF" ;;
    *) echo "unknown partition $1" >&2; return 1 ;;
  esac
}
_cab_snap() { echo "$CABINET_SNAPSHOT/$1$2"; }   # snapshot layout: <snapshot>/<part>/<path>

cab_mode() { if [ -n "${CABINET_SNAPSHOT:-}" ] && [ -d "$CABINET_SNAPSHOT" ]; then echo snapshot; else echo image; fi; }

cab_check() {
  if [ "$(cab_mode)" = snapshot ]; then return 0; fi
  [ -f "${IMG:-}" ] || { echo "cabinet image not found: '${IMG:-}' (edit cabinet.conf, or set CABINET_SNAPSHOT)" >&2; return 1; }
}

cab_readlink() {
  if [ "$(cab_mode)" = snapshot ]; then readlink "$(_cab_snap "$1" "$2")" 2>/dev/null || true
  else debugfs -R "stat \"$2\"" "$(_cab_dev "$1")" 2>/dev/null | grep -o 'Fast link dest: "[^"]*"' | cut -d'"' -f2; fi
}

cab_exists() {
  if [ "$(cab_mode)" = snapshot ]; then [ -e "$(_cab_snap "$1" "$2")" ] || [ -L "$(_cab_snap "$1" "$2")" ]
  else debugfs -R "stat \"$2\"" "$(_cab_dev "$1")" 2>/dev/null | grep -q '^Inode:'; fi
}

# Follow symlinks inside the cabinet (absolute targets are cabinet paths, not host paths).
cab_resolve() {
  local part=$1 p=$2 i t
  for i in 1 2 3 4 5 6 7 8; do
    t=$(cab_readlink "$part" "$p")
    [ -z "$t" ] && { echo "$p"; return; }
    case $t in /*) p=$t ;; *) p=$(dirname "$p")/$t ;; esac
  done
  echo "$p"
}

cab_dump() {
  local real; real=$(cab_resolve "$1" "$2")
  if [ "$(cab_mode)" = snapshot ]; then cp "$(_cab_snap "$1" "$real")" "$3"
  else debugfs -R "dump \"$real\" \"$3\"" "$(_cab_dev "$1")" >/dev/null 2>&1; [ -s "$3" ] || [ -f "$3" ]; fi
}

cab_rdump() {
  mkdir -p "$3"
  if [ "$(cab_mode)" = snapshot ]; then cp -a "$(_cab_snap "$1" "$2")" "$3/"
  else (cd "$3" && debugfs -R "rdump \"$2\" ." "$(_cab_dev "$1")" >/dev/null 2>&1) || true; fi
}

cab_ls() {
  if [ "$(cab_mode)" = snapshot ]; then ls -A "$(_cab_snap "$1" "$2")" 2>/dev/null
  else debugfs -R "ls \"$2\"" "$(_cab_dev "$1")" 2>/dev/null | tr -s ' ' '\n' | grep -vE '^$|^\(|^[0-9]+$|^\.\.?$'; fi
}
