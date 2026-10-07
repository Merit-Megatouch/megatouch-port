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
                'libsystem_info.so', 'liblocale.so', 'libenums.so',
                'libdebug_shared.so', 'libmvideo.so',
                'libcontent.so', 'libads.so', 'libdebug_mock.so']

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
# imports of the game's own libraries: its .so plus helper libraries it ships (libmerit2d.so,
# libmeritbasegame.so...), less what they define for each other
miss = set()
gamedefs = set()
for so in own:
    b = os.path.basename(so)
    if b in SERVICE_LIBS: continue
    miss |= nm(so, False)
    gamedefs |= nm(so, True)
miss -= provided | gamedefs
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
chosen = [lib for lib, n in count.most_common()]

# The chosen libraries' own imports that nothing else provides (e.g. libdebug_shared needs
# libdebug_mock's ExtendedCrashInfo): add their providers, loaded first.
def lib_path(name):
    for f in (f'{R}/games/{g}/lib/{name}', f'{R}/shared/engine-sdk/{name}', f'{R}/cabinet/root/usr/local/lib/{name}'):
        if os.path.exists(f): return f
full = {}
for l in open(IDX):
    s_, f_ = l.rstrip('\n').split('\t')
    if os.path.basename(f_) in SERVICE_LIBS: full.setdefault(s_, set()).add(os.path.basename(f_))
# Close over the chosen libraries' own imports, then order so providers load before users.
order = list(chosen)
deps = {}
i = 0
while i < len(order):
    lib = order[i]; i += 1
    p_ = lib_path(lib)
    if not p_: deps[lib] = set(); continue
    d = set()
    for sym in nm(p_, False) - provided:
        cands = sorted(full.get(sym, ()))
        if cands and lib not in cands:
            d.add(cands[0])
            if cands[0] not in order: order.append(cands[0])
    deps[lib] = d
out, state = [], {}
def visit(l):
    if state.get(l) == 2: return
    if state.get(l) == 1: return               # cycle: leave as is
    state[l] = 1
    for d in sorted(deps.get(l, ())): visit(d)
    state[l] = 2
    out.append(l)
for l in order: visit(l)
order = out
for lib in order: print(lib)
if '--all' in sys.argv:
    for lib, n in other.most_common(): print(f'  (other provider) {lib}: {n}', file=sys.stderr)
