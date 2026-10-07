#!/usr/bin/env python3
"""Dump the string literals and called methods of .NET methods (no full disassembly).
usage: ildump.py Assembly.dll [TypeName.MethodName-substring ...]"""
import struct, sys
import dnfile
pe = dnfile.dnPE(sys.argv[1])
md = pe.net.mdtables
us = pe.net.user_strings
filters = sys.argv[2:]
def member_name(tok):
    t, i = tok >> 24, tok & 0xffffff
    try:
        if t == 0x06: r = md.MethodDef[i - 1]; return str(r.Name)
        if t == 0x0A:
            r = md.MemberRef[i - 1]
            cls = r.Class.row
            cname = getattr(cls, 'TypeName', getattr(cls, 'Name', '?'))
            return f'{cname}::{r.Name}'
    except Exception:
        pass
    return hex(tok)
types = md.TypeDef
# map method index -> type name
owner = {}
for ti, t in enumerate(types):
    for m in t.MethodList:
        owner[m.row_index] = str(t.TypeName)
for mi, m in enumerate(md.MethodDef, start=1):
    full = f'{owner.get(mi, "?")}.{m.Name}'
    if filters and not any(f in full for f in filters):
        continue
    rva = m.Rva
    if not rva:
        continue
    off = pe.get_offset_from_rva(rva)
    data = pe.__data__
    hdr = data[off]
    if hdr & 3 == 2: code, size = off + 1, hdr >> 2
    else:
        size = struct.unpack_from('<I', data, off + 4)[0]; code = off + (struct.unpack_from('<H', data, off)[0] >> 12) * 4
    body = data[code:code + size]
    out = []
    i = 0
    while i < len(body):
        op = body[i]
        if op == 0x72:   # ldstr
            tok = struct.unpack_from('<I', body, i + 1)[0]
            try:
                out.append('"' + us.get(tok & 0xffffff).value + '"')
            except Exception:
                # read the #US heap directly: compressed length, UTF-16LE
                heap = pe.net.user_strings.__data__ if hasattr(pe.net.user_strings, '__data__') else None
                o = tok & 0xffffff
                try:
                    raw = pe.net.user_strings.get_bytes(o) if hasattr(pe.net.user_strings, 'get_bytes') else None
                    out.append('"' + (raw.decode('utf-16le', 'replace') if raw else f'#US@{o:x}') + '"')
                except Exception:
                    out.append(f'ldstr#US@{o:x}')
            i += 5; continue
        if op in (0x28, 0x6F, 0x73):  # call, callvirt, newobj
            tok = struct.unpack_from('<I', body, i + 1)[0]
            out.append({0x28: 'call ', 0x6F: 'callvirt ', 0x73: 'new '}[op] + member_name(tok))
            i += 5; continue
        i += 1
    print(f'== {full}')
    for o in out: print('   ', o)
