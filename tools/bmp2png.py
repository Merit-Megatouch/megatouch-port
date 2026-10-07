#!/usr/bin/env python3
"""bmp2png.py in.bmp out.png — 24/32-bit uncompressed BMP to PNG, stdlib only (smoke frames)."""
import struct, sys, zlib
d = open(sys.argv[1], 'rb').read()
off, = struct.unpack_from('<I', d, 10)
w, h, _, bpp = struct.unpack_from('<iiHH', d, 18)
bp = bpp // 8; stride = (w * bp + 3) & ~3
flip = h > 0; h = abs(h)
raw = bytearray()
for y in range(h):
    r = off + (h - 1 - y if flip else y) * stride
    raw.append(0)
    for x in range(w):
        b, g, rr = d[r + x*bp: r + x*bp + 3]
        raw += bytes((rr, g, b))
def chunk(t, c): return struct.pack('>I', len(c)) + t + c + struct.pack('>I', zlib.crc32(t + c))
open(sys.argv[2], 'wb').write(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0))
                              + chunk(b'IDAT', zlib.compress(bytes(raw), 6)) + chunk(b'IEND', b''))
