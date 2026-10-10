#!/usr/bin/env python3
"""The older security key (DS1991 MultiKey) for the 2005 software (Maxx Crown), as the fake board
serves it: key-ds1991.bin = ROM ID (8), then three subkeys of 8-byte ID + 48 data bytes, then the
64-byte scratchpad, in plain text. The board encrypts and scrambles it per read (fakeio.c,
ds1991_out); the layout is in docs/reference/maxx.md.

    ds1991key.py make|--force KEYFILE      write it (factory-like defaults)
    ds1991key.py --show KEYFILE            print what it holds
"""
import struct
import sys

# ION 2014 KeyManager::ParseOptionData, legacy keys: option i is read from position LEGACY[i]
# (below 64: a 2-bit code in subkey 3's data, byte pos/4, bits (3 - pos%4)*2; from 64: one bit in
# data byte 0x10 + (pos-64)/8, bit 7 - pos%8)
LEGACY = (0, 1, 2, 49, 3, 4, 85, 76, 5, 6, 7, 69, 70, 8, 9, 68, 80, 79, 10, 11, 75, 12, 13, 14, 73,
          77, 82, 15, 83, 72, 16, 17, 18, 19, 20, 21, 74, 22, 81, 78, 23, 24, 25, 26, 27, 28, 29, 64,
          30, 65, 31, 32, 33, 34, 71, 35, 36, 37, 38, 39, 40, 41, 42, 84, 43, 44, 45, 66, 46, 53, 47,
          67, 87, 86, 51, 48, 50, 52, 54, 56, 55, 58, 57, 59, 61, 60, 62, 63, 88, 89, 90, 91, 92, 93,
          94, 95)
# a genuine ION 2014 key's option bytes (scripts/loader-key.sh): the factory defaults
FACTORY = bytes.fromhex('030302000303000302030301010203010001030303030202010303030300030302000302020303030202020203030200020003030302010102000203'
                        '030203010302020000030300020000000202020200030000000200030200000001000202020100010202020001010101010000000000000200000200')
CABINET_CODE = bytes.fromhex('8723879fe2432498')     # FUN_0823ff40: subkey 1 data 0x28-0x2F


def crc8(data):
    """Dallas/Maxim 1-Wire CRC-8 (the board checks ROM IDs with it)"""
    crc = 0
    for b in data:
        for _ in range(8):
            mix = (crc ^ b) & 1
            crc >>= 1
            if mix:
                crc ^= 0x8C
            b >>= 1
    return crc


def rom_id():
    # family 0x82; bytes 6-5 = 0x0040 and bytes 4-1 in (0x3137F, 0x54000): the range
    # FUN_0823e140 accepts ("KEY RANGE MISMATCH" otherwise)
    body = bytes([0x82]) + struct.pack('<I', 0x00045000) + bytes([0x40, 0x00])
    return body + bytes([crc8(body)])


def make():
    id1, d1 = b'SA316010', bytearray(48)          # page 1 (DallasKeyP1: ID at +0, data at +0x10)
    d1[0] = 0                                     # NJ exception number
    d1[1] = d1[2] = 0x08                          # Champion Edition cost (and the u16 at +0x11)
    d1[3:7] = struct.pack('>I', (0x9F - 1) // 2)  # language mask (big-endian, mask = v * 2 + 1)
    for ch in range(6):                           # coin channels at +0x17: credits per pulse
        d1[7 + ch * 4] = 1
    d1[0x27] = 0                                  # lockout value
    d1[0x28:0x30] = CABINET_CODE

    id2, d2 = b'08/30/05', bytearray(48)          # page 2: ID = the key's date
    d2[0x1E] = 1                                  # default language + 1 (English)
    d2[0x1F] = 2                                  # price of the games at +0x2F
    d2[0x20:0x2A] = b'USA-STD'.ljust(10, b'\0')   # locale (+0x30; "NJ…" = New Jersey rules)
    d2[0x2A] = d2[0x2B] = 2                       # default price
    d2[0x2E] = 0                                  # country: USA

    id3, d3 = b'R00\0\0\0\0\0', bytearray(48)     # page 3: ID = revision
    for i, pos in enumerate(LEGACY):
        v = FACTORY[i]
        if pos < 64:
            d3[pos // 4] |= (v & 3) << ((3 - pos % 4) * 2)
        elif v & 1:
            d3[0x10 + (pos - 64) // 8] |= 0x80 >> (pos % 8)
    d3[0x1C] |= 0x08                              # Force hardware (ForceChk1 agrees)

    sc = bytearray(64)                            # currencies: name, value from its digits
    for n, (name, value) in enumerate((('25c', 25), ('50c', 50), ('$1', 1), ('$5', 5), ('$10', 10), ('$20', 20))):
        e = name.encode().ljust(5, b'\0') + str(value).encode()[:3].ljust(3, b'\0')
        sc[n * 8:n * 8 + 8] = e
    return rom_id() + id1 + bytes(d1) + id2 + bytes(d2) + id3 + bytes(d3) + bytes(sc)


def show(img):
    rid, p = img[:8], img[8:]
    pages = [(p[i * 56:i * 56 + 8], p[i * 56 + 8:i * 56 + 56]) for i in range(3)]
    sc = p[168:]
    print('ROM ID', rid.hex(), 'crc ok' if crc8(rid[:7]) == rid[7] else 'crc BAD')
    print('part', pages[0][0].decode(errors='replace'), 'date', pages[1][0].decode(errors='replace'),
          'revision', pages[2][0].rstrip(b'\0').decode(errors='replace'))
    d1, d2 = pages[0][1], pages[1][1]
    print('cabinet code', d1[0x28:0x30].hex(), 'languages', hex(struct.unpack('>I', d1[3:7])[0] * 2 + 1),
          'locale', d2[0x20:0x2A].split(b'\0')[0].decode(errors='replace'), 'country', d2[0x2E],
          'price', d2[0x2A])
    print('currencies', ' '.join(sc[i * 8:i * 8 + 5].split(b'\0')[0].decode(errors='replace') for i in range(6)))


def main(argv):
    if len(argv) != 3 or argv[1] not in ('make', '--force', '--show'):
        print(__doc__)
        return 2
    if argv[1] == '--show':
        show(open(argv[2], 'rb').read())
        return 0
    img = make()
    assert len(img) == 8 + 3 * 56 + 64
    open(argv[2], 'wb').write(img)
    print('%s: DS1991 key, part SA316010 R00, ROM ID %s' % (argv[2], img[:8].hex()))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
