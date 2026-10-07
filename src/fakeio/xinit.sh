#!/bin/bash
# Inside the loader sandbox: the parts of /home/maxx/.xinitrc and /usr/local/bin/layout_start
# the loader depends on, then the loader itself (arguments are passed on).
cd /home/maxx
. /etc/init.d/merit-functions 2>/dev/null || true
. /usr/local/bin/hardware_detect_utils.sh
rm -f /var/config/commandline
start_bin=${MEGA_LOADER_BIN:-/usr/local/bin/start}

# network daemon (normally /etc/init.d/merit-networkmanager, started by .xinitrc); with the
# default slirp network, wait for the virtual eth0 first
for i in $(seq 40); do grep -q "eth0:" /proc/net/dev && break; sleep 0.1; done
nice -n 19 /usr/local/bin/network_manager >/dev/null 2>&1 &
# what the cabinet's network looks like once configured (troubleshooting: /var/merit/fakeio/net.txt)
(sleep 25; { /sbin/ifconfig -a; cat /proc/net/route; cat /etc/resolv.conf; ps -e -o pid,args | grep -i "dhc\|network"; } > /var/merit/fakeio/net.txt 2>&1) &

# database health daemon (normally /etc/init.d/merit-checkdb)
/usr/local/bin/db_state >/dev/null 2>&1 &

# layout_daemon manages the cabinet's windows; start pushes its layout through it
layout_daemon --layout-cmd /opt/fakeio/layout-quiet &
for i in $(seq 50); do [ -S /dev/merit_ipc/layout_daemon ] && break; sleep 0.1; done
# as /usr/local/bin/layout_start, minus the browser and credit-card layouts (neither runs here)
EVENTS="--push start_layout"
IsWidescreen && EVENTS="$EVENTS --clone --compose sidebar_layout"   # side ads + left/right switcher
layout_client $EVENTS --compose loading_layout

exec $start_bin -name merit-start --videomode F "$@"
