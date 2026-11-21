#!/usr/bin/env python3
"""Extract every entry of PalettePtrs by index (ps2.asm:26819).

`extract_palettes.py` pulls the palettes that have names. Fifteen of them
do not: entries 2 through 16 are the level palettes, stored as bare
`loc_*` labels, and `LoadLevelData` reaches them by index rather than by
name (`move.w d1, d0; bsr PaletteLoad1`, d1 being the top byte of the
level's +14 pointer).

An entry is 8 bytes:

    +0  u32  pointer to the CRAM words
    +4  u16  destination in the palette buffer; $FB00 is line 0
    +6  u16  longs to copy, minus one -- so (count + 1) * 2 words

`PaletteLoad1` adds $80 to the destination, which selects the fade target
buffer rather than the live one; `PaletteLoad2` does not. That is a
destination choice, not a difference in the data, so both land here the
same way.

A level loads two of these: index 2 always, which is lines 0 and 1 and is
shared by every level, and its own index, which is lines 2 and 3.

(build 185) From the cartridge image (tools/rommap.py), not ps2.asm: the
entries are the table's own bytes, and each pointer is named by the label
at the address it holds.
"""

import argparse
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
import asset_names  # noqa: E402
import ps2png  # noqa: E402
from rommap import RomMap  # noqa: E402

PAL_BASE = 0xFB00          # palette_table, line 0 entry 0
ENTRY_BYTES = 8


def closes_block(rom, name):
    """Whether a label is `<label>End`, the end of another label's block."""
    if not name.endswith("End") or not rom.has(name[:-3]):
        return False
    rom.addr(name[:-3])     # looked up, so a map cut to what is used keeps it
    return True


def label_at(rom, address):
    """The label the data at an address is written under, or None.

    Where several labels share an address, an `<label>End` closes the block
    before (Pal_SegaEnd, at Pal_TitleScr), so a pointer there means the
    other one. More than one left would be a guess, and stops.
    """
    names = rom.names_at(address)
    if len(names) <= 1:
        return names[0] if names else None
    starts = [n for n in names if not closes_block(rom, n)]
    if len(starts) != 1:
        raise SystemExit(f"${address:X} is {', '.join(names)}: "
                         "which is meant?")
    return starts[0]


def parse_table(rom, label):
    """The entries of a PalettePtrs-style table, from its bytes.

    The table has no terminator. It ends at the first entry whose pointer
    is no label's address, which is unrelated data laid out next (here
    Pal_Sega's first long, $00000EEE). A pointer with flag bits in its top
    byte names no label either, as the table never has one.
    """
    at = rom.addr(label)
    entries = []
    while at + ENTRY_BYTES <= len(rom.rom):
        symbol = label_at(rom, rom.u32(at))
        if symbol is None:
            break
        dest = rom.u16(at + 4)
        entries.append({
            "index": len(entries),
            "symbol": symbol,
            "dest": dest,
            "words": (rom.u16(at + 6) + 1) * 2,
            "line": (dest - PAL_BASE) // 32,
            "entry": ((dest - PAL_BASE) % 32) // 2,
        })
        at += ENTRY_BYTES
    return entries


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("rom", type=Path, help="the cartridge image")
    ap.add_argument("-o", "--outdir", type=Path,
                    default=Path("assets/palettes"))
    args = ap.parse_args()

    rom = RomMap(args.rom)
    args.outdir.mkdir(parents=True, exist_ok=True)

    entries = parse_table(rom, "PalettePtrs")
    out = []
    for e in entries:
        # The words an entry copies should all be its label's own: running
        # into the next label would mean the count or the pointer is wrong.
        start = rom.addr(e["symbol"])
        avail = (rom.next_addr(e["symbol"]) - start) // 2
        want = e["words"]
        if avail < want:
            print(f"  entry {e['index']} ({e['symbol']}): short, "
                  f"{avail}/{want} words", file=sys.stderr)
            continue
        words = [rom.u16(start + i * 2) for i in range(want)]
        # (build 97b) By what the entry is for (tools/asset_names.py);
        # before, ptr<index>.png.
        name = asset_names.PALETTE_NAMES[e["index"]]
        ps2png.write_palette(args.outdir / f"{name}.png", words)
        e["name"] = name
        e["file"] = f"{name}.png"
        out.append(e)

    (args.outdir / "paletteptrs.json").write_text(json.dumps(out, indent=2), newline="\n")
    print(f"{len(out)} PalettePtrs entries")
    unnamed = [e for e in out if e["symbol"].startswith("loc_")]
    print(f"  {len(unnamed)} with no name, indices "
          f"{unnamed[0]['index']}..{unnamed[-1]['index']}"
          if unnamed else "  all named")


if __name__ == "__main__":
    main()
