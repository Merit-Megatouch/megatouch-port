#!/bin/bash
# Make the fake I/O board's security-key image (/var/merit/fakeio/key.bin) from the loader's own
# NVRAM, so the cabinet reads a licence again (scripts/loader.sh runs this when key.bin is missing).
#
#   scripts/loader-key.sh [--force]     write key.bin for build/loader/var (or $MEGA_LOADER_VAR)
#   scripts/loader-key.sh --show        decode and print the current key.bin
#
# What a real key holds and what this one puts there (docs/reference/io-board.md, "Security key"):
#   identity    part number + revision from nvram.dat (SA362801 R00 here). They must match NVRAM:
#               a different key makes the loader wipe NVRAM at boot.
#   options     one byte per game option (0x2DD + index): 0 = locked off, 1 = locked on,
#               2 = operator's choice (default off), 3 = operator's choice (default on). Operator
#               Setup only shows options the key leaves to the operator; a settings reset sets
#               NVRAM to (value & 1). Here: every option is the operator's choice, defaulting to
#               its current NVRAM value, except
#                 - the "…_ALWAYS" / "…_NEVER" placeholders (locked on / off), and
#                 - options for hardware this setup does not have, locked off: MindSpark mode (67,
#                   changes platform detection), TouchTunes (88), credit card (100, 101, 115) and
#                   the Rowe download selector (114: licensed without its amplifier/board the
#                   loader stops with "invalid key with the current hardware configuration").
#   everything else (prices, coin values, country, languages, per-game licence bits) stays 0,
#   which is what the loader saw without a key (no per-game bits = every game licensed).
#
# Locale (environment or cabinet.local.conf; remake the key with --force, cabinet stopped):
#   KEY_COUNTRY=USA                 the key's country (name or number: GERMANY, ENGLAND, 11 …;
#                                   `scripts/loader-key.sh --countries` lists them). Changes
#                                   country-specific behaviour (Germany: the books printer variant)
#   KEY_LANGUAGES="ENGLISH GERMAN …" or all   languages the cabinet offers (default: the factory
#                                   USA-STD set: English, German, Italian, French, Spanish, Dutch,
#                                   Latin American, Canadian French, Portuguese)
#   KEY_PLAYER_LANGUAGES=…          those players may pick (default: KEY_LANGUAGES, or the factory
#                                   set without Portuguese); the first one listed is the default
# `scripts/loader-key.sh --languages` lists the 31 languages.
#
# Encoding: the data goes in block 5 (also the loader's fallback block), XORed with
# id[1..6] ^ "`bnrou" (bytes 0x12-0x15 with the first four mask bytes), u16 checksum at 0x355;
# block 7 is the footer naming block 5, XORed with rol(id1,1) rol(id2,6) rol(id3,3).
set -euo pipefail
P=$(cd "$(dirname "$0")/.." && pwd)
VAR="${MEGA_LOADER_VAR:-$P/build/loader/var}"
KEY="$VAR/merit/fakeio/key.bin"
NV="$VAR/merit/nvram.dat"
mode=${1:-make}
# locale settings may also live in cabinet.local.conf (the environment wins)
if [ -f "$P/cabinet.local.conf" ]; then
  while IFS= read -r line; do
    k=${line%%=*}
    [ -n "${!k+x}" ] || eval "export $line"
  done < <(grep -E '^KEY_(COUNTRY|LANGUAGES|PLAYER_LANGUAGES)=' "$P/cabinet.local.conf")
fi
if [ "$mode" != --show ] && [ "$mode" != --languages ] && [ "$mode" != --countries ]; then
  [ -f "$NV" ] || { echo "no $NV" >&2; exit 1; }
  [ -f "$KEY" ] && [ "$mode" != --force ] && { echo "$KEY exists (--force to rewrite)"; exit 0; }
  mkdir -p "$(dirname "$KEY")"
fi
python3 - "$mode" "$NV" "$KEY" "${MEGAIO_KEY_ID:-8c14fc020040000e}" <<'EOF'
import struct, sys, time
mode, nvpath, keypath, idhex = sys.argv[1:5]
kid = bytes.fromhex(idhex)                      # ROM ID served by the fake board (fakeio.c)
mask = bytes(kid[1 + k] ^ b'`bnrou'[k] for k in range(6))
rol = lambda b, n: ((b << n) | (b >> (8 - n))) & 0xff
fmask = (rol(kid[1], 1), rol(kid[2], 6), rol(kid[3], 3))
BLOCK, OPT, CSUM = 5, 0x2DD, 0x355

import os
LANGUAGES = ('ENGLISH GERMAN ITALIAN FRENCH SPANISH BRZ_PORTUGESE GREEK DUTCH BRITISH FINNISH CROATIA '
             'DANISH NORWEGIAN SWEDISH HUNGARIAN VIETNAMESE JAPANESE LATIN_AMERICAN TURKISH RUSSIAN '
             'AUSTRALIAN CAN_FRENCH PORTUGUESE IRISH CHINESE ARABIC LATVIAN KOREAN THAI ALBANIAN '
             'TRAD_CHINESE').split()                  # Locale::Languages (liblocale.so), bit = index
COUNTRIES = dict(enumerate(
    'USA CANADA MEXICO ENGLAND IRELAND PORTUGAL SPAIN FRANCE HOLLAND BELGIUM LUXEMBURG GERMANY '
    'SWITZERLAND ITALY DENMARK CZECH_REPUBLIC AUSTRIA SLOVENIA CROATIA_COUNTRY POLAND SLOVAKIA HUNGARY '
    'SERBIA MACEDONIA ALBANIA GREECE ESTONIA LATVIA LITHUANIA BELARUA UKRAINE MOLDOVIA ROMANIA '
    'BULGARIA TURKEY RUSSIA FINLAND NORWAY SWEDEN BELIZE COSTA_RICA EL_SALVADOR GUATEMALA HONDURAS '
    'NICARAGUA PANAMA ARGENTINA BOLIVIA BRAZIL CHILE COLUMBIA ECUADOR FALKLAND_ISLANDS FRENCH_GUIANA '
    'GUYANA PARAGUAY PATAGONIA PERU SURINAME URUGUAY VENEZUELA ALGERIA ANGOLA ASCENSION_ISLAND BENIN '
    'BOTSWANA BURKINA_FASO BURUNDI CAMEROON CAPE_VERDE CENTRAL_AFRICAN_REPUBLIC CHAD COMOROS CONGO '
    'DJIBOUTI EGYPT EQUITORIAL_GUINEA ERITREA ETHIOPIA GABON GAMBIA GHANA GUINEA GUINEA_BISSAU KENYA '
    'LESOTHO LIBERIA LIBYA MADAGASCAR MALAWI MALI MAURITANIA MAURITIUS MOROCCO MOZAMBIQUE NAMIBIA '
    'NIGER NIGERIA REUNION RWANDA SAINT_HELENA_ISLAND SAO_TOME_AND_PRINCIPE SENEGAL SEYCHELLES '
    'SIERRA_LEONE SOMALIA SOUTH_AFRICA SUDAN SWAZILAND TANZANIA TOGO TUNISIA UGANDA WESTERN_SAHARA '
    'ZAMBIA ZIMBABWE AUSTRALIA CHINA INDIA JAPAN USA_GK KOREA'.split()))
COUNTRIES.update({128: 'UAE', 129: 'KUWAIT', 130: 'THAILAND', 131: 'SYRIA', 132: 'QATAR', 133: 'SAUDI_ARABIA',
                  134: 'VIETNAM', 135: 'AZERBAIJAN', 136: 'JORDAN', 137: 'TAIWAN', 144: 'BAHRAIN',
                  145: 'PHILIPPINES', 146: 'LEBANON', 240: 'TEST_US'})   # Locale::Countries
FACTORY_LANGS, FACTORY_PLAYER = 0x0062009F, 0x0022009F    # a genuine USA-STD key

def lang_mask(text, default):
    if not text:
        return default
    if text.strip().lower() == 'all':
        return (1 << len(LANGUAGES)) - 1
    m = 0
    for w in text.replace(',', ' ').split():
        w = w.upper()
        if w not in LANGUAGES:
            sys.exit(f'unknown language {w} (--languages lists them)')
        m |= 1 << LANGUAGES.index(w)
    return m

def mask_names(m):
    return ' '.join(n for i, n in enumerate(LANGUAGES) if m >> i & 1)

if mode == '--languages':
    print('\n'.join(f'{i:2d} {n}' for i, n in enumerate(LANGUAGES)))
    sys.exit()
if mode == '--countries':
    print('\n'.join(f'{i:3d} {n}' for i, n in sorted(COUNTRIES.items())))
    sys.exit()

def xor_block(b):
    b = bytearray(b)
    for i in range(0x400):
        b[i] ^= mask[i - 0x12] if 0x12 <= i <= 0x15 else mask[i % 6]
    return b

def checksum(b):
    return sum(b[i] for i in range(0x3F0) if not (0x12 <= i <= 0x15 or i in (CSUM, CSUM + 1))) & 0xffff

if mode == '--show':
    img = open(keypath, 'rb').read()
    foot = bytes(c ^ fmask[i % 3] for i, c in enumerate(img[7 * 1024:7 * 1024 + 0xB4]))
    blk = struct.unpack_from('<H', foot, 8)[0]
    ok_f = sum(foot[:0xB2]) & 0xffff == struct.unpack_from('<H', foot, 0xB2)[0]
    d = xor_block(img[blk * 1024:(blk + 1) * 1024])
    ok_d = checksum(d) == struct.unpack_from('<H', d, CSUM)[0]
    print(f'footer {"ok" if ok_f else "BAD"}, block {blk}, data {"ok" if ok_d else "BAD"}')
    print('part', d[0:8].decode(errors='replace'), 'revision', d[0xD:0x11].rstrip(b'\0').decode(),
          'date', time.strftime('%Y-%m-%d', time.gmtime(struct.unpack_from('<I', d, 9)[0])),
          'server', d[0x2A:0xF2].split(b'\0')[0].decode())
    print('country', COUNTRIES.get(d[0xF2], d[0xF2]), 'locale', d[0x16:0x20].split(b'\0')[0].decode(errors='replace'))
    lm, pm = struct.unpack_from('<I', d, 0x104)[0], struct.unpack_from('<I', d, 0x3E5)[0]
    print('languages', mask_names(lm) or '(none: only the default)')
    print('player languages', mask_names(pm) or '(none)')
    print('options', ' '.join(f'{i}:{d[OPT + i]}' for i in range(120)))
    sys.exit()

nv = open(nvpath, 'rb').read()
part, rev = nv[1:9], nv[0x38:0x3C]              # what InitVars stored from the original key
if not part.startswith(b'SA3'):
    sys.exit(f'nvram.dat has no key part number ({part!r}); not making a key')
locked_off = {67, 88, 100, 101, 114, 115, 110, 111, 112, 113, 119}
locked_on = {104, 105, 106, 107, 108}
d = bytearray(0x400)
d[0:8] = part
struct.pack_into('<I', d, 9, 1377820800)        # 2013-08-30, this build's date
d[0xD:0x11] = rev
d[0x16:0x1D] = b'USA-STD'                              # locale package (genuine keys: USA-STD); selects the player-selectable languages
d[0x2A:0x2A + 18] = b'us.accessmerit.com'
d[0xF5:0xFD] = bytes.fromhex('93e41c6e20911b9b')    # cabinet code: one of the two KeyManager::Check accepts for a DS1995 key (the unpatched loader checks it)
d[0x1B0:0x1B0 + 300] = b'\x22' * 300                  # per game: default price 2 credits (low nibble), continue 2 (high); 0 = game not offered (KeyManager::DefaultGamePrice)
# factory USA-STD defaults, as on a genuine key (the unpatched loader reads them; KeyManager::*):
struct.pack_into('<HHI', d, 0xFE, 8, 0, 10)          # Champion Edition: game cost 8, prize mode, pool rate 10 (x5 = 50 %)
langs = lang_mask(os.environ.get('KEY_LANGUAGES', ''), FACTORY_LANGS)
player = lang_mask(os.environ.get('KEY_PLAYER_LANGUAGES', ''),
                   FACTORY_PLAYER if not os.environ.get('KEY_LANGUAGES') else langs) & langs
struct.pack_into('<IIi', d, 0x104, langs, 0, 0)       # LanguageMask (bit = Locale::Languages); +0x10C default: the first
struct.pack_into('<I', d, 0x3E5, player)            # PlayerLanguageMask: the player-selectable ones
cty = os.environ.get('KEY_COUNTRY', 'USA').upper()
by_name = {v: k for k, v in COUNTRIES.items()}
if cty.isdigit() and int(cty) in COUNTRIES:
    d[0xF2] = int(cty)
elif cty in by_name:
    d[0xF2] = by_name[cty]
else:
    sys.exit(f'unknown country {cty} (--countries lists them)')
factory_pricing = bytes.fromhex('01010000803e0000803e0000000000000000000000000000803f0000000000000000'
                                '010204061e00')     # pricing scheme 1; credits per coin value 1,2,4,6,30
assert len(factory_pricing) == 0x28
d[0x170:0x198] = factory_pricing
struct.pack_into('<5f', d, 0x198, 0.25, 0.5, 0.75, 1.0, 5.0)   # coin values ($)
for i in range(120):
    d[OPT + i] = 0 if i in locked_off else 1 if i in locked_on else 2 | (1 if nv[0x40 + i] else 0)
assert len(d) == 0x400
struct.pack_into('<H', d, CSUM, checksum(d))
foot = bytearray(0xB4)
foot[0:7] = b'MTFAKE1'
struct.pack_into('<H', foot, 8, BLOCK)
struct.pack_into('<H', foot, 0xB2, sum(foot[:0xB2]) & 0xffff)
img = bytearray(8 * 1024)
img[BLOCK * 1024:(BLOCK + 1) * 1024] = xor_block(d)
img[7 * 1024:7 * 1024 + 0xB4] = bytes(c ^ fmask[i % 3] for i, c in enumerate(foot))
open(keypath, 'wb').write(img)
print(f'{keypath}: {part.decode()} {rev.rstrip(b"\0").decode()}, {COUNTRIES[d[0xF2]]}, '
      f'{bin(langs).count("1")} languages ({bin(player).count("1")} for players), '
      f'{sum(1 for i in range(120) if d[OPT + i] >= 2)} options left to the operator')
EOF
