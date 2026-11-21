#!/usr/bin/env python3
"""Everything ProcessRandomBattle and BattleScreen read from the ROM.

Writes assets/data/battle.json:

  enemies        EnemyData, 64 bytes a record: 15 CRAM words (entries
                 1-15 of the group's palette line), the art pointer, EXP,
                 meseta, attack, defense, type, agility, HP, technique,
                 technique rate, success-rate index, escape rate, the
                 object id, animation duration, four animation offsets,
                 two sound ids, the map pointer, two id bytes. Named from
                 EnemyNames, ten text codes each.
  formations     EnemyBattleFormationData: id, count, id, count, x-layout
                 index. Entry 0 is formation 1 (loc_796 does `subq.w #1`);
                 the four bosses are $100-$103.
  groups         EnemyFormationTable: eight formation ids a group.
  regions        the encounter-zone tables: Motavia 12 x 8 bytes on foot
                 (loc_23C3A), on the scooter (loc_23C9A), after Climatrol
                 (loc_23CFA) and both (loc_23D5A -- the offsets add); Skure
                 8 x 8 (loc_23DBA).
  enemy_x        EnemyXPosArray: five x positions a layout.
  char_pos       Battle_CharCoordinates.
  char_sprites   Battle_CharObjectPtrs: map, art, palette line.
  anim           loc_2601A: the animation scripts, by index.
  maps           every Battle_*Map: frames of (x, y, w, h, cells), the
                 nametable blocks loc_572 writes into plane A.
  sprites        every Map_*Battle: frames of sprite pieces, as
                 BuildSprites reads them (y, size, tile word, x).
  anim_tables    the seven animation-script tables the fight runs
                 (Battle_AnimateSprites): idle (loc_2601A), the enemies'
                 attack (loc_26118) and technique (loc_26216) scripts,
                 their two child overlays (loc_26314, loc_26414), the
                 party's (CharBattle_AnimOffsets) and the weapon and
                 technique effects' (loc_27364, loc_27226).
  weapon_props   WeaponProperties, 12 bytes a weapon from the Knife: kind,
                 spread, sound, then two (art index | map) longs -- the
                 thrown/held effect and the hit effect.
  tech_props     TechniqueProperties, 10 bytes a technique: a byte, the
                 sound, and the same two longs.
  effect_art     WeaponTechArtPtrs: per art index, the pieces loc_608A
                 queues -- an art label and the VRAM address.
  effect_maps    every Map_Weapon* / Map_Tech* the two property tables
                 name, decoded like the enemy maps.
  success_rate   EnemyTechSuccessRate; target_chances is
                 Battle_CharTargetChances; hit_anim is loc_34EE.

(build 185) From the ROM itself (tools/rommap.py), not ps2.asm: a pointer
or a table's word offset is named by the label at the address it gives
(the nearest one before it and "+N" when none starts there), art by the
binclude'd file at its address, and a table runs from its label to the
next.
"""

import argparse
import bisect
import json
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
import asset_names  # noqa: E402
from rommap import RomMap  # noqa: E402
from extract_gamedata import (decode_name, file_at, file_stem,  # noqa: E402
                              label_at, run_of)

ENEMY_SIZE = 64
NAME_LEN = 10           # EnemyNameLength
FORMATION_SIZE = 5
GROUP_SIZE = 8
SCRIPT_MAX = 0x100      # an animation script is read this far at most
TERMINATORS = (0xFE, 0xFF)


class Labels:
    """The map's labels by address, for naming what a pointer points at."""

    def __init__(self, rom):
        self.rom = rom
        self.sorted = sorted((a, i, n) for i, (n, a) in enumerate(rom.labels))
        self.addresses = [a for a, _, _ in self.sorted]

    def name(self, address, pattern):
        """The label matching `pattern` at an address; failing that, the
        nearest one before it, as "label+N" -- what the disassembly writes
        for a pointer into the middle of a block, (loc_274C6+6)."""
        rx = re.compile(pattern)
        here = [n for n in self.rom.names_at(address) if rx.fullmatch(n)]
        if len(here) > 1:
            sys.exit(f"${address:X} is {', '.join(here)}: which is meant?")
        if here:
            return here[0]
        k = bisect.bisect_left(self.addresses, address)
        while k > 0:
            k -= 1
            a, _, n = self.sorted[k]
            if rx.fullmatch(n):
                self.rom.addr(n)
                return f"{n}+{address - a}"
        sys.exit(f"no {pattern} label at or before ${address:X}")


def pointer_label(rom, address, prefix):
    label = label_at(rom, address, prefix)
    if label is None:
        sys.exit(f"no {prefix} label at ${address:X}")
    return label


def words(data):
    return [int.from_bytes(data[i:i + 2], "big") for i in range(0, len(data), 2)]


def read_enemy_names(rom):
    """EnemyNames: ten codes a name, $C4 ending a short one."""
    raw = rom.blob("EnemyNames")
    return [decode_name(raw[i:i + NAME_LEN])
            for i in range(0, len(raw) - (NAME_LEN - 1), NAME_LEN)]


def read_enemies(rom, names, art_files):
    """One record per Enemy_* label in EnemyData order."""
    entries, _ = run_of(rom, "EnemyData", lambda n: n.startswith("Enemy_"))
    enemies = []
    for n, (label, at) in enumerate(entries, start=1):
        raw = rom.read(at, ENEMY_SIZE)
        if rom.next_addr(label) - at < ENEMY_SIZE:
            sys.exit(f"{label}: {rom.next_addr(label) - at} bytes")
        pal = words(raw[:30])
        art = pointer_label(rom, int.from_bytes(raw[30:34], "big"), "Battle_")
        r = raw[34:]
        e = {
            "id": n,
            "label": label,
            "name": names[n] if n < len(names) else "",
            "palette": ["%04X" % w for w in pal],
            "art": art,
            "art_file": art_files.get(art),
            "exp": int.from_bytes(r[0:2], "big"),
            "meseta": int.from_bytes(r[2:4], "big"),
            "attack": int.from_bytes(r[4:6], "big"),
            "defense": int.from_bytes(r[6:8], "big"),
            "type": r[8],
            "agility": r[9],
            "hp": int.from_bytes(r[10:12], "big"),
            "technique": r[12],
            "technique_rate": r[13],
            "success_index": r[14],
            "escape": r[15],
            "object": r[16],
            "anim_duration": r[17],
            "anim_bytes": [r[18], r[19], r[20], r[21]],
            "sfx_attack": r[22],
            "sfx_technique": r[23],
            "map": pointer_label(rom, int.from_bytes(r[24:28], "big"), "Battle_"),
            "anim": r[28],
            "id_copy": r[29],
        }
        enemies.append(e)
    return enemies


def read_formations(rom):
    toks = rom.blob("EnemyBattleFormationData")
    out = []
    for i in range(0, len(toks) - (FORMATION_SIZE - 1), FORMATION_SIZE):
        out.append({"enemy1": toks[i], "count1": toks[i + 1],
                    "enemy2": toks[i + 2], "count2": toks[i + 3],
                    "layout": toks[i + 4]})
    return out


def read_groups(rom):
    data = rom.blob("EnemyFormationTable")
    return [list(data[i:i + GROUP_SIZE])
            for i in range(0, len(data) - (GROUP_SIZE - 1), GROUP_SIZE)]


def read_regions(rom):
    a = rom.run("loc_23C3A", 0x60)
    b = rom.run("loc_23C9A", 0x60)
    c = rom.run("loc_23CFA", 0xC0)
    d = rom.run("loc_23DBA", 0x40)
    return {
        "motavia": list(a),
        "motavia_scooter": list(b),
        "motavia_late": list(c[:0x60]),
        "motavia_late_scooter": list(c[0x60:0xC0]),
        "skure": list(d),
    }


def read_char_sprites(rom):
    """Battle_CharObjectPtrs: two longs a character, the map (palette
    line in bits 29-30) and the art."""
    data = rom.blob("Battle_CharObjectPtrs")
    out = []
    for i in range(0, len(data) - 7, 8):
        m = int.from_bytes(data[i:i + 4], "big")
        art = int.from_bytes(data[i + 4:i + 8], "big")
        out.append({"map": pointer_label(rom, m & 0xFFFFFF, "Map_"),
                    "art": pointer_label(rom, art, "Battle_"),
                    "palette_line": (m >> 29) & 3})
    return out


def script_end(rom, start):
    """Where the run of animation scripts from `start` ends: at the
    `even` after the last of them -- a zero pad byte at an odd address,
    right after a script's end mark, with a label on the next (even)
    byte -- or SCRIPT_MAX bytes on, as far as battle.c keeps.

    A script ends at its own $FF (or $FE), so what follows it in the run
    is never played (roundtrip_json.py counts it as holes); the run is
    kept whole because the tables point into it. (build 185) Where a run
    has no `even` after it, this goes on through the ROM's next bytes:
    the asm reader skipped dc lines that had no `$` number (a table of
    `Label-Table` words, `SXFID_` bytes) and joined what lay past them."""
    limit = start + SCRIPT_MAX
    for p in range(start + 1, limit):
        if p & 1 and rom.u8(p) == 0 and rom.u8(p - 1) in TERMINATORS \
                and rom.names_at(p + 1):
            return p
    return limit


def read_anim_table(rom, labels, table, entry_re=r"loc_[0-9A-F]+|AnimOff_\w+"):
    """A table of word offsets to scripts, as loc_2601A is: each entry is
    named by the label its offset reaches. An entry may point into a
    script, `(loc_X+N)-table` -- loc_27364's $41 is (loc_274C6+6) -- and
    is keyed `loc_X+N` with the script from there. Before build 88 only
    `loc_X-table` was read, so that entry was dropped and every one after
    it moved down one: loc_3462's hit effects $41-$46 played the next
    script, and $47 (an enemy in the far column, x $198) was past the end
    -- an effect that never ended and a fight that never went on."""
    base = rom.addr(table)
    offsets, at = [], {}
    for off in words(rom.blob(table)):
        key = labels.name(base + off, entry_re)
        offsets.append(key)
        at.setdefault(key, base + off)
    scripts = {}
    # (build 113a) Each label once, in the index's order: a set's order is
    # its strings' hashes, which PYTHONHASHSEED changes run to run, and
    # battle.json came out differently every time.
    for key in dict.fromkeys(offsets):
        start = at[key]
        scripts[key] = list(rom.read(start, script_end(rom, start) - start))
    return {"index": offsets, "scripts": scripts}


def read_props(rom, table, prefix, nbytes):
    """WeaponProperties / TechniqueProperties: `nbytes` bytes, two longs
    of (art index | palette bit) << 24 | map, 0 for none."""
    entries, _ = run_of(rom, table, lambda n: n.startswith(prefix))
    out = []
    for label, at in entries:
        bytes_ = list(rom.read(at, nbytes))
        longs = [rom.u32(at + nbytes), rom.u32(at + nbytes + 4)]
        maps = [pointer_label(rom, v & 0xFFFFFF, "Map_") if v & 0xFFFFFF
                else None for v in longs]
        a1, a2 = longs[0] >> 24, longs[1] >> 24
        out.append({"label": label, "bytes": bytes_,
                    "art": a1 & 0x7F, "pal": a1 >> 7, "map": maps[0],
                    "art2": a2 & 0x7F, "pal2": a2 >> 7, "map2": maps[1]})
    return out


def art_label_name(rom, address):
    """The PNG an art block decodes to, named after its binclude'd file;
    None when the art is written out in place (no file starts there)."""
    f = file_at(rom, address)
    return None if f is None else asset_names.art_name(file_stem(f))


def read_effect_art(rom):
    """WeaponTechArtPtrs: pointers to (count-1, then art pointer + VRAM
    address pairs). Returns the entries and the set of art labels."""
    entries = []
    labels = set()
    data = rom.blob("WeaponTechArtPtrs")
    for i in range(0, len(data) - 3, 4):
        at = int.from_bytes(data[i:i + 4], "big")
        lab = pointer_label(rom, at, "")
        count = rom.u16(at) + 1
        parts = []
        for k in range(count):
            art_at = rom.u32(at + 2 + 6 * k)
            art = pointer_label(rom, art_at, "")
            vram = rom.u16(at + 6 + 6 * k)
            parts.append({"art": art, "file": art_label_name(rom, art_at) or art,
                          "vram": vram})
            labels.add(art)
        entries.append({"label": lab, "parts": parts})
    return entries, labels


def effect_art_inline(rom, labels):
    return sorted(lab for lab in labels if file_at(rom, rom.addr(lab)) is None)


def frame_table(rom, label):
    """A map's word offsets (one a frame) to where each frame starts."""
    base = rom.addr(label)
    return [base + off for off in words(rom.blob(label))]


def sprite_pieces(data):
    n = data[0]
    pieces = []
    for i in range(n):
        b = data[1 + i * 5:6 + i * 5]
        pieces.append({
            "y": int.from_bytes(b[0:1], "big", signed=True),
            "size": b[1],
            "tile": (b[2] << 8) | b[3],
            "x": int.from_bytes(b[4:5], "big", signed=True),
        })
    return pieces


def read_frame(rom, at):
    """A count and five-byte pieces (BuildSprites, loc_526), or a
    nametable block loc_572 writes into plane A: x, y, w-1, h-1, cells."""
    first = rom.u8(at)
    if first != 0 and first < 0x80:
        return {"pieces": sprite_pieces(rom.read(at, 1 + 5 * first))}
    head = rom.read(at, 6)
    x = int.from_bytes(head[0:2], "big", signed=True)
    y = int.from_bytes(head[2:4], "big", signed=True)
    w = head[4] + 1
    h = head[5] + 1
    cells = []
    p = at + 6
    for _ in range(h):
        row = []
        for _ in range(w):
            row.append(rom.u16(p))
            p += 2
        cells.append(row)
    return {"x": x, "y": y, "w": w, "h": h, "cells": cells}


def read_enemy_maps(rom, labels):
    return {label: [read_frame(rom, f) for f in frame_table(rom, label)]
            for label in labels}


def read_sprite_maps(rom, labels):
    """Map_*Battle: word offsets to frames; a frame is a count and that
    many five-byte pieces (y, size, tile hi, tile lo, x)."""
    out = {}
    for label in labels:
        out[label] = [sprite_pieces(rom.read(f, 1 + 5 * rom.u8(f)))
                      for f in frame_table(rom, label)]
    return out


def art_file_names(rom):
    """Which PNG each Battle_*Art label decodes to: a binclude keeps its
    file's stem, art written out in place is named from the label. Also
    returns the set of labels that are inline, for extract_inline_art.py."""
    out = {}
    inline = set()
    for label, at in rom.matching(r"^Battle_.*Art$"):
        name = art_label_name(rom, at)
        if name is None:
            out[label] = inline_art_name(label)
            inline.add(label)
        else:
            out[label] = name
    return out, inline


def inline_art_name(label):
    core = label[len("Battle_"):-len("Art")]
    return "battle_" + core.lower() + "_art"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("rom", type=Path, help="the cartridge image")
    ap.add_argument("-o", "--outdir", type=Path, default=Path("assets/data"))
    args = ap.parse_args()

    rom = RomMap(args.rom)
    labels = Labels(rom)
    names = read_enemy_names(rom)
    art_files, inline = art_file_names(rom)

    enemies = read_enemies(rom, names, art_files)
    maps = read_enemy_maps(rom, sorted({e["map"] for e in enemies}))
    chars = read_char_sprites(rom)
    sprites = read_sprite_maps(rom, [c["map"] for c in chars])

    weapons = read_props(rom, "WeaponProperties", "WeaponProp_", 4)
    techs = read_props(rom, "TechniqueProperties", "TechProp_", 2)
    effect_art, art_labels = read_effect_art(rom)
    map_labels = sorted({p[k] for p in weapons + techs for k in ("map", "map2")
                         if p[k] is not None})
    effect_maps = read_enemy_maps(rom, map_labels)
    anim_tables = {
        "idle": read_anim_table(rom, labels, "loc_2601A"),
        "enemy_attack": read_anim_table(rom, labels, "loc_26118"),
        "enemy_tech": read_anim_table(rom, labels, "loc_26216"),
        "enemy_child_a": read_anim_table(rom, labels, "loc_26314"),
        "enemy_child_b": read_anim_table(rom, labels, "loc_26414"),
        "chars": read_anim_table(rom, labels, "CharBattle_AnimOffsets"),
        "effect": read_anim_table(rom, labels, "loc_27364"),
        "effect2": read_anim_table(rom, labels, "loc_27226"),
    }

    xs = words(rom.blob("EnemyXPosArray"))
    out = {
        "enemies": enemies,
        "formations": read_formations(rom),
        "groups": read_groups(rom),
        "regions": read_regions(rom),
        "enemy_x": [xs[i:i + 5] for i in range(0, len(xs), 5)],
        "char_pos": words(rom.blob("Battle_CharCoordinates")),
        "char_sprites": chars,
        "anim": read_anim_table(rom, labels, "loc_2601A", r"loc_[0-9A-F]+"),
        "maps": maps,
        "sprites": sprites,
        "inline_art": sorted(inline),
        "anim_tables": anim_tables,
        "weapon_props": weapons,
        "tech_props": techs,
        "effect_art": effect_art,
        "effect_art_inline": effect_art_inline(rom, art_labels),
        "effect_maps": effect_maps,
        "success_rate": list(rom.blob("EnemyTechSuccessRate")),
        "target_chances": list(rom.blob("Battle_CharTargetChances")),
        "hit_anim": list(rom.run("loc_34EE", 28)),
    }

    args.outdir.mkdir(parents=True, exist_ok=True)
    path = args.outdir / "battle.json"
    path.write_text(json.dumps(out, indent=1), newline="\n")
    print(f"{len(enemies)} enemies, {len(out['formations'])} formations, "
          f"{len(out['groups'])} groups, {len(maps)} map sets, "
          f"{len(out['anim']['scripts'])} animation scripts -> {path}")


if __name__ == "__main__":
    main()
