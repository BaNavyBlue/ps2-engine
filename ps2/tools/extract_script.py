#!/usr/bin/env python3
"""Extract the game script -- every block of text the windows display.

`script_id` is a bank in the high byte and a 1-based entry in the low
byte. `LoadScript` (ps2.asm:25476) turns the bank into an index into
`GameScriptPtrs` with `lsr.w #6` and `andi.w #$3FC`, so bank $14 is entry
$14 of the table, `Script_Opening`. The entry number then walks a table
of deltas at the head of the block:

    a1 = block
    repeat `entry` times: a1 += next byte of the table

so the first delta is the size of the table itself and each one after is
the length of the script before it. Entry 1 therefore lands on the first
script rather than on the table.

(build 185) From the ROM itself (tools/rommap.py), not ps2.asm: the
pointer table and each block's deltas are read as LoadScript reads them,
the table's size being its first delta. An entry's text runs from where
its deltas land to the next label the map has, as it ran from the entry's
label to the next in ps2.asm. Each byte is decoded by the charset below;
`dc.b "..."` strings and `dc.b $XX` codes are one and the same in the
ROM, and the script's text has no `$XX` code that the charset gives a
character for, so the text comes out as the disassembly wrote it.

Control codes, from `LoadScript`:

    $BB  character name        $BE  technique name
    $BC  second character      $BF  item name
    $BD  enemy name            $C0  meseta value
    $C1  line break            $C3  pause, then a break unless $C2 follows
    $C2  continues a window    >= $C4 ends the script and opens the window

A code below $C4 that is not one of these is a literal glyph. The glyph
codes are the script text's `charset` (ps2.asm:32183): 'A'..'Z' are
11..36, 'a'..'z' 37..62, '0'..'9' 1..10, space 0, and eleven marks. A
code it gives no character to is written `{XX}`, as the disassembly
writes it in hex ($47 among them).
"""

import argparse
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from rommap import RomMap  # noqa: E402

PTR_TABLE = "GameScriptPtrs"

CONTROL = {
    0xBB: "{char}",
    0xBC: "{char2}",
    0xBD: "{enemy}",
    0xBE: "{tech}",
    0xBF: "{item}",
    0xC0: "{meseta}",
    0xC1: "\n",
    0xC2: "{cont}",
    0xC3: "{pause}",
}

FIRST_END = 0xC4

# The script text's charset, code to character: window.c's win_charset
# the other way round. (build 129a) The double quote is glyph $42, one
# cell, which LoadScript copies into the text buffer like any glyph below
# $BB; the disassembly writes it `\I` inside its strings.
GLYPH = {0x00: " ", 0x3F: ",", 0x40: ".", 0x41: ";", 0x42: '"', 0x43: "?",
         0x44: "!", 0x45: "'", 0x46: "-", 0x77: ":"}
for _i in range(10):
    GLYPH[1 + _i] = chr(ord("0") + _i)
for _i in range(26):
    GLYPH[11 + _i] = chr(ord("A") + _i)
    GLYPH[37 + _i] = chr(ord("a") + _i)


def decode(codes):
    """The script's bytes in the port's notation."""
    out = []
    for code in codes:
        if code >= FIRST_END:
            out.append(f"{{end:{code:02X}}}")
        elif code in CONTROL:
            out.append(CONTROL[code])
        elif code in GLYPH:
            out.append(GLYPH[code])
        else:
            out.append(f"{{{code:02X}}}")
    return "".join(out)


def label_of(rom, address, what):
    """The one label at an address. Two would leave which one the
    disassembly meant a guess, and none means the bytes there are not the
    start of anything ps2.asm names; either stops the run."""
    names = rom.names_at(address)
    if len(names) != 1:
        raise SystemExit(f"{what} at ${address:X}: "
                         f"{', '.join(names) or 'no label'}")
    return names[0]


def script_bytes(rom, start):
    """An entry's bytes: from its start to the next label.

    The last script of a block is followed by `even`'s pad byte, and the
    last block by the free space after it (`rept $2A8` of `dc.b 0`). Both
    are zeros past the script's last end code, which LoadScript never
    reaches, and the disassembly did not write them as text: zeros after
    the last end code are dropped. Text after an end code that is not all
    zeros is kept, as ps2.asm has it (four scripts have some).
    """
    names = rom.names_at(start)
    if not names:
        raise SystemExit(f"a script at ${start:X} has no label")
    data = rom.read(start, rom.next_addr(names[-1]) - start)
    last_end = max((i for i, c in enumerate(data) if c >= FIRST_END),
                   default=None)
    if last_end is not None and not any(data[last_end + 1:]):
        data = data[:last_end + 1]
    return data


def read_scripts(rom, block):
    """Every script in a block, in the delta table's order, as text.

    LoadScript walks the deltas, so an entry whose delta is zero is the
    same text as the entry before it; numbering by the deltas keeps it
    (numbering by label, as the first reader did, dropped it and shifted
    every later entry down by one).
    """
    count = rom.u8(block)
    deltas = rom.read(block, count)
    scripts, at = [], block
    for delta in deltas:
        at += delta
        scripts.append(decode(script_bytes(rom, at)))
    return scripts


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("rom", type=Path, help="the cartridge image")
    ap.add_argument("-o", "--outdir", type=Path, default=Path("assets/script"))
    ap.add_argument("-b", "--blocks", nargs="*", default=None,
                    help="only these block labels (default: all)")
    args = ap.parse_args()

    rom = RomMap(args.rom)
    args.outdir.mkdir(parents=True, exist_ok=True)

    table = rom.addr(PTR_TABLE)
    banks, total = {}, 0
    for index in range(len(rom.blob(PTR_TABLE)) // 4):
        block = rom.u32(table + 4 * index) & 0xFFFFFF
        name = label_of(rom, block, f"{PTR_TABLE} entry {index:#04x}")
        if args.blocks and name not in args.blocks:
            continue

        scripts = read_scripts(rom, block)
        if not scripts:
            continue

        out = args.outdir / f"{name}.txt"
        with out.open("w", newline="\n") as fh:
            fh.write(f"# {name}, bank {index:#04x};"
                     f" script_id = {index:#04x}NN, NN 1-based\n")
            for n, text in enumerate(scripts, start=1):
                fh.write(f"\n[{n}]\n{text}\n")

        banks[name] = {"bank": index, "count": len(scripts),
                       "file": out.name}
        total += len(scripts)

    (args.outdir / "script.json").write_text(json.dumps(banks, indent=2), newline="\n")
    print(f"{len(banks)} script blocks, {total} scripts")


if __name__ == "__main__":
    main()
