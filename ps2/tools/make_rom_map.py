#!/usr/bin/env python3
"""tools/rom_map.json: where things are in the ROM, from the disassembly.

    python3 tools/make_rom_map.py PS2_ASM LISTING ROM [-o tools/rom_map.json]

(build 185) The extractors read the cartridge image itself (tools/rommap.py),
not ps2.asm, so that they need no disassembly. What they still need from it
is where things are and what the disassembly calls them, and that is this
file:

  * every label ps2.asm has (as tools/ps2asm.py reads them: in column 0),
    in the order of its lines, with its ROM address;
  * the drum samples' labels (sound/ps2.pcm_drums.asm, assembled for the Z80
    at org 0 under `listing off`): their ROM address is PCMDrums plus their
    Z80 address, which the listing's symbol table gives;
  * every binclude'd file (art/...), with its address and length;
  * every constant (`NAME = value`, `equ`) with its value as the assembler
    worked it out (the listing's symbol table);
  * the ROM's size and SHA-1, which tools/rommap.py checks an image against.

LISTING is AS's listing of ps2.asm assembled to the ROM byte for byte
(tools/make_rom_map.sh makes one, with `cpu 68020` for the assembly and the
disassembly's AS/ps2p2bin.c, and runs this). Run it when the disassembly
changes.
"""
import argparse
import hashlib
import json
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from ps2asm import Asm, LABEL_RE  # noqa: E402

LST_LINE = re.compile(r"^(?:\((\d+)\))?\s*(\d+)/\s*([0-9A-F]+) : ")
# A symbol table entry: NAME : value type |, two or three to a line. The
# type is C for a code address, - for an integer (AS 1.42), S a string.
SYMBOL = re.compile(r"\*?\s*([A-Za-z0-9_.@$]+)\s*:\s*(\"[^\"]*\"|-?[0-9A-F.]+)"
                    r"\s+([A-Z-])\s*\|")
ASSIGN = re.compile(r"^([A-Za-z_][A-Za-z0-9_]*)\s*(:=|=|\bequ\b|\bset\b)",
                    re.IGNORECASE)
BINCLUDE = re.compile(r'^\s*(?:[A-Za-z_][A-Za-z0-9_]*:?)?\s*binclude\s+"([^"]+)"'
                      r'\s*(?:;.*)?$', re.IGNORECASE)


def read_listing(path):
    """(line number -> address) for ps2.asm's own lines (include level 0),
    and the symbol table (UPPER-CASE name -> (value, type))."""
    addr, symbols, in_table = {}, {}, False
    for line in Path(path).read_text(encoding="latin-1").splitlines():
        if "Symbol Table" in line:
            in_table = True
            continue
        if in_table:
            for m in SYMBOL.finditer(line):
                value = m.group(2)
                if m.group(3) in "CI-" and not value.startswith('"') \
                        and "." not in value:
                    symbols[m.group(1).upper()] = (int(value, 16), m.group(3))
            continue
        m = LST_LINE.match(line)
        if m and not m.group(1):
            addr.setdefault(int(m.group(2)), int(m.group(3), 16))
    return addr, symbols


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("asm", type=Path)
    ap.add_argument("listing", type=Path)
    ap.add_argument("rom", type=Path)
    ap.add_argument("-o", "--out", type=Path,
                    default=Path(__file__).resolve().parent / "rom_map.json")
    args = ap.parse_args()

    rom = args.rom.read_bytes()
    asm = Asm(args.asm)
    line_addr, symbols = read_listing(args.listing)
    disasm = args.asm.parent
    problems = []

    # ps2.asm's labels, in line order, as Asm sees them (its marks).
    labels = []
    for i in asm.marks:
        name = LABEL_RE.match(asm.lines[i]).group(1)
        a = line_addr.get(i + 1)
        sym = symbols.get(name.upper())
        if a is None and sym is not None:
            a = sym[0]
        if a is None:
            problems.append("no address for %s (ps2.asm:%d)" % (name, i + 1))
            continue
        if sym is not None and sym[1] == "C" and sym[0] != a \
                and asm.label_line.get(name) == i:
            problems.append("%s: listing %X, symbol table %X"
                            % (name, a, sym[0]))
        labels.append([name, a])

    # The drum samples: Z80 addresses from org 0, placed at PCMDrums.
    base = dict((n, a) for n, a in labels).get("PCMDrums")
    drums = []
    pcm = disasm / "sound" / "ps2.pcm_drums.asm"
    if pcm.exists() and base is not None:
        for line in pcm.read_text(errors="replace").splitlines():
            m = LABEL_RE.match(line)
            if not m:
                continue
            sym = symbols.get(m.group(1).upper())
            if sym is None:
                problems.append("no value for %s (drums)" % m.group(1))
                continue
            drums.append([m.group(1), base + sym[0]])

    # binclude'd files: the line's own address, the file's length.
    files = {}
    for i, line in enumerate(asm.lines):
        m = BINCLUDE.match(line)
        if not m:
            continue
        name = m.group(1).replace("\\", "/")
        a = line_addr.get(i + 1)
        size = (disasm / name).stat().st_size
        if a is None:
            problems.append("no address for binclude %s" % name)
            continue
        if rom[a:a + size] != (disasm / name).read_bytes():
            problems.append("%s is not the ROM's bytes at %X" % (name, a))
        files[name] = [a, size]

    # Constants: names as the sources write them, values as AS made them.
    # Names given := or set (a running value, such as a table's `length`)
    # have no one value and are left out.
    label_names = {n.upper() for n, _ in labels} | {n.upper() for n, _ in drums}
    consts, running = {}, set()
    sources = [args.asm] + sorted(disasm.glob("*.asm")) + [pcm]
    for src in sources:
        if not src.exists():
            continue
        for line in src.read_text(errors="replace").splitlines():
            m = ASSIGN.match(line)
            if not m:
                continue
            name, op = m.group(1), m.group(2).lower()
            if op in (":=", "set"):
                running.add(name.upper())
                continue
            sym = symbols.get(name.upper())
            if sym is None or name.upper() in label_names:
                continue
            consts.setdefault(name, sym[0])
    consts = {n: v for n, v in consts.items() if n.upper() not in running}

    doc = {
        "rom": {"size": len(rom), "sha1": hashlib.sha1(rom).hexdigest()},
        "labels": labels,
        "drums": drums,
        "files": dict(sorted(files.items())),
        "constants": dict(sorted(consts.items())),
    }
    # One entry a line: the file diffs well when the disassembly changes.
    out = ["{"]
    out.append('"rom": %s,' % json.dumps(doc["rom"]))
    for key in ("labels", "drums"):
        out.append('"%s": [' % key)
        out.append(",\n".join('[%s, %d]' % (json.dumps(n), a)
                              for n, a in doc[key]))
        out.append("],")
    out.append('"files": {')
    out.append(",\n".join("%s: %s" % (json.dumps(k), json.dumps(v))
                          for k, v in doc["files"].items()))
    out.append("},")
    out.append('"constants": {')
    out.append(",\n".join("%s: %d" % (json.dumps(k), v)
                          for k, v in doc["constants"].items()))
    out.append("}")
    out.append("}")
    args.out.write_text("\n".join(out) + "\n", encoding="utf-8", newline="\n")
    json.loads(args.out.read_text(encoding="utf-8"))
    print("%s: %d labels, %d drum labels, %d files, %d constants"
          % (args.out, len(labels), len(drums), len(files), len(consts)))
    for p in problems[:30]:
        print("  PROBLEM", p)
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
