#!/usr/bin/env python3
"""Show how a library calls each imported function: the pushes before one call site.
usage: callsites.py lib.so [name-filter]   (32-bit cdecl; first push shown last = 1st arg)"""
import re, subprocess, sys
lib = sys.argv[1]; flt = sys.argv[2] if len(sys.argv) > 2 else ''
out = subprocess.run(['objdump', '-d', '-M', 'intel', '--no-show-raw-insn', lib], capture_output=True, text=True).stdout.split('\n')
seen = {}
for i, l in enumerate(out):
    m = re.search(r'call\s+[0-9a-f]+ <([^@>]+)@plt>', l)
    if not m or flt not in m.group(1) or m.group(1) in seen:
        continue
    pushes = []
    for j in range(i - 1, max(0, i - 25), -1):
        t = out[j]
        if re.search(r'\scall\s|\sret\s*$|add\s+esp', t):
            break
        if '\tpush ' in t or ' push ' in t:
            pushes.append(t.split('push', 1)[1].strip())
    seen[m.group(1)] = pushes
for name, p in sorted(seen.items()):
    dem = subprocess.run(['c++filt', name], capture_output=True, text=True).stdout.strip()
    print(f'{dem}\n    args: {" | ".join(p)}')
