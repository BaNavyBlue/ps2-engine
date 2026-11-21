#!/usr/bin/env python3
"""Every interaction-list record against the cells that can reach it.

A record is only ever read by loc_6E4E (ps2.asm:10190), which matches an
entry at (y, x) against a probe cell (py, px) when py <= y <= py + 2 and
px <= x <= px + 2. Three things call it:

  loc_6DB2/loc_6DC4  a step blocked by a quadrant that is solid AND
                     special -- the leader only ($E400 < $E800), and on
                     level type 2 only walking up
  JetScooter_ChkCanMove   the same, on Motavia on the scooter, with
                     $FFFFF72E pointing at loc_29E16 instead of the
                     level's table (ObjJetSctr_Init, loc_D9AA)
  loc_6F56 -> loc_7104    a special quadrant underfoot, unless the level
                     type and block send it elsewhere first (town edges,
                     damage floors, holes, loc_70D6)

The table is $FFFFF72E (the level's, or the scooter's); the map is
$FFFFF732, plane A unless bits 4-6 of the +10 flag byte are set, and
loc_6F56 reads plane B on level type 2 whatever $F732 says. Nothing else
writes $F72E, $F732 or $F736, and the only writes into the block maps
after decompression are loc_9258's (assets/data/map_patches.json), which
this applies as if every flag were set and every item held -- so a record
reported here is unreachable in any state of the game.

    python3 tools/door_audit.py PS2_ASM [--assets assets] [-v]

-v also prints the records only the scooter or a map patch reaches.
"""

import argparse
import json
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from ps2asm import Asm  # noqa: E402

SCOOTER_TABLE = "loc_29E16"


def tmx_layers(path):
    text = path.read_text()
    out = {}
    for m in re.finditer(r'<layer [^>]*name="(\w+)" width="(\d+)" '
                         r'height="(\d+)"[^>]*>\s*<data encoding="csv">'
                         r'(.*?)</data>', text, re.S):
        vals = [int(v) - 1 for v in m.group(4).replace("\n", "").split(",")
                if v.strip()]
        out[m.group(1)] = vals
    return out


def diverted(typ, level, block):
    """loc_6F76-loc_706C: what the cell underfoot does instead."""
    if typ == 2 and block in (0x53, 0x72):
        return "town edge"
    if typ == 3 and block == 0x63:
        return "damage floor"
    if typ == 3 and 0x50 <= block <= 0x62 and level in (0x15, 0x16):
        return "Crevice hole"
    if typ == 5 and block in (0x1A, 0x46, 0x77, 0x78):
        return "damage floor"
    if typ == 5 and block == 0x3A:
        return "hole"
    if typ == 6 and block == 0x33:
        return "loc_70D6"
    if typ == 6 and block == 0x04:
        return "hole"
    return None


def apply_patches(maps, level, patches):
    for p in patches:
        if p.get("level") != level:
            continue
        m = maps["planeB" if p.get("plane") == "B" else "planeA"]
        rep = p.get("replace")
        if rep:
            for i in range(min(rep["bytes"], len(m))):
                if m[i] == rep["from"]:
                    m[i] = rep["to"]
        for at in p.get("set", {}).get("at", []):
            m[at] = p["set"]["block"]
        runs = p.get("runs")
        for row, col, n in (runs["entries"] if runs else []):
            at = row * runs["stride"] + col
            if n & 0x80:
                m[at] = n
                continue
            for _ in range(n & 0x3F):
                m[at] = runs["block"]
                at += runs["stride"] if n & 0x40 else 1


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("asm", type=Path)
    ap.add_argument("--assets", type=Path, default=Path("assets"))
    ap.add_argument("-v", "--verbose", action="store_true")
    args = ap.parse_args()

    asm = Asm(args.asm)
    lvl_dir = args.assets / "levels"
    manifest = json.loads((lvl_dir / "levels.json").read_text())
    patch_file = args.assets / "data" / "map_patches.json"
    patches = (json.loads(patch_file.read_text())["patches"]
               if patch_file.exists() else [])
    by_list = {}
    for lv in manifest["levels"]:
        by_list.setdefault(lv["interactions"], []).append(lv["index"])

    total, dead, special_only = 0, [], []
    live_at = set()
    rows_out = []
    for lv in manifest["levels"]:
        idx, typ = lv["index"], lv["tileset"]
        cols, rows = lv["cols"], lv["rows"]
        maps = {}
        for f in [lv["files"]["map"]] + list(lv["files"]["backdrop"].values()):
            if f:
                maps.update(tmx_layers(lvl_dir / f))
        plain = {k: list(v) for k, v in maps.items()}
        apply_patches(maps, idx, patches)
        coll = "planeB" if lv["collision_flags"] & 0x70 else "planeA"
        tables = [("walk", asm.blob(lv["collision"]))]
        if idx == 0:
            tables.append(("scooter", asm.blob(SCOOTER_TABLE)))

        def cell(tbl, plane, cy, cx):
            by, bx = cy // 2, cx // 2
            if not (0 <= by < rows and 0 <= bx < cols):
                return None
            block = plane[by * cols + bx]
            q = (cy & 1) * 2 + (cx & 1)
            b = tbl[block] if block < len(tbl) else 0
            return block, bool(b & (1 << q)), bool(b & (0x10 << q))

        doors = json.loads((lvl_dir / f"{idx:02x}_{lv['name']}.doors.json")
                           .read_text())["entries"]
        for d in doors:
            total += 1
            how = set()
            for tname, tbl in tables:
                for mname, m in (("", maps), ("unpatched", plain)):
                    for cy in range(d["y"] - 2, d["y"] + 1):
                        for cx in range(d["x"] - 2, d["x"] + 1):
                            c = cell(tbl, m[coll], cy, cx)
                            if c and c[1] and c[2]:
                                how.add((tname, mname))
                            under = m["planeB"] if typ == 2 else m[coll]
                            c = cell(tbl, under, cy, cx)
                            if (c and c[2] and not c[1]
                                    and not diverted(typ, idx, c[0])):
                                how.add((tname, mname))
            name = f"{idx:02x} {lv['name']}"
            target = (f"{d['to_name']} (${d['to_level']:02X}) "
                      f"{d['to_y']},{d['to_x']}" if "to_level" in d else
                      f"building {d['building']} portrait {d['portrait']}")
            if not how:
                dead.append((idx, lv["interactions"], d, name, target))
                continue
            live_at.add((lv["interactions"], d["y"], d["x"]))
            if ("walk", "unpatched") not in how:
                ways = sorted({"scooter" if t == "scooter" else "map patch"
                               for t, _ in how})
                special_only.append(f"{name:28s} {d['y']:3d},{d['x']:3d}"
                                    f" -> {target}   ({', '.join(ways)})")

    for idx, lst, d, name, target in dead:
        shared = [i for i in by_list[lst] if i != idx]
        note = ""
        if (lst, d["y"], d["x"]) in live_at:
            note = (f"   (list {lst} is shared with "
                    f"{', '.join(f'${i:02X}' for i in shared)}; live there)")
        rows_out.append(f"{name:28s} {d['y']:3d},{d['x']:3d} -> "
                        f"{target}{note}")

    print(f"{total} records, {len(dead)} that nothing can reach")
    for r in rows_out:
        print("  " + r)
    if args.verbose:
        print(f"\n{len(special_only)} reached only on the scooter or "
              f"through a map patch")
        for r in special_only:
            print("  " + r)


if __name__ == "__main__":
    main()
