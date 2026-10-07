#!/usr/bin/env python3
"""Find a game's entry in the cabinet's gamedata.xml files.

usage: gameinfo.py <name> <gamedata.xml> [<gamedata.xml> ...]

<name> may be a DLLName (g_trix), a Directory, or a GameId (G_TRIX), case-insensitive.
Prints shell assignments: GAMEID_NAME DLL DIR DESC RES (eval-able), exit 1 if not found.
"""
import re
import shlex
import sys


def entries(path):
    try:
        text = open(path, encoding='utf-8', errors='replace').read()
    except OSError:
        return
    for block in re.findall(r'<game>(.*?)</game>', text, re.S):
        def tag(t):
            m = re.search(rf'<{t}>\s*(.*?)\s*</{t}>', block, re.S)
            return m.group(1) if m else ''
        yield {'GAMEID_NAME': tag('GameId'), 'DLL': tag('DLLName'), 'DIR': tag('Directory'),
               'DESC': tag('Description'), 'RES': tag('UseResolution'), 'ALT': tag('DLLSupportFileName')}


def main():
    want = sys.argv[1].lower()
    for path in sys.argv[2:]:
        for e in entries(path):
            if want in (e['GAMEID_NAME'].lower(), e['DLL'].lower(), e['DIR'].lower()):
                for k, v in e.items():
                    print(f'{k}={shlex.quote(v)}')
                return 0
    return 1


if __name__ == '__main__':
    sys.exit(main())
