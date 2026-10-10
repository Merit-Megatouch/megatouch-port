# Connecting real hardware and services

The cabinet's hardware is simulated, but the simulation can be wired to the real world:

- **Outputs:** lights that follow the cabinet's own light show, coin meters that click, home
  automation that knows when a game starts.
- **Inputs:** real coin acceptors and buttons, a real iButton reader for Merit keys.
- **Books printer:** printouts saved as text files.

| What | Cabinet part it stands in for | How |
| --- | --- | --- |
| [Light show](#light-show) | The ION light-show kit (LED lighting on the I/O board) | `MEGAIO_LIGHTSHOW=1`; lights through WLED, MQTT or a command |
| [Event feed](#the-event-feed) | Everything the cabinet does with its hardware, plus games starting and ending | always on: `events.jsonl`; passed on by the hardware bridge |
| [Commands](#run-a-command-on-an-event) | Coin meters, lockout coils, anything you can switch from Linux | `HW_ON_METER="…"`, `HW_ON_LOCKOUT="…"`, … |
| [MQTT](#mqtt-home-automation) | – | `HW_MQTT=broker` (Home Assistant, Node-RED, …) |
| [Webhook](#webhook) | – | `HW_WEBHOOK=URL` |
| [WLED lights](#wled-lights) | The light-show kit's LEDs | `HW_WLED=host` |
| [Keyboards, encoders, coin acceptors](#keyboards-encoders-and-coin-acceptors) | Coin mechs, SETUP/CALIBRATE buttons | `HW_INPUT=/dev/input/…` |
| [GPIO](#gpio-raspberry-pi-and-similar) | The same, wired to GPIO pins | `HW_GPIO="gpiochip0:17=coin1 …"` |
| [iButton reader](#a-real-ibutton-reader) | The front key reader | `HW_IBUTTON=on` |
| [Books printer](#books-printer) | The handheld books printer (Germany: "Minidrucker") | **F4** or `megaio print`; files in `printouts/` |

Not covered, because the services behind them are gone or need a payment provider: the
credit-card reader, the TouchTunes and Rowe jukebox links, and the dial-up modem.

- [Setting it up](#setting-it-up)
- [The event feed](#the-event-feed)
- [Outputs](#outputs)
- [Inputs](#inputs)
- [Light show](#light-show)
- [Books printer](#books-printer)
- [Checking and troubleshooting](#checking-and-troubleshooting)

## Setting it up

Every connector is a setting. Put the settings in `cabinet.local.conf` (in the project folder,
next to `IMG=`), one per line:

```bash
MEGAIO_LIGHTSHOW=1
HW_MQTT=192.168.1.10
HW_ON_METER="scripts/examples/click-meter.sh"
```

Settings in the environment win over the file
(`HW_WEBHOOK=http://… make loader-run`). After a change, restart the cabinet.

When any `HW_` setting is made, starting the cabinet also starts the **hardware bridge**
(`scripts/hwbridge.py`, Python, no extra packages) and prints
`hardware bridge on (log: …/merit/fakeio/hwbridge.log)`. The bridge stops with the cabinet.
`HW_BRIDGE=off` keeps it from starting without removing the settings.

Kiosk mode (`make kiosk`) uses the same settings. Extra cabinets (`MEGA_LOADER_VAR=…`) each
start their own bridge for their own state, all reading the same `cabinet.local.conf`. Give
them different settings through the environment when they need different hardware.

## The event feed

The fake I/O board writes everything the cabinet does with its hardware to
`build/loader/var/merit/fakeio/events.jsonl`, one JSON object per line. A small hook in the
cabinet adds games starting and ending. The log is always written, bridge or not. It's kept
under 5 MB: the older part moves to `events.jsonl.1`.

```json
{"t":1791651484.800,"type":"coin","channel":1,"pulses":2}
{"t":1791651484.813,"type":"lights","cmd":"play","sequence":5,"repeat":true}
{"t":1791651484.899,"type":"meter","meter":"coin","pulses":2,"total":208}
{"t":1791651758.337,"type":"game","state":"start","id":280,"tag":"G_HIDDEN_OBJECT_3"}
{"t":1791652087.103,"type":"game","state":"end","id":280,"tag":"G_HIDDEN_OBJECT_3"}
```

Watch it live while the cabinet runs:

```bash
scripts/hwbridge.py --watch
```

| `type` | When | Main fields |
| --- | --- | --- |
| `board` | the cabinet opened the I/O board (start-up) | `lightshow` |
| `coin` | a coin or bill pulse reached the cabinet | `channel` (1–8), `pulses` |
| `meter` | the cabinet pulsed a mechanical meter | `meter` (`coin` or `tournamaxx`), `pulses`, `total` |
| `lockout` | coin/bill lockout switched | `coins_locked`, `bills_locked` |
| `outputs` | an output byte other than the meters changed | `bytes` |
| `button` | SETUP or CALIBRATE pressed or released | `button`, `pressed` |
| `key` | a key touched or left the front reader | `state` (`on`/`off`), `kind`, `id` |
| `lights` | the cabinet sent the light-show kit a command | `cmd` and its values |
| `printer` | the books printer was plugged in or out | `plugged` |
| `print` | a books printout finished | `file` |
| `game` | a game started or ended | `state`, `id`, `tag` |
| `screen` | one of the cabinet's own screens (attract loop `IDLE`, Operator Setup `OPSETUP`, `CALIBRATE`, …) started or ended | `state`, `id`, `tag` |

Every field is described in the [event reference](../reference/events.md). Game IDs and tags are
listed in [games](../reference/games.md); a game's tag is its `GameIds` name, such as
`G_HIDDEN_OBJECT_3` for Photo Hunt Expedition III.

## Outputs

### Run a command on an event

```bash
HW_ON_METER="scripts/examples/click-meter.sh"     # one event type (HW_ON_<TYPE>)
HW_ON_GAME="logger -t megatouch game \$MEGA_EVENT_STATE \$MEGA_EVENT_TAG"
HW_ON_EVENT="cat >> /tmp/everything.jsonl"        # every event
```

The command runs through `/bin/sh` in the project folder. It gets:

- **On standard input:** the event's JSON.
- **In the environment:** `MEGA_EVENT` (the same JSON) and one `MEGA_EVENT_<FIELD>` per field.
  For example, a meter event sets `MEGA_EVENT_TYPE=meter`, `MEGA_EVENT_METER=coin`,
  `MEGA_EVENT_PULSES=2` and `MEGA_EVENT_TOTAL=208`. Lists are space-separated, true/false are
  lower case, and a printout's full path is in `MEGA_EVENT_PATH`.

Events of one type run one after another, in order, so a meter command can take its time
pulsing a relay. Anything a command prints goes to `hwbridge.log`.

**Example: a real coin meter on a USB relay.** A mechanical counter wants a pulse of about
50–100 ms per count. With the common HID USB relay boards (`usbrelay` package):

```bash
#!/bin/sh
# scripts/examples/click-meter.sh: one relay click per meter pulse (coin meter only)
[ "$MEGA_EVENT_METER" = coin ] || exit 0
i=0
while [ $i -lt "$MEGA_EVENT_PULSES" ]; do
  usbrelay HURTM_1=1 >/dev/null; sleep 0.08
  usbrelay HURTM_1=0 >/dev/null; sleep 0.08
  i=$((i + 1))
done
```

(`usbrelay` with no arguments lists your board's relay names.) On a Raspberry Pi,
`gpioset -t 80ms,0 -c gpiochip0 5=1` (libgpiod 2) or `gpioset --mode=time --usec=80000 gpiochip0 5=1`
(libgpiod 1) does the same on GPIO 5.

**Example: coin lockout.** Real coin mechs have an inhibit input. Drive it from
`HW_ON_LOCKOUT="scripts/examples/coin-lockout.sh"`, which switches a relay with
`MEGA_EVENT_COINS_LOCKED`. The examples in `scripts/examples/` are starting points: change the
relay names or GPIO lines to yours.

### MQTT (home automation)

```bash
HW_MQTT=192.168.1.10            # or host:port (default 1883)
HW_MQTT_TOPIC=megatouch/bar     # default: megatouch/<this PC's host name>
HW_MQTT_USER=…                  # if the broker wants a login
HW_MQTT_PASSWORD=…
```

| Topic | Payload |
| --- | --- |
| `<topic>/event/<type>` | every event, as JSON (`megatouch/bar/event/coin`, `…/event/game`, …) |
| `<topic>/state` (retained) | the cabinet's current state: `game`, `screen`, `lights`, `meters`, `coins_locked`, `printer`, `key` |
| `<topic>/status` (retained) | `online` while the bridge runs; the broker sets `offline` if it drops |

Plain MQTT 3.1.1, QoS 0, no TLS. Keep the broker on your local network or VPN.

The bridge only publishes. It doesn't subscribe to anything, so nothing on the network can
press buttons or add credits.

**Home Assistant:** an MQTT sensor on `megatouch/bar/state` with
`value_template: "{{ value_json.game or 'menu' }}"` shows what's being played; an automation
on `megatouch/bar/event/coin` can flash a light when someone pays.

### Webhook

```bash
HW_WEBHOOK=http://192.168.1.10:8123/api/webhook/megatouch
HW_WEBHOOK_TYPES="coin game print"      # optional: only these event types
```

Each event is POSTed as JSON (`Content-Type: application/json`).

### WLED lights

[WLED](https://kno.wled.ge) runs on cheap ESP32/ESP8266 LED controllers. With `HW_WLED` the
cabinet's light show drives your strips:

```bash
MEGAIO_LIGHTSHOW=1                    # the cabinet only sends light commands when the kit is there
HW_WLED="192.168.1.50 192.168.1.51"   # one or more WLED controllers
HW_WLED_PRESETS="0:1 4:3 5:2"         # optional: light-show sequence → your WLED preset
HW_WLED_IDLE=4                        # optional: preset when the show stops (default: off)
```

| Cabinet command | WLED gets |
| --- | --- |
| play sequence *n* | your preset for *n*, or a built-in effect: 0 Rainbow (attract loop), 2/3 Fireworks (high-score animation), 4 Glitter (high-score entry), 5 Blink (power-on, coins in), 1 Breathe |
| set colour *r,g,b* | solid colour |
| brightness *v* | `bri`: values up to 100 are taken as percent and scaled to 0–255 (the cabinet's range isn't confirmed) |
| lights on/off | `on` true/false |
| stop | `HW_WLED_IDLE`'s preset, or off |

Other lights (Philips Hue, OpenRGB, DMX) work through MQTT and your home-automation system, or
through `HW_ON_LIGHTS` and a command for that system.

## Inputs

Inputs act on the simulated board directly, whatever window has focus. Each input is mapped to
an action:

| Action | Does |
| --- | --- |
| `coin1` … `coin8` | one pulse into that coin/bill channel |
| `setup`, `calibrate` | the SETUP / CALIBRATE button, held while the input is held |
| `print` | plug in the books printer (it prints on the attract screen) |
| `operator-key`, `player-key` | the operator key (`MEGAIO_OPERATOR_KEY`) or player key (`MEGAIO_PLAYER_KEY`) on the reader while held |

### Keyboards, encoders and coin acceptors

Arcade keyboard encoders (I-PAC, Zero Delay boards) and many coin acceptors with a USB
interface show up as keyboards; gamepads and button boxes as joysticks. List them:

```bash
ls -l /dev/input/by-id/
```

```bash
HW_INPUT=/dev/input/by-id/usb-Ultimarc_I-PAC_2-event-kbd     # several: space-separated; * works
HW_INPUT_MAP="KEY_5=coin1 KEY_6=coin2 KEY_F1=setup KEY_F2=calibrate KEY_P=print"
HW_INPUT_GRAB=1     # the device's keys go only to the cabinet, not to the desktop
```

- **Default map:** without `HW_INPUT_MAP`, the cabinet's hotkeys: F1 setup, F2 calibrate,
  F4 print, F5–F8 coins 1–4, F9 operator key, F10 player key.
- **Key names:** `KEY_…` and `BTN_…` are Linux input names; `scripts/hwbridge.py --input-names`
  lists them. A number works too (the key code).
- **Permissions:** reading `/dev/input` needs your user in the `input` group:
  `sudo usermod -aG input $USER`, then log in again.
- **Plugging in:** a device that's unplugged or not there yet is waited for.
- **WSL:** USB devices reach WSL only through `usbipd` (`usbipd attach --wsl --busid …` on Windows).

### GPIO (Raspberry Pi and similar)

Coin mechs with a pulse output, microswitches and buttons can go straight to GPIO pins:

```bash
HW_GPIO="gpiochip0:17=coin1 gpiochip0:27=setup gpiochip0:22=calibrate"
HW_GPIO_ACTIVE=low      # default: pressed = pin pulled to ground (internal pull-up on); or high
```

- **Tool:** needs `gpiomon` (`sudo apt install gpiod`); libgpiod 1 and 2 both work.
- **Debounce:** edges closer than 20 ms are ignored.
- **Coin pulses:** a typical coin mech pulses 30–100 ms per coin, so each pulse is one coin.
- **Wiring:** a coin mech's output is often 12 V open collector. Wire it to pull the pin to
  ground; never put 12 V on a GPIO pin.

### A real iButton reader

Megatouch operator and player keys are Dallas iButtons (DS1991, family `02`). A USB 1-Wire
adapter (DS9490R / DS2490) with a key probe reads them through Linux's 1-Wire driver:

```bash
HW_IBUTTON=on                                   # watches /sys/bus/w1/devices
HW_OPERATOR_KEYS="0247454d000000cc"             # these are operator keys; the rest are player keys
```

1. Plug in the adapter. `lsmod | grep ds2490` should list the driver. If not, run
   `sudo modprobe ds2490 wire`.
2. Speed up key detection. The driver normally looks for keys every 10 s, so set it to every
   1 s and forget a removed key fast:

   ```bash
   echo "options wire timeout=1 slave_ttl=1" | sudo tee /etc/modprobe.d/ibutton.conf
   sudo modprobe -r ds2490 wire; sudo modprobe ds2490
   ```

3. Touch a key. `hwbridge.log` prints `ibutton: key 02… touching (player)`. Copy that ID into
   `HW_OPERATOR_KEYS` if it's an operator key.
4. Register it in the cabinet as on a real one: *System → Setup Operator Keys → Set Key*, while
   holding the key on the reader.

What's simulated: the cabinet sees the key's real ROM ID on its reader. The key's protected
memory isn't read; the fake board keeps each key's record itself (`fakeio/login/<id>`), as it
does for F9/F10. So a key works here once registered, but records written by a real cabinet
don't come across. `HW_IBUTTON=/some/dir` watches another directory of 1-Wire devices.

## Light show

ION cabinets could have a light-show kit: LED lighting run by the I/O board's PSoC
microcontroller. The cabinet plays sequences on it at boot, in the attract loop, when coins go in
and for high scores, and Operator Setup has brightness and profile settings for it.

`MEGAIO_LIGHTSHOW=1` adds the kit to the simulated board. The board then reports PSoC version 2
and a detected kit, and answers the cabinet's light commands. Each command becomes a `lights`
event, which `HW_WLED`, MQTT, webhooks or `HW_ON_LIGHTS` can turn into real light.

| Sequence | Played |
| --- | --- |
| 0 | attract loop (`idle`) |
| 2, 3 | the special high-score animation (2 when a score table had an empty slot) |
| 4 | high-score entry |
| 5 | power-on (repeating) and coins in |
| 1 | not seen yet |

Side effect, as on a real cabinet with that PSoC: games that need the joystick accessory are
switched off in the menu when no joystick is present (`MEGAIO_JOYSTICK=1` adds one).

## Books printer

Operators in some markets (the code's German "Minidrucker") read the books with a small printer
plugged into the cabinet. The cabinet notices it on the attract screen, prints the books and asks
**Do you want to clear current books?**

- **On the cabinet:** press **F4** while it shows the attract loop. On a kiosk box, map an
  input to `print`.
- **From a terminal:** run `build/loader/bin/megaio print`. It waits for the printout and says
  where it went.

The printout is saved as
`build/loader/var/merit/fakeio/printouts/books-YYYYMMDD-HHMMSS.txt` and announced as a `print`
event. The printer unplugs itself once the job is done. `megaio printer on|off` plugs it in or
out by hand.

The printout has the program version, last reading and clear dates, meter pulses, total and free
credits, plays per game, and tournament figures, for both the current period and the life of the
machine. To print it on paper, give the file to a printer when the event arrives
(`MEGA_EVENT_PATH` is its full path):

```bash
HW_ON_PRINT='lp "$MEGA_EVENT_PATH"'                  # a CUPS printer
HW_ON_PRINT='cat "$MEGA_EVENT_PATH" > /dev/usb/lp0'  # a USB receipt printer
```

## Checking and troubleshooting

```bash
scripts/hwbridge.py --check          # which HW_ settings are on
scripts/hwbridge.py --watch          # the cabinet's events, live
scripts/hwbridge.py --send coin channel=1 pulses=1      # push a made-up event to your outputs
scripts/hwbridge.py --send lights cmd=play sequence=0   # e.g. test WLED without the cabinet
tail -f build/loader/var/merit/fakeio/hwbridge.log      # what the bridge did
```

| Problem | Cause / fix |
| --- | --- |
| No `hardware bridge on` line at start | No `HW_` setting found: check spelling in `cabinet.local.conf` (no spaces around `=`); `HW_BRIDGE=off`? |
| `cannot open … (add yourself to the "input" group)` | `sudo usermod -aG input $USER`, log out and in |
| No `lights` events | `MEGAIO_LIGHTSHOW=1` missing; or the cabinet was started before it was set |
| WLED doesn't react | `scripts/hwbridge.py --send lights cmd=set values=[255,0,0,0]` should turn it red; check the address in a browser |
| `mqtt … refused` | wrong user/password, or the broker doesn't allow anonymous clients |
| iButton never seen | `ls /sys/bus/w1/devices` while touching the key: no `02-…` entry means the driver or adapter isn't working |
| Printer: `nothing printed in 150 s` | the cabinet prints only on its attract screen; leave setup and menus first |
| Coin counts double | a device mapped both through `HW_INPUT` and seen by the window as F5–F8: use `HW_INPUT_GRAB=1` |

How each piece works inside: [event reference](../reference/events.md),
[I/O board](../reference/io-board.md#light-show) (light show and printer protocol) and
[cabinet software](../reference/cabinet-software.md).
