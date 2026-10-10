#!/bin/bash
# One-command install of the Megatouch cabinet on Ubuntu (desktop, touchscreen box or WSL2).
#
#   bash <(curl -fsSL https://raw.githubusercontent.com/Merit-Megatouch/megatouch-port/main/scripts/install.sh)
#   scripts/install.sh [--image PATH] [--kiosk] [--no-shortcut] [--yes] [--dir DIR]
#
#   --image PATH    the cabinet disk image (asked for if not given and not set yet)
#   --kiosk         dedicated box: start the cabinet fullscreen at login and keep it running
#                   (scripts/cabinet.sh autostart on); set up automatic login yourself
#   --no-shortcut   don't create a desktop shortcut
#   --yes           don't ask before installing Ubuntu packages
#   --dir DIR       where to clone when run from outside a checkout (default ~/megatouch-port)
#
# Steps: Ubuntu packages (sudo, only the missing ones) → clone (if needed) → image path in
# cabinet.local.conf, checked → make setup → make loader-setup → make loader → shortcut / kiosk.
# Safe to run again: everything already done is skipped.
set -euo pipefail
REPO_URL="https://github.com/Merit-Megatouch/megatouch-port"
IMAGE="" KIOSK=0 SHORTCUT=1 YES=0 DIR="$HOME/megatouch-port"
while [ $# -gt 0 ]; do
  case "$1" in
    --image) IMAGE="$2"; shift 2 ;;
    --kiosk) KIOSK=1; shift ;;
    --no-shortcut) SHORTCUT=0; shift ;;
    --yes|-y) YES=1; shift ;;
    --dir) DIR="$2"; shift 2 ;;
    -h|--help)   # (not read from "$0": that is bash itself when piped from curl)
      cat <<'USAGE'
Install the Megatouch cabinet (Ubuntu, touchscreen box or WSL2).
  scripts/install.sh [--image PATH] [--kiosk] [--no-shortcut] [--yes] [--dir DIR]
  --image PATH    the cabinet disk image (asked for if not given and not set yet)
  --kiosk         dedicated box: start fullscreen at login, restart on crash or hang
  --no-shortcut   don't create a desktop shortcut
  --yes           don't ask before installing Ubuntu packages
  --dir DIR       where to clone when run from outside a checkout (default ~/megatouch-port)
USAGE
      exit 0 ;;
    *) echo "unknown option: $1 (see --help)" >&2; exit 1 ;;
  esac
done
say() { printf '\n\033[1m== %s\033[0m\n' "$*"; }
ask() {   # ask "question" → 0 for yes; reads the terminal even when this script is piped
  [ "$YES" = 1 ] && return 0
  local a; read -r -p "$1 [Y/n] " a < /dev/tty || return 1
  case "$a" in ""|y|Y|yes) return 0 ;; *) return 1 ;; esac
}

# ---------------------------------------------------------------- system
say "Checking the system"
. /etc/os-release 2>/dev/null || true
case "${ID:-}${ID_LIKE:-}" in
  *ubuntu*) ;;
  *) echo "This installer supports Ubuntu (also under WSL2). Found: ${PRETTY_NAME:-unknown}." >&2
     echo "Other distributions need apt-get and dpkg-deb; see docs/guides/quick-start.md." >&2
     ask "Try anyway?" || exit 1 ;;
esac
[ "$(uname -m)" = x86_64 ] || { echo "Needs a 64-bit Intel/AMD (x86_64) machine." >&2; exit 1; }
WSL=0; grep -qi microsoft /proc/version 2>/dev/null && WSL=1
if [ "$WSL" = 1 ]; then
  echo "Running under WSL ($WSL_DISTRO_NAME)."
  [ -n "${DISPLAY:-}${WAYLAND_DISPLAY:-}" ] || echo "Warning: no WSLg display. In PowerShell: wsl --update, then wsl --shutdown." >&2
fi

PKGS="git gcc g++ make python3 python3-venv curl e2fsprogs binutils bubblewrap"
missing=""
for p in $PKGS; do dpkg -s "$p" >/dev/null 2>&1 || missing="$missing $p"; done
if [ -n "$missing" ]; then
  say "Installing Ubuntu packages:$missing"
  ask "Install them now (needs your password)?" || { echo "Install them with: sudo apt install$missing" >&2; exit 1; }
  sudo apt-get update
  sudo apt-get install -y $missing
else
  echo "Packages: all present."
fi

# ---------------------------------------------------------------- the repository
here=""
if [ -n "${BASH_SOURCE[0]:-}" ] && [ -f "$(dirname "${BASH_SOURCE[0]}")/../Makefile" ] \
   && grep -q loader-setup "$(dirname "${BASH_SOURCE[0]}")/../Makefile" 2>/dev/null; then
  here=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
fi
if [ -z "$here" ]; then
  if [ -d "$DIR/.git" ]; then
    say "Updating $DIR"; git -C "$DIR" pull --ff-only || true
  else
    say "Cloning into $DIR"; git clone "$REPO_URL" "$DIR"
  fi
  here="$DIR"
fi
cd "$here"
case "$here" in /mnt/*) echo "Warning: $here is on a Windows drive; it will be slow. ~/megatouch-port is better." >&2 ;; esac

# ---------------------------------------------------------------- the image
say "The cabinet disk image"
. ./cabinet.conf
if [ -z "$IMAGE" ] && [ -n "${IMG:-}" ] && [ -f "$IMG" ]; then
  echo "Using $IMG (from cabinet.local.conf)."
  IMAGE="$IMG"
fi
while [ -z "$IMAGE" ] || [ ! -f "$IMAGE" ]; do
  [ -n "$IMAGE" ] && echo "Not found: $IMAGE"
  echo "Path of 'Megatouch ION 2014 HDD Keyless.img' (Windows drives are under /mnt, e.g."
  echo "/mnt/e/Images/Megatouch ION 2014 HDD Keyless.img):"
  read -r -p "> " IMAGE < /dev/tty || exit 1
  IMAGE="${IMAGE%\"}"; IMAGE="${IMAGE#\"}"; IMAGE="${IMAGE%\'}"; IMAGE="${IMAGE#\'}"
done
if ! debugfs -R "stat /usr/local/bin/start" "$IMAGE?offset=$ROOT_OFF" 2>/dev/null | grep -q "Type: regular"; then
  echo "This image doesn't look like the Megatouch ION 2014 Keyless image: no cabinet software at" >&2
  echo "the expected place. Other images need other partition offsets (docs/guides/troubleshooting.md)." >&2
  exit 1
fi
if [ "$(. ./cabinet.local.conf 2>/dev/null; echo "${IMG:-}")" != "$IMAGE" ]; then
  { grep -v '^IMG=' cabinet.local.conf 2>/dev/null || true; printf 'IMG="%s"\n' "$IMAGE"; } > cabinet.local.conf.new
  mv cabinet.local.conf.new cabinet.local.conf
fi
echo "Image OK: $IMAGE"

# ---------------------------------------------------------------- build
say "Setting up (first time 10-20 minutes, mostly downloads)"
make setup
make loader-setup
make loader

# ---------------------------------------------------------------- start it easily
if [ "$SHORTCUT" = 1 ] && [ "$KIOSK" = 0 ]; then
  if [ "$WSL" = 1 ] && command -v powershell.exe >/dev/null; then
    say "Windows desktop shortcut"
    if ask "Put a 'Megatouch ION' shortcut on the Windows desktop?"; then
      args="-d $WSL_DISTRO_NAME -e bash -lc \"cd '$here' && make loader-run\""
      powershell.exe -NoProfile -Command "\$s=(New-Object -ComObject WScript.Shell).CreateShortcut([Environment]::GetFolderPath('Desktop')+'\\Megatouch ION.lnk'); \$s.TargetPath='C:\\Windows\\System32\\wsl.exe'; \$s.Arguments='$(printf '%s' "$args" | sed "s/'/''/g")'; \$s.Description='Megatouch ION cabinet'; \$s.Save()" \
        && echo "Shortcut created." || echo "Couldn't create the shortcut; see the operator guide for doing it by hand." >&2
    fi
  elif [ -n "${DISPLAY:-}${WAYLAND_DISPLAY:-}" ]; then
    say "Desktop launcher"
    if ask "Add 'Megatouch ION' to the applications menu?"; then
      d="${XDG_DATA_HOME:-$HOME/.local/share}/applications"; mkdir -p "$d"
      cat > "$d/megatouch-cabinet.desktop" <<EOF
[Desktop Entry]
Type=Application
Name=Megatouch ION
Comment=Megatouch ION cabinet
Exec=bash -c "cd '$here' && make loader-run"
Terminal=false
Categories=Game;
EOF
      echo "Added: $d/megatouch-cabinet.desktop"
    fi
  fi
fi
if [ "$KIOSK" = 1 ]; then
  say "Kiosk mode"
  scripts/cabinet.sh autostart on
fi

say "Done"
cat <<EOF
Start the cabinet:   cd $here && make loader-run       (kiosk: make kiosk)
Operator guide:      $here/docs/guides/operator-guide.md
F1 = Operator Setup, F5-F8 = coins, F9 = operator key, F10 = player key, F11 = fullscreen.
EOF
