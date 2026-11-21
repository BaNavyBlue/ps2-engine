#!/usr/bin/env python3
"""Doors, entrances and exits, as JSON.

Two tables drive leaving one place for another (ps2.asm, loc_6E4E and
loc_6F56 at 10170 and 10289):

  * Each level's record points at an interaction list at $FFFFF736 --
    six bytes an entry, $FF ends it:

        y, x         16px cells; an entry matches a probe cell (py, px)
                     when py <= y <= py + 2 and px <= x <= px + 2
        kind         0: go to another level; else a BuildingID_
        a, b, c      kind 0: level, y, x (16px cells)
                     else:   portrait_index, $FFFFF764, $FFFFF766

  * loc_28546, three bytes a level from index 4: where walking off the
    edge of a town (or out of a dungeon with no entry to match) puts you.

Output: assets/levels/<stem>.doors.json per level, and
assets/levels/exits.json. Positions stay in the game's 16px cells.

(build 185) Both are read from the cartridge image (tools/rommap.py), not
ps2.asm: each list's bytes from its label to the next, and the levels.json
extract_levels.py wrote for the lists' labels and the levels' names.
"""

import argparse
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from rommap import RomMap  # noqa: E402

FIRST_EXIT_LEVEL = 4
EXIT_TABLE = "loc_28546"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("rom", type=Path, help="the cartridge image")
    ap.add_argument("-o", "--outdir", type=Path, default=Path("assets/levels"))
    args = ap.parse_args()

    rom = RomMap(args.rom)
    manifest = json.loads((args.outdir / "levels.json").read_text())
    names = {lv["index"]: lv["name"] for lv in manifest["levels"]}

    for lv in manifest["levels"]:
        raw = rom.blob(lv["interactions"])
        entries = []
        for at in range(0, len(raw) - 5, 6):
            rec = raw[at:at + 6]
            if rec[0] == 0xFF:
                break
            y, x, kind, a, b, c = rec
            if kind == 0:
                entries.append({"y": y, "x": x, "to_level": a,
                                "to_name": names.get(a, "?"),
                                "to_y": b, "to_x": c})
            else:
                entries.append({"y": y, "x": x, "building": kind,
                                "portrait": a, "f764": b, "f766": c})
        stem = f"{lv['index']:02x}_{lv['name']}"
        (args.outdir / f"{stem}.doors.json").write_text(json.dumps(
            {"units": "16px cells", "source": lv["interactions"],
             "entries": entries}, indent=1), newline="\n")

    # Three bytes an exit, the first a level index. The table's odd length
    # leaves `even`'s pad byte at its end, which makes no whole entry.
    raw = rom.blob(EXIT_TABLE)
    exits = []
    for i in range(len(raw) // 3):
        level, y, x = raw[i * 3:i * 3 + 3]
        exits.append({"from_level": FIRST_EXIT_LEVEL + i,
                      "from_name": names.get(FIRST_EXIT_LEVEL + i, "?"),
                      "to_level": level, "to_name": names.get(level, "?"),
                      "to_y": y, "to_x": x})
    (args.outdir / "exits.json").write_text(json.dumps(
        {"units": "16px cells", "source": EXIT_TABLE, "entries": exits},
        indent=1), newline="\n")

    print(f"{len(manifest['levels'])} door lists, {len(exits)} exits "
          f"-> {args.outdir}")


if __name__ == "__main__":
    main()
