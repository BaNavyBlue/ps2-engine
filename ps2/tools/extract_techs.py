#!/usr/bin/env python3
"""Techniques: TechniqueData and CharAvailableTechniques, as JSON.

TechniqueData is 8 bytes a technique: a 5-code name ($C4-terminated when
shorter), TP cost, an action bitfield, and a damage or healing rate. Ids
are the place in the table, which the TechID_ constants number.

CharAvailableTechniques is 32 bytes a character: 16 techniques usable on
the map, then 16 usable in battle, in the order they are learnt.
loc_112C4 copies the first N of each, N being the low five bits of the
character's +$26 (map) and +$25 (battle) bytes, zero-fills the rest, and
when bit 7 of that byte is set puts Musik ($33) in the last place taken.

(build 185) From the ROM itself (tools/rommap.py), not ps2.asm: each table
is the run of its entries' labels (Tech_*, <Who>_TechLevel/_TechBattle)
after its own, and a character's block is named by its _TechLevel label.
"""

import argparse
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from rommap import RomMap                                            # noqa: E402
from extract_gamedata import constant_names, decode_name, run_of    # noqa: E402

TECH_SIZE = 8
AVAIL_SIZE = 32         # 16 map techniques, 16 battle techniques


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("rom", type=Path, help="the cartridge image")
    ap.add_argument("-o", "--out", type=Path, default=Path("assets/data"))
    args = ap.parse_args()

    rom = RomMap(args.rom)

    # A technique's id is its place in the table (id() in the constants),
    # so name each record by its own label: the constants name only 54 of
    # the 64 entries.
    start = rom.addr("TechniqueData")
    entries, end = run_of(rom, "TechniqueData", lambda n: n.startswith("Tech_"))
    labels = [n[len("Tech_"):] for n, _ in entries]
    if end - start != TECH_SIZE * len(labels) or any(
            a != start + TECH_SIZE * i for i, (_, a) in enumerate(entries)):
        sys.exit(f"TechniqueData is {end - start} bytes for "
                 f"{len(labels)} entries")
    for i, k in sorted(constant_names(rom, "TechID_").items()):
        if i >= len(labels) or labels[i] != k:
            sys.exit(f"TechID_{k} is not entry {i}")
    raw = rom.read(start, end - start)

    techs = []
    for i, key in enumerate(labels):
        rec = raw[i * TECH_SIZE:(i + 1) * TECH_SIZE]
        techs.append({"id": i, "key": key, "name": decode_name(rec[:5]),
                      "tp": rec[5], "flags": rec[6], "rate": rec[7]})

    start = rom.addr("CharAvailableTechniques")
    _, end = run_of(rom, "CharAvailableTechniques", lambda n: "_Tech" in n)
    count = (end - start) // AVAIL_SIZE
    if (end - start) % AVAIL_SIZE > 1:      # at most an `even` byte over
        sys.exit(f"CharAvailableTechniques is {end - start} bytes")

    available = {}
    for c in range(count):
        at = start + c * AVAIL_SIZE
        who = [n for n in rom.names_at(at) if n.endswith("_TechLevel")]
        if len(who) != 1:
            sys.exit(f"no one <Who>_TechLevel label at ${at:X}")
        block = rom.read(at, AVAIL_SIZE)
        available[who[0][:-len("_TechLevel")]] = {
            "map": [labels[b] for b in block[:16]],
            "battle": [labels[b] for b in block[16:]]}

    args.out.mkdir(parents=True, exist_ok=True)
    (args.out / "techniques.json").write_text(json.dumps(
        {"source": "TechniqueData, CharAvailableTechniques",
         "techniques": techs, "available": available}, indent=1), newline="\n")
    print(f"{len(techs)} techniques, {len(available)} characters")


if __name__ == "__main__":
    main()
