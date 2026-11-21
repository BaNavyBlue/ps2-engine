#!/usr/bin/env python3
"""Per-block collision tables, as text.

Each level's LevelData record (ps2.asm, LoadLevelData at 13229) carries a
pointer to a table with one byte per 32x32 block. A block is four 16x16
quadrants and the byte is two nibbles of quadrant bits:

    bits 0-3  solid         a party member cannot step onto the quadrant
    bits 4-7  special       the quadrant is looked up in the level's
                            interaction list (doors, exits, signs)

The bit for a quadrant is ((y >> 3) & 2) + ((x >> 4) & 1), loc_6DE8, which
makes bit 0 top-left, 1 top-right, 2 bottom-left, 3 bottom-right.

Output is assets/levels/<stem>.collision.txt, one line a block:

    # block  solid  special        quadrants read TL TR BL BR
    03       ..##   #...

Needs only the cartridge image and the levels.json extract_levels.py wrote.
(build 185) A table is read from the ROM (tools/rommap.py), not ps2.asm:
its bytes from its label to the next.

One table belongs to no level: loc_29E16, the jet scooter's. Nothing in
LevelData points at it; ObjJetSctr_Init (ps2.asm:6780) and the getting-off
event (loc_D9AA, 19941) write it to $FFFFF72E by hand while the party rides
across Motavia, whose blocks it indexes (tileset 0). Water is open in it,
land solid, and the dam entrances ($0E, $11, $31, $55) solid and special.
It is written as assets/levels/jet_scooter.collision.txt, same format.
"""

import argparse
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from rommap import RomMap  # noqa: E402


def nibble(bits):
    return "".join("#" if bits & (1 << q) else "." for q in range(4))


# Tables no LevelData record names: (label, file stem, what it is).
EXTRA = [("loc_29E16", "jet_scooter",
          "the jet scooter's, on MotaviaOutside (blocks_motavia_outside)")]


def write_table(path, title, table):
    lines = [title,
             "# block  solid  special        quadrants read TL TR BL BR"]
    for block, byte in enumerate(table):
        lines.append(f"{block:02x}       {nibble(byte)}   "
                     f"{nibble(byte >> 4)}")
    path.write_text("\n".join(lines) + "\n", newline="\n")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("rom", type=Path, help="the cartridge image")
    ap.add_argument("-o", "--outdir", type=Path, default=Path("assets/levels"))
    args = ap.parse_args()

    rom = RomMap(args.rom)
    manifest = json.loads((args.outdir / "levels.json").read_text())

    count = 0
    for lvl in manifest["levels"]:
        label = lvl.get("collision")
        if not label or not rom.has(label):
            continue
        table = rom.blob(label)
        stem = f"{lvl['index']:02x}_{lvl['name']}"

        write_table(args.outdir / f"{stem}.collision.txt",
                    f"# {lvl['name']}: {label}, one line per block of "
                    f"blocks{lvl['tileset']}", table)
        count += 1

    for label, stem, what in EXTRA:
        write_table(args.outdir / f"{stem}.collision.txt",
                    f"# {label}: {what}, one line per block", rom.blob(label))
        count += 1

    print(f"{count} collision tables -> {args.outdir}")


if __name__ == "__main__":
    main()
