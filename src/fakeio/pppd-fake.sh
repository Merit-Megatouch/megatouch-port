#!/bin/sh
# Stand-in for pppd on the 2005 software (Maxx Crown): WvDial starts it once modemfake.so has
# "connected". There is no PPP (the WSL kernel has none): the sandbox's slirp interface is already
# named ppp0 with an address and the default route, so this only does what a finished PPP login
# would have: report it, set the DNS server the "ISP" handed out (usepeerdns; slirp's resolver;
# Crown's /etc/resolv.conf is /var/config/resolv.conf), and stay running until hung up.
case " $* " in *" --version "*) echo "pppd version 2.4.1" >&2; exit 0 ;; esac
for f in /etc/ppp/resolv.conf /var/config/resolv.conf; do
  echo "nameserver 10.0.2.3" > "$f" 2>/dev/null
done
echo "pppd-fake: $*" >&2
echo "Using interface ppp0" >&2
echo "Connect: ppp0 <--> /dev/modem" >&2
echo "local  IP address 10.0.2.100" >&2
echo "remote IP address 10.0.2.2" >&2
trap 'kill $! 2>/dev/null; echo "Connection terminated." >&2; exit 0' TERM INT HUP
while :; do sleep 60 & wait $!; done
