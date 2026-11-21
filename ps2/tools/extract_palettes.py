#!/usr/bin/env python3
"""Extract the named palettes (the Pal_* labels) from the cartridge image.

Genesis CRAM word layout:  0000 BBB0 GGG0 RRR0
    blue  = (w >> 9) & 7
    green = (w >> 5) & 7
    red   = (w >> 1) & 7
The DAC is not a linear ramp; DAC_LADDER holds the measured 8-bit
output levels for a Mega Drive VA4 in normal (non shadow/highlight) mode.

(build 185) From the ROM itself (tools/rommap.py), not ps2.asm: a palette
runs from its label to its `...End` label where it has one, else to the
next label; each two bytes are a CRAM word.
"""

import argparse
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from rommap import RomMap  # noqa: E402

DAC_LADDER = [0, 52, 87, 116, 144, 172, 206, 255]

PAL_RE = r"^Pal_[A-Za-z0-9_]+$"


def decode(word):
    r = DAC_LADDER[(word >> 1) & 7]
    g = DAC_LADDER[(word >> 5) & 7]
    b = DAC_LADDER[(word >> 9) & 7]
    return r, g, b


def collect(rom):
    palettes, skipped = {}, []
    names = {n for n, _ in rom.matching(PAL_RE)}
    for name, start in rom.matching(PAL_RE):
        if name.endswith("End"):
            continue
        end = rom.addr(name + "End") if name + "End" in names \
            else rom.next_addr(name)
        data = rom.read(start, end - start)
        if len(data) % 2:
            skipped.append((name, "an odd number of bytes"))
            continue
        palettes[name] = [int.from_bytes(data[i:i + 2], "big")
                          for i in range(0, len(data), 2)]
    return palettes, skipped


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("rom", type=Path, help="the cartridge image")
    ap.add_argument("-o", "--outdir", type=Path, default=Path("assets/palettes"))
    args = ap.parse_args()

    palettes, skipped = collect(RomMap(args.rom))
    args.outdir.mkdir(parents=True, exist_ok=True)

    doc = {}
    for name, words in palettes.items():
        doc[name] = {
            "count": len(words),
            "cram": [f"0x{w:04X}" for w in words],
            "rgb": ["#%02X%02X%02X" % decode(w) for w in words],
        }
    (args.outdir / "palettes.json").write_text(json.dumps(doc, indent=2), newline="\n")

    # (build 97b) No PNGs: every palette the game loads is a PalettePtrs
    # entry, written by extract_paletteptrs.py under its own name, and the
    # Pal_* PNGs this wrote were byte-for-byte copies of those entries that
    # nothing loaded. palettes.json is what extract_art.py colours with.

    print(f"extracted {len(palettes)} palettes -> {args.outdir/'palettes.json'}")
    for name, why in skipped:
        print(f"  skipped {name}: {why}", file=sys.stderr)


if __name__ == "__main__":
    main()
