#!/usr/bin/env python3
"""Look for a game's data in a tree that is meant to have none.

  rom_leak_check.py --rom ROM [--rom ROM ...] [--assets DIR ...]
                    [--known-text PATH ...] [--min-text N] [--allow GLOB ...]
                    TREE [TREE ...]

Four checks, each file of each TREE (.git skipped):

  raw      a run of the ROM's bytes in the file (16-byte windows of every
           ROM; padding -- fewer than 5 different byte values -- and counts
           in equal steps are skipped, see `telling`): cut-out chunks,
           packed art, a ROM renamed
  hex      the ROM's bytes written as two-digit hex in a text file
           ($0C, 0x0C or bare 0C, in order): hex dumps, dc.b blocks
  asset    a file identical to one under an --assets DIR (the private
           tree's assets: PNGs, JSON and maps the extractors made, which the
           raw check cannot see because they are converted)
  text     a line of the game's text from --known-text files or folders
           (the private tree's script files: `{...}` codes split a line,
           fragments of at least --min-text characters, default 20)

Exit 1 when anything is found. --allow GLOB passes files that are meant to
carry data (a licence text, a test vector), matched against the path
relative to its TREE.
"""
import argparse
import fnmatch
import hashlib
import os
import re
import sys

W = 16


def telling(w):
    """Whether a window says anything: not padding (fewer than 5 different
    bytes) and not a count ($10, $11, $12 ... in equal steps, which index
    comments and enum tables share with any counting table in a ROM)."""
    if len(set(w)) < 5:
        return False
    steps = {(w[k + 1] - w[k]) & 0xFF for k in range(len(w) - 1)}
    return len(steps) > 1


def windows(data):
    s = set()
    for i in range(len(data) - W + 1):
        w = data[i:i + W]
        if telling(w):
            s.add(w)
    return s


HEXTOK = re.compile(rb"(?<![0-9A-Za-z$#_.])(?:\$|0[xX])?([0-9A-Fa-f]{2})"
                    rb"(?![0-9A-Za-z_])")


def is_text(data):
    return b"\0" not in data[:8192]


def walk(tree):
    for root, dirs, files in os.walk(tree):
        dirs[:] = [d for d in dirs if d != ".git"]
        for f in files:
            p = os.path.join(root, f)
            yield p, os.path.relpath(p, tree).replace("\\", "/")


def known_fragments(paths, min_len):
    frags = set()
    files = []
    for p in paths:
        if os.path.isdir(p):
            files += [q for q, _ in walk(p)]
        else:
            files.append(p)
    for q in files:
        try:
            text = open(q, "rb").read().decode("utf-8", "replace")
        except OSError:
            continue
        for line in text.splitlines():
            if line.startswith(("#", "[")):
                continue
            for frag in re.split(r"\{[^}]*\}", line):
                frag = frag.strip()
                if len(frag) >= min_len:
                    frags.add(frag)
    return frags


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--rom", action="append", required=True)
    ap.add_argument("--assets", action="append", default=[])
    ap.add_argument("--known-text", action="append", default=[])
    ap.add_argument("--min-text", type=int, default=20)
    ap.add_argument("--allow", action="append", default=[])
    ap.add_argument("tree", nargs="+")
    a = ap.parse_args()

    rom_w = set()
    for r in a.rom:
        rom_w |= windows(open(r, "rb").read())
    asset_hashes = {}
    for d in a.assets:
        for p, rel in walk(d):
            h = hashlib.sha256(open(p, "rb").read()).hexdigest()
            asset_hashes.setdefault(h, rel)
    frags = known_fragments(a.known_text, a.min_text)
    by_head = {}
    for f in frags:
        by_head.setdefault(f[:a.min_text], []).append(f)

    found = 0
    checked = 0
    for tree in a.tree:
        for p, rel in walk(tree):
            if any(fnmatch.fnmatch(rel, g) for g in a.allow):
                continue
            try:
                data = open(p, "rb").read()
            except OSError:
                continue
            checked += 1
            hits = []
            h = hashlib.sha256(data).hexdigest()
            if h in asset_hashes:
                hits.append("asset: the same file as %s" % asset_hashes[h])
            raw = [i for i in range(0, len(data) - W + 1, 8)
                   if data[i:i + W] in rom_w]
            if raw:
                hits.append("raw: %d windows of ROM bytes, first at offset "
                            "%d" % (len(raw), raw[0]))
            if is_text(data):
                toks = bytes(int(m.group(1), 16)
                             for m in HEXTOK.finditer(data))
                hx = sum(1 for i in range(len(toks) - W + 1)
                         if toks[i:i + W] in rom_w)
                if hx:
                    hits.append("hex: %d windows of ROM bytes written as "
                                "hex" % hx)
                if by_head:
                    text = data.decode("utf-8", "replace")
                    n = a.min_text
                    seen = []
                    for i in range(len(text) - n + 1):
                        for f in by_head.get(text[i:i + n], ()):
                            if text.startswith(f, i):
                                seen.append(f)
                    if seen:
                        hits.append("text: %d lines of the game's text, "
                                    "e.g. %r" % (len(seen), seen[0][:60]))
            if hits:
                found += 1
                print(rel)
                for x in hits:
                    print("   ", x)
    print("%d files checked, %d with the game's data" % (checked, found))
    return 1 if found else 0


if __name__ == "__main__":
    sys.exit(main())
