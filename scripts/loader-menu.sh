#!/bin/bash
# The ION cabinet's game menu (the menu pages in NVRAM): list it, find games that are licensed and
# installed but on no page, and put them on it. Normally the operator arranges the menu in
# Operator Setup → Games → Game Setup; that screen can only place a game in a category the
# factory assigned it to, and the 2014 factory menu leaves out games a real 2014 key never
# offered (the original PixMix and PhotoHunt, Texas Hold'em, the mini-golfs, Merit-thons, the
# chain and adult variants …). Our key image (scripts/loader-key.sh) licenses every game, so
# without this they are licensed, switched on, and unreachable.
#
#   scripts/loader-menu.sh --list                 every category and its games
#   scripts/loader-menu.sh --missing              licensed, installed, switched-on games on no page
#   scripts/loader-menu.sh --fill                 put all of those on the menu (category below);
#                                                 loader stopped; a backup is taken first
#   scripts/loader-menu.sh add GAME CATEGORY      one game (G_PIXMIX or PIXMIX; STRATEGY, PUZZLES …)
#   scripts/loader-menu.sh remove GAME            take a game off every page
#
# scripts/loader.sh runs `--fill --auto` before every ION start (MEGA_LOADER_MENU=keep turns
# that off). --auto adds only games it has never added before (fakeio/menu-filled.txt), so a
# game the operator takes off in Game Setup stays off.
#
# Where a game goes: the table in the script for the games the 2014 factory menu leaves out,
# else from gamedata.xml (adult games to Erotic, else by name), else New Games.
# nvram.dat: 17 categories at 0x0D8, 0xA0 bytes apart, 40 u32 game IDs each (255 = empty); the
# last two bytes are a 16-bit sum of bytes 1..0x1881. The running loader keeps NVRAM in memory
# and rewrites the file, so changes are made only while it is stopped. ION only: a Force's
# NVRAM map is different.
set -euo pipefail
P=$(cd "$(dirname "$0")/.." && pwd)
C="${MEGA_LOADER_CABINET:-$P/build/loader}"
VAR="${MEGA_LOADER_VAR:-$C/var}"
NV="$VAR/merit/nvram.dat"
[ $# -ge 1 ] || { sed -n 2,21p "$0"; exit 1; }
[ -f "$NV" ] || { echo "no $NV (the cabinet hasn't started yet)" >&2; exit 1; }
[ -e "$C/root/usr/local/bin/layout_daemon" ] || { echo "not an ION cabinet: only the ION's menu is supported" >&2; exit 1; }
[ "$(stat -c %s "$NV")" = 6276 ] || { echo "$NV: unexpected size (not ION 2014 NVRAM)" >&2; exit 1; }
[ -x "$P/build/loader/bin/enumtag" ] || { echo "run make loader first" >&2; exit 1; }

mode=$1 auto=0
[ "${2:-}" = --auto ] && auto=1
case "$mode" in
  --list|--missing) ;;
  --fill|add|remove)
    if pgrep -af bwrap | grep -qF -- "--bind $VAR /var"; then
      echo "stop the cabinet first (it rewrites nvram.dat)" >&2; exit 1
    fi ;;
  *) sed -n 2,21p "$0"; exit 1 ;;
esac

# the cabinet's own game IDs (they differ between releases), cached next to its state
IDS="$VAR/merit/fakeio/gameids.txt"
if [ ! -s "$IDS" ] || [ "$IDS" -ot "$C/root/usr/local/lib/libenums.so" ]; then
  mkdir -p "$(dirname "$IDS")"
  MEGA_LOADER_DISPLAY=none "$P/scripts/loader.sh" run /opt/fakeio/enumtag game $(seq 0 299) 2>/dev/null \
    | awk 'NF == 2' > "$IDS.new"
  [ -s "$IDS.new" ] || { rm -f "$IDS.new"; echo "could not read the cabinet's game IDs" >&2; exit 1; }
  mv "$IDS.new" "$IDS"
fi

changes=0
if [ "$mode" != --list ] && [ "$mode" != --missing ]; then
  changes=1
  if [ "$auto" = 0 ] && [ -z "${MEGA_LOADER_VAR:-}" ] && [ "$C" = "$P/build/loader" ]; then
    "$P/scripts/loader-backup.sh" "before-menu" >/dev/null
  fi
fi

python3 - "$mode" "$auto" "$NV" "$IDS" "$C" "$VAR" "${MEGAIO_KEY_ID:-8c14fc020040000e}" "${3:-}" "${2:-}" <<'EOF'
import os, re, struct, sys
mode, auto, nvpath, idspath, cab, var, idhex, arg2, arg1 = sys.argv[1:10]
auto = auto == '1'
CATS = ['PUZZLES', 'CARD', 'QUIZ', 'STRATEGY', 'H2H', 'NEW', 'EROTIC', 'ACTION', 'MEGALINK', 'KIDS',
        'TOURNAMENT', 'HOTTOPICS', 'PREMEROTIC', 'PRIZEZONE_REMOVED', 'CHIPEROTIC', 'FUNZONE_STANDARD',
        'FUNZONE_KIDS']
SHOWN = {'PUZZLES': 'Puzzles', 'CARD': 'Cards', 'QUIZ': 'Quiz & Word', 'STRATEGY': 'Strategy',
         'NEW': 'New Games', 'EROTIC': 'Erotic', 'ACTION': 'Action', 'MEGALINK': 'Megalink', 'KIDS': 'Kids'}
# games the ION 2014 factory menu leaves out (a genuine 2014 key prices them 0), and where they go
PLACE = {
    'G_PIXMIX': 'STRATEGY', 'G_BOXXI_LOCALCONTENT': 'STRATEGY',
    'G_PHUNT': 'PUZZLES', 'G_PUZZLEMERITHON': 'PUZZLES',
    'G_TEXAS_HOLD_EM': 'CARD', 'G_CHAIN_ECARDBANDITS': 'CARD',
    'G_PSH2HTRIV': 'QUIZ', 'G_PSMYST': 'QUIZ', 'G_SHOWTENDERS_COCKTAIL_COLLEGE': 'QUIZ',
    'G_QUIZMERITHON': 'QUIZ', 'G_QUIZMERITHON2': 'QUIZ', 'G_SPORTSMERITHON': 'QUIZ',
    'G_ATWMINIGOLF': 'ACTION', 'G_SPACEMINIGOLF': 'ACTION', 'G_DRIVING': 'ACTION',
    'G_TRACKBALL_BOWLING_REMOVED': 'ACTION', 'G_TMAXXMERITHON': 'TOURNAMENT',
    'G_CHAIN_BOXXI': 'EROTIC',
}

ids = {}
for l in open(idspath):
    n, name = l.split()
    ids[name] = int(n)
names = {v: k for k, v in ids.items()}

d = bytearray(open(nvpath, 'rb').read())
assert struct.unpack_from('<H', d, 0x1882)[0] == sum(d[1:0x1882]) & 0xffff, 'nvram.dat checksum is wrong; not touching it'
slot = lambda c, i: 0xD8 + c * 0xA0 + i * 4
menu = [[struct.unpack_from('<I', d, slot(c, i))[0] for i in range(40)] for c in range(17)]
on_menu = {g for c in menu for g in c if g not in (255, 300)}

# gamedata.xml: the installed games' libraries, folders and flags
root = os.path.join(cab, 'root')
gd = open(os.path.join(root, 'usr/local/gamedata/config/gamedata.xml'), errors='replace').read()
info = {}
for blk in re.findall(r'<game>(.*?)</game>', gd, re.S):
    t = lambda k: (re.search(r'<%s>(.*?)</%s>' % (k, k), blk, re.S) or [0, ''])[1].strip()
    if t('GameId'):
        info[t('GameId')] = dict(dll=t('DLLName'), dir=t('Directory'), desc=t('Description'),
                                 sex=t('SexFlag') not in ('', '0'), menu=t('IsMenu') == 'true')

def installed(g):
    i = info.get(g)
    if not i or not i['dll'] or i['menu']:
        return False
    if not os.path.exists(os.path.join(root, 'usr/local/lib', i['dll'] + '.so')):
        return False
    for base in ('usr/local/gamedata/gamegraphics', 'usr/local/ion_only/gamegraphics'):
        if os.path.isdir(os.path.normpath(os.path.join(root, base, i['dir']))):
            return True
    return os.path.isdir(os.path.normpath(os.path.join(cab, 'ion/gamegraphics', i['dir'])))

# switched on by the operator (settings.xml; a game it has no <Active> for is on, as for the loader)
inactive = set()
try:
    s = open(os.path.join(var, 'merit/settings.xml'), errors='replace').read()
    for g in re.findall(r'<GameInfo>(.*?)</GameInfo>', s):
        m, a = re.search(r'<GameId>(.*?)</GameId>', g), re.search(r'<Active>(.*?)</Active>', g)
        if m and a and a.group(1) != 'GAME_ACTIVE':
            inactive.add(m.group(1))
except FileNotFoundError:
    pass

# licensed by the key image (per-game price at 0x1B0 + ID; bit field at 0x3BF, all clear = all)
price = None
keyp = os.path.join(var, 'merit/fakeio/key.bin')
if os.path.exists(keyp):
    kid = bytes.fromhex(idhex)
    rol = lambda b, n: ((b << n) | (b >> (8 - n))) & 0xff
    fm = (rol(kid[1], 1), rol(kid[2], 6), rol(kid[3], 3))
    img = open(keyp, 'rb').read()
    blk = struct.unpack_from('<H', bytes(c ^ fm[i % 3] for i, c in enumerate(img[7168:7168 + 16])), 8)[0]
    mask = bytes(kid[1 + k] ^ b'`bnrou'[k] for k in range(6))
    key = bytes(c ^ (mask[i - 0x12] if 0x12 <= i <= 0x15 else mask[i % 6]) for i, c in enumerate(img[blk * 1024:blk * 1024 + 0x400]))
    bits = key[0x3BF:0x3BF + 38]
    price = lambda n: n < 300 and key[0x1B0 + n] & 0x0F and (not any(bits) or bits[n // 8] & (0x80 >> n % 8))

def licensed(g):
    return price is None or bool(price(ids[g]))

def category(g):
    if g in PLACE:
        return PLACE[g]
    i = info[g]
    if i['sex']:
        return 'EROTIC'
    n = (g + ' ' + i['desc']).upper()
    for words, c in ((('TRIVIA', 'QUIZ', 'PHRAZE', 'WORD', 'MYST'), 'QUIZ'),
                     (('POKER', 'CARD', 'SOLITAIRE', 'HOLD', '21', 'BLACKJACK'), 'CARD'),
                     (('GOLF', 'BOWLING', 'RACING', 'DRIVING', 'BASKETBALL'), 'ACTION'),
                     (('PHUNT', 'PHOTO', 'PUZZLE'), 'PUZZLES')):
        if any(w in n for w in words):
            return c
    return 'NEW'

def missing():
    return [g for g in sorted(ids, key=ids.get)
            if g.startswith('G_') and ids[g] not in on_menu and g not in inactive and installed(g) and licensed(g)]

def find(name):
    n = name.upper()
    for c in (n, 'G_' + n):
        if c in ids:
            return c
    sys.exit(f'unknown game: {name} (see --list or --missing)')

def put(g, c, why=''):
    ci = CATS.index(c)
    if ids[g] in menu[ci]:
        return False
    if 255 not in menu[ci]:
        print(f'  {c} is full (40 games): {g} not added', file=sys.stderr)
        return False
    i = menu[ci].index(255)
    menu[ci][i] = ids[g]
    struct.pack_into('<I', d, slot(ci, i), ids[g])
    print(f'  {g} ({info.get(g, {}).get("desc", "")}) → {SHOWN.get(c, c)}{why}')
    return True

def save():
    struct.pack_into('<H', d, 0x1882, sum(d[1:0x1882]) & 0xffff)
    tmp = nvpath + '.new'
    open(tmp, 'wb').write(d)
    os.replace(tmp, nvpath)

if mode == '--list':
    for ci, c in enumerate(CATS):
        gs = [names.get(g, str(g)) for g in menu[ci] if g not in (255, 300)]
        if gs:
            print(f'{c} ({len(gs)}): ' + ', '.join(gs))
elif mode == '--missing':
    m = missing()
    for g in m:
        print(f'{g:34} {info[g]["desc"]:32} → {category(g)}')
    print(f'{len(m)} game(s) licensed and installed but on no menu page' if m else 'every licensed, installed game is on the menu')
elif mode == '--fill':
    done_path = os.path.join(var, 'merit/fakeio/menu-filled.txt')
    done = set(open(done_path).read().split()) if os.path.exists(done_path) else set()
    added = [g for g in missing() if not (auto and g in done) and put(g, category(g))]
    if added:
        save()
        with open(done_path, 'a') as f:
            f.write(''.join(g + '\n' for g in added))
        print(f'{len(added)} game(s) added to the menu')
    elif not auto:
        print('nothing to add')
elif mode == 'add':
    g, c = find(arg1), arg2.upper().removeprefix('CAT_')
    c = {'CARDS': 'CARD', 'ACTION_NEW': 'ACTION', 'QUIZ_WORD': 'QUIZ'}.get(c, c)
    if c not in CATS:
        sys.exit(f'unknown category: {arg2} ({", ".join(CATS)})')
    if put(g, c):
        save()
elif mode == 'remove':
    g = find(arg1)
    hit = False
    for ci in range(17):
        for i in range(40):
            if menu[ci][i] == ids[g]:
                struct.pack_into('<I', d, slot(ci, i), 255)
                hit = True
                print(f'  {g} taken off {SHOWN.get(CATS[ci], CATS[ci])}')
    if hit:
        save()
EOF
