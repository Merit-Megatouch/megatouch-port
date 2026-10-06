#!/usr/bin/env python3
"""Dump C++ vtables from a 32-bit ELF shared object.

usage: vtdump.py lib.so [filter-substring]
Each slot is resolved through dynamic relocations (R_386_32 -> symbol,
R_386_RELATIVE -> local address mapped back through .dynsym).
"""
import sys, struct, subprocess
from elftools.elf.elffile import ELFFile
from elftools.elf.relocation import RelocationSection


def demangle(names):
    if not names:
        return []
    out = subprocess.run(['c++filt'], input='\n'.join(names), capture_output=True, text=True).stdout
    return out.split('\n')[:len(names)]


def main():
    path = sys.argv[1]
    filt = sys.argv[2] if len(sys.argv) > 2 else ''
    f = open(path, 'rb')
    elf = ELFFile(f)
    dynsym = elf.get_section_by_name('.dynsym')
    syms = list(dynsym.iter_symbols())
    addr2name = {}
    for s in syms:
        if s['st_value'] and s['st_info']['type'] in ('STT_FUNC', 'STT_OBJECT'):
            addr2name.setdefault(s['st_value'], s.name)
    relocs = {}
    for sec in elf.iter_sections():
        if isinstance(sec, RelocationSection):
            st = elf.get_section(sec['sh_link'])
            for r in sec.iter_relocations():
                relocs[r['r_offset']] = (r['r_info_type'], st.get_symbol(r['r_info_sym']).name if r['r_info_sym'] else None)

    def read(addr, n):
        for seg in elf.iter_segments():
            if seg['p_type'] == 'PT_LOAD' and seg['p_vaddr'] <= addr < seg['p_vaddr'] + seg['p_filesz']:
                off = seg['p_offset'] + addr - seg['p_vaddr']
                f.seek(off)
                return f.read(n)
        return b'\0' * n

    vts = [s for s in syms if s.name.startswith('_ZTV') and s['st_value'] and s['st_size']]
    for vt in sorted(vts, key=lambda s: s.name):
        title = demangle([vt.name])[0]
        if filt and filt not in title:
            continue
        print(f'== {title}  (size {vt["st_size"]})')
        base = vt['st_value']
        n = vt['st_size'] // 4
        entries = []
        for i in range(n):
            a = base + 4 * i
            val = struct.unpack('<I', read(a, 4))[0]
            r = relocs.get(a)
            if r and r[1]:
                entries.append((i, r[1]))
            elif r and r[0] == 8:  # R_386_RELATIVE
                entries.append((i, addr2name.get(val, f'<local 0x{val:x}>')))
            else:
                entries.append((i, f'{val:#x}' if val else '0'))
        names = demangle([e[1] for e in entries])
        for (i, raw), d in zip(entries, names):
            print(f'  [{i:2}] {d}')


if __name__ == '__main__':
    main()
