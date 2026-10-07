#!/usr/bin/env python3
"""Cabinet service libraries that define symbols a game still misses.

usage: tools/cabinet-providers.py <game> [--all]
Prints the libraries (one per line, most symbols first) from SERVICE_LIBS that define any
symbol the game imports and the port does not implement. --all also lists other providers
(e.g. other games' .so) on stderr for information.
Needs build/index/cabinet-syms.tsv (symbol<TAB>/path for every cabinet .so; made by
`tools/cabinet-providers.py --index`).
"""
import collections, glob, os, subprocess, sys
R = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
IDX = f'{R}/build/index/cabinet-syms.tsv'
SERVICE_LIBS = ['libsettings.so', 'libgendef_xml.so', 'libgendef_common.so', 'libgendef_db.so',
                'libbooks.so', 'libsystem_info.so', 'liblocale.so', 'libenums.so', 'liblayout.so',
                'libmoney.so', 'libdebug_shared.so', 'libami.so', 'libttiface.so', 'libmvideo.so',
                'libcontent.so', 'libads.so']

def nm(path, defined):
    flag = '--defined-only' if defined else '--undefined-only'
    out = subprocess.run(['nm', '-D', flag, path], capture_output=True, text=True).stdout
    return {l.split()[-1].split('@')[0] for l in out.splitlines() if l.strip() and (not defined or l.split()[1] not in 'Uw')}

if sys.argv[1:2] == ['--index']:
    os.makedirs(os.path.dirname(IDX), exist_ok=True)
    root = f'{R}/cabinet/root'
    rows = set()
    for dp, _, fs in os.walk(root):
        for f in fs:
            p = os.path.join(dp, f)
            if '.so' in f and os.path.isfile(p) and not os.path.islink(p):
                for s in nm(p, True): rows.add(f'{s}\t{p[len(root):]}')
    open(IDX, 'w').write('\n'.join(sorted(rows)) + '\n')
    sys.exit(0)

g = sys.argv[1]
provided = set()
for lib in [f'{R}/shared/bin/libmerit_legacy.so', f'{R}/shared/bin/megatouch-host'] + glob.glob(f'{R}/shared/runtime/*.so*'):
    if os.path.isfile(lib): provided |= nm(lib, True)
miss = set()
own = [so for so in glob.glob(f'{R}/games/{g}/lib/*.so') if not os.path.islink(so) and 'libmega_stubs' not in so]
main = f'{R}/games/{g}/lib/{g}.so'
for so in own:
    if os.path.basename(so) in SERVICE_LIBS: provided |= nm(so, True)
miss = nm(main, False) if os.path.exists(main) else set()
miss -= provided
idx = collections.defaultdict(set)
for l in open(IDX):
    s, f = l.rstrip('\n').split('\t')
    if s in miss: idx[s].add(f)
count = collections.Counter()
other = collections.Counter()
for s, fs in idx.items():
    names = {os.path.basename(f) for f in fs}
    svc = [n for n in SERVICE_LIBS if n in names]
    if svc: count[svc[0]] += 1
    else:
        for n in names: other[n] += 1
for lib, n in count.most_common(): print(lib)
if '--all' in sys.argv:
    for lib, n in other.most_common(): print(f'  (other provider) {lib}: {n}', file=sys.stderr)
