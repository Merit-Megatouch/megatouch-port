#!/bin/bash
# Inside the loader sandbox, for a Force cabinet (2004–2011 software): what /home/maxx/.xinitrc
# does there, then the loader. Much simpler than the ION's: no layout daemon or sidebar.
cd /home/maxx
rm -f /var/config/commandline
start_bin=${MEGA_LOADER_BIN:-/usr/local/bin/start}

# what the cabinet's network looks like once configured (troubleshooting: /var/merit/fakeio/net.txt)
for i in $(seq 40); do grep -q "eth0:" /proc/net/dev && break; sleep 0.1 2>/dev/null || sleep 1; done
(sleep 25; mkdir -p /var/merit/fakeio; { /sbin/ifconfig -a; cat /proc/net/route; cat /etc/resolv.conf; } > /var/merit/fakeio/net.txt 2>&1) &

xsetroot -solid black 2>/dev/null &
xmodmap /etc/X11/modmap 2>/dev/null
echo "-----------------------------------------------------" >> /var/merit/log
cat /etc/version >> /var/merit/log 2>/dev/null
date >> /var/merit/log
# --videomode W: a normal X window (XWIN). The cabinet's own default (nothing, or F) switches the
# real X server's resolution with XF86VidMode/DGA, which our nested X server doesn't offer.
exec "$start_bin" --videomode "${MEGA_FORCE_VIDEOMODE:-W}" "$@"
