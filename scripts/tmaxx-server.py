#!/usr/bin/env python3
"""Minimal TournaMAXX server for the 2005 Maxx software (Crown, PG3002-01 V16.10).

Packets are little-endian: u16 type, u16 total length (header included), body.
The server speaks first: Login Request (0x0011); the cabinet answers Login Response (0x0012);
the server asks for the cabinet's tournament list (0x0041 -> 0x0042), optionally sends one
tournament (0x0021 -> 0x0022), and ends with Goodbye (0xFF01) + "COMPLETE."; the cabinet
acknowledges (0xFF02). Tested against Crown on 2026-10-11.

    tmaxx-server.py [--tournament] [PORT]      (default port 17751)
"""
import socket
import struct
import sys
import time

GOODBYE_MESSAGE = b'COMPLETE.\0'


def send(c, ptype, body=b''):
    c.sendall(struct.pack('<HH', ptype, 4 + len(body)) + body)
    print('  -> %04x len %d' % (ptype, 4 + len(body)))


def recv_packet(c):
    head = b''
    while len(head) < 4:
        d = c.recv(4 - len(head))
        if not d:
            return None, None
        head += d
    ptype, length = struct.unpack('<HH', head)
    body = b''
    while len(body) < length - 4:
        d = c.recv(length - 4 - len(body))
        if not d:
            return None, None
        body += d
    return ptype, body


def cstr(b):
    return b.split(b'\0')[0].decode('latin-1')


def show_login(body):
    p = struct.pack('<HH', 0x12, 4 + len(body)) + body          # offsets below count from the header
    u32 = lambda o: struct.unpack_from('<I', p, o)[0]
    print('     version field   ', struct.unpack_from('<H', p, 4)[0])
    print('     game serial     ', repr(cstr(p[0x06:0x16])))
    print('     password        ', repr(cstr(p[0x16:0x2f])))
    print('     key ROM ID      ', p[0x32:0x3a].hex())
    print('     software        ', 'V%d.%d' % (u32(0x3b), u32(0x3f)))
    print('     check.ini       ', repr(cstr(p[0x43:0x4b])))
    print('     cabinet time    ', time.strftime('%Y-%m-%d %H:%M:%S', time.gmtime(u32(0x4c))), 'UTC')
    print('     ISP account     ', repr(cstr(p[0x51:0x60])), repr(cstr(p[0x60:0x79])), repr(cstr(p[0x79:0x92])))
    print('     key part/rev    ', repr(cstr(p[0x92:0xa2])))
    print('     platform        ', p[0xa6])
    print('     modem           ', repr(cstr(p[0xa7:0x10b])))
    print('     hardware ID     ', repr(cstr(p[0x10b:0x11b])))


def tournament_packet(tid, game, name, desc, start, end, status=2, scores_kept=10):
    """Tourn_Packet (0x0021), offsets from the packet start; times are seconds from now"""
    p = bytearray(0xf93)
    struct.pack_into('<IIIiiII', p, 4, tid, game, status, start, end, 0, 0)  # +0x18 credits/play (0 = game's price), +0x1c game options
    p[0x20:0x20 + len(name)] = name.encode()[:50]
    p[0x53:0x53 + len(desc)] = desc.encode()[:100]
    struct.pack_into('<II', p, 0xb8, 12345, 1)                     # random seed, seed increment
    for i in range(3):
        g = ('Group %d' % (i + 1)).encode()
        r = ('Prize %d' % (i + 1)).encode()
        p[0xc0 + i * 0x33:0xc0 + i * 0x33 + len(g)] = g          # group names
        p[0x159 + i * 0x33:0x159 + i * 0x33 + len(r)] = r        # prize names
    struct.pack_into('<i', p, 0x1f2, end)                          # show scores from (seconds from now)
    struct.pack_into('<I', p, 0x22a, 0xffffffff)                   # no jackpot/location value
    for i in range(3):
        p[0xf83 + i * 4] = 0xff                                    # promote_1..3: none
    struct.pack_into('<I', p, 0xf8f, scores_kept)
    return bytes(p[4:])


def session(c, addr):
    print('cabinet connected from %s:%d' % addr)
    session_id = int(time.time()) & 0x7fffffff
    send(c, 0x0011, struct.pack('<HII', 0, session_id, int(time.time())))
    while True:
        ptype, body = recv_packet(c)
        if ptype is None:
            print('  cabinet closed the connection')
            return
        print('  <- %04x len %d' % (ptype, 4 + len(body)))
        if ptype == 0x0012:
            show_login(body)
            send(c, 0x0041)                                         # list your tournaments
        elif ptype == 0x0042:
            n = body[0x11]
            ids = struct.unpack_from('<10I', body, 0x12)[:n]
            print('     tournaments on the cabinet:', list(ids))
            if TOURNAMENT:
                send(c, 0x0021, tournament_packet(*TOURNAMENT))
            else:
                send(c, 0xFF01, GOODBYE_MESSAGE)
        elif ptype == 0x0022:
            print('     tournament %d acknowledged, %d records' % (struct.unpack_from('<I', body, 0)[0], (len(body) - 8) // 0x4d))
            send(c, 0xFF01, GOODBYE_MESSAGE)
        elif ptype == 0xFF02:
            print('  goodbye acknowledged')
            return


TOURNAMENT = None


def main():
    global TOURNAMENT
    if '--tournament' in sys.argv:
        sys.argv.remove('--tournament')
        # id, game (1 = Run 21), name, description, start, end (seconds from now)
        TOURNAMENT = (1001, 1, 'Test Tournament', 'Run 21 test week', -3600, 7 * 86400)
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 17751
    s = socket.socket()
    s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    s.bind(('', port))
    s.listen(4)
    print('listening on port %d' % port)
    while True:
        c, addr = s.accept()
        c.settimeout(150)
        try:
            session(c, addr)
        except (OSError, socket.timeout) as e:
            print('  error:', e)
        finally:
            c.close()


if __name__ == '__main__':
    main()
