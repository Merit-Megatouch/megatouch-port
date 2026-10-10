#!/bin/sh
# One relay click per coin-meter pulse: drives a real electromechanical counter.
#   cabinet.local.conf:  HW_ON_METER="scripts/examples/click-meter.sh"
# Needs a HID USB relay board and the `usbrelay` tool (`usbrelay` alone lists the relay names);
# set RELAY to yours. On a Raspberry Pi, replace the two usbrelay lines with gpioset (see
# docs/guides/connectors.md). A counter wants about 50-100 ms on and off per count.
RELAY=${RELAY:-HURTM_1}
[ "$MEGA_EVENT_METER" = coin ] || exit 0          # "tournamaxx" is the second meter
i=0
while [ "$i" -lt "${MEGA_EVENT_PULSES:-0}" ]; do
  usbrelay "$RELAY=1" >/dev/null 2>&1; sleep 0.08
  usbrelay "$RELAY=0" >/dev/null 2>&1; sleep 0.08
  i=$((i + 1))
done
