#!/usr/bin/env python3
"""Design resolution of a GameDevice game from its layout XMLs: the right/bottom edges most
sprites stay within (90th percentile of x+width and y+height), snapped to a known screen size.
usage: layout-extent.py <asset dir>"""
import os, re, sys
xs, ys = [], []
for root, _, files in os.walk(sys.argv[1], followlinks=True):
    for f in files:
        if not f.lower().endswith('.xml'):
            continue
        try:
            t = open(os.path.join(root, f), errors='replace').read()
        except OSError:
            continue
        for blk in re.findall(r'<sprite2d>(.*?)</sprite2d>', t, re.S):
            def g(tag):
                m = re.search(rf'<{tag}>\s*(-?[\d.]+)\s*</{tag}>', blk)
                return float(m.group(1)) if m else None
            x, y, w, h = g('x'), g('y'), g('width'), g('height')
            if None not in (x, y, w, h) and w > 0 and h > 0:
                xs.append(x + w); ys.append(y + h)
if not xs:
    print('none'); sys.exit()
xs.sort(); ys.sort()
px, py = xs[int(len(xs) * .97)], ys[int(len(ys) * .97)]
sizes = [(640, 480), (800, 600), (1024, 768), (1280, 800), (1280, 1024)]
best = min((s for s in sizes if s[0] >= px * 0.98 and s[1] >= py * 0.98), default=sizes[-1], key=lambda s: s[0] * s[1])
print(f'{best[0]}x{best[1]} (97th pct edges {px:.0f},{py:.0f} from {len(xs)} sprites)')
