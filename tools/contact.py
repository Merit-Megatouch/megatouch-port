#!/usr/bin/env python3
"""Contact sheet of smoke-test screenshots: one tile per game (its last screenshot, or the
N-th from the end), labelled.

usage: toolchain/venv/bin/python tools/contact.py out.png game [game...] [--pick -1]
Reads build/smoke/<game>/frame*.(png|bmp) written by tools/smoke.sh.
"""
import glob
import os
import sys

from PIL import Image, ImageDraw

args = sys.argv[1:]
pick = -1
if '--pick' in args:
    i = args.index('--pick')
    pick = int(args[i + 1])
    del args[i:i + 2]
out, games = args[0], args[1:]
root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TW, TH, cols = 400, 300, 4
rows = (len(games) + cols - 1) // cols
sheet = Image.new('RGB', (cols * TW, rows * (TH + 18)), (32, 32, 32))
draw = ImageDraw.Draw(sheet)
for n, g in enumerate(games):
    shots = sorted(glob.glob(os.path.join(root, 'build/smoke', g, 'frame*')))
    x, y = (n % cols) * TW, (n // cols) * (TH + 18)
    draw.text((x + 4, y + 2), g, fill=(255, 255, 0))
    if not shots:
        draw.text((x + 4, y + 40), 'no screenshots', fill=(255, 80, 80))
        continue
    im = Image.open(shots[pick if -len(shots) <= pick < len(shots) else -1]).convert('RGB')
    im.thumbnail((TW, TH))
    sheet.paste(im, (x, y + 18))
sheet.save(out)
print(out)
