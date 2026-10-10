#!/usr/bin/env python3
"""hwbridge: connects the cabinet's (simulated) hardware to real devices and services.

The fake I/O board (src/fakeio/fakeio.c) and the game hook (src/fakeio/gameevents.c) write
everything the cabinet does to <dir>/events.jsonl, one JSON object per line. This program
follows that log and passes each event on (outputs), and feeds real inputs into the board's
control block <dir>/ctl (inputs). scripts/loader.sh starts it when any HW_ setting is made and
stops it with the cabinet. Standard library only.

  scripts/hwbridge.py [--dir DIR] [--parent PID]     run (DIR: the cabinet's merit/fakeio)
  scripts/hwbridge.py --watch                        print the events as they happen
  scripts/hwbridge.py --check                        show what the settings switch on
  scripts/hwbridge.py --send TYPE [key=value ...]    push a made-up event to the outputs (testing)
  scripts/hwbridge.py --input-names                  list the key/button names HW_INPUT_MAP takes

Settings (environment, or cabinet.local.conf; docs/guides/connectors.md has the details):
 outputs
  HW_ON_EVENT="cmd"          run for every event (JSON on stdin, MEGA_EVENT_* variables;
                             MEGA_EVENT_PATH = a printout's full path)
  HW_ON_<TYPE>="cmd"         run for one type: HW_ON_COIN, HW_ON_METER, HW_ON_GAME, HW_ON_LIGHTS,
                             HW_ON_LOCKOUT, HW_ON_PRINT, HW_ON_KEY, HW_ON_BUTTON, HW_ON_SCREEN ...
  HW_WEBHOOK=URL             POST every event as JSON (HW_WEBHOOK_TYPES="coin game" to filter)
  HW_MQTT=HOST[:PORT]        publish to MQTT: <topic>/event/<type>, retained <topic>/state and
                             <topic>/status; HW_MQTT_TOPIC (default megatouch/<host name>),
                             HW_MQTT_USER, HW_MQTT_PASSWORD
  HW_WLED="HOST [HOST...]"   drive WLED lights from the cabinet's light show;
                             HW_WLED_PRESETS="0:1 5:2" (light sequence:WLED preset),
                             HW_WLED_IDLE=preset when the show stops (default: lights off)
 inputs
  HW_INPUT="/dev/input/by-id/...-event-kbd ..."   keyboards, keyboard encoders, coin acceptors
                             that type keys, gamepads (Linux input devices; globs allowed)
  HW_INPUT_MAP="KEY_5=coin1 BTN_0=setup ..."      key → action (default: the loader's hotkeys)
  HW_INPUT_GRAB=1            keep those devices' keys away from the desktop
  HW_GPIO="gpiochip0:17=coin1 gpiochip0:27=setup" GPIO lines (via gpiomon from gpiod);
                             HW_GPIO_ACTIVE=low (default, with pull-up) or high
  HW_IBUTTON=on|PATH         a real iButton reader through the kernel's 1-Wire bus
                             (/sys/bus/w1/devices): a key touching it is a key on the reader;
                             HW_OPERATOR_KEYS="ID ..." are operator keys, the rest player keys
 actions: coin1..coin8, setup, calibrate, print, operator-key, player-key
"""
import glob, json, mmap, os, re, shlex, signal, socket, struct, subprocess, sys, threading, time
import queue, urllib.request

P = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
LOG_LOCK = threading.Lock()


def log(*a):
    with LOG_LOCK:
        print(time.strftime('%F %T'), *a, flush=True)


# ---------------------------------------------------------------- settings
def load_settings():
    s = {}
    conf = os.path.join(P, 'cabinet.local.conf')
    if os.path.exists(conf):
        for line in open(conf, errors='replace'):
            m = re.match(r'^((?:HW|MEGAIO)_[A-Z0-9_]+)=(.*)$', line.strip())
            if m:
                try:
                    v = shlex.split(m.group(2))
                    s[m.group(1)] = v[0] if v else ''
                except ValueError:
                    s[m.group(1)] = m.group(2)
    for k, v in os.environ.items():
        if k.startswith(('HW_', 'MEGAIO_')):
            s[k] = v
    return s


# ---------------------------------------------------------------- the board's control block
class Ctl:
    """<dir>/ctl, struct megaio_ctl (src/fakeio/megaio.h), shared with the fake board."""
    MAGIC, SIZE = 0x4D494F32, 88
    COINS, ST, FOB_PRESENT, FOB_ID, FOB_KIND, PRINTER_AUTO = 4, 12, 36, 37, 45, 80

    def __init__(self, d):
        self.path = os.path.join(d, 'ctl')
        self.m = None
        self.lock = threading.Lock()

    def _map(self):
        if self.m:
            return True
        try:
            fd = os.open(self.path, os.O_RDWR | os.O_CREAT, 0o666)
        except OSError:
            return False
        try:
            if os.fstat(fd).st_size < self.SIZE:
                os.ftruncate(fd, self.SIZE)
            self.m = mmap.mmap(fd, self.SIZE)
        finally:
            os.close(fd)
        if struct.unpack_from('<I', self.m, 0)[0] != self.MAGIC:
            self.m[:self.SIZE] = bytes(self.SIZE)
            struct.pack_into('<I', self.m, 0, self.MAGIC)
            self.m[self.ST + 8] = 0xFF
        return True

    def coin(self, ch, n=1):
        with self.lock:
            if self._map():
                self.m[self.COINS + ch] = (self.m[self.COINS + ch] + n) & 0xFF

    def bit9(self, bit, on):
        with self.lock:
            if self._map():
                b = self.m[self.ST + 9]
                self.m[self.ST + 9] = (b | (1 << bit)) if on else (b & ~(1 << bit) & 0xFF)

    def printer(self):
        with self.lock:
            if self._map() and not self.m[self.ST + 9] & 4:
                self.m[self.PRINTER_AUTO] = 1
                self.m[self.ST + 9] |= 4

    def key(self, rom_id, operator):
        with self.lock:
            if not self._map():
                return
            if rom_id is None:
                self.m[self.FOB_PRESENT] = 0
                return
            self.m[self.FOB_ID:self.FOB_ID + 8] = rom_id
            self.m[self.FOB_KIND] = 1 if operator else 0
            self.m[self.FOB_PRESENT] = 1


def parse_rom(h):
    h = h.strip().lower()
    return bytes.fromhex(h) if re.fullmatch(r'[0-9a-f]{16}', h) else None


class Actions:
    """Turns input presses into board changes."""
    NAMES = ['coin%d' % i for i in range(1, 9)] + ['setup', 'calibrate', 'print', 'operator-key', 'player-key']

    def __init__(self, ctl, settings):
        self.ctl = ctl
        self.keys = {'operator-key': parse_rom(settings.get('MEGAIO_OPERATOR_KEY', '')) or bytes.fromhex('024d454741100130'),
                     'player-key': parse_rom(settings.get('MEGAIO_PLAYER_KEY', '')) or bytes.fromhex('02504c4159455205')}

    def do(self, action, pressed, source=''):
        a = action.lower()
        if a.startswith('coin') and a[4:].isdigit() and 1 <= int(a[4:]) <= 8:
            if pressed:
                self.ctl.coin(int(a[4:]) - 1)
                log('input', source, '→', a)
        elif a in ('setup', 'calibrate'):
            self.ctl.bit9(0 if a == 'setup' else 1, pressed)
            log('input', source, '→', a, 'down' if pressed else 'up')
        elif a == 'print':
            if pressed:
                self.ctl.printer()
                log('input', source, '→ books printer plugged in')
        elif a in self.keys:
            self.ctl.key(self.keys[a] if pressed else None, a == 'operator-key')
            log('input', source, '→', a, 'on' if pressed else 'off')
        else:
            log('unknown action', action, 'from', source)


# ---------------------------------------------------------------- inputs: Linux input devices
EV_KEY = 1
EVIOCGRAB = 0x40044590
DEFAULT_MAP = 'KEY_F1=setup KEY_F2=calibrate KEY_F4=print KEY_F5=coin1 KEY_F6=coin2 KEY_F7=coin3 KEY_F8=coin4 ' \
              'KEY_F9=operator-key KEY_F10=player-key'


def key_codes():
    codes = {}
    for h in ('/usr/include/linux/input-event-codes.h',):
        try:
            for line in open(h):
                m = re.match(r'#define\s+((?:KEY|BTN)_\w+)\s+(0x[0-9a-fA-F]+|\d+)\b', line)
                if m:
                    codes[m.group(1)] = int(m.group(2), 0)
        except OSError:
            pass
    if not codes:   # the common ones, if the kernel headers aren't installed
        for i, c in enumerate('1234567890'):
            codes['KEY_' + c] = 2 + i
        for row, start in (('QWERTYUIOP', 16), ('ASDFGHJKL', 30), ('ZXCVBNM', 44)):
            for i, c in enumerate(row):
                codes['KEY_' + c] = start + i
        for i in range(10):
            codes['KEY_F%d' % (i + 1)] = 59 + i
            codes['BTN_%d' % i] = 0x100 + i
        codes.update(KEY_F11=87, KEY_F12=88, KEY_ESC=1, KEY_ENTER=28, KEY_SPACE=57, KEY_LEFT=105,
                     KEY_RIGHT=106, KEY_UP=103, KEY_DOWN=108, BTN_TRIGGER=0x120, BTN_THUMB=0x121,
                     BTN_THUMB2=0x122, BTN_TOP=0x123, BTN_TOP2=0x124, BTN_PINKIE=0x125,
                     BTN_SOUTH=0x130, BTN_EAST=0x131, BTN_NORTH=0x133, BTN_WEST=0x134,
                     BTN_SELECT=0x13a, BTN_START=0x13b)
    return codes


def input_thread(pattern, keymap, grab, actions):
    fmt = 'llHHi'
    size = struct.calcsize(fmt)
    warned = False
    while True:
        paths = sorted(glob.glob(pattern)) or ([pattern] if os.path.exists(pattern) else [])
        if not paths:
            if not warned:
                log('input', pattern, 'not there (yet); waiting for it')
                warned = True
            time.sleep(2)
            continue
        path = paths[0]
        try:
            f = open(path, 'rb', buffering=0)
        except OSError as e:
            if not warned:
                log('input', path, 'cannot open:', e.strerror, '(add yourself to the "input" group)')
                warned = True
            time.sleep(5)
            continue
        warned = False
        log('input', path, 'open', '(grabbed)' if grab else '')
        try:
            if grab:
                import fcntl
                fcntl.ioctl(f, EVIOCGRAB, 1)
            while True:
                data = f.read(size)
                if len(data) < size:
                    break
                _, _, typ, code, value = struct.unpack(fmt, data)
                if typ == EV_KEY and value in (0, 1) and code in keymap:
                    actions.do(keymap[code], value == 1, os.path.basename(path))
        except OSError as e:
            log('input', path, 'lost:', e.strerror)
        finally:
            f.close()
        time.sleep(1)


# ---------------------------------------------------------------- inputs: GPIO (gpiomon)
def gpio_threads(spec, active_high, actions):
    chips = {}
    for item in spec.split():
        m = re.fullmatch(r'([^:=]+):(\d+)=(\S+)', item)
        if not m:
            log('HW_GPIO: bad entry', item, '(want chip:line=action)')
            continue
        chips.setdefault(m.group(1), {})[int(m.group(2))] = m.group(3)
    try:
        ver = subprocess.run(['gpiomon', '--version'], capture_output=True, text=True).stdout
    except OSError:
        log('HW_GPIO: gpiomon not found (install the gpiod package)')
        return
    v2 = bool(re.search(r'v2\.', ver))
    for chip, lines in chips.items():
        threading.Thread(target=gpio_run, args=(chip, lines, v2, active_high, actions), daemon=True).start()


def gpio_run(chip, lines, v2, active_high, actions):
    bias = '--bias=pull-down' if active_high else '--bias=pull-up'
    offs = [str(o) for o in sorted(lines)]
    if v2:
        cmd = ['gpiomon', bias, '--format=%o %e', '--chip', chip] + offs
    else:
        cmd = ['gpiomon', bias, '--format=%o %e', chip] + offs
    last = {}
    while True:
        log('gpio', chip, 'lines', ' '.join('%s=%s' % (o, lines[o]) for o in sorted(lines)))
        try:
            p = subprocess.Popen(['stdbuf', '-oL'] + cmd if shutil_which('stdbuf') else cmd,
                                 stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        except OSError as e:
            log('gpio: cannot run gpiomon:', e)
            return
        for line in p.stdout:
            parts = line.split()
            if len(parts) < 2 or not parts[0].isdigit():
                continue
            off, rising = int(parts[0]), parts[1] in ('1', 'rising', 'RISING')
            now = time.monotonic()
            if now - last.get(off, 0) < 0.02:      # contact bounce
                continue
            last[off] = now
            if off in lines:
                actions.do(lines[off], rising == active_high, '%s:%d' % (chip, off))
        err = p.stderr.read().strip()
        p.wait()
        log('gpio: gpiomon ended', err[:200])
        time.sleep(5)


def shutil_which(name):
    import shutil
    return shutil.which(name)


# ---------------------------------------------------------------- inputs: iButton reader (1-Wire)
def crc8(data):
    crc = 0
    for b in data:
        for _ in range(8):
            mix = (crc ^ b) & 1
            crc >>= 1
            if mix:
                crc ^= 0x8C
            b >>= 1
    return crc


def w1_rom(name):
    """Kernel 1-Wire slave name "02-0000004d4547" → the 8-byte ROM ID (family first, CRC last)."""
    fam, ser = name.split('-')
    body = bytes([int(fam, 16)]) + bytes.fromhex(ser)[::-1]
    return body + bytes([crc8(body)])


def ibutton_thread(base, operator_ids, actions):
    held = None
    log('ibutton: watching', base, '(operator keys: %s)' % (' '.join(sorted(operator_ids)) or 'none listed'))
    missing = False
    while True:
        try:
            names = [n for n in os.listdir(base) if re.fullmatch(r'[0-9a-f]{2}-[0-9a-f]{12}', n)]
            missing = False
        except OSError:
            if not missing:
                log('ibutton:', base, 'not there: is the reader plugged in (modules ds2490, wire)?')
                missing = True
            names = []
        if names and held is None:
            rom = w1_rom(sorted(names)[0])
            hexid = rom.hex()
            op = hexid in operator_ids
            if rom[0] != 0x02:
                log('ibutton: key', hexid, 'is family %02x; Megatouch keys are family 02 (DS1991)' % rom[0])
            actions.ctl.key(rom, op)
            log('ibutton: key', hexid, 'touching', '(operator)' if op else '(player)')
            held = hexid
        elif not names and held is not None:
            actions.ctl.key(None, False)
            log('ibutton: key', held, 'removed')
            held = None
        time.sleep(0.2)


# ---------------------------------------------------------------- outputs
class Worker:
    """Runs one output's jobs in order on its own thread, so a slow one never holds up the rest."""

    def __init__(self, name, fn):
        self.name, self.fn, self.q = name, fn, queue.Queue(maxsize=1000)
        threading.Thread(target=self.run, daemon=True).start()

    def put(self, ev):
        try:
            self.q.put_nowait(ev)
        except queue.Full:
            pass

    def run(self):
        while True:
            ev = self.q.get()
            try:
                self.fn(ev)
            except Exception as e:  # keep going whatever one output does
                log(self.name, 'failed:', e)


BOARD_DIR = ''


def event_env(ev):
    env = dict(os.environ)
    env['MEGA_EVENT'] = json.dumps(ev)
    if isinstance(ev.get('file'), str):          # a printout: its full path too
        env['MEGA_EVENT_PATH'] = os.path.join(BOARD_DIR, ev['file'])
    for k, v in ev.items():
        env['MEGA_EVENT_' + re.sub(r'\W', '_', k).upper()] = ' '.join(map(str, v)) if isinstance(v, list) else (
            str(v).lower() if isinstance(v, bool) else str(v))
    return env


def command_output(cmd):
    def run(ev):
        r = subprocess.run(['/bin/sh', '-c', cmd], input=json.dumps(ev) + '\n', text=True, env=event_env(ev),
                           cwd=P, capture_output=True, timeout=60)
        out = (r.stdout + r.stderr).strip()
        if r.returncode or out:
            log('command (%s):' % ev.get('type'), 'exit %d' % r.returncode, out[:300])
    return run


def webhook_output(url):
    def run(ev):
        req = urllib.request.Request(url, data=json.dumps(ev).encode(), method='POST',
                                     headers={'Content-Type': 'application/json'})
        urllib.request.urlopen(req, timeout=10).read()
    return run


class MQTT:
    """A small MQTT 3.1.1 publisher (QoS 0) with a retained online/offline status."""

    def __init__(self, server, topic, user, password):
        host, _, port = server.partition(':')
        self.addr = (host, int(port or 1883))
        self.topic, self.user, self.password = topic.rstrip('/'), user, password
        self.sock, self.lock, self.last = None, threading.Lock(), 0
        threading.Thread(target=self.keepalive, daemon=True).start()

    @staticmethod
    def _str(s):
        b = s.encode()
        return struct.pack('>H', len(b)) + b

    @staticmethod
    def _packet(kind, body):
        n, ln = len(body), b''
        while True:
            d, n = n % 128, n // 128
            ln += bytes([d | (0x80 if n else 0)])
            if not n:
                break
        return bytes([kind]) + ln + body

    def _connect(self):
        s = socket.create_connection(self.addr, timeout=10)
        flags = 0x02 | 0x04 | 0x20 | (0x08 * 0)          # clean session, will (QoS 0, retained)
        payload = self._str('megatouch-%s-%d' % (socket.gethostname(), os.getpid()))
        payload += self._str(self.topic + '/status') + self._str('offline')
        if self.user:
            flags |= 0x80
            payload += self._str(self.user)
            if self.password:
                flags |= 0x40
                payload += self._str(self.password)
        body = self._str('MQTT') + bytes([4, flags]) + struct.pack('>H', 60) + payload
        s.sendall(self._packet(0x10, body))
        ack = s.recv(4)
        if len(ack) < 4 or ack[0] != 0x20 or ack[3] != 0:
            s.close()
            raise OSError('broker refused the connection (CONNACK %s)' % ack.hex())
        self.sock = s
        log('mqtt: connected to %s:%d, topic %s' % (self.addr + (self.topic,)))
        self._publish(self.topic + '/status', 'online', True)

    def _publish(self, topic, payload, retain=False):
        body = self._str(topic) + (payload if isinstance(payload, bytes) else payload.encode())
        self.sock.sendall(self._packet(0x30 | (1 if retain else 0), body))
        self.last = time.monotonic()

    def publish(self, topic, payload, retain=False):
        with self.lock:
            for attempt in (0, 1):
                try:
                    if not self.sock:
                        self._connect()
                    self._publish(topic, payload, retain)
                    return
                except OSError as e:
                    if self.sock:
                        self.sock.close()
                    self.sock = None
                    if attempt:
                        raise OSError('mqtt %s:%d: %s' % (self.addr + (e,)))

    def keepalive(self):
        while True:
            time.sleep(20)
            with self.lock:
                if self.sock and time.monotonic() - self.last > 30:
                    try:
                        self.sock.sendall(b'\xc0\x00')     # PINGREQ
                        self.last = time.monotonic()
                    except OSError:
                        self.sock.close()
                        self.sock = None


class State:
    """What the cabinet is doing right now, for the retained MQTT state message."""

    def __init__(self):
        self.s = {'game': None, 'screen': None, 'lights': None, 'meters': {}, 'coins_locked': None,
                  'printer': False, 'key': None}

    def update(self, ev):
        t = ev.get('type')
        if t == 'game':
            self.s['game'] = ev.get('tag') if ev.get('state') == 'start' else None
        elif t == 'screen':
            self.s['screen'] = ev.get('tag') if ev.get('state') == 'start' else None
        elif t == 'lights':
            self.s['lights'] = {k: v for k, v in ev.items() if k not in ('t', 'type')}
        elif t == 'meter':
            self.s['meters'][ev.get('meter')] = ev.get('total')
        elif t == 'lockout':
            self.s['coins_locked'] = ev.get('coins_locked')
        elif t == 'printer':
            self.s['printer'] = ev.get('plugged')
        elif t == 'key':
            self.s['key'] = ev.get('kind') if ev.get('state') == 'on' else None
        return dict(self.s, t=ev.get('t'))


def mqtt_output(mq, state):
    def run(ev):
        mq.publish('%s/event/%s' % (mq.topic, ev.get('type', 'unknown')), json.dumps(ev))
        mq.publish(mq.topic + '/state', json.dumps(state.update(ev)), True)
    return run


# The cabinet's light-show sequences (LightShowManager::Play, docs/reference/cabinet-internals.md):
# 0 attract mode, 2/3 the special high-score animation, 4 high-score entry, 5 power-on and coin-in.
WLED_EFFECTS = {0: 9, 1: 2, 2: 42, 3: 42, 4: 87, 5: 1}   # Rainbow, Breathe, Fireworks, Glitter, Blink


def wled_output(hosts, presets, idle):
    def send(state):
        for h in hosts:
            req = urllib.request.Request('http://%s/json/state' % h, data=json.dumps(state).encode(),
                                         method='POST', headers={'Content-Type': 'application/json'})
            urllib.request.urlopen(req, timeout=5).read()

    def run(ev):
        if ev.get('type') != 'lights':
            return
        cmd = ev.get('cmd')
        if cmd == 'play':
            seq = ev.get('sequence')
            if seq in presets:
                send({'on': True, 'ps': presets[seq]})
            else:
                send({'on': True, 'seg': [{'fx': WLED_EFFECTS.get(seq, 0)}]})
        elif cmd == 'set':
            r, g, b = (ev.get('values') or [0, 0, 0])[:3]
            send({'on': True, 'seg': [{'fx': 0, 'col': [[r, g, b]]}]})
        elif cmd == 'brightness':
            v = int(ev.get('value', 100))
            send({'bri': max(1, min(255, v * 255 // 100 if v <= 100 else v))})
        elif cmd == 'active':
            send({'on': bool(ev.get('on'))})
        elif cmd == 'stop':
            send({'on': True, 'ps': idle} if idle else {'on': False})
    return run


def build_outputs(s):
    outs = []
    if s.get('HW_ON_EVENT'):
        outs.append((None, Worker('HW_ON_EVENT', command_output(s['HW_ON_EVENT']))))
    for k, v in s.items():
        if k.startswith('HW_ON_') and k != 'HW_ON_EVENT' and v:
            outs.append(({k[6:].lower()}, Worker(k, command_output(v))))
    if s.get('HW_WEBHOOK'):
        types = set(s.get('HW_WEBHOOK_TYPES', '').split()) or None
        outs.append((types, Worker('webhook', webhook_output(s['HW_WEBHOOK']))))
    if s.get('HW_MQTT'):
        mq = MQTT(s['HW_MQTT'], s.get('HW_MQTT_TOPIC') or 'megatouch/' + socket.gethostname(),
                  s.get('HW_MQTT_USER', ''), s.get('HW_MQTT_PASSWORD', ''))
        outs.append((None, Worker('mqtt', mqtt_output(mq, State()))))
    if s.get('HW_WLED'):
        presets = {}
        for item in s.get('HW_WLED_PRESETS', '').split():
            a, _, b = item.partition(':')
            if a.isdigit() and b.isdigit():
                presets[int(a)] = int(b)
        idle = int(s['HW_WLED_IDLE']) if s.get('HW_WLED_IDLE', '').isdigit() else None
        outs.append(({'lights'}, Worker('wled', wled_output(s['HW_WLED'].split(), presets, idle))))
    return outs


def dispatch(outs, ev):
    for types, w in outs:
        if types is None or ev.get('type') in types:
            w.put(ev)


# ---------------------------------------------------------------- the event log
def follow(path, from_start=False):
    """Yields each new line of the event log, across rotation (events.jsonl → .1) and restarts."""
    f, ino = None, None
    while True:
        if f is None:
            try:
                f = open(path, 'r', errors='replace')
                ino = os.fstat(f.fileno()).st_ino
                if not from_start:
                    f.seek(0, 2)         # only what happens from now on
                from_start = True        # a new file after rotation is read from its start
            except OSError:
                from_start = True        # it doesn't exist yet: read it from its start when it appears
                time.sleep(1)
                continue
        line = f.readline()
        if line:
            if line.endswith('\n'):
                yield line
            else:
                f.seek(f.tell() - len(line))
                time.sleep(0.05)
            continue
        try:
            st = os.stat(path)
            if st.st_ino != ino or st.st_size < f.tell():
                f.close()
                f = None
                continue
        except OSError:
            f.close()
            f = None
            continue
        time.sleep(0.05)


# ---------------------------------------------------------------- main
def default_dir():
    var = os.environ.get('MEGA_LOADER_VAR') or os.path.join(P, 'build', 'loader', 'var')
    return os.path.join(var, 'merit', 'fakeio')


def main():
    args = sys.argv[1:]
    d, parent, mode, rest = default_dir(), None, 'run', []
    while args:
        a = args.pop(0)
        if a == '--dir' and args:
            d = args.pop(0)
        elif a == '--parent' and args:
            parent = int(args.pop(0))
        elif a in ('--watch', '--check', '--send', '--input-names'):
            mode = a[2:]
            rest = args
            break
        elif a in ('-h', '--help'):
            print(__doc__)
            return 0
        else:
            print('unknown argument', a, '(see --help)', file=sys.stderr)
            return 2
    s = load_settings()
    events = os.path.join(d, 'events.jsonl')
    global BOARD_DIR
    BOARD_DIR = os.path.abspath(d)

    if mode == 'input-names':
        print(' '.join(sorted(key_codes())))
        return 0
    if mode == 'watch':
        print('watching', events, '(Ctrl+C stops)', flush=True)
        try:
            for line in follow(events):
                print(line.rstrip(), flush=True)
        except KeyboardInterrupt:
            return 0
    if mode == 'send':
        if not rest:
            print('usage: hwbridge.py --send TYPE [key=value ...]', file=sys.stderr)
            return 2
        ev = {'t': round(time.time(), 3), 'type': rest[0]}
        for kv in rest[1:]:
            k, _, v = kv.partition('=')
            try:
                ev[k] = json.loads(v)
            except ValueError:
                ev[k] = v
        outs = build_outputs(s)
        if not outs:
            print('no outputs configured (HW_ON_*, HW_WEBHOOK, HW_MQTT, HW_WLED)')
            return 1
        dispatch(outs, ev)
        deadline = time.time() + 15
        while time.time() < deadline and any(not w.q.empty() for _, w in outs):
            time.sleep(0.1)
        time.sleep(0.5)
        print('sent', json.dumps(ev))
        return 0

    hw = {k: v for k, v in s.items() if k.startswith('HW_') and v}
    if mode == 'check':
        print('cabinet state:', d)
        for k in sorted(hw):
            print('  %s=%s' % (k, '***' if 'PASSWORD' in k else hw[k]))
        if not hw:
            print('  no HW_ settings: the bridge does not run')
        return 0

    # run
    log('hwbridge starting for', d, '(settings: %s)' % (' '.join(sorted(hw)) or 'none'))
    ctl = Ctl(d)
    actions = Actions(ctl, s)
    if s.get('HW_INPUT'):
        codes = key_codes()
        keymap = {}
        for item in (s.get('HW_INPUT_MAP') or DEFAULT_MAP).split():
            name, _, action = item.partition('=')
            code = codes.get(name.upper()) if not name.isdigit() else int(name)
            if code is None or not action:
                log('HW_INPUT_MAP: unknown key', name, '(--input-names lists them)')
                continue
            keymap[code] = action
        for pattern in s['HW_INPUT'].split():
            threading.Thread(target=input_thread, args=(pattern, keymap, s.get('HW_INPUT_GRAB') == '1', actions),
                             daemon=True).start()
    if s.get('HW_GPIO'):
        gpio_threads(s['HW_GPIO'], s.get('HW_GPIO_ACTIVE', 'low') == 'high', actions)
    if s.get('HW_IBUTTON') and s['HW_IBUTTON'] not in ('off', '0'):
        base = s['HW_IBUTTON'] if s['HW_IBUTTON'].startswith('/') else '/sys/bus/w1/devices'
        ops = {i.lower() for i in s.get('HW_OPERATOR_KEYS', '').split()}
        threading.Thread(target=ibutton_thread, args=(base, ops, actions), daemon=True).start()
    outs = build_outputs(s)

    if parent:
        def watch_parent():
            while True:
                try:
                    os.kill(parent, 0)
                except ProcessLookupError:
                    log('cabinet stopped: hwbridge exiting')
                    os._exit(0)
                except PermissionError:
                    pass
                time.sleep(2)
        threading.Thread(target=watch_parent, daemon=True).start()
    signal.signal(signal.SIGTERM, lambda *a: os._exit(0))

    for line in follow(events):
        try:
            ev = json.loads(line)
        except ValueError:
            continue
        if outs:
            dispatch(outs, ev)
    return 0


if __name__ == '__main__':
    sys.exit(main())
