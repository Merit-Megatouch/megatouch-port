#!/usr/bin/env python3
"""Summarize a TRIX_PROFILE sample file.

usage: profreport.py out.prof [game.log] [--top N]

Prints self/inclusive time per function for the whole run and, if the game log with
[hitch] lines is given, for the samples that fall inside hitch windows only.
"""
import re
import sys
from collections import Counter


def load(path):
    syms, samples = {}, []
    with open(path, errors='replace') as f:
        for line in f:
            if line.startswith('S '):
                _, i, lib, off, name = line.rstrip('\n').split(' ', 4)
                label = name if name != '?' else f'{lib}+0x{off}'
                syms[int(i)] = f'{label}  [{lib}]'
            elif line.startswith('T '):
                parts = line.split()
                samples.append((float(parts[1]), [int(x) for x in parts[2:]]))
    return syms, samples


def hitches(log):
    out = []
    for line in open(log, errors='replace'):
        m = re.search(r'\[hitch\] t=([\d.]+)ms gap=([\d.]+)ms', line)
        if m:
            t, gap = float(m.group(1)), float(m.group(2))
            out.append((t - gap, t, line.strip()))
    return out


def report(title, syms, samples, top, period_ms):
    selfc, incl, libs = Counter(), Counter(), Counter()
    for _, st in samples:
        if not st:
            continue
        selfc[st[0]] += 1
        libs[syms[st[0]].rsplit('[', 1)[-1].rstrip(']')] += 1
        for s in set(st):
            incl[s] += 1
    n = len(samples)
    print(f'\n=== {title}: {n} samples (~{n * period_ms / 1000:.1f}s of main-thread time) ===')
    print('\n-- self time by library --')
    for lib, c in libs.most_common(12):
        print(f'{100 * c / n:6.1f}%  {lib}')
    print('\n-- self time by function --')
    for s, c in selfc.most_common(top):
        print(f'{100 * c / n:6.1f}%  {syms[s]}')
    print('\n-- inclusive time by function --')
    for s, c in incl.most_common(top):
        print(f'{100 * c / n:6.1f}%  {syms[s]}')


def main():
    args = [a for a in sys.argv[1:] if not a.startswith('--')]
    top = int(sys.argv[sys.argv.index('--top') + 1]) if '--top' in sys.argv else 30
    if '--top' in sys.argv:
        args.remove(str(top))
    syms, samples = load(args[0])
    period = (samples[-1][0] - samples[0][0]) / max(1, len(samples) - 1) if len(samples) > 1 else 2
    idle_ids = {i for i, n in syms.items() if n.startswith(('usleep', 'nanosleep', '__clock_nanosleep'))}
    total = len(samples)
    samples = [s for s in samples if not idle_ids.intersection(s[1])]
    print(f'{total} samples, {total - len(samples)} idle (frame limiter sleep) excluded')
    report('whole run', syms, samples, top, period)
    if len(args) > 1:
        hs = hitches(args[1])
        print(f'\n{len(hs)} hitches; worst:')
        for a, b, line in sorted(hs, key=lambda h: h[0] - h[1])[:15]:
            print('  ' + line)
        inside = [s for s in samples if any(a <= s[0] <= b for a, b, _ in hs)]
        if inside:
            report('inside hitch windows', syms, inside, top, period)


if __name__ == '__main__':
    main()
