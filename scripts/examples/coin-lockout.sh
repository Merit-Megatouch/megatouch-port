#!/bin/sh
# Coin lockout: energise a relay (wired to the coin mech's inhibit input) while the cabinet
# refuses coins.
#   cabinet.local.conf:  HW_ON_LOCKOUT="scripts/examples/coin-lockout.sh"
# Needs `usbrelay`; set RELAY to yours. Check your coin mech's manual for which way inhibit works.
RELAY=${RELAY:-HURTM_2}
if [ "$MEGA_EVENT_COINS_LOCKED" = true ]; then usbrelay "$RELAY=1"; else usbrelay "$RELAY=0"; fi >/dev/null 2>&1
