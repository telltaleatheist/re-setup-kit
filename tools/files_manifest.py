#!/usr/bin/env python3
"""Write rebuild-files.sha256 at the top of a folder: the record the in-game menu's CHECK MY FILES reads
(include/rekit/manifest.h). A game's packaging runs it over the program's folder before archiving, so the package
carries the record of its own files. The format is sha256sum's ("<hex>  <relative/path>", '/' between folders),
every regular file but the record itself, sorted by path.

When it runs: on the finished folder, after the last file is in place and before anything seals it. On the Mac, before
codesign: the record lives inside the signed .app, and writing it after signing breaks the seal. On Linux, before the
AppImage is squashed. On Windows, before the setup or the zip is made. Files the game itself rewrites in its folder
are recorded as they ship; the game names them (MenuHooks::rewritten) and the check never compares them.

    tools/files_manifest.py <folder> [--leave-out NAME ...]
"""
import argparse
import hashlib
import os
import sys

RECORD = 'rebuild-files.sha256'


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    ap.add_argument('folder')
    ap.add_argument('--leave-out', action='append', default=[], help='a file name left out, any folder (no case)')
    a = ap.parse_args()
    root = os.path.abspath(a.folder)
    leave = {n.lower() for n in a.leave_out}
    files = []
    for top, dirs, names in os.walk(root):
        for n in names:
            p = os.path.join(top, n)
            rel = os.path.relpath(p, root).replace(os.sep, '/')
            if rel in (RECORD, RECORD + '.part') or os.path.islink(p) or not os.path.isfile(p) or n.lower() in leave:
                continue
            files.append(rel)
    lines = []
    for rel in sorted(files):
        h = hashlib.sha256()
        with open(os.path.join(root, rel), 'rb') as f:
            for block in iter(lambda: f.read(1 << 20), b''):
                h.update(block)
        lines.append(f'{h.hexdigest()}  {rel}\n')
    part = os.path.join(root, RECORD + '.part')
    with open(part, 'w', encoding='utf-8', newline='\n') as f:
        f.writelines(lines)
    os.replace(part, os.path.join(root, RECORD))
    print(f'files_manifest: {len(lines)} files in {os.path.join(root, RECORD)}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
