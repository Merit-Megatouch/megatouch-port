#!/usr/bin/env python3
"""partimage: read Megatouch installer discs (partimage 0.6 images inside ISO files).

  scripts/partimage.py info DISC.iso [DISC.iso ...]        the discs' version and partition images
  scripts/partimage.py extract N OUT.img DISC.iso [...]    partition image partN (its volumes may be
                                                           spread over several discs) → raw image
  scripts/partimage.py convert OUT.img VOLUME [...]        convert loose volume files (partN.gz.000 …)

Megatouch installer discs (Force and ION, 2004–2014) are bootable ISOs whose `lib/images/` holds
one partimage image per partition, gzip-compressed and split into ~512 MB volumes:
`partN.gz.000`, `.001`, … The ION installer restores part1 (bootloader), part2 (/home), part5
(root) and part6 (/usr/local/ion_only); /var starts empty and is filled at first boot from
root's /factorybackup/var.tar.gz. The raw image this writes is an ext2/3 filesystem; read it with
debugfs (`debugfs -R "rdump / DIR" OUT.img`), no mounting or root needed.

Format (partimage 0.6.x, as found on these discs): each volume starts with a 512-byte header
("PaRtImAgE-VoLuMe", volume number at 0x60). Volume 0 then holds the main header, an MBR backup,
"MAGIC-BEGIN-LOCALHEADER" (u64 block size, used blocks, block count, bitmap size), the block
bitmap after "MAGIC-BEGIN-BITMAP", then after "MAGIC-BEGIN-DATABLOCKS" the used blocks in order.
Later volumes continue the same stream after their header. Every 64 KiB of block data is
followed by a 16-byte check record: "CHK\\0", CRC-32 of those 64 KiB, position.
"""
import gzip, io, os, re, struct, sys, zlib

VOLHDR, CHUNK = 512, 65536


# ---------------------------------------------------------------- ISO 9660 (with Joliet)
class Iso:
    def __init__(self, path):
        self.path, self.f = path, open(path, 'rb')
        pvd = self.rd(16, 2048)
        if pvd[1:6] != b'CD001':
            raise ValueError('%s: not an ISO 9660 image' % path)
        self.volume = pvd[40:72].decode(errors='replace').strip()
        self.root, self.enc = pvd[156:190], 'ascii'
        for i in range(17, 32):
            d = self.rd(i, 2048)
            if d[0] == 255:
                break
            if d[0] == 2 and d[88:91] in (b'%/@', b'%/C', b'%/E'):
                self.root, self.enc = d[156:190], 'utf-16-be'

    def rd(self, lba, n):
        self.f.seek(lba * 2048)
        return self.f.read(n)

    def entries(self, rec):
        lba, size = struct.unpack_from('<I', rec, 2)[0], struct.unpack_from('<I', rec, 10)[0]
        data, o = self.rd(lba, size), 0
        while o < len(data):
            n = data[o]
            if n == 0:
                o = (o // 2048 + 1) * 2048
                continue
            r, o = data[o:o + n], o + n
            name = r[33:33 + r[32]]
            if name in (b'\0', b'\1'):
                continue
            yield name.decode(self.enc, errors='replace').split(';')[0], r

    def find(self, path):
        rec = self.root
        for part in path.strip('/').split('/'):
            rec = next((r for n, r in self.entries(rec) if n.lower() == part.lower()), None)
            if rec is None:
                return None
        return rec

    def listdir(self, path):
        rec = self.find(path)
        return [n for n, _ in self.entries(rec)] if rec else []

    def open(self, path):
        rec = self.find(path)
        if rec is None:
            return None
        lba, size = struct.unpack_from('<I', rec, 2)[0], struct.unpack_from('<I', rec, 10)[0]
        return Window(open(self.path, 'rb'), lba * 2048, size)

    def read(self, path):
        w = self.open(path)
        return w.read() if w else None


class Window(io.RawIOBase):
    """a byte range of a file"""

    def __init__(self, f, off, size):
        self.f, self.pos, self.end = f, off, off + size

    def readable(self):
        return True

    def readinto(self, b):
        n = min(len(b), self.end - self.pos)
        if n <= 0:
            return 0
        self.f.seek(self.pos)
        d = self.f.read(n)
        b[:len(d)] = d
        self.pos += len(d)
        return len(d)


# ---------------------------------------------------------------- partimage volumes
class Volumes:
    """the logical stream: volume 0 whole, then each further volume without its header"""

    def __init__(self, opener_list):
        self.openers, self.i, self.g, self.first = opener_list, -1, None, None
        self.next()

    def next(self):
        self.i += 1
        if self.i >= len(self.openers):
            self.g = None
            return False
        name, opener = self.openers[self.i]
        self.g = gzip.GzipFile(fileobj=opener())
        hdr = self.g.read(VOLHDR)
        if not hdr.startswith(b'PaRtImAgE-VoLuMe'):
            sys.exit('%s: not a partimage volume' % name)
        if struct.unpack_from('<I', hdr, 0x60)[0] != self.i:
            sys.exit('%s: volume %d, expected %d' % (name, struct.unpack_from('<I', hdr, 0x60)[0], self.i))
        if self.i == 0:
            self.first = hdr
        return True

    def read(self, n):
        out = b''
        if self.first is not None:
            out, self.first = self.first[:n], None
            n -= len(out)
        while n > 0 and self.g:
            b = self.g.read(n)
            if not b:
                if not self.next():
                    break
                continue
            out += b
            n -= len(b)
        return out


def read_exact(s, n):
    d = s.read(n)
    if len(d) != n:
        sys.exit('image ends early (%d of %d bytes)' % (len(d), n))
    return d


def find_marker(s, marker, buf=b''):
    while marker not in buf:
        d = s.read(1 << 16)
        if not d:
            sys.exit('marker %s not found' % marker.decode())
        buf += d
    return buf[buf.index(marker) + len(marker):]


def convert(openers, out_path, quiet=False):
    s = Volumes(openers)
    rest = find_marker(s, b'MAGIC-BEGIN-LOCALHEADER')
    rest += read_exact(s, max(0, 104 - len(rest)))
    bs, used, count, bmsize, _ = struct.unpack_from('<QQQQQ', rest, 0)
    label = rest[40:104].split(b'\0')[0].decode(errors='replace')
    if not quiet:
        print('  %s: %d MB, %d of %d blocks used' % (label, count * bs >> 20, used, count), flush=True)
    rest = find_marker(s, b'MAGIC-BEGIN-BITMAP', rest)
    rest += read_exact(s, max(0, bmsize - len(rest)))
    bitmap, rest = rest[:bmsize], rest[bmsize:]
    pending = find_marker(s, b'MAGIC-BEGIN-DATABLOCKS', rest)

    out = open(out_path + '.partial', 'wb')
    out.truncate(count * bs)
    since, crc, bad, written, step = 0, 0, 0, 0, max(used // 10, 1)
    for blk in range(count):
        if not bitmap[blk >> 3] >> (blk & 7) & 1:
            continue
        data, need = [], bs
        while need:
            take = min(need, CHUNK - since)
            if len(pending) < take:
                pending += s.read(max(take - len(pending), 1 << 20))
                if len(pending) < take:
                    sys.exit('image ends early at block %d' % blk)
            part, pending = pending[:take], pending[take:]
            data.append(part)
            crc = zlib.crc32(part, crc)
            need -= take
            since += take
            if since == CHUNK:
                if len(pending) < 16:
                    pending += s.read(1 << 20)
                chk, pending = pending[:16], pending[16:]
                if chk[:4] != b'CHK\0':
                    sys.exit('lost sync at block %d (no check record)' % blk)
                if struct.unpack_from('<I', chk, 4)[0] != crc:
                    bad += 1
                crc = since = 0
        out.seek(blk * bs)
        out.write(b''.join(data))
        written += 1
        if not quiet and written % step == 0:
            print('    %d%%' % (written * 100 // used), flush=True)
    out.close()
    if bad:
        sys.exit('%s: %d checksum errors: the disc image is damaged' % (out_path, bad))
    os.replace(out_path + '.partial', out_path)
    return label


def disc_volumes(n, isos):
    """partN's volumes across the given discs, in order"""
    found = {}
    for iso in isos:
        for name in iso.listdir('lib/images'):
            m = re.fullmatch(r'part%d\.gz\.(\d{3})' % n, name, re.I)
            if m:
                found[int(m.group(1))] = (iso, 'lib/images/' + name)
    if not found:
        sys.exit('no partition image part%d on these discs' % n)
    missing = [i for i in range(max(found) + 1) if i not in found]
    if missing:
        sys.exit('part%d: volume(s) %s missing: give all the discs' % (n, ', '.join('%03d' % i for i in missing)))
    return [('%s:%s' % (os.path.basename(iso.path), p), (lambda iso=iso, p=p: iso.open(p)))
            for _, (iso, p) in sorted(found.items())]


def disc_info(isos):
    """version string and the partition images per disc"""
    out = []
    for iso in isos:
        ver = iso.read('share/version.dat') or b''
        parts = sorted({re.sub(r'\.gz\.\d+$', '', n) for n in iso.listdir('lib/images')})
        out.append((iso, ver.decode(errors='replace').splitlines()[0] if ver else '?', parts))
    return out


def main(argv):
    if len(argv) < 2 or argv[1] in ('-h', '--help'):
        print(__doc__)
        return 0 if len(argv) > 1 else 2
    cmd = argv[1]
    if cmd == 'info':
        for iso, ver, parts in disc_info([Iso(p) for p in argv[2:]]):
            print('%s: volume %s, %s, images: %s' % (iso.path, iso.volume, ver, ' '.join(parts) or 'none'))
        return 0
    if cmd == 'extract' and len(argv) >= 5:
        convert(disc_volumes(int(argv[2]), [Iso(p) for p in argv[4:]]), argv[3])
        return 0
    if cmd == 'convert' and len(argv) >= 4:
        convert([(p, (lambda p=p: open(p, 'rb'))) for p in argv[3:]], argv[2])
        return 0
    print(__doc__)
    return 2


if __name__ == '__main__':
    sys.exit(main(sys.argv))
