#!/bin/bash
# Can the cabinet's sandbox run its own network here? Sourced by loader-setup.sh, loader.sh and
# report.sh.
#
#   sandbox_net_check [full]   prints one line per problem (and the fix) on stderr, returns 1 on a
#                              problem. Quick: inside a sandbox built like the cabinet's (new user
#                              and network namespace, CAP_NET_ADMIN), bring up lo and add an
#                              address. full: also plug slirp4netns in, as loader.sh does, and wait
#                              for its address.
#
# Why: Ubuntu 23.10+ (AppArmor, kernel.apparmor_restrict_unprivileged_userns=1) lets bwrap create
# its namespaces but takes the capabilities away inside them, so the cabinet's own network setup
# (DHCP, routes) fails and the cabinet has no network while everything else works. WSL has no
# AppArmor and never shows it.

sandbox_net_fix() {
  cat >&2 <<'EOF'
  Fix (Ubuntu 23.10 and later), either:
    sudo sysctl -w kernel.apparmor_restrict_unprivileged_userns=0
    echo kernel.apparmor_restrict_unprivileged_userns=0 | sudo tee /etc/sysctl.d/60-megatouch-userns.conf
  (the second line keeps it after a reboot), or allow bwrap alone with an AppArmor profile:
    printf 'abi <abi/4.0>,\ninclude <tunables/global>\nprofile megatouch-bwrap /usr/bin/bwrap flags=(unconfined) {\n  userns,\n}\n' | sudo tee /etc/apparmor.d/megatouch-bwrap
    sudo apparmor_parser -r /etc/apparmor.d/megatouch-bwrap
  Then start the cabinet again.
EOF
}

sandbox_net_check() {
  local mode="${1:-quick}" ok=0 out restrict=""
  command -v bwrap >/dev/null || { echo "sandbox check: bwrap is not installed (sudo apt install bubblewrap)" >&2; return 1; }
  command -v ip >/dev/null || return 0      # nothing to test with (iproute2 missing): no verdict
  [ -r /proc/sys/kernel/apparmor_restrict_unprivileged_userns ] &&
    restrict=$(cat /proc/sys/kernel/apparmor_restrict_unprivileged_userns)
  out=$(bwrap --unshare-user --uid 0 --gid 0 --unshare-net --cap-add CAP_NET_ADMIN \
          --ro-bind / / --dev /dev --proc /proc \
          sh -c 'ip link set lo up && ip addr add 10.99.99.1/32 dev lo && echo NETOK' 2>&1)
  case "$out" in
    *NETOK*) ;;
    *)
      ok=1
      echo "sandbox check: the cabinet's sandbox can't set up its network:" >&2
      echo "  ${out:-no output}" | head -3 >&2
      if [ "$restrict" = 1 ]; then
        echo "  AppArmor restricts unprivileged user namespaces here (kernel.apparmor_restrict_unprivileged_userns=1)." >&2
        sandbox_net_fix
      fi
      return $ok ;;
  esac
  [ "$mode" = full ] || return 0
  local P SLIRP info pid bw slirp_pid addr
  P=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
  SLIRP="$P/toolchain/debug/root/usr/bin/slirp4netns"
  [ -x "$SLIRP" ] || { echo "sandbox check: no slirp4netns (make loader-setup): the cabinet would get the host's network and report it as down" >&2; return 1; }
  info=$(mktemp)
  bwrap --unshare-user --uid 0 --gid 0 --unshare-net --cap-add CAP_NET_ADMIN --ro-bind / / --dev /dev \
    --proc /proc --info-fd 9 sh -c 'for i in $(seq 50); do ip -4 addr show eth0 2>/dev/null | grep -q "inet " && { ip -4 addr show eth0 | sed -n "s/.*inet \([0-9./]*\).*/\1/p"; exit 0; }; sleep 0.1; done; echo none' \
    9>"$info" > "$info.out" 2>&1 &
  bw=$!
  for _ in $(seq 50); do
    pid=$(sed -n 's/.*"child-pid": *\([0-9]*\).*/\1/p' "$info" 2>/dev/null); [ -n "$pid" ] && break; sleep 0.05
  done
  if [ -n "$pid" ]; then
    LD_LIBRARY_PATH="$P/toolchain/debug/root/usr/lib/x86_64-linux-gnu" "$SLIRP" --configure --mtu 1500 \
      --disable-host-loopback "$pid" eth0 > "$info.slirp" 2>&1 &
    slirp_pid=$!
  fi
  wait "$bw" 2>/dev/null
  [ -n "${slirp_pid:-}" ] && kill "$slirp_pid" 2>/dev/null
  addr=$(head -1 "$info.out" 2>/dev/null)
  case "$addr" in
    */*) ;;
    *)
      ok=1
      echo "sandbox check: slirp4netns couldn't give the sandbox a network (address: ${addr:-none})" >&2
      sed 's/^/  slirp4netns: /' "$info.slirp" 2>/dev/null | head -5 >&2
      [ "$restrict" = 1 ] && sandbox_net_fix ;;
  esac
  rm -f "$info" "$info.out" "$info.slirp"
  return $ok
}
