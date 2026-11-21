#!/usr/bin/env python3
"""Townspeople: who stands where, what they look like, what they say.

  LevelSpriteData (ps2.asm:945)   per level, six bytes an object:
        byte 0   (object id - $14) << 2 | interaction type
        byte 1   interaction index (for type 1, people: a dialogue number)
        x, y     words, the object's world position
     LoadSpritesInLevel (799) turns each into an object at $FFFFE800.

  loc_283EE (LoadLevelData, 13305)  the level record's extra-art byte picks
     a list of (art, VRAM address) pairs to decompress: the people's tiles.

  Obj_Mota* (5762...)  each type's init sets its tile word, its mappings
     and its first wait, and shares one of two wandering routines.

  loc_E6B0 / loc_E70C / loc_E730 (ObjType_People, 20500)  dialogue number
     to a script in bank $16.

  Obj_DezoTreasureChest / Obj_MotaTreasureChest (7856, 7880)  the two chest
     objects: tile word, mappings (the Motavian one has a second set for
     level type 1), and the hit-box row of loc_7278 they put in $2C.

  TreasureChestContentArray (21480)  what chest n holds, n from 1: an
     item ($8000 | id), meseta, or 0 for garbage.

  Obj_Darum, Obj_Teim and object $2B (4E08, 4F0C, 504C)  the Teim and
     Darum scene's three objects: tile word, mappings, hit-box row, and
     whether they ask to be drawn in front (bit 5 of 2(a0)).

  Obj_DeadBody (ps2.asm:7583)  object $2F, the scoundrels on Shure's top
     floor, read the same way: tile word, mappings and hit-box row. It
     goes in with the scene objects since its routine is an init only.

  Obj_OpenableDoor / Obj_DestroyableDoor (ps2.asm:7915, 7961)  objects
     $3B and $3C, the locked doors and the dynamite doors: tile word,
     mappings (four frames: shut, ajar, open, blown) and hit-box row 6.
     Their routines are npc.c's; they ride with the scene objects.

  Obj_JetScooter (ps2.asm:6767)  object $22, placed first on Motavia and
     re-placed in slot 0 by LoadSpritesInLevel's tail: tile word $400 (the
     first entry of Motavia's extra-art list 10, loc_5D98E), Map_JetScooter's
     twelve frames, hit-box row 5 while parked. Its routine is the port's
     (walk.c and npc.c), so it rides with the scene objects too.

(build 185) From the ROM itself (tools/rommap.py), not ps2.asm. The
tables are read as the game reads them: an offset table runs to the next
label, an art list is a count word (less one) and six bytes an entry, a
mapping table is word offsets to the next label and a frame is a piece
count and five bytes a piece. An object's routine is the `bra.w` of
ObjectTable its id picks; its init is the first `bra.w` of the table its
`jsr table(pc,d0.w)` jumps through, and the init's fields are the
`move #value, n(a0)` instructions it runs before the next label or `bra`
or `rts`, decoded from the 68000 code (decode() below). An id is the
ObjID_ constant's value, an item the ItemID_ constant's name with that
value, and a label the map's name for an address.

Output: assets/data/level_sprites.json, npc_types.json, people_dialogue.json,
chests.json, scene_objects.json and assets/tiles/sprite_<name>.png (names:
tools/asset_names.py, build 97b; before, people_<label>.png).
"""

import argparse
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
import asset_names  # noqa: E402
import ps2png  # noqa: E402
from ps2art import decompress  # noqa: E402
from rommap import RomMap  # noqa: E402

FIRST_LEVEL_OBJECT = 0x14
WANDER = {"loc_3C06": "vertical", "loc_3DCA": "horizontal"}

# The object fields the inits set (offsets into an object's RAM).
FIELD_MAPPINGS = 4
FIELD_TILE = 8
FIELD_WAIT = 0x28
FIELD_BOX = 0x2C
FIELD_FLAGS = 2
LEVEL_TYPE = 0xF710        # ($FFFFF710).w, the level type a chest tests


# ---------------------------------------------------------------------------
# The 68000 code, as far as reading an init needs (and the search for the
# routines that name a flag byte, extract_flag_names.py): each
# instruction's length, so a walk stays on instruction boundaries, and
# its operands.

class Insn:
    """One instruction: `op` its first word, `size` 1/2/4 (0 if none),
    `ops` its operands as (mode, reg, value) -- see _ea -- and `target`
    a branch's destination."""

    def __init__(self, at, op):
        self.at, self.op = at, op
        self.size, self.ops, self.target, self.length = 0, [], None, 2

    @property
    def is_bra(self):
        return self.op >> 8 == 0x60

    @property
    def is_rts(self):
        return self.op == 0x4E75


_SIZES = {0: 1, 1: 2, 2: 4}


def _ea(rom, at, mode, reg, size):
    """An effective address whose extension words start at `at`:
    ((mode, reg, value), bytes of extension). Modes 0-6 are the register
    modes; mode 7 is ("abs", address), ("pc", address) for d16(pc),
    ("pcx", base) for d8(pc,Xn), ("imm", value)."""
    if mode <= 4:
        return (mode, reg, None), 0
    if mode == 5:
        return (5, reg, rom.s16(at)), 2
    if mode == 6:
        return (6, reg, rom.u16(at)), 2
    if reg == 0:
        return ("abs", 0, rom.s16(at) & 0xFFFFFF), 2
    if reg == 1:
        return ("abs", 0, rom.u32(at) & 0xFFFFFF), 4
    if reg == 2:
        return ("pc", 0, at + rom.s16(at)), 2
    if reg == 3:
        return ("pcx", 0, at + rom.s8(at + 1)), 2
    if reg == 4:
        if size == 4:
            return ("imm", 0, rom.u32(at)), 4
        value = rom.u16(at)
        return ("imm", 0, value & 0xFF if size == 1 else value), 2
    raise ValueError("mode 7 reg %d" % reg)


def decode(rom, at):
    """The 68000 instruction at `at` (every one the game uses; an opcode
    that is none raises ValueError)."""
    op = rom.u16(at)
    insn = Insn(at, op)
    top, mode, reg = op >> 12, (op >> 3) & 7, op & 7
    p = at + 2

    def ea(size, m=mode, r=reg):
        nonlocal p
        operand, n = _ea(rom, p, m, r, size)
        p += n
        insn.ops.append(operand)

    def imm(size):
        ea(size, 7, 4)

    ss = (op >> 6) & 3
    if top == 0:
        if op & 0x100:
            if mode == 1:                          # movep
                p += 2
            else:                                  # btst/bchg/bclr/bset Dn
                insn.ops.append((0, (op >> 9) & 7, None))
                ea(1)
        elif (op >> 8) & 0xF == 8:                 # bit op, #n
            imm(1)
            ea(1)
        elif ss == 3 or (op >> 9) & 7 == 7:        # no such size; moves
            raise ValueError("opcode %04X at %X" % (op, at))
        else:                                      # ori andi subi addi eori cmpi
            insn.size = _SIZES[ss]
            imm(insn.size)
            if (op & 0x3F) != 0x3C:                # not to CCR/SR
                ea(insn.size)
    elif top in (1, 2, 3):                         # move, movea
        insn.size = {1: 1, 3: 2, 2: 4}[top]
        ea(insn.size)
        ea(insn.size, (op >> 6) & 7, (op >> 9) & 7)
    elif top == 4:
        if op in (0x4E70, 0x4E71, 0x4E73, 0x4E75, 0x4E76, 0x4E77, 0x4AFC):
            pass                                   # reset nop rte rts ...
        elif op == 0x4E72:                         # stop #n
            p += 2
        elif op & 0xFFF0 in (0x4E40, 0x4E60) or op & 0xFFF8 == 0x4E58:
            pass                                   # trap, move usp, unlk
        elif op & 0xFFF8 == 0x4E50:                # link
            p += 2
        elif op & 0xFF80 == 0x4E80:                # jsr, jmp
            ea(0)
        elif op & 0xF1C0 in (0x41C0, 0x4180):      # lea, chk
            ea(4 if op & 0x40 else 2)
        elif op & 0xFB80 == 0x4880 and mode != 0:  # movem
            p += 2
            ea(4 if op & 0x40 else 2)
        elif op & 0xFFC0 in (0x4840, 0x4880, 0x48C0) and mode == 0:
            pass                                   # swap, ext
        elif op & 0xFFC0 in (0x4800, 0x4840, 0x4AC0):
            ea(1)                                  # nbcd, pea, tas
        elif op & 0xF9C0 == 0x40C0:                # move sr/ccr
            ea(2)
        elif op & 0xF900 == 0x4000 or op & 0xFF00 == 0x4A00:
            if ss == 3:                            # negx clr neg not tst
                raise ValueError("opcode %04X at %X" % (op, at))
            insn.size = _SIZES[ss]
            ea(insn.size)
        else:
            raise ValueError("opcode %04X at %X" % (op, at))
    elif top == 5:
        if ss == 3:
            if mode == 1:                          # dbcc
                insn.target = p + rom.s16(p)
                p += 2
            else:                                  # scc
                ea(1)
        else:                                      # addq, subq
            insn.size = _SIZES[ss]
            ea(insn.size)
    elif top == 6:                                 # bra bsr bcc
        if op & 0xFF:
            insn.target = p + rom.s8(at + 1)
        else:
            insn.target = p + rom.s16(p)
            p += 2
    elif top == 7:                                 # moveq
        pass
    elif top in (8, 0xC):                          # or divu divs, and mulu muls
        opmode = (op >> 6) & 7
        if opmode in (3, 7):
            ea(2)
        elif opmode >= 4 and mode in (0, 1):
            pass                                   # sbcd abcd exg
        else:
            insn.size = _SIZES[opmode & 3]
            ea(insn.size)
    elif top in (9, 0xB, 0xD):                     # sub add cmp eor (a/x/m)
        opmode = (op >> 6) & 7
        if opmode in (3, 7):
            ea(2 if opmode == 3 else 4)
        elif opmode >= 4 and mode in (0, 1) and top != 0xB:
            pass                                   # subx, addx
        elif opmode >= 4 and mode == 1:
            pass                                   # cmpm
        else:
            insn.size = _SIZES[opmode & 3]
            ea(insn.size)
    elif top == 0xE:
        if ss == 3:                                # shift memory
            ea(2)
    else:
        raise ValueError("opcode %04X at %X" % (op, at))
    insn.length = p - at
    return insn


def bra_target(rom, at):
    """Where the `bra.w` at `at` goes; a stop if it is not one."""
    insn = decode(rom, at)
    if insn.op != 0x6000:
        raise SystemExit(f"${at:X} is not a bra.w")
    return insn.target


def jump_table(rom, routine):
    """The table an object routine's `jsr table(pc,d0.w)` jumps through:
    the routine's head picks an entry by the object's routine number;
    None if the head reaches its `rts` or a branch without one."""
    at = routine
    for _ in range(8):
        insn = decode(rom, at)
        if insn.op & 0xFFC0 == 0x4E80 and insn.ops[0][0] == "pcx":
            return insn.ops[0][2]
        if insn.is_rts or insn.is_bra or insn.op & 0xFFC0 == 0x4EC0:
            return None
        at += insn.length
    return None


def init_of(rom, routine):
    """An object routine's init: its jump table's first `bra.w`."""
    table = jump_table(rom, routine)
    if table is None:
        raise SystemExit(f"${routine:X} has no jump table")
    return bra_target(rom, table)


def init_code(rom, init):
    """The init's instructions up to the next label or `bra` or `rts`,
    as far as the disassembly's init runs before the code it shares."""
    out, at = [], init
    while True:
        if at != init and rom.names_at(at):
            break
        insn = decode(rom, at)
        if insn.is_bra or insn.is_rts:
            break
        out.append(insn)
        at += insn.length
    return out


def stores(code, field):
    """The immediates the code puts in n(a0), n = `field`, in order:
    `move #value, n(a0)` (an immediate source, a d16(a0) destination)."""
    return [(i.size, i.ops[0][2]) for i in code
            if 1 <= i.op >> 12 <= 3 and len(i.ops) == 2
            and i.ops[0][0] == "imm" and i.ops[1][:2] == (5, 0)
            and i.ops[1][2] == field]


def first_store(code, field, what):
    found = stores(code, field)
    if not found:
        raise SystemExit(f"the init at ${code[0].at:X} sets no {what}")
    return found[0][1]


def label_of(rom, address, prefix=""):
    """The one label at an address (that starts with `prefix`): what the
    disassembly calls it. None, or two, stops the run, since which one it
    wrote would be a guess."""
    names = [n for n in rom.names_at(address) if n.startswith(prefix)]
    if len(names) != 1:
        raise SystemExit(f"${address:X}: {', '.join(names) or 'no label'}"
                         f"{' starting ' + prefix if prefix else ''}")
    return names[0]


def mapping_stores(rom, code):
    """The labels of the mappings the code puts in 4(a0), in order."""
    return [label_of(rom, value) for size, value in
            stores(code, FIELD_MAPPINGS) if size == 4]


# ---------------------------------------------------------------------------
# The tables

def offset_table(rom, label):
    """A table of word offsets from its own label, to the next label:
    the addresses its entries point to."""
    base = rom.addr(label)
    data = rom.blob(label)
    return [base + int.from_bytes(data[i:i + 2], "big")
            for i in range(0, len(data) - 1, 2)]


def constants(rom, prefix):
    """{name less the prefix: value} of a family of constants."""
    return {name[len(prefix):]: rom.const(name)
            for name in sorted(rom.consts) if name.startswith(prefix)}


def object_routine(rom, obj_id):
    """The routine ObjectTable's `bra.w` for object id `obj_id` jumps to
    (ids from 1, one `bra.w` an id)."""
    return bra_target(rom, rom.addr("ObjectTable") + 4 * (obj_id - 1))


def parse_mappings(rom, label):
    """A sprite mapping table: word offsets, then for each frame a piece
    count and five bytes a piece -- y, size, tile word, x."""
    frames = []
    for at in offset_table(rom, label):
        pieces = []
        for i in range(rom.u8(at)):
            y, size, hi, lo, x = rom.read(at + 1 + i * 5, 5)
            word = hi << 8 | lo
            pieces.append({
                "x": x - 256 if x > 127 else x,
                "y": y - 256 if y > 127 else y,
                "w": ((size >> 2) & 3) + 1,
                "h": (size & 3) + 1,
                "tile": word & 0x7FF,
                "flip_h": bool(word & 0x0800),
                "flip_v": bool(word & 0x1000),
            })
        frames.append(pieces)
    return frames


def parse_chest(rom, name, value):
    """A chest object's init: tile word, hit-box row and mappings. A
    second mappings pointer under a `cmpi.w #n, ($FFFFF710).w` is the
    one used on level type n."""
    code = init_code(rom, init_of(rom, object_routine(rom, value)))
    maps = mapping_stores(rom, code)
    tile_word = first_store(code, FIELD_TILE, "tile word")
    chest = {
        "object": value,
        "name": name,
        "tile": tile_word & 0x7FF,
        "palette_line": (tile_word >> 13) & 3,
        "box": first_store(code, FIELD_BOX, "hit-box row"),
        "frames": parse_mappings(rom, maps[0]),
    }
    alt = [i.ops[0][2] for i in code
           if i.op == 0x0C78 and i.ops[1] == ("abs", 0, 0xFF0000 | LEVEL_TYPE)]
    if alt and len(maps) > 1:
        chest["alt_level_type"] = alt[0]
        chest["alt_frames"] = parse_mappings(rom, maps[1])
    return chest


def parse_scene(rom, obj_id, name):
    """A scene object's init, found the way parse_chest finds a chest's.
    The hit-box row is optional ($2C left 0 means nothing can bump into
    it)."""
    code = init_code(rom, init_of(rom, object_routine(rom, obj_id)))
    tile_word = first_store(code, FIELD_TILE, "tile word")
    box = stores(code, FIELD_BOX)
    flags = [v for size, v in stores(code, FIELD_FLAGS) if size == 1]
    return {
        "object": obj_id,
        "name": name,
        "tile": tile_word & 0x7FF,
        "palette_line": (tile_word >> 13) & 3,
        "box": box[0][1] if box else 0,
        "front": bool(flags and flags[0] & 0x20),
        "frames": parse_mappings(rom, mapping_stores(rom, code)[0]),
    }


def person(rom, routine, obj_id, name, wander):
    """A townsperson type: its init's tile word, mappings and first
    wait."""
    code = init_code(rom, init_of(rom, routine))
    tile_word = first_store(code, FIELD_TILE, "tile word")
    return {
        "object": obj_id,
        "name": name,
        "tile": tile_word & 0x7FF,
        "palette_line": (tile_word >> 13) & 3,
        "wander": wander,
        "first_wait": first_store(code, FIELD_WAIT, "first wait"),
        "frames": parse_mappings(rom, mapping_stores(rom, code)[0]),
    }


def chest_contents(rom):
    """TreasureChestContentArray, one word a chest from index 1: an item
    ($8000 | its ItemID_), meseta, or 0."""
    items = {}
    for item, value in constants(rom, "ItemID_").items():
        if value in items:
            raise SystemExit(f"ItemID_{items[value]} and ItemID_{item} "
                             f"are both {value}")
        items[value] = item
    data = rom.blob("TreasureChestContentArray")
    out = []
    for n in range(1, len(data) // 2 + 1):
        value = int.from_bytes(data[2 * n - 2:2 * n], "big")
        if value & 0x8000:
            out.append({"index": n, "item": items[value & 0x7FFF]})
        elif value == 0:
            out.append({"index": n, "garbage": True})
        else:
            out.append({"index": n, "meseta": value})
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("rom", type=Path, help="the cartridge image")
    ap.add_argument("-o", "--out", type=Path, default=Path("assets"))
    args = ap.parse_args()

    rom = RomMap(args.rom)
    obj_value = constants(rom, "ObjID_")

    levels = json.loads((args.out / "levels/levels.json").read_text())["levels"]

    # ---- who stands where
    sprites = []
    for index, at in enumerate(offset_table(rom, "LevelSpriteData")):
        label = label_of(rom, at)
        raw = rom.blob(label)
        objects = []
        for at in range(0, len(raw) - 5, 6):
            b0, b1 = raw[at], raw[at + 1]
            if b0 == 0 and b1 == 0:
                break
            objects.append({
                "object": FIRST_LEVEL_OBJECT + (b0 >> 2),
                "interaction": b0 & 3,
                "index": b1,
                "x": raw[at + 2] << 8 | raw[at + 3],
                "y": raw[at + 4] << 8 | raw[at + 5],
            })
        record = next((lv for lv in levels if lv["index"] == index), {})
        sprites.append({"level": index, "name": record.get("name", "?"),
                        "source": label,
                        # the level record's extra-art byte; bit 7 is the
                        # plane-B parallax flag, not part of the index
                        "art_list": record.get("extra_art", 0) & 0x7F,
                        "objects": objects})

    # ---- their art: a count less one, then the art's long pointer and
    # its VRAM address an entry (LoadLevelData's dbf loop)
    art_lists = []
    wanted = set()
    for at in offset_table(rom, "loc_283EE"):
        entries = []
        for n in range(rom.u16(at) + 1):
            entry = at + 2 + 6 * n
            label = label_of(rom, rom.u32(entry) & 0xFFFFFF)
            # (build 97b) The art by name (tools/asset_names.py),
            # the ROM's label kept as "source".
            entries.append({"art": asset_names.SPRITE_ART_NAMES[label],
                            "source": label,
                            "tile": rom.u16(entry + 4) // 32})
            wanted.add(label)
        art_lists.append(entries)

    tiles_dir = args.out / "tiles"
    for label in sorted(wanted):
        tiles, _ = decompress(rom.rom, rom.addr(label))
        name = asset_names.SPRITE_ART_NAMES[label]
        ps2png.write_tile_sheet(tiles_dir / f"sprite_{name}.png", tiles,
                                ps2png.grey_ramp())

    # ---- what each type is: the Mota objects whose second routine is
    # one of the two wanders
    wander_at = {rom.addr(label): kind for label, kind in WANDER.items()}
    types = []
    for name, value in sorted(obj_value.items(), key=lambda kv: kv[1]):
        if not name.startswith("Mota"):
            continue
        routine = object_routine(rom, value)
        table = jump_table(rom, routine)
        second = decode(rom, table + 4) if table is not None else None
        wander = wander_at.get(second.target) \
            if second is not None and second.op == 0x6000 else None
        if wander is None:
            continue
        types.append(person(rom, routine, value, name, wander))

    # Object $1B (loc_4288, ps2.asm:6273) has no PtrObj_ label and no Obj_
    # name: the Mota people in Roron, among them the jet scooter's maker.
    # Its init (loc_42A6) puts its art at $400 with mappings loc_59F62,
    # and its wander (loc_42F8) is four-way, which none of the nine town
    # types share: see npc.c's decide_four.
    types.append(person(rom, rom.addr("loc_4288"), 0x1B, "MotaRoron",
                        "four"))

    # Obj_Dezolian ($1A, ps2.asm:6083) and Obj_MuskCat ($1C, 6482): the
    # people of Aukba, Zosa and Ryuon and the cats under Skure. Each init
    # (loc_3FF8, loc_456C) is the same shape as the others -- tile word,
    # mappings, $2C = 4, first wait -- but their wanders (loc_402C,
    # loc_45A0) are their own: see npc.c's decide_dezolian and
    # decide_muskcat. A Dezolian's frames are two pieces (loc_59EBA,
    # 16x32 over 16x8); a cat's are one, twenty of them (loc_5A394).
    for name, wander in (("Dezolian", "dezolian"), ("MuskCat", "muskcat")):
        types.append(person(rom, rom.addr("Obj_" + name), obj_value[name],
                            name, wander))

    # ---- what they say
    dialogue = {
        "bank": 0x16,
        "after_climatrol": list(rom.blob("loc_E6B0")),
        "before_climatrol": list(rom.blob("loc_E70C")),
        "later": list(rom.blob("loc_E730")),
    }

    # ---- the chests
    chest_types = [parse_chest(rom, name, obj_value[name])
                   for name in ("DezoTreasureChest", "MotaTreasureChest")]
    chests = {"source": "TreasureChestContentArray",
              "types": chest_types,
              "contents": chest_contents(rom)}

    # ---- the Teim and Darum scene. Object $2B has no PtrObj_ label: it is
    # the burst Teim leaves where Darum stood (loc_501E). The dead bodies
    # on Shure's top floor ride along: SpriteData_ShureF3 writes them as
    # raw $6C bytes, object $2F with interaction type 0.
    # The two doors (Obj_OpenableDoor, $3B, ps2.asm:7915, and
    # Obj_DestroyableDoor, $3C, 7961) share one init shape: tile word
    # $44B7, mappings loc_5D4F8 (shut, ajar, open, blown), hit-box row 6.
    # Obj_Neifirst ($32, ps2.asm:7699): tile word $450 (art list 6's
    # loc_5D05E), one 16x32 piece (loc_5D17A), hit-box row 4. Its init is
    # what deletes it once $C737 is set or slot 0 is empty (npc.c).
    # Obj_RandomExplosion ($2D, 7509): tile word $400, five frames
    # (loc_5B574), no hit box; screen-relative and hidden until its timer
    # runs out. Its init sets $24 to -1 and 2(a0) to $12, which parse_scene
    # reads as frame 0 and not in front -- npc.c's own tick sets both.
    # Object $3D (loc_580A, ps2.asm:8013; no PtrObj_ label) is the pump
    # SpriteData_MotaWorldMap puts on Motavia at 1168,1168: tile word
    # $4437 (line 2, art list 10's loc_5E322 at $437), mappings loc_5E516
    # (four frames), hit-box row 4. Its init hides it (2(a0) = 2) until
    # $C737 is set; its two routines animate it (npc.c).
    # Object 7 is Tyler's monitor's explosion (build 82), below.
    scene = [parse_scene(rom, obj_value["Darum"], "Darum"),
             parse_scene(rom, obj_value["Neifirst"], "Neifirst"),
             parse_scene(rom, obj_value["RandomExplosion"], "RandomExplosion"),
             parse_scene(rom, obj_value["Teim"], "Teim"),
             parse_scene(rom, 0x2B, "Burst"),
             parse_scene(rom, obj_value["DeadBody"], "DeadBody"),
             parse_scene(rom, obj_value["JetScooter"], "JetScooter"),
             parse_scene(rom, obj_value["OpenableDoor"], "OpenableDoor"),
             parse_scene(rom, obj_value["DestroyableDoor"],
                         "DestroyableDoor"),
             parse_scene(rom, 0x3D, "Pump"),
             # Obj_ExplosionTylerSpaceship (object 7, ps2.asm:2471), which
             # loc_CE20 puts in $E800 at TylerSpaceShip_EventFrames' $FE:
             # tile word $6000 (line 3, tile 0 -- TylerSpaceshipArt is at
             # tile 1), Map_PalmExplosion's six entries (frames 1, 1, 2,
             # 2, 3, 4 -- the last 33 pieces), no hit box. Screen-relative
             # at $118,$98; game.c runs it on the building screen.
             parse_scene(rom, 7, "MonitorExplosion"),
             # Obj_Spaceship ($44, ps2.asm:8138), which Building_Roof's
             # flight (loc_C58A) puts in $EC00: tile word $4400 (line 2,
             # tile $400 -- loc_71702's art), loc_71DEA's eleven frames,
             # the last never drawn. game.c runs it on the building screen.
             parse_scene(rom, obj_value["Spaceship"], "Spaceship"),
             # The Esper mansion (build 87). Obj_Esper ($2E, ps2.asm:7555)
             # and Obj_MovingEsper ($30, 7604): tile word $400 (art list
             # 5 or 11's loc_5B5B4), loc_5C806's twelve one-piece frames,
             # hit-box row 4. Objects $23-$27 (loc_4B28-loc_4CFC, 6936),
             # the pieces of B1's scene: $23-$26 tile word $647D
             # (loc_5BCA2 at $47D, line 3), loc_5C866's four frames; $26
             # alone has a box ($A, or $B once $C743 is 3, npc.c); $27
             # tile word $245A (loc_5C366 at $45A, line 1), loc_5C8A2's
             # three frames, no box. $31 (loc_5408, 7644) and $33
             # (loc_549A, 7690): tile words $6449 and $2449 (loc_5C67E at
             # $449), loc_5C8E4's two frames, box $C; F1's door and the
             # way in behind it.
             parse_scene(rom, 0x2E, "Esper"),
             parse_scene(rom, 0x30, "MovingEsper"),
             parse_scene(rom, 0x23, "MansionB1_23"),
             parse_scene(rom, 0x24, "MansionB1_24"),
             parse_scene(rom, 0x25, "MansionB1_25"),
             parse_scene(rom, 0x26, "MansionB1_26"),
             parse_scene(rom, 0x27, "MansionB1_27"),
             parse_scene(rom, 0x31, "MansionDoor"),
             parse_scene(rom, 0x33, "MansionDoorOpen"),
             # Noah (build 92). Object $2C (loc_50AE, ps2.asm:7420):
             # Mother Brain's image, which the arrival scene (loc_7E02
             # case $E) puts at $EC00; object $45 (loc_59FA, 8196): an
             # energy arc, two of them at $EC40/$EC80. Both tile word $400
             # (loc_5C900, Noah's art list 12, line 0), loc_5CF30's ten
             # frames -- 0-2 the arc, 3-6 the image forming, 7-9 the image
             # -- and no hit box.
             parse_scene(rom, 0x2C, "MotherBrainImage"),
             parse_scene(rom, 0x45, "EnergyArc"),
             # The Earthmen's leader (build 97d). Object $28 (loc_4DD8,
             # ps2.asm:7166), the only object on Noah F1's sprite list, at
             # 33,7 with $2E = 0: tile word $44C (loc_5DFEA, Noah's art
             # list 12, line 0), loc_5D986's three 24x32 frames -- only the
             # first ever shown -- and hit-box row 4. The walk to the
             # Earthmen stops two cells below him.
             parse_scene(rom, 0x28, "EarthmanLeader"),
             # Obj_EndingUFO (object 8, ps2.asm:2505), which the credits'
             # palette fade (loc_6AE0) puts in the first object slot at its
             # last step: tile word $4480 (line 2, tile $480 -- loc_B6E30,
             # whose first 50 tiles are the starfield's, so the pieces
             # start at 50), loc_B7E32's five frames (the last 47 pieces),
             # no hit box.
             # Screen-relative at $156,$100; game.c's ending runs it.
             parse_scene(rom, 8, "EndingUfo")]
    data = args.out / "data"
    data.mkdir(parents=True, exist_ok=True)
    (data / "level_sprites.json").write_text(json.dumps(
        {"levels": sprites, "art_lists": art_lists}, indent=1), newline="\n")
    (data / "npc_types.json").write_text(json.dumps(types, indent=1), newline="\n")
    (data / "people_dialogue.json").write_text(json.dumps(dialogue, indent=1), newline="\n")
    (data / "chests.json").write_text(json.dumps(chests, indent=1), newline="\n")
    (data / "scene_objects.json").write_text(json.dumps(scene, indent=1), newline="\n")
    print(f"{sum(len(s['objects']) for s in sprites)} objects in "
          f"{len(sprites)} levels, {len(types)} types, {len(wanted)} art "
          f"blocks, {len(chests['contents'])} chests -> {args.out}")


if __name__ == "__main__":
    main()
