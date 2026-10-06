#!/usr/bin/env python3
"""Decode a Megatouch .spr(.gz) file and write frames as PNG over a checkerboard.
usage: sprdump.py file.spr.gz outprefix [frame...] [--mode N]"""
import gzip, struct, sys, zlib

def frames(path):
    d = gzip.open(path).read() if path.endswith('.gz') else open(path, 'rb').read()
    o = 4
    while o + 12 <= len(d):
        sz, w, h = struct.unpack_from('<III', d, o)
        yield w, h, d[o + 12:o + 12 + sz]
        o += 12 + sz

def decode(w, h, fr, alpha_of):
    px = [[(0, 0, 0, 0)] * w for _ in range(h)]
    n = len(fr) // 2
    W = struct.unpack_from('<%dH' % n, fr)
    i = x = y = 0
    def rgb(c): return ((c >> 11) & 31) * 255 // 31, ((c >> 5) & 63) * 255 // 63, (c & 31) * 255 // 31
    while i < n:
        c = W[i]; op = c >> 10; k = c & 0x3ff; i += 1
        if op == 0: y += k
        elif op == 1: x += k
        elif op == 2:
            for j in range(k):
                if y < h and x < w: px[y][x] = rgb(W[i + j]) + (255,)
                x += 1
            i += k
        elif op == 3: y += 1; x = 0
        elif op == 4: break
        elif op == 5:
            for j in range(k):
                col, a = W[i + 2 * j], W[i + 2 * j + 1]
                if y < h and x < w: px[y][x] = rgb(col) + (alpha_of(a),)
                x += 1
            i += 2 * k
    return px

def png(path, rows):
    h = len(rows); w = len(rows[0])
    raw = b''.join(b'\0' + bytes(v for p in r for v in p) for r in rows)
    def chunk(t, b): return struct.pack('>I', len(b)) + t + b + struct.pack('>I', zlib.crc32(t + b) & 0xffffffff)
    open(path, 'wb').write(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0)) +
                           chunk(b'IDAT', zlib.compress(raw)) + chunk(b'IEND', b''))

def over_checker(px, scale=2):
    out = []
    for y, r in enumerate(px):
        row = []
        for x, (R, G, B, A) in enumerate(r):
            bg = 200 if ((x // 8 + y // 8) & 1) else 120
            row.append(tuple((v * A + bg * (255 - A)) // 255 for v in (R, G, B)))
        out.append(row)
    return out

if __name__ == '__main__':
    path, prefix = sys.argv[1], sys.argv[2]
    mode = 0
    args = sys.argv[3:]
    if '--mode' in args:
        mode = int(args[args.index('--mode') + 1]); args = args[:args.index('--mode')]
    want = set(int(a) for a in args)
    modes = {0: lambda a: min(255, (a & 0xff) * 255 // 31), 1: lambda a: a >> 8, 2: lambda a: min(255, (a & 0x1f) * 8)}
    for idx, (w, h, fr) in enumerate(frames(path)):
        if want and idx not in want: continue
        png('%s_%02d.png' % (prefix, idx), over_checker(decode(w, h, fr, modes[mode])))
