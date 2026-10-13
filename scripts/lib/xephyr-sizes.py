#!/usr/bin/env python3
"""Give Xephyr's RandR the cabinet's screen sizes: xephyr-sizes.py XEPHYR

Xephyr only offers a fixed list of sizes (1600x1200 ... 1024x768, 832x624, 800x600, 720x400,
480x640, 640x480, 640x400, 320x240, 240x320). The ION switches between 640x480, 768x480,
800x600, 960x600 and 1280x800; a size missing from the list leaves the game drawing in the top-left
corner of a bigger screen (issue #1: 960x600). Replaced in place: 832x624 -> 1280x800,
720x400 -> 768x480, 480x640 (portrait) -> 960x600. Idempotent; prints what it did.
"""
import os
import struct
import sys

ORIGINAL = struct.pack('<8i', 832, 624, 800, 600, 720, 400, 480, 640)
FIRST = struct.pack('<8i', 1280, 800, 800, 600, 768, 480, 480, 640)     # patched before 2026-10-13
DONE = struct.pack('<8i', 1280, 800, 800, 600, 768, 480, 960, 600)


def main(path):
    d = bytearray(open(path, 'rb').read())
    if d.find(DONE) >= 0:
        print('  Xephyr size table: already patched')
        return 0
    i = d.find(FIRST)
    if i < 0:
        i = d.find(ORIGINAL)
    if i < 0:
        print('  Xephyr size table not found: unknown Xephyr build', file=sys.stderr)
        return 1
    d[i:i + len(DONE)] = DONE
    open(path + '.new', 'wb').write(d)
    os.chmod(path + '.new', 0o755)
    os.replace(path + '.new', path)
    print('  Xephyr size table patched at %#x (1280x800, 768x480, 960x600)' % i)
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1]))
