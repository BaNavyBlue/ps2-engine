#!/usr/bin/env python3
"""(build 187) Write src/extract/asset_names.c from tools/asset_names.py:
the asset names the C extractors (src/extract/) take, the same ones the
Python extractors take, in the same order. Run from ps2/:

    python3 tools/gen_asset_names.py           # write it
    python3 tools/gen_asset_names.py --check   # exit 1 if it is not current

The file is checked in, so building needs no Python;
tools/extract_c_check.py runs --check.
"""
import argparse
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import asset_names  # noqa: E402

OUT = HERE.parent / "src" / "extract" / "asset_names.c"


def c_str(s):
    for ch in s:
        if not (" " <= ch <= "~") or ch in "\\\"":
            raise SystemExit("a name C would have to escape: %r" % s)
    return '"%s"' % s


def pairs(name, items):
    lines = ["const ExNamePair %s[] = {" % name]
    for k, v in items:
        lines.append("    { %s, %s }," % (c_str(k), c_str(v)))
    lines.append("    { NULL, NULL }")
    lines.append("};")
    return lines


def strings(name, items):
    lines = ["const char *const %s[] = {" % name]
    for s in items:
        lines.append("    %s," % c_str(s))
    lines.append("    NULL")
    lines.append("};")
    return lines


def text():
    out = ["/* Written by tools/gen_asset_names.py from tools/asset_names.py",
           " * (build 187): edit that, then run this. The C extractors' names",
           " * for the assets whose ROM label or file says nothing, in the",
           " * Python's order. */",
           "",
           '#include "asset_names.h"',
           "",
           "#include <stddef.h>",
           ""]
    top = max(asset_names.PALETTE_NAMES)
    out.append("/* PalettePtrs by index; NULL where there is no name. */")
    out.append("const char *const EX_PALETTE_NAMES[%d] = {" % (top + 1))
    for i in range(top + 1):
        n = asset_names.PALETTE_NAMES.get(i)
        out.append("    %s," % (c_str(n) if n is not None else "NULL"))
    out.append("};")
    out.append("const int EX_PALETTE_NAME_COUNT = %d;" % (top + 1))
    out.append("")
    out.append("/* The level tilesets by index, in the Python's order. */")
    out.append("const ExIndexName EX_TILESET_NAMES[] = {")
    for i, n in asset_names.TILESET_NAMES.items():
        out.append("    { %d, %s }," % (i, c_str(n)))
    out.append("    { -1, NULL }")
    out.append("};")
    out.append("")
    out += pairs("EX_SPRITE_ART_NAMES", asset_names.SPRITE_ART_NAMES.items())
    out.append("")
    out += pairs("EX_LEVEL_ANIM_NAMES", asset_names.LEVEL_ANIM_NAMES.items())
    out.append("")
    out += pairs("EX_ART_RENAMES", asset_names.ART_RENAMES.items())
    out.append("")
    out += strings("EX_ART_NOT_ART", sorted(asset_names.ART_NOT_ART))
    out.append("")
    out += strings("EX_PICTURE_ART", sorted(asset_names.PICTURE_ART))
    out.append("")
    out.append("const ExRamp EX_RAMPS[] = {")
    for label, (name, across) in asset_names.RAMP_NAMES.items():
        out.append("    { %s, %s, %d }," % (c_str(label), c_str(name), across))
    out.append("    { NULL, NULL, 0 }")
    out.append("};")
    out.append("")
    out += pairs("EX_DRUM_SAMPLE_FILES",
                 asset_names.DRUM_SAMPLE_FILES.items())
    out.append("")
    out += pairs("EX_MUSIC_FILES", asset_names.MUSIC_FILES.items())
    out.append("")
    out += pairs("EX_TABLE_FILES", asset_names.TABLE_FILES.items())
    out.append("")
    out += pairs("EX_SCRIPT_FILES", asset_names.SCRIPT_FILES.items())
    out.append("")
    out += strings("EX_INSTRUMENT_NAMES", asset_names.INSTRUMENT_NAMES)
    out.append("")
    out += strings("EX_ENVELOPE_NAMES", asset_names.ENVELOPE_NAMES)
    return "\n".join(out) + "\n"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--check", action="store_true")
    a = ap.parse_args()
    want = text()
    if a.check:
        have = OUT.read_text(encoding="utf-8") if OUT.is_file() else ""
        if have != want:
            print("%s is not tools/asset_names.py's: run "
                  "tools/gen_asset_names.py" % OUT.relative_to(HERE.parent))
            return 1
        print("asset names: current")
        return 0
    OUT.write_text(want, encoding="utf-8", newline="\n")
    print("wrote %s" % OUT.relative_to(HERE.parent))
    return 0


if __name__ == "__main__":
    sys.exit(main())
