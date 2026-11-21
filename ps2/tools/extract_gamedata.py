#!/usr/bin/env python3
"""Item and character tables out of the cartridge image, as JSON the game
loads.

    InventoryData        16 bytes an item: 10 name codes, price word,
                         action flags, who-can-equip mask, attack, defense
    CharNames            8 names, 4 codes each, $C4 ends a short one
    CharInitialSetup     16 bytes a character: five equipment slots, two
                         filler bytes, item count, eight inventory slots
    CharExperiencePtrs   per character, rows of a long (level << 24 | exp)
                         and ten bytes: HP TP STR MNT AGL LCK DEX ATK DEF
                         and the technique counts as two nibbles. The
                         first row is absolute; later rows are gains.
    AgilityIncreasesArray  footwear bonus, Shoes..GardaBoots

Items are keyed by their ItemID_ constant ("Knife", "CarbonSuit") so save
files and these tables can name an item rather than number it.

(build 185) From the ROM itself (tools/rommap.py), not ps2.asm: each table
runs from its label, an item's key is the ItemID_ constant whose value is
its place in InventoryData, and a name is decoded from the game's text
codes. The helpers the other game-data extractors share are here too.
"""

import argparse
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from rommap import RomMap  # noqa: E402

# The game's text codes for names (the `charset` lines ps2.asm puts before
# InventoryData, CharNames and EnemyNames): digits from 1, capitals from
# 11, small letters from 37, and a few marks.
CODE_TO_CHAR = {0: " ", 0x3F: ",", 0x40: ".", 0x41: ";", 0x42: '"',
                0x43: "?", 0x44: "!", 0x45: "'", 0x46: "-", 0x77: ":"}
for _i in range(10):
    CODE_TO_CHAR[1 + _i] = str(_i)
for _i in range(26):
    CODE_TO_CHAR[11 + _i] = chr(ord("A") + _i)
    CODE_TO_CHAR[37 + _i] = chr(ord("a") + _i)

NAME_END = 0xC4         # nametxt's end mark for a name shorter than its field

ITEM_SIZE = 16
SETUP_SIZE = 16
EXP_ROW = 14


def decode_name(codes):
    """The codes to $C4 or the field's end. (Build 131a) A trailing space
    is kept: the ROM prints it -- Script_ProcessItemEnemyNames copies up
    to ten codes and stops only at $C4 -- and one name has one, AC SLASHR
    (`nametxt "AC SLASHR "`)."""
    text = ""
    for c in codes:
        if c == NAME_END:
            break
        text += CODE_TO_CHAR.get(c, "?")
    return text


def constant_names(rom, prefix, skip=()):
    """{value: name less the prefix} of a family of constants (ItemID_,
    TechID_, ...): what the disassembly calls a byte of that kind. Two
    names for one value would make the byte's name a guess, so that
    stops the run."""
    out = {}
    for name in sorted(rom.consts):
        if not name.startswith(prefix) or name.endswith(tuple(skip)):
            continue
        value = rom.const(name)
        if value in out:
            sys.exit(f"{prefix}{out[value]} and {name} are both {value}")
        out[value] = name[len(prefix):]
    return out


def id_keys(rom, prefix):
    """A family numbered 0, 1, 2 ... (ItemID_ is id() of each record) as a
    list in value order."""
    names = constant_names(rom, prefix)
    if sorted(names) != list(range(len(names))):
        sys.exit(f"the {prefix} constants are not 0..{len(names) - 1}")
    return [names[i] for i in range(len(names))]


def labels_after(rom, first):
    """(name, address) of each label after `first`, in ps2.asm's line
    order. Every name looked at is one the run used (rommap's record of
    names, which a trimmed map keeps), the one that ends a walk too."""
    i = rom.index(first) + 1
    while i < len(rom.labels):
        name, address = rom.labels[i]
        rom.index(name)
        yield name, address
        i += 1


def run_of(rom, first, part_of):
    """The labels after `first` while `part_of(name)` holds, and where the
    first label that is not one starts: a table and its entries."""
    entries = []
    for name, address in labels_after(rom, first):
        if not part_of(name):
            return entries, address
        entries.append((name, address))
    return entries, len(rom.rom)


def block_at(rom, address):
    """The bytes from an address labels start at to the next label: a
    table a pointer names, as far as its label's block goes."""
    names = rom.names_at(address)
    if not names:
        sys.exit(f"no label at ${address:X}")
    return rom.read(address, rom.next_addr(names[-1]) - address)


def label_at(rom, address, prefix=""):
    """The one label at an address that starts with `prefix`; None if
    there is none, and a stop if there are several (which one the
    disassembly wrote would be a guess)."""
    names = [n for n in rom.names_at(address) if n.startswith(prefix)]
    if len(names) > 1:
        sys.exit(f"${address:X} is {', '.join(names)}: which is meant?")
    return names[0] if names else None


def file_at(rom, address):
    """The binclude'd file (rom_map.json's "files", "art/foo.bin") that
    starts at an address, or None: what the disassembly includes there
    rather than writing it out as dc lines."""
    found = [n for n, (a, _) in rom.files.items() if a == address]
    if len(found) > 1:
        sys.exit(f"${address:X} is {', '.join(found)}: which is meant?")
    if not found:
        return None
    rom.file(found[0])          # the run used it (rommap's record of names)
    return found[0]


def file_stem(name):
    """"art/foo.bin" -> "foo"."""
    return name.replace("\\", "/").split("/")[-1].rsplit(".", 1)[0]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("rom", type=Path, help="the cartridge image")
    ap.add_argument("-o", "--out", type=Path, default=Path("assets/data"))
    args = ap.parse_args()

    rom = RomMap(args.rom)

    # --- items: one record per ItemID_, in id order ---
    item_keys = id_keys(rom, "ItemID_")
    start = rom.addr("InventoryData")
    size = rom.addr("TechniqueData") - start
    if size != ITEM_SIZE * len(item_keys):
        sys.exit(f"InventoryData is {size} bytes, "
                 f"expected {ITEM_SIZE * len(item_keys)}")
    raw = rom.read(start, size)

    items = []
    for i, key in enumerate(item_keys):
        rec = raw[i * ITEM_SIZE:(i + 1) * ITEM_SIZE]
        items.append({
            "id": i,
            "key": key,
            "name": decode_name(rec[:10]),
            "price": rec[10] << 8 | rec[11],
            "action_flags": rec[12],
            "equip_mask": rec[13],
            "attack": rec[14],
            "defense": rec[15],
        })

    # --- characters ---
    names_raw = rom.read(rom.addr("CharNames"),
                         rom.addr("CharNamesEnd") - rom.addr("CharNames"))
    setup_raw = rom.read(rom.addr("CharInitialSetup"),
                         rom.addr("CharInitialSetupEnd")
                         - rom.addr("CharInitialSetup"))
    ptrs = rom.addr("CharExperiencePtrs")
    tables = [rom.u32(a) & 0xFFFFFF
              for a in range(ptrs, rom.addr("CharExperiencePtrsEnd"), 4)]

    slots = ["head", "right_hand", "left_hand", "body", "legs"]
    stat_keys = ["hp", "tp", "strength", "mental", "agility", "luck",
                 "dexterity", "attack", "defense"]

    chars = []
    for i, table in enumerate(tables):
        setup = setup_raw[i * SETUP_SIZE:(i + 1) * SETUP_SIZE]
        count = setup[7]
        # A table runs to the next label; whatever lies past its last
        # whole row (an `even` byte) is not a row.
        rows = block_at(rom, table)
        levels = []
        for r in range(0, len(rows) - (EXP_ROW - 1), EXP_ROW):
            row = rows[r:r + EXP_ROW]
            entry = {"level": row[0],
                     "exp": row[1] << 16 | row[2] << 8 | row[3]}
            entry.update(dict(zip(stat_keys, row[4:13])))
            entry["techs_a"] = row[13] >> 4
            entry["techs_b"] = row[13] & 0xF
            levels.append(entry)
        chars.append({
            "name": decode_name(names_raw[i * 4:(i + 1) * 4]),
            "equipment": {s: item_keys[setup[j] & 0x7F]
                          for j, s in enumerate(slots)},
            "items": [{"item": item_keys[b & 0x7F], "equipped": bool(b & 0x80)}
                      for b in setup[8:8 + count]],
            "levels": levels,
        })

    # One bonus per footwear item, Shoes to GardaBoots in id order: the
    # table's length is the run of ids, not its block (code follows it).
    first = item_keys.index("Shoes")
    last = item_keys.index("GardaBoots")
    agility = rom.run("AgilityIncreasesArray", last - first + 1)
    agility = {item_keys[first + i]: v for i, v in enumerate(agility)}

    args.out.mkdir(parents=True, exist_ok=True)
    (args.out / "items.json").write_text(json.dumps(items, indent=1), newline="\n")
    (args.out / "characters.json").write_text(json.dumps(chars, indent=1), newline="\n")
    (args.out / "agility_bonus.json").write_text(json.dumps(agility, indent=1), newline="\n")
    print(f"{len(items)} items, {len(chars)} characters -> {args.out}")


if __name__ == "__main__":
    main()
