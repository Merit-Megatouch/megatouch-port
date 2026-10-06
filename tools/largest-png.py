#!/usr/bin/env python3
"""Print the largest PNG under a folder as 'WxH (relative/path.png)' — a full-screen
background is the best hint of a game's real screen size. Prints 'none' if there are no PNGs."""
import os
import struct
import sys

best = (0, 0, '')
for root, _, files in os.walk(sys.argv[1]):
    for name in files:
        if not name.lower().endswith('.png'):
            continue
        path = os.path.join(root, name)
        try:
            with open(path, 'rb') as f:
                w, h = struct.unpack('>II', f.read(24)[16:24])
        except (OSError, struct.error):
            continue
        if w * h > best[0] * best[1]:
            best = (w, h, os.path.relpath(path, sys.argv[1]))
print(f'{best[0]}x{best[1]} ({best[2]})' if best[0] else 'none')
