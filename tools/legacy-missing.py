#!/usr/bin/env python3
"""Which loader functions legacy games still miss.

usage: tools/legacy-missing.py [--per-game] [--top N] [game...]
For every game whose game.conf preloads libmerit_legacy.so: undefined dynamic symbols of its
lib/*.so that nothing provides (libmerit_legacy.so, the runtime libraries, cabinet liballeg).
Prints symbols ranked by how many games need them, and games ranked by how many they miss.
"""
import glob, os, subprocess, sys, collections
R = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
args = sys.argv[1:]
per = '--per-game' in args
top = 60
if '--top' in args: i = args.index('--top'); top = int(args[i + 1]); del args[i:i + 2]
args = [a for a in args if not a.startswith('--')]

def syms(path, defined):
    flag = '--defined-only' if defined else '--undefined-only'
    out = subprocess.run(['nm', '-D', flag, path], capture_output=True, text=True).stdout
    return {l.split()[-1].split('@')[0] for l in out.splitlines() if l.strip() and (not defined or l.split()[1] not in 'Uw')}

provided = set()
for lib in [f'{R}/shared/bin/libmerit_legacy.so', f'{R}/shared/bin/megatouch-host'] + glob.glob(f'{R}/shared/runtime/*.so*'):
    if os.path.isfile(lib): provided |= syms(lib, True)
games = args or sorted(os.path.basename(os.path.dirname(c)) for c in glob.glob(f'{R}/games/*/game.conf')
                       if 'libmerit_legacy' in open(c).read())
need = {}
for g in games:
    miss = set()
    for so in glob.glob(f'{R}/games/{g}/lib/*.so'):
        if os.path.islink(so) or 'libmega_stubs' in so: continue
        miss |= syms(so, False)
    for so in glob.glob(f'{R}/games/{g}/lib/*.so'):
        if not os.path.islink(so) and 'libmega_stubs' not in so: miss -= syms(so, True)
    need[g] = sorted(s for s in miss - provided if s not in ('__gmon_start__', '_Jv_RegisterClasses'))
count = collections.Counter(s for m in need.values() for s in m)
dem = lambda s: subprocess.run(['c++filt', s], capture_output=True, text=True).stdout.strip()
print(f'{len(games)} games, {len(count)} distinct missing symbols')
print('\n# games by missing count')
for g in sorted(need, key=lambda g: len(need[g])):
    print(f'{len(need[g]):5d}  {g}' + ('  ' + ' '.join(dem(s) for s in need[g]) if per and len(need[g]) <= 12 else ''))
print(f'\n# top {top} symbols')
for s, n in count.most_common(top): print(f'{n:5d}  {dem(s)}')
