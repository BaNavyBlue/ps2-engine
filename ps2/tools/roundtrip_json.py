#!/usr/bin/env python3
"""Level 2 of the asset round-trip for the JSON data tables (build 131).

Each packer reads a checked-in JSON file the way the game's C loader reads
it -- the first of two equal keys wins, a missing or mistyped field takes
the loader's fallback, numbers are cut to the loader's field width, names
are looked up in the tables the game looks them up in -- and lays what the
game would hold back out in the ROM table's own layout. That is compared
with the table assembled fresh from ps2.asm and the constants by
tools/asmdata.py, which evaluates the symbols (ItemID_Knife, id(),
nametxt, charset) itself rather than taking the extractor's word for them.

A pointer compares by the label it names (asmdata.PtrCell). A ROM byte the
game never reads and the JSON does not carry is a Hole in the packed
table: listed in the report with the ROM routine that shows it unread,
never compared. A difference that was read in the ROM and found harmless
goes in CHECKED; it is printed as a note, and any other difference fails.

Each table also names the fields its loader reads; the report lists them
and every field in the file the loader never reads (names, "source"
labels, ids that are only the entry's place).

    python3 tools/roundtrip_json.py --ps2-dir ../ps2_disasm [--assets DIR]
    python3 tools/roundtrip_json.py --ps2-dir ../ps2_disasm --prove-edits

--prove-edits copies assets/ to a scratch directory, changes one field the
game reads in each table's file, and checks both levels fail: level 1 (the
table's extractor run fresh no longer matches the file) and level 2 (the
packed bytes no longer match the ROM).
"""

import argparse
import json
import re
import shutil
import subprocess
import sys
import tempfile
from dataclasses import dataclass, field
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from asmdata import AsmDataError, PtrCell, Rom, Sym  # noqa: E402

TOOLS = Path(__file__).resolve().parent


# ----------------------------------------------------------------------
# The JSON as the game's reader sees it (src/core/json.c)
# ----------------------------------------------------------------------

class JObj(list):
    """An object as json.c keeps it: members in file order, and json_get
    returns the first with the key."""
    path = ""
    doc = None


class JArr(list):
    path = ""
    doc = None


# Objects whose members are named entries rather than fields: the report
# lists "<character>" once, not each name.
DICT_PATHS = {
    # (build 136d) ignored bits by register name
    "music/fm_instruments.json": {"instruments[].ignored_bits": "<register>"},
    "data/techniques.json": {"available": "<character>"},
    "data/agility_bonus.json": {"": "<item>"},
    "levels/levels.json": {"tilesets": "<tileset>",
                           "levels[].files.backdrop": "<plane>"},
    "data/battle.json": {
        "maps": "<map set>", "effect_maps": "<map set>",
        "sprites": "<map set>", "anim.scripts": "<script>",
        **{f"anim_tables.{t}.scripts": "<script>"
           for t in ("idle", "enemy_attack", "enemy_tech", "enemy_child_a",
                     "enemy_child_b", "chars", "effect", "effect2")}},
}


class GameJson:
    """One file, with a record of every key path the loader asked for."""

    def __init__(self, path, dict_paths=None):
        self.path = Path(path)
        self.group = None           # one line in the report for many files
        self.dict_paths = dict_paths or {}
        self.reads = set()
        try:
            text = self.path.read_text()
            self.root = json.loads(text, object_pairs_hook=JObj)
        except (OSError, ValueError):
            self.root = None        # json_parse_file's NULL
        if self.root is not None:
            self.root = self._adopt(self.root, "")

    def _adopt(self, v, path):
        if isinstance(v, JObj):
            v.path, v.doc = path, self
            for i, (k, sub) in enumerate(v):
                name = self.dict_paths.get(path, k)
                v[i] = (k, self._adopt(sub, f"{path}.{name}" if path
                                       else name))
            return v
        if isinstance(v, list):
            arr = JArr(self._adopt(sub, f"{path}[]") for sub in v)
            arr.path, arr.doc = path, self
            return arr
        return v

    def present(self):
        """Every key path in the file."""
        out = set()

        def walk(v):
            if isinstance(v, JObj):
                for k, sub in v:
                    name = self.dict_paths.get(v.path, k)
                    out.add(f"{v.path}.{name}" if v.path else name)
                    walk(sub)
            elif isinstance(v, JArr):
                for sub in v:
                    walk(sub)
        walk(self.root)
        return out


def jget(obj, key):
    if not isinstance(obj, JObj):
        return None
    name = obj.doc.dict_paths.get(obj.path, key)
    obj.doc.reads.add(f"{obj.path}.{name}" if obj.path else name)
    for k, v in obj:
        if k == key:
            return v
    return None


def jat(arr, i):
    if not isinstance(arr, JArr) or i < 0 or i >= len(arr):
        return None
    return arr[i]


def jlen(v):
    return len(v) if isinstance(v, (JArr, JObj)) else 0


def is_num(v):
    return isinstance(v, (int, float)) and not isinstance(v, bool)


def jint(obj, key, fallback):
    """json_int: a number member cast to long (toward zero), else the
    fallback."""
    v = jget(obj, key)
    return int(v) if is_num(v) else fallback


def jbool(obj, key, fallback):
    v = jget(obj, key)
    return v if isinstance(v, bool) else fallback


def jstr(obj, key, fallback):
    v = jget(obj, key)
    return v if isinstance(v, str) else fallback


def cstr(text, size):
    """snprintf(buf, size, "%s", text): at most size-1 bytes."""
    raw = text.encode("utf-8")[:size - 1]
    return raw.decode("utf-8", errors="ignore")


def u8(v):
    return v & 0xFF


def u16(v):
    return v & 0xFFFF


# ----------------------------------------------------------------------
# Packed tables
# ----------------------------------------------------------------------

@dataclass(frozen=True)
class Hole:
    """A ROM byte the game never reads and the JSON does not carry."""
    why: str


@dataclass
class Packed:
    cells: list = field(default_factory=list)
    errors: list = field(default_factory=list)   # values the layout can't hold
    # (build 131c) The ROM side, where its shape is the file's (a mapping
    # frame's or a script's length): rom_side(rom) -> cells.
    rom_side: object = None

    def byte(self, v):
        self.cells.append(u8(v))

    def word(self, v):
        self.cells += [(v >> 8) & 0xFF, v & 0xFF]

    def hole(self, why, n=1):
        self.cells += [Hole(why)] * n


def cell_str(c):
    if isinstance(c, int):
        return f"${c:02X}"
    if isinstance(c, PtrCell):
        return str(c)
    if isinstance(c, Hole):
        return "(hole)"
    return repr(c)


@dataclass
class Table:
    key: str                 # short id, used by CHECKED and --only
    title: str               # the report's name for it
    rom_label: str           # the ROM label(s), for the report
    files: list              # asset paths the packer reads
    extractor: str           # Makefile target that writes the files
    rom: object              # rom(Rom) -> cells
    pack: object             # pack(assets, Rom, docs) -> Packed
    edit: object = None      # edit(assets_dir) -> text, for --prove-edits
    not_packed: str = ""     # what the file carries that isn't ROM data


# (table key, offset) -> (rom cell, packed cell, why it's harmless).
# Each entry was read in the ROM; an entry that no longer matches, or any
# other difference, still fails. Filled below by checked_ptr().
CHECKED = {}


def checked_ptr(key, offset, name, label, rom_addend, packed_addend, why):
    """A four-byte pointer field that packs to the same label with another
    constant in its top byte."""
    for k in range(4):
        CHECKED[(key, offset + k)] = (
            PtrCell(Sym(label, rom_addend), 4, k),
            PtrCell(Sym(label, packed_addend), 4, k), why, name)


# Portraits 28 and 29 (PtrPortrait_CentralTowerOutside3 and _Copy): the ROM
# names palette entry 45 (pal_id_cent_tow_out3_port, PtrPal_CTwOut3Port),
# portraits.json entry 44 (PtrPal_RadarPort), extract_buildings.py taking
# the first entry for the palette they both point at. The two PalettePtrs
# entries are the same eight bytes (ps2.asm:26919-26922: Pal_RadarPortrait,
# $FB40, eight longs) and palette.c loads either file into line 2;
# portrait_radar.png and portrait_radar_copy_1.png are the same bytes.
# The same colours either way (build 131).
for _off, _n in ((108, 28), (112, 29)):
    checked_ptr("portraits", _off, f"portrait {_n}", "CentTowOutPortArt",
                0x2D000000, 0x2C000000,
                "palette entry 44 where the ROM names 45: the two entries "
                "are the same bytes (Pal_RadarPortrait, $FB40, 16 words) "
                "and the same PNG")


# ----------------------------------------------------------------------
# Shared lookups, as the game does them
# ----------------------------------------------------------------------

ITEM_MAX = 128
TECH_MAX = 64
CHAR_ID = ["Rolf", "Nei", "Rudo", "Amy", "Hugh", "Anna", "Kain", "Shir"]


def item_keys(docs, assets):
    """gamedata_load_tables' g_item[].key: items.json in order, at most
    128, each key cut to 23 bytes."""
    d = docs.get(assets, "data/items.json")
    items = d.root
    n = min(jlen(items), ITEM_MAX)
    return [cstr(jstr(jat(items, i), "key", "None"), 24) for i in range(n)]


def item_by_key(keys, key):
    """item_by_key: the first entry with the key, else 0 (the game warns
    on stderr)."""
    if key is None:
        return 0
    for i, k in enumerate(keys):
        if k == key:
            return i
    return 0


class Docs:
    """Each file parsed once a run, reads accumulated across packers."""

    def __init__(self):
        self.cache = {}

    def get(self, assets, rel):
        p = Path(assets) / rel
        if p not in self.cache:
            self.cache[p] = GameJson(p, DICT_PATHS.get(rel))
        return self.cache[p]

    def tmx(self, assets, rel, group):
        """(build 131b) A Tiled map as level.c reads it (TmxDoc)."""
        p = Path(assets) / rel
        if p not in self.cache:
            self.cache[p] = TmxDoc(p, group)
        return self.cache[p]


# ----------------------------------------------------------------------
# The shop stock table and Shir's steal lists (extract_stores.py)
# ----------------------------------------------------------------------

STORE_MAX, STORE_ITEMS = 32, 6


def rom_stores(rom):
    return rom.region("StoreEquipItemArray").cells


def pack_stores(assets, rom, docs):
    """window.c load_stores: "stores" in order, at most 32; six "items"
    each, a missing or non-string entry "None"; gamedata_item_id."""
    keys = item_keys(docs, assets)
    d = docs.get(assets, "data/stores.json")
    lst = jget(d.root, "stores")
    out = Packed()
    count = 0
    for e in (lst if isinstance(lst, JArr) else []):
        if count >= STORE_MAX:
            break
        items = jget(e, "items")
        for i in range(STORE_ITEMS):
            it = jat(items, i)
            out.byte(item_by_key(keys, it if isinstance(it, str) else "None"))
        count += 1
    return out


def edit_stores(assets):
    p = assets / "data/stores.json"
    j = json.loads(p.read_text())
    j["stores"][2]["items"][1] = "LaserKnife"
    p.write_text(json.dumps(j, indent=1), newline="\n")
    return "stores[2].items[1] Dagger -> LaserKnife"


STEAL_LISTS = ("weapon", "armor", "item", "room")


def rom_steal(rom):
    r = rom.region("StealItemArray", "CheckPlanetAndCaps")
    return [c for c in r.cells[:32]] + r.cells[32:]


def pack_steal(assets, rom, docs):
    """building.c steal_item: lists[list][pick], pick 0-7, a missing or
    non-string entry "None"; item_id. The four lists in the ROM's order
    (StealItemArray_Weapon, _Armor, _Item, _Room); the region ends with
    the `even` pad, none here (32 bytes)."""
    keys = item_keys(docs, assets)
    d = docs.get(assets, "data/steal.json")
    lists = jget(d.root, "lists")
    out = Packed()
    for name in STEAL_LISTS:
        lst = jget(lists, name)
        for pick in range(8):
            it = jat(lst, pick)
            out.byte(item_by_key(keys, it if isinstance(it, str) else "None"))
    return out


def edit_steal(assets):
    p = assets / "data/steal.json"
    j = json.loads(p.read_text())
    j["lists"]["room"][3] = "MoonDew"
    p.write_text(json.dumps(j, indent=1), newline="\n")
    return "lists.room[3] Visiphone -> MoonDew"


# ----------------------------------------------------------------------
# Techniques (extract_techs.py)
# ----------------------------------------------------------------------

def tech_region_end(rom):
    """TechniqueData runs through its Tech_ labels: the first label after
    it that is not one ends the table."""
    lines = rom.asm.lines
    start = rom.asm.label_line["TechniqueData"]
    for i in rom.asm.marks:
        if i <= start:
            continue
        label = lines[i].split(":")[0].strip()
        if not label.startswith("Tech_"):
            return label
    raise AsmDataError("TechniqueData has no end")


def rom_techs(rom):
    return rom.region("TechniqueData", tech_region_end(rom)).cells


def name_codes(rom, label, text, length):
    """`nametxt "text"` as the assembler lays it out under `label`: the
    charset in force on the table's first nametxt line (the item table
    sets its own just below its label), $C4 after a short name, zeros to
    `length`."""
    line = rom.asm.label_line[label]
    while "nametxt" not in rom.lines[line]:
        line += 1
    table = rom.charset_at(line)
    codes = [table.get(ord(ch), ord(ch)) for ch in text]
    if len(codes) < length:
        codes += [0xC4] + [0] * (length - len(codes) - 1)
    return codes


def pack_techs(assets, rom, docs):
    """gamedata_load_tables: techniques[i] for i < 64, "name" into char[8],
    "tp", "flags", "rate" as u8. Eight bytes an entry: the name as
    nametxt (TechNameLength) lays it out, then tp, flags, rate."""
    d = docs.get(assets, "data/techniques.json")
    lst = jget(d.root, "techniques")
    length = rom.const("TechNameLength")
    out = Packed()
    for i in range(min(TECH_MAX, jlen(lst))):
        t = jat(lst, i)
        name = cstr(jstr(t, "name", ""), 8)
        codes = name_codes(rom, "TechniqueData", name, length)
        if len(codes) > length:
            out.errors.append(f"techniques[{i}].name {name!r} is longer "
                              f"than {length}")
        out.cells += codes
        out.byte(jint(t, "tp", 0))
        out.byte(jint(t, "flags", 0))
        out.byte(jint(t, "rate", 0))
    return out


def rom_avail(rom):
    lines = rom.asm.lines
    start = rom.asm.label_line["CharAvailableTechniques"]
    end = next(lines[i].split(":")[0].strip() for i in rom.asm.marks
               if i > start and "_Tech" not in lines[i].split(":")[0])
    return rom.region("CharAvailableTechniques", end).cells


def pack_avail(assets, rom, docs):
    """gamedata_load_tables: available[who].map / .battle, 16 keys each, a
    missing or non-string one "None", looked up by "key" in the
    techniques list (the first match, else 0). 32 bytes a character:
    the map half, then the battle half."""
    d = docs.get(assets, "data/techniques.json")
    lst = jget(d.root, "techniques")
    avail = jget(d.root, "available")
    tech_keys = [jstr(jat(lst, t), "key", "") for t in range(jlen(lst))]
    out = Packed()
    for who in CHAR_ID:
        entry = jget(avail, who)
        for half in ("map", "battle"):
            keys = jget(entry, half)
            for i in range(16):
                k = jat(keys, i)
                key = k if isinstance(k, str) else "None"
                out.byte(next((t for t, tk in enumerate(tech_keys)
                               if tk == key), 0))
    return out


def edit_techs(assets):
    p = assets / "data/techniques.json"
    j = json.loads(p.read_text())
    j["techniques"][1]["tp"] = 3
    p.write_text(json.dumps(j, indent=1), newline="\n")
    return "techniques[1].tp (Foi) 2 -> 3"


def edit_avail(assets):
    p = assets / "data/techniques.json"
    j = json.loads(p.read_text())
    j["available"]["Nei"]["battle"][0] = "Gifoi"
    p.write_text(json.dumps(j, indent=1), newline="\n")
    return f"available.Nei.battle[0] -> Gifoi"


# ----------------------------------------------------------------------
# Items, the characters' starting tables, footwear agility
# (extract_gamedata.py)
# ----------------------------------------------------------------------

def rom_items(rom):
    return rom.region("InventoryData", "TechniqueData").cells


def pack_items(assets, rom, docs):
    """gamedata_load_tables: items[i] for i < 128, "name" into char[12],
    "price" u16, "action_flags", "equip_mask", "attack", "defense" u8.
    Sixteen bytes an entry: the name as nametxt (ItemNameLength), then
    the price word and the four bytes. "key" names the entry for every
    lookup by name (its place is the id); "id" is not read."""
    d = docs.get(assets, "data/items.json")
    items = d.root
    length = rom.const("ItemNameLength")
    out = Packed()
    for i in range(min(ITEM_MAX, jlen(items))):
        it = jat(items, i)
        jstr(it, "key", "None")
        name = cstr(jstr(it, "name", ""), 12)
        codes = name_codes(rom, "InventoryData", name, length)
        if len(codes) > length:
            out.errors.append(f"items[{i}].name {name!r} is longer than "
                              f"{length}")
        out.cells += codes
        out.word(u16(jint(it, "price", 0)))
        out.byte(jint(it, "action_flags", 0))
        out.byte(jint(it, "equip_mask", 0))
        out.byte(jint(it, "attack", 0))
        out.byte(jint(it, "defense", 0))
    return out


def edit_items(assets):
    p = assets / "data/items.json"
    j = json.loads(p.read_text())
    j[86]["price"] += 1
    p.write_text(json.dumps(j, indent=1), newline="\n")
    return f"items[86].price ({j[86]['key']}) +1"


def game_name_codes(text, length=4):
    """gamedata.c name_from_ascii: digits, letters, anything else 0; a
    short name ends in $C4, zero-filled."""
    out = []
    for ch in text[:length]:
        if "0" <= ch <= "9":
            out.append(1 + ord(ch) - ord("0"))
        elif "A" <= ch <= "Z":
            out.append(11 + ord(ch) - ord("A"))
        elif "a" <= ch <= "z":
            out.append(37 + ord(ch) - ord("a"))
        else:
            out.append(0)
    if len(text) < length:
        out += [0xC4] + [0] * (length - len(text) - 1)
    return out


def edit_names(assets):
    p = assets / "data/characters.json"
    j = json.loads(p.read_text())
    j[2]["name"] = "Rodo"
    p.write_text(json.dumps(j, indent=1), newline="\n")
    return "characters[2].name Rudo -> Rodo"


def rom_names(rom):
    return rom.region("CharNames", "CharNamesEnd").cells


def pack_names(assets, rom, docs):
    """gamedata_load_tables: characters[c].name for c < 8 through
    name_from_ascii into the four-byte name (CharNameLength)."""
    chars = docs.get(assets, "data/characters.json").root
    out = Packed()
    for c in range(min(8, jlen(chars))):
        out.cells += game_name_codes(jstr(jat(chars, c), "name", ""),
                                     rom.const("CharNameLength"))
    return out


EQUIP_KEY = ["head", "right_hand", "left_hand", "body", "legs"]
GD_ITEM_SLOTS = 8

SETUP_FILLER = ("SetCharInitialStats writes the level table's technique "
                "counts over these two bytes (character +$25, +$26)")


def rom_setup(rom):
    return rom.region("CharInitialSetup", "CharInitialSetupEnd").cells


def pack_setup(assets, rom, docs):
    """gamedata_load_tables: equipment.<slot> by key ("None" if missing),
    the item count as json_length(items) cut to a byte, then items[i]
    for i < 8, "item" by key | $80 if "equipped". The rest of the eight
    slots stay 0 (g_initial is zeroed). Bytes 5 and 6 are Holes."""
    keys = item_keys(docs, assets)
    chars = docs.get(assets, "data/characters.json").root
    out = Packed()
    for c in range(min(8, jlen(chars))):
        src = jat(chars, c)
        eq = jget(src, "equipment")
        inv = jget(src, "items")
        for s in EQUIP_KEY:
            out.byte(item_by_key(keys, jstr(eq, s, "None")))
        out.hole(SETUP_FILLER, 2)
        count = u8(jlen(inv))
        out.byte(count)
        slots = [0] * GD_ITEM_SLOTS
        for i in range(min(count, GD_ITEM_SLOTS)):
            e = jat(inv, i)
            iid = item_by_key(keys, jstr(e, "item", "None"))
            slots[i] = u8(iid | (0x80 if jbool(e, "equipped", False) else 0))
        out.cells += slots
    return out


LEVEL_ROWS = 50
STAT_KEYS = ["hp", "tp", "strength", "mental", "agility", "luck",
             "dexterity", "attack", "defense"]


def rom_levels(rom):
    """The level tables in CharExperiencePtrs' order, each to the label
    after it."""
    ptrs = rom.region("CharExperiencePtrs", "CharExperiencePtrsEnd").cells
    cells = []
    for i in range(0, len(ptrs), 4):
        label = ptrs[i].sym.label
        start = rom.asm.label_line[label]
        end = next(rom.lines[j].split(":")[0].strip()
                   for j in rom.asm.marks if j > start)
        cells += rom.region(label, end).cells
    return cells


def pack_levels(assets, rom, docs):
    """gamedata_load_tables: levels[r] for r < 50 of each character:
    "exp" (u32), "level" (default r+1), the nine stats, "techs_a" and
    "techs_b" as u8. Fourteen bytes a row: the level byte and the
    experience in 24 bits (one long), the nine stats, the counts as
    battle nibble << 4 | map nibble."""
    chars = docs.get(assets, "data/characters.json").root
    out = Packed()
    for c in range(min(8, jlen(chars))):
        levels = jget(jat(chars, c), "levels")
        for r in range(LEVEL_ROWS):
            row = jat(levels, r)
            exp = jint(row, "exp", 0) & 0xFFFFFFFF
            if exp > 0xFFFFFF:
                out.errors.append(f"characters[{c}].levels[{r}].exp "
                                  f"{exp} is wider than 24 bits")
            out.byte(jint(row, "level", r + 1))
            out.cells += [(exp >> 16) & 0xFF, (exp >> 8) & 0xFF, exp & 0xFF]
            for k in STAT_KEYS:
                out.byte(jint(row, k, 0))
            a, b = u8(jint(row, "techs_a", 0)), u8(jint(row, "techs_b", 0))
            if a > 15 or b > 15:
                out.errors.append(f"characters[{c}].levels[{r}] technique "
                                  "counts wider than a nibble")
            out.byte((a & 0xF) << 4 | (b & 0xF))
    return out


def edit_levels(assets):
    p = assets / "data/characters.json"
    j = json.loads(p.read_text())
    j[3]["levels"][4]["luck"] += 1
    p.write_text(json.dumps(j, indent=1), newline="\n")
    return "characters[3].levels[4].luck (Amy) +1"


def edit_setup(assets):
    p = assets / "data/characters.json"
    j = json.loads(p.read_text())
    j[0]["equipment"]["head"] = "Headgear"
    p.write_text(json.dumps(j, indent=1), newline="\n")
    return "characters[0].equipment.head (Rolf) None -> Headgear"


def rom_agility(rom):
    return rom.region("AgilityIncreasesArray").cells


def pack_agility(assets, rom, docs):
    """gamedata_load_tables: each item's agility is agility_bonus[key]
    (u8, 0 if absent). The ROM reads AgilityIncreasesArray only for a
    leg item from Shoes to GardaBoots (the equip routine at loc_AD4C,
    ps2.asm:15919), so those ids pack; a bonus on any other item is a
    value the ROM cannot hold."""
    keys = item_keys(docs, assets)
    agil = docs.get(assets, "data/agility_bonus.json").root
    agility = [u8(jint(agil, k, 0)) for k in keys]
    first, last = rom.const("ItemID_Shoes"), rom.const("ItemID_GardaBoots")
    out = Packed()
    for i in range(first, last + 1):
        out.byte(agility[i] if i < len(agility) else 0)
    for i, v in enumerate(agility):
        if v and not first <= i <= last:
            out.errors.append(f"agility bonus on {keys[i]}, outside "
                              "Shoes..GardaBoots")
    return out


def edit_agility(assets):
    p = assets / "data/agility_bonus.json"
    j = json.loads(p.read_text())
    j["HirzaBoots"] += 1
    p.write_text(json.dumps(j, indent=1), newline="\n")
    return "HirzaBoots +1"



# ----------------------------------------------------------------------
# Portraits, building music, portrait windows (extract_buildings.py)
# ----------------------------------------------------------------------

PORTRAITS_MAX = 32          # game.c
BUILDINGS_MAX = 32
WIN_PORTRAIT_COUNT = 31     # window.h: portraits $00-$1E


def art_labels(rom):
    """Picture file stem (as extract_art.py names the PNG) -> the labels
    whose binclude reads that art/ file."""
    import asset_names
    out = {}
    for line in rom.lines:
        m = re.match(r'^(\w+):\s*binclude\s+"art[\\/](\w+)\.bin"', line)
        if m:
            out.setdefault(asset_names.art_name(m.group(2)), []).append(
                m.group(1))
    return out


def rom_portraits(rom):
    return rom.region("PortraitTable", "BuildingMusicPtrs").table


def pack_portraits(assets, rom, docs):
    """game.c load_building_tables: each entry with 0 < "index" < 32 sets
    that portrait's file, assets/tiles/<"art">.png (in char[48]), and
    palette ("palette", a PalettePtrs entry by palette.c's
    palette_load_entry); a later entry with the same index wins. The ROM
    long is (pal_id << 24) | the art's address: the palette packs as the
    top byte, the file as the label whose binclude it was made from.
    Portraits 1 up to the highest index given, each needing an entry."""
    d = docs.get(assets, "data/portraits.json")
    files, pals = {}, {}
    for e in (d.root if isinstance(d.root, JArr) else []):
        i = jint(e, "index", -1)
        if 0 < i < PORTRAITS_MAX:
            files[i] = cstr(f"assets/tiles/{jstr(e, 'art', '')}.png", 48)
            pals[i] = jint(e, "palette", 0)
    labels = art_labels(rom)
    out = Packed()
    for i in range(1, max(files, default=0) + 1):
        if i not in files:
            out.errors.append(f"portrait {i} has no entry")
            out.cells += [0] * 4
            continue
        stem = files[i][len("assets/tiles/"):-len(".png")] \
            if files[i].endswith(".png") else None
        found = labels.get(stem, [])
        if len(found) != 1:
            out.errors.append(f"portrait {i}: {files[i]} is "
                              f"{'no ROM art' if not found else 'several'}")
            out.cells += [0] * 4
            continue
        sym = Sym(found[0], (pals[i] & 0xFF) << 24)
        out.cells += [PtrCell(sym, 4, k) for k in range(4)]
    return out


def edit_portraits(assets):
    p = assets / "data/portraits.json"
    j = json.loads(p.read_text())
    j[3]["palette"] += 1
    p.write_text(json.dumps(j, indent=1), newline="\n")
    return f"portraits[3].palette ({j[3]['name']}) +1"


def rom_building_music(rom):
    return rom.region("BuildingMusicPtrs").table


def pack_building_music(assets, rom, docs):
    """game.c load_building_tables: each entry with 0 <= "index" < 32 sets
    that building's music id ("music", 0 if absent); a later entry wins.
    One byte a building, 0 up to the highest index given."""
    d = docs.get(assets, "data/buildings.json")
    music = {}
    for e in (d.root if isinstance(d.root, JArr) else []):
        i = jint(e, "index", -1)
        if 0 <= i < BUILDINGS_MAX:
            music[i] = jint(e, "music", 0)
    out = Packed()
    for i in range(max(music, default=-1) + 1):
        out.byte(music.get(i, 0))
    return out


def edit_building_music(assets):
    p = assets / "data/buildings.json"
    j = json.loads(p.read_text())
    j[4]["music"] = 141
    p.write_text(json.dumps(j, indent=1), newline="\n")
    return f"buildings[4].music ({j[4]['name']}) -> 141"


def rom_portrait_windows(rom):
    """WindowArtLayoutPtrs from PtrWin_PortraitStart: eight bytes a
    window; the first 31 are the portraits' (WinID_PortraitStart +
    portrait_index), the rest other windows."""
    r = rom.region("PtrWin_PortraitStart", "WinArt_PlayerMenu")
    return r.cells[:8 * WIN_PORTRAIT_COUNT]


def pack_portrait_windows(assets, rom, docs):
    """window.c gather_descriptors: each entry with 0 <= "portrait" < 31
    adds a window, "pos" (u16), "w", "h" (u8), "art" (the winart map's
    name, char[40]). find_desc takes the first window with an id, while
    "art" goes into a buffer per portrait that a later entry overwrites:
    position and size from the first entry, art from the last. Eight
    bytes a window: position word, art address (by label), width,
    height."""
    d = docs.get(assets, "data/portrait_windows.json")
    first, art = {}, {}
    count = 0
    static = 0x3B               # WIN_PORTRAIT_START's place; DESC_MAX room
    for e in (d.root if isinstance(d.root, JArr) else []):
        if count >= WIN_PORTRAIT_COUNT + static:
            break
        n = jint(e, "portrait", -1)
        if n < 0 or n >= WIN_PORTRAIT_COUNT:
            continue
        art[n] = cstr(jstr(e, "art", ""), 40)
        if n not in first:
            first[n] = (u16(jint(e, "pos", 0)), u8(jint(e, "w", 0)),
                        u8(jint(e, "h", 0)))
        count += 1
    out = Packed()
    for n in range(WIN_PORTRAIT_COUNT):
        if n not in first:
            out.errors.append(f"portrait window {n} has no entry")
            out.cells += [0] * 8
            continue
        pos, w, h = first[n]
        out.word(pos)
        out.cells += [PtrCell(Sym(art[n], 0), 4, k) for k in range(4)]
        out.byte(w)
        out.byte(h)
    return out


def edit_portrait_windows(assets):
    p = assets / "data/portrait_windows.json"
    j = json.loads(p.read_text())
    j[5]["w"] += 1
    p.write_text(json.dumps(j, indent=1), newline="\n")
    return "portrait_windows[5].w +1"



# ----------------------------------------------------------------------
# Doors, entrances and edge exits (extract_interactions.py)
# ----------------------------------------------------------------------

WALK_DOORS_MAX, WALK_EXITS_MAX = 128, 128    # walk.h (build 131: 64 dropped doors)
LEVEL_RECORD = 20


def next_label(rom, label):
    start = rom.asm.label_line[label]
    i = next(i for i in rom.asm.marks if i > start)
    return rom.lines[i].split(":")[0].strip()


def level_records(rom):
    """LevelData's 20-byte records, in level order, to the first label
    that is not a Level_ record's (the extra-art table)."""
    start = rom.asm.label_line["LevelData"]
    end = next(rom.lines[i].split(":")[0].strip() for i in rom.asm.marks
               if i > start and not rom.lines[i].startswith("Level_"))
    cells = rom.region("LevelData", end).cells
    return [cells[i:i + LEVEL_RECORD]
            for i in range(0, len(cells), LEVEL_RECORD)]


def game_level_stems(assets, docs):
    """load_level's lookup: the last levels.json entry with "index" == n
    gives the stem "%02x_<name>" (char[64])."""
    d = docs.get(assets, "levels/levels.json")
    stems = {}
    for lv in (jget(d.root, "levels") or []):
        n = jint(lv, "index", -1)
        if n >= 0:
            stems[n] = cstr(f"{n:02x}_{jstr(lv, 'name', '')}", 64)
    return stems


def rom_doors(rom):
    """Each level's interaction list, through LevelData +14 (the long's
    top byte is the level's palette): six-byte entries to the $FF that
    ends it, the $FF included."""
    out = []
    for rec in level_records(rom):
        label = rec[14].sym.label
        cells = rom.region(label).cells
        n = 0
        while n < len(cells) and cells[n] != 0xFF:
            n += 6
        out += cells[:n + 1]
    return out


def game_door_list(assets, docs, stem, errors):
    """walk.c walk_load_doors over assets/levels/<stem>.doors.json:
    "entries", at most WALK_DOORS_MAX; an entry with a "to_level" member
    (any type) is a level change -- to_level, to_y, to_x -- else
    "building", "portrait", "f764", "f766"; "y" and "x" first; each u8.
    Six bytes an entry, then the $FF the ROM ends a list with. An entry
    at y = $FF would end the ROM's list early: the layout can't hold it.
    (build 131b) Shared by the doors and the level records' interaction
    pointer."""
    d = docs.get(assets, f"levels/{stem}.doors.json")
    d.group = "levels/*.doors.json"
    lst = jget(d.root, "entries")
    cells = []
    for k, e in enumerate(lst[:WALK_DOORS_MAX]
                          if isinstance(lst, JArr) else []):
        y, x = u8(jint(e, "y", 0)), u8(jint(e, "x", 0))
        if y == 0xFF:
            errors.append(f"{stem}: entry {k} at y $FF")
        if jget(e, "to_level") is not None:
            row = [0, jint(e, "to_level", 0), jint(e, "to_y", 0),
                   jint(e, "to_x", 0)]
        else:
            row = [jint(e, "building", 0), jint(e, "portrait", 0),
                   jint(e, "f764", 0), jint(e, "f766", 0)]
        cells += [y, x] + [u8(v) for v in row]
    return cells + [0xFF]


def pack_doors(assets, rom, docs):
    """Each level's list in turn (game_door_list), by the stem levels.json
    gives it."""
    stems = game_level_stems(assets, docs)
    out = Packed()
    for level in range(len(level_records(rom))):
        if level not in stems:
            out.errors.append(f"level {level}: no levels.json entry")
            continue
        out.cells += game_door_list(assets, docs, stems[level], out.errors)
    return out


def edit_doors(assets):
    p = assets / "levels/04_Paseo.doors.json"
    j = json.loads(p.read_text())
    j["entries"][2]["f764"] += 1
    p.write_text(json.dumps(j, indent=1), newline="\n")
    return "04_Paseo.doors.json entries[2].f764 +1"


def rom_exits(rom):
    return rom.region("loc_28546", next_label(rom, "loc_28546")).table


def pack_exits(assets, rom, docs):
    """walk.c walk_load_exits: "entries" in order, at most 128, each
    "to_level", "to_y", "to_x" as u8; the game takes entry (level - 4).
    Three bytes an entry (loc_28546)."""
    d = docs.get(assets, "levels/exits.json")
    lst = jget(d.root, "entries")
    out = Packed()
    for e in (lst[:WALK_EXITS_MAX] if isinstance(lst, JArr) else []):
        out.byte(jint(e, "to_level", 0))
        out.byte(jint(e, "to_y", 0))
        out.byte(jint(e, "to_x", 0))
    return out


def edit_exits(assets):
    p = assets / "levels/exits.json"
    j = json.loads(p.read_text())
    j["entries"][1]["to_x"] += 1
    p.write_text(json.dumps(j, indent=1), newline="\n")
    return "exits.json entries[1].to_x +1"



# ----------------------------------------------------------------------
# The townspeople, chests and scene objects (extract_npcs.py)
# ----------------------------------------------------------------------

NPC_MAX, NPC_LEVELS_MAX = 32, 128           # npc.h
NPC_LISTS_MAX, NPC_LIST_ART, NPC_ART_MAX = 16, 8, 32
NPC_CHESTS_MAX = 128
FIRST_LEVEL_OBJECT = 0x14


def offset_table(rom, label):
    """The labels a `dc.w X-label` table names, in order."""
    start = rom.asm.label_line[label]
    out = []
    for line in rom.lines[start + 1:]:
        m = re.match(r"^\s*dc\.w\s+(\w+)\s*-\s*" + label + r"\b", line)
        if m:
            out.append(rom.label(m.group(1)))
            continue
        if line.strip() and not line.strip().startswith(";"):
            break
    return out


def rom_chests(rom):
    return rom.region("TreasureChestContentArray").table


def pack_chests(assets, rom, docs):
    """npc.c load_chests: "contents" by "index" (0 < index < 128), a later
    entry winning; "item" (a string) packs $8000 | its id's low byte,
    else "meseta" (u16) unless "garbage" is true, which leaves 0. One word
    a chest from chest 1 to the highest index given."""
    keys = item_keys(docs, assets)
    d = docs.get(assets, "data/chests.json")
    words = {}
    for c in (jget(d.root, "contents") or []):
        index = jint(c, "index", 0)
        if index <= 0 or index >= NPC_CHESTS_MAX:
            continue
        item = jstr(c, "item", None)
        if item is not None:
            words[index] = 0x8000 | (item_by_key(keys, item) & 0xFF)
        elif not jbool(c, "garbage", False):
            words[index] = u16(jint(c, "meseta", 0))
        else:
            words[index] = 0
    out = Packed()
    for index in range(1, max(words, default=0) + 1):
        out.word(words.get(index, 0))
    return out


def edit_chests(assets):
    p = assets / "data/chests.json"
    j = json.loads(p.read_text())
    j["contents"][0]["meseta"] += 10
    p.write_text(json.dumps(j, indent=1), newline="\n")
    return "chests contents[0].meseta +10"


def level_sprite_lists(assets, docs):
    """npc.c npc_load_tables' g_level: for each "levels" entry with
    0 <= "level" < 128, its "objects" appended to that level's list while
    it holds fewer than 32 (a second entry for a level adds to the
    first's); "art_list" the last entry's."""
    d = docs.get(assets, "data/level_sprites.json")
    people, art = {}, {}
    for lv in (jget(d.root, "levels") or []):
        index = jint(lv, "level", -1)
        if index < 0 or index >= NPC_LEVELS_MAX:
            continue
        art[index] = jint(lv, "art_list", 0)
        lst = people.setdefault(index, [])
        for o in (jget(lv, "objects") or []):
            if len(lst) >= NPC_MAX:
                break
            lst.append((jint(o, "object", 0), jint(o, "interaction", 0),
                        jint(o, "index", 0), jint(o, "x", 0),
                        jint(o, "y", 0)))
    return people, art


def rom_level_sprites(rom):
    """LevelSpriteData's lists in its offset table's order: six bytes an
    object, to the zero word that ends one."""
    out = []
    for label in offset_table(rom, "LevelSpriteData"):
        cells = rom.region(label).cells
        n = 0
        while n + 1 < len(cells) and (cells[n], cells[n + 1]) != (0, 0):
            n += 6
        out += cells[:n + 2]
    return out


def pack_level_sprites(assets, rom, docs):
    """Each level's people in the ROM's order of levels: byte 0 is
    (object - $14) << 2 | interaction, byte 1 the index, then x and y
    words; the zero word after. An object below $14, an interaction over
    3 or an entry whose first two bytes would be zero can't be held."""
    people, _ = level_sprite_lists(assets, docs)
    out = Packed()
    for level in range(len(offset_table(rom, "LevelSpriteData"))):
        for k, (obj, inter, index, x, y) in enumerate(people.get(level, [])):
            b0 = ((obj - FIRST_LEVEL_OBJECT) << 2) | (inter & 3)
            if obj < FIRST_LEVEL_OBJECT or not 0 <= inter <= 3 \
                    or (u8(b0), u8(index)) == (0, 0):
                out.errors.append(f"level {level} object {k}: object "
                                  f"${obj:02X}, interaction {inter} can't "
                                  "be held")
            out.byte(b0)
            out.byte(index)
            out.word(u16(x))
            out.word(u16(y))
        out.word(0)
    return out


def edit_level_sprites(assets):
    p = assets / "data/level_sprites.json"
    j = json.loads(p.read_text())
    j["levels"][4]["objects"][2]["x"] += 16
    p.write_text(json.dumps(j, indent=1), newline="\n")
    return "level_sprites levels[4].objects[2].x +16"


def sprite_art_labels():
    import asset_names
    out = {}
    for label, name in asset_names.SPRITE_ART_NAMES.items():
        out.setdefault(name, []).append(label)
    return out


def rom_art_lists(rom):
    """loc_283EE's lists (LoadLevelData, ps2.asm:13305): a count word the
    loop's dbf runs from (entries - 1), then each entry's art address and
    VRAM address word."""
    out = []
    for label in offset_table(rom, "loc_283EE"):
        cells = rom.region(label).cells
        count = (cells[0] << 8 | cells[1]) + 1
        out += cells[:2 + 6 * count]
    return out


def pack_art_lists(assets, rom, docs):
    """npc.c npc_load_tables: "art_lists" in order, at most 16; each
    entry's "art" loads assets/tiles/sprite_<art>.png (art_index: an
    entry whose file will not load, or past 32 pictures, is left out),
    "tile" its place; at most 8 an entry. Packed as the count less one,
    then the art's ROM label (asset_names.SPRITE_ART_NAMES, the name the
    PNG was written under) and the VRAM address, tile * 32."""
    d = docs.get(assets, "data/level_sprites.json")
    names = sprite_art_labels()
    loaded = []
    out = Packed()
    for n, lst in enumerate((jget(d.root, "art_lists") or [])[:NPC_LISTS_MAX]):
        entries = []
        for e in (lst if isinstance(lst, JArr) else []):
            if len(entries) >= NPC_LIST_ART:
                break
            art = cstr(jstr(e, "art", ""), 24)
            path = Path(assets) / f"tiles/sprite_{art}.png"
            if art not in loaded:
                if len(loaded) >= NPC_ART_MAX or not path.exists():
                    continue
                loaded.append(art)
            entries.append((art, jint(e, "tile", 0)))
        out.word(len(entries) - 1)
        for art, tile in entries:
            labels = names.get(art, [])
            if len(labels) != 1:
                out.errors.append(f"art list {n}: {art} is "
                                  f"{'no ROM art' if not labels else 'several'}")
                out.cells += [0] * 4
            else:
                out.cells += [PtrCell(Sym(labels[0], 0), 4, k)
                              for k in range(4)]
            if tile * 32 > 0xFFFF:
                out.errors.append(f"art list {n}: tile {tile} past VRAM")
            out.word(u16(tile * 32))
    return out


def edit_art_lists(assets):
    p = assets / "data/level_sprites.json"
    j = json.loads(p.read_text())
    j["art_lists"][0][1]["tile"] += 1
    p.write_text(json.dumps(j, indent=1), newline="\n")
    return "level_sprites art_lists[0][1].tile +1"


def rom_level_art(rom):
    """LevelData +18 of each level, bits 0-6: which of loc_283EE's lists
    LoadLevelData decompresses. Bit 7 is the plane-B parallax flag,
    levels.json's (extra_art), not the people's."""
    return [rec[18] & 0x7F for rec in level_records(rom)]


def pack_level_art(assets, rom, docs):
    """level_sprites.json's "art_list" for each level (the last entry for
    a level; 0 for a level with none), as npc_spawn takes it."""
    _, art = level_sprite_lists(assets, docs)
    out = Packed()
    for level in range(len(level_records(rom))):
        v = art.get(level, 0)
        if not 0 <= v <= 0x7F:
            out.errors.append(f"level {level}: art list {v} past 7 bits")
        out.byte(v)
    return out


def edit_level_art(assets):
    p = assets / "data/level_sprites.json"
    j = json.loads(p.read_text())
    j["levels"][5]["art_list"] += 1
    p.write_text(json.dumps(j, indent=1), newline="\n")
    return "level_sprites levels[5].art_list +1"


DIALOGUE = (("after_climatrol", "loc_E6B0"), ("before_climatrol", "loc_E70C"),
            ("later", "loc_E730"))


def rom_dialogue(rom):
    out = []
    for _, label in DIALOGUE:
        out += rom.region(label, next_label(rom, label)).table
    return out


def pack_dialogue(assets, rom, docs):
    """npc.c read_bytes over "after_climatrol", "before_climatrol" and
    "later": each element's number cut to a byte (anything but a number
    reads as 0), at most 256 a table. The three tables of dialogue
    numbers ObjType_People reads (loc_E6B0, loc_E70C, loc_E730)."""
    d = docs.get(assets, "data/people_dialogue.json")
    out = Packed()
    for key, _ in DIALOGUE:
        for v in (jget(d.root, key) or [])[:256]:
            out.byte(int(v) if is_num(v) else 0)
    return out


def edit_dialogue(assets):
    p = assets / "data/people_dialogue.json"
    j = json.loads(p.read_text())
    j["later"][3] += 1
    p.write_text(json.dumps(j, indent=1), newline="\n")
    return "people_dialogue later[3] +1"



# The object types: each one's init (tile word, mappings, $2C box, $28
# first wait, 2(a0) flags) and its sprite mappings. The mappings are ROM
# tables; the rest are operands of the init's instructions, read fresh
# from ps2.asm and laid out here as a table of their own.

NPC_TYPES_MAX, NPC_FRAMES, NPC_TYPE_PIECES = 16, 20, 2
SCENE_TYPES_MAX, NPC_SCENE_FRAMES, NPC_SCENE_PIECES = 32, 12, 47
CHEST_TYPES_MAX, NPC_CHEST_PIECES = 4, 2
BOX_PERSON, WAIT_FRAMES = 4, None       # WAIT_FRAMES: read below

# npc.c's names for the wandering routines, and the ROM routine each one
# ports (the object's second branch).
WANDER_ROUTINE = {"vertical": "loc_3C06", "horizontal": "loc_3DCA",
                  "four": "loc_42F8", "dezolian": "loc_402C",
                  "muskcat": "loc_45A0"}


def object_table(rom):
    """ObjectTable's branch targets: object n is the n-th, from 1."""
    out = []
    for line in rom.lines[rom.asm.label_line["ObjectTable"] + 1:]:
        m = re.match(r"^(?:\w+:)?\s*bra\.w\s+(\w+)", line)
        if not m:
            if out and line.strip() and not line.lstrip().startswith(";"):
                break
            continue
        out.append(m.group(1))
    return out


def object_branches(rom, obj):
    """The object's routine table: `jsr X(pc,d0.w)` in the routine's first
    lines, and X's bra.w targets in order."""
    routine = object_table(rom)[obj - 1]
    start = rom.asm.label_line[routine]
    for line in rom.lines[start:start + 10]:
        m = re.search(r"jsr\s+\(?(\w+)\(pc", line)
        if m:
            break
    else:
        raise AsmDataError(f"object ${obj:02X} ({routine}): no routine table")
    out = []
    for line in rom.lines[rom.asm.label_line[m.group(1)] + 1:]:
        b = re.match(r"^\s*bra\.w\s+(\w+)", line)
        if b:
            out.append(b.group(1))
        elif out and line.strip() and not line.lstrip().startswith(";"):
            break
    return out


def init_ops(rom, obj):
    """The object's init (its routine table's first branch), to the next
    label or bra/rts: {destination: [operand values, in order]}."""
    init = object_branches(rom, obj)[0]
    ops = {}
    for line in rom.lines[rom.asm.label_line[init] + 1:]:
        code = line.split(";")[0]
        if re.match(r"^[A-Za-z_]\w*:", code) or re.match(r"^\s*(bra|rts)",
                                                            code):
            break
        m = re.match(r"^\s*(move|cmpi)\.[bwl]\s+#([^,]+),\s*(.+?)\s*$",
                     code)
        if m:
            dest = m.group(1) + " " + re.sub(r"\)\.[wl]$", ")",
                                             m.group(3).replace(" ", ""))
            ops.setdefault(dest, []).append(rom.eval(m.group(2)))
    return ops


def mapping_frames(rom, table):
    """A sprite mapping table as the frames its offset words name, in
    their order: each the piece count and five bytes a piece (y, size,
    tile word, x). The offsets themselves are layout, not packed."""
    out = []
    for label in offset_table(rom, table):
        count = rom.cells_from(label, 1)[0]
        out += rom.cells_from(label, 1 + 5 * count)
    return out


def pack_frames(frames, max_frames, max_pieces, where, out):
    """read_piece over each frame (npc.c): "x", "y" as s8, "w", "h" (1 if
    absent), "tile", "flip_h", "flip_v"; the loader keeps max_frames
    frames of max_pieces pieces."""
    for f, fr in enumerate((frames if isinstance(frames, JArr) else [])
                           [:max_frames]):
        pieces = (fr if isinstance(fr, JArr) else [])[:max_pieces]
        out.byte(len(pieces))
        for p in pieces:
            w, h = u8(jint(p, "w", 1)), u8(jint(p, "h", 1))
            tile = u16(jint(p, "tile", 0))
            if not (1 <= w <= 4 and 1 <= h <= 4) or tile > 0x7FF:
                out.errors.append(f"{where} frame {f}: a piece the mapping "
                                  "can't hold")
            word = (tile & 0x7FF) | (0x800 if jbool(p, "flip_h", False)
                                     else 0) \
                | (0x1000 if jbool(p, "flip_v", False) else 0)
            out.byte(jint(p, "y", 0))
            out.byte(((w - 1) & 3) << 2 | ((h - 1) & 3))
            out.word(word)
            out.byte(jint(p, "x", 0))


def type_entries(root, cap):
    """The types a loader keeps (the first `cap`), and of those the first
    for each object id -- what type_of / scene_of / chest_of find."""
    kept = (root if isinstance(root, JArr) else [])[:cap]
    first = {}
    for t in kept:
        first.setdefault(jint(t, "object", 0), t)
    return first


def ptr(label, width=4):
    return [PtrCell(Sym(label, 0), width, k) for k in range(width)]


def rom_one_op(ops, dest, default=0):
    vals = ops.get(dest)
    return vals[0] if vals else default


def word_cells(v):
    return [(v >> 8) & 0xFF, v & 0xFF]


def rom_people_types(rom, objects):
    out = []
    for obj in objects:
        ops = init_ops(rom, obj)
        branches = object_branches(rom, obj)
        maps = ops.get("move 4(a0)", [])
        out += word_cells(rom_one_op(ops, "move 8(a0)") & 0xFFFF)
        out += word_cells(rom_one_op(ops, "move $28(a0)") & 0xFFFF)
        out += ptr(branches[1])
        out += mapping_frames(rom, maps[0].label)
    return out


def pack_people_types(assets, rom, docs):
    """npc.c npc_load_tables: npc_types.json's first 16 entries; for an
    object the first with its id. Per type, in the file's order: the tile
    word (palette_line << 13 | tile), "first_wait", "wander" as the
    routine it names (vertical loc_3C06, horizontal loc_3DCA, four
    loc_42F8, dezolian loc_402C, muskcat loc_45A0; anything else
    vertical), then the frames (20 of up to two pieces)."""
    d = docs.get(assets, "data/npc_types.json")
    out = Packed()
    for obj, t in type_entries(d.root, NPC_TYPES_MAX).items():
        line, tile = jint(t, "palette_line", 0), jint(t, "tile", 0)
        out.word(u16((line & 3) << 13 | (tile & 0x7FF)))
        out.word(u16(jint(t, "first_wait", 0x20)))
        wander = jstr(t, "wander", "")
        out.cells += ptr(WANDER_ROUTINE.get(wander,
                                            WANDER_ROUTINE["vertical"]))
        pack_frames(jget(t, "frames"), NPC_FRAMES, NPC_TYPE_PIECES,
                    f"object ${obj:02X}", out)
    return out


def people_objects(assets, docs):
    d = docs.get(assets, "data/npc_types.json")
    return list(type_entries(d.root, NPC_TYPES_MAX))


def edit_people_types(assets):
    p = assets / "data/npc_types.json"
    j = json.loads(p.read_text())
    j[1]["frames"][2][0]["x"] += 1
    p.write_text(json.dumps(j, indent=1), newline="\n")
    return f"npc_types[1].frames[2][0].x +1"


def rom_scene_types(rom, objects):
    out = []
    for obj in objects:
        ops = init_ops(rom, obj)
        out += word_cells(rom_one_op(ops, "move 8(a0)") & 0xFFFF)
        out.append(rom_one_op(ops, "move $2C(a0)") & 0xFF)
        out.append((rom_one_op(ops, "move 2(a0)") >> 5) & 1)
        out += mapping_frames(rom, ops["move 4(a0)"][0].label)
    return out


def pack_scene_types(assets, rom, docs):
    """npc.c load_scene: the first 32 entries; per object the first. The
    tile word, "box" (0 if outside 0-13), "front" as bit 5 of the init's
    2(a0) byte, then the frames (12 of up to 47 pieces)."""
    d = docs.get(assets, "data/scene_objects.json")
    out = Packed()
    for obj, t in type_entries(d.root, SCENE_TYPES_MAX).items():
        line, tile = jint(t, "palette_line", 0), jint(t, "tile", 0)
        out.word(u16((line & 3) << 13 | (tile & 0x7FF)))
        box = jint(t, "box", 0)
        out.byte(box if 0 <= box < 14 else 0)
        out.byte(1 if jbool(t, "front", False) else 0)
        pack_frames(jget(t, "frames"), NPC_SCENE_FRAMES, NPC_SCENE_PIECES,
                    f"object ${obj:02X}", out)
    return out


def edit_scene_types(assets):
    p = assets / "data/scene_objects.json"
    j = json.loads(p.read_text())
    j[0]["tile"] += 1
    p.write_text(json.dumps(j, indent=1), newline="\n")
    return "scene_objects[0].tile (Darum) +1"


def rom_chest_types(rom, objects):
    out = []
    for obj in objects:
        ops = init_ops(rom, obj)
        maps = ops["move 4(a0)"]
        out += word_cells(rom_one_op(ops, "move 8(a0)") & 0xFFFF)
        out.append(rom_one_op(ops, "move $2C(a0)", BOX_PERSON) & 0xFF)
        out += mapping_frames(rom, maps[0].label)
        alt = ops.get("cmpi ($FFFFF710)")
        if alt and len(maps) > 1:
            out.append(alt[0] & 0xFF)
            out += mapping_frames(rom, maps[1].label)
        else:
            out.append(0xFF)
    return out


def pack_chest_types(assets, rom, docs):
    """npc.c load_chests: "types", the first 4; per object the first. The
    tile word, "box" (BOX_PERSON if absent or outside 0-13), the two
    frames of up to two pieces, then "alt_level_type" (-1, packed $FF,
    if absent) and, with one, "alt_frames" -- the Motavian chest's own
    for that level type (the init's `cmpi.w #n, ($FFFFF710)`)."""
    d = docs.get(assets, "data/chests.json")
    out = Packed()
    for obj, t in type_entries(jget(d.root, "types"), CHEST_TYPES_MAX).items():
        line, tile = jint(t, "palette_line", 0), jint(t, "tile", 0)
        out.word(u16((line & 3) << 13 | (tile & 0x7FF)))
        box = jint(t, "box", BOX_PERSON)
        out.byte(box if 0 <= box < 14 else BOX_PERSON)
        pack_frames(jget(t, "frames"), 2, NPC_CHEST_PIECES,
                    f"chest object ${obj:02X}", out)
        alt = jint(t, "alt_level_type", -1)
        out.byte(alt)
        if alt >= 0:
            pack_frames(jget(t, "alt_frames"), 2, NPC_CHEST_PIECES,
                        f"chest object ${obj:02X} alt", out)
    return out


def edit_chest_types(assets):
    p = assets / "data/chests.json"
    j = json.loads(p.read_text())
    j["types"][1]["alt_frames"][0][0]["y"] += 1
    p.write_text(json.dumps(j, indent=1), newline="\n")
    return "chests types[1].alt_frames[0][0].y +1"


# ----------------------------------------------------------------------
# The level records (extract_levels.py; build 131b)
# ----------------------------------------------------------------------
#
# LevelData's 20 bytes a level, as LoadLevelData (ps2.asm:13229) reads
# them: +0 planet (bit 7) and tileset (bits 0-3, also the level type); +1
# the height and width nibbles, $100 pixels each; +2 and +6 the plane B
# and plane A block maps, each under the enemy group byte
# ProcessRandomBattle rolls from (before and after Climatrol); +10 the
# collision table under the flag byte (bits 0-3 the camera clamp, 2 and 3
# the backdrop planes, 4-6 the collision plane); +14 the interaction list
# under the palette byte; +18 the extra-art byte (bit 7 the parallax
# flag, bits 0-6 the people's art list); +19 the music.
#
# The game does not read these from levels/<nn>_<name>.json. load_level
# (game.c) takes the planet, the stem and the tileset's two files from
# levels.json; level_load (level.c) the rest from the level's .tmx map
# properties with a scanner, not an XML parser; walk_begin uses the flag
# byte and bit 7 of the extra-art byte; npc_spawn takes the art list from
# level_sprites.json. The per-level .json is read only when a .tmx has no
# extra_art property, which none lacks.
#
# A pointer packs as the ROM label whose data is what the game reads from
# the level's own file: the block map whose DecompressArt2 output starts
# with the layer's blocks, the collision table the .collision.txt loads
# as, the interaction list the .doors.json loads as. Where two labels hold
# the same data, the record's own is taken -- the same thing to the game.
# The data itself is compared elsewhere at level 2 (the Tiled maps, the
# collision tables, the doors); here, which one each level points at.

LEVEL_BACKDROP_COLS, LEVEL_BACKDROP_ROWS = 16, 8    # level.h
WALK_BLOCKS_MAX = 256                               # walk.h
TMX_GROUP = "levels/<nn>_<name>.tmx"
TMX_BACKDROP_GROUP = "levels/<nn>_<name>_A.tmx, _B.tmx (backdrops)"
LEVEL_JSON_GROUP = "levels/<nn>_<name>.json"
# (layer, LEVEL_FLAG_*_BACKDROP, the record's offset, its group property)
LEVEL_PLANES = (("planeB", 0x04, 2, "enemy_group"),
                ("planeA", 0x08, 6, "enemy_group_late"))


def c_atoi(text):
    """atoi: leading blanks, a sign, digits; 0 when there are none."""
    m = re.match(r"[ \t\n\r\f\v]*([+-]?[0-9]+)", text)
    return int(m.group(1)) if m else 0


class TmxDoc:
    """A Tiled map as level.c reads it -- tmx_property_int and tmx_layer,
    strstr and atoi, not XML -- with a record of the properties and layers
    asked for, so the report lists them beside the JSON files."""

    def __init__(self, path, group):
        self.path = Path(path)
        self.group = group
        self.reads = set()
        try:
            self.text = self.path.read_text()
        except OSError:
            self.text = None        # slurp's NULL: the level does not load

    def present(self):
        if self.text is None:
            return set()
        out = set(re.findall(r'<property name="([^"]*)"', self.text))
        out |= {f"layer {n}" for n in
                re.findall(r'<layer\b[^>]*\bname="([^"]*)"', self.text)}
        return out

    def _attr_int(self, at, name, fallback):
        """tmx_attr_int: the first `name="` from `at` on."""
        hit = self.text.find(f'{name}="', at)
        return c_atoi(self.text[hit + len(name) + 2:]) if hit >= 0 \
            else fallback

    def prop(self, name, fallback):
        """tmx_property_int: the first `<property name="X"`, its value."""
        self.reads.add(name)
        if self.text is None:
            return fallback
        hit = self.text.find(f'<property name="{name}"')
        return self._attr_int(hit, "value", fallback) if hit >= 0 \
            else fallback

    def layer(self, name):
        """tmx_layer: (width, height, block ids) or None -- the first
        `name="X"`, its width and height, the numbers after `<data...>`,
        each gid less one (0 stays 0), Tiled's flip bits masked off."""
        self.reads.add(f"layer {name}")
        t = self.text
        if t is None:
            return None
        at = t.find(f'name="{name}"')
        if at < 0:
            return None
        w, h = self._attr_int(at, "width", 0), self._attr_int(at, "height", 0)
        if w <= 0 or h <= 0:
            return None
        data = t.find("<data", at)
        data = t.find(">", data) if data >= 0 else -1
        if data < 0:
            return None
        pos, out, digits = data + 1, bytearray(), re.compile(r"[0-9]+")
        for _ in range(w * h):
            while pos < len(t) and t[pos] in ",\n\r \t":
                pos += 1
            m = digits.match(t, pos)
            if m is None:
                return None
            gid = int(m.group(0)) & 0x1FFFFFFF
            out.append(((gid - 1) if gid else 0) & 0xFF)
            pos = m.end()
        return w, h, bytes(out)


def _scan_token(line, pos, width):
    """sscanf's %Ns: skip blanks, then up to `width` non-blanks."""
    while pos < len(line) and line[pos] in " \t\n\r\f\v":
        pos += 1
    end = pos
    while end < len(line) and end - pos < width \
            and line[end] not in " \t\n\r\f\v":
        end += 1
    return (line[pos:end], end) if end > pos else (None, pos)


def game_collision(path):
    """walk.c load_table: WALK_BLOCKS_MAX bytes, zeroed; each line not
    starting '#' that sscanf("%x %4s %4s") reads sets its block's byte:
    bit q for a '#' at q in the first word, bit 4+q in the second. None
    when the file is missing."""
    try:
        text = Path(path).read_text()
    except OSError:
        return None
    table = [0] * WALK_BLOCKS_MAX
    for line in text.split("\n"):
        line = line[:127]           # assetfs_next_line into line[128]
        if line.startswith("#"):
            continue
        m = re.match(r"[ \t\r\f\v]*[+-]?(?:0[xX])?([0-9A-Fa-f]+)", line)
        if m is None:
            continue
        solid, pos = _scan_token(line, m.end(), 4)
        special, _ = _scan_token(line, pos, 4) if solid else (None, pos)
        block = int(m.group(1), 16)
        if special is None or block >= WALK_BLOCKS_MAX:
            continue
        byte = 0
        for q in range(4):
            if q < len(solid) and solid[q] == "#":
                byte |= 1 << q
            if q < len(special) and special[q] == "#":
                byte |= 0x10 << q
        table[block] = byte
    return table


def game_level_entries(assets, docs):
    """load_level's walk of levels.json: for each "levels" entry whose
    "index" is the level's, the planet (u16), the stem "%02x_<name>" and
    the two files, "blocks" and "tiles" (char[64] each); a later entry for
    a level overwrites an earlier one."""
    d = docs.get(assets, "levels/levels.json")
    out = {}
    for lv in (jget(d.root, "levels") or []):
        n = jint(lv, "index", -1)
        if n < 0:
            continue
        files = jget(lv, "files")
        out[n] = {"planet": u16(jint(lv, "planet", 0)),
                  "stem": cstr(f"{n:02x}_{jstr(lv, 'name', '')}", 64),
                  "blocks": cstr(jstr(files, "blocks", ""), 64),
                  "tiles": cstr(jstr(files, "tiles", ""), 64)}
    return out


def rom_level_data(rom):
    """What LevelData's pointers point at, by label in record order: the
    block maps decompressed (the extractor's reader, ps2asm.Asm.run, and
    ps2art -- DecompressArt2's format), the collision tables to the next
    label, the interaction lists to their $FF. Read once a Rom."""
    cached = getattr(rom, "_level_data", None)
    if cached is not None:
        return cached
    import ps2art
    recs = level_records(rom)
    maps, colls, inters = {}, {}, {}
    for rec in recs:
        for _, _, off, _ in LEVEL_PLANES:
            label = rec[off].sym.label
            if label not in maps:
                chunks, _ = ps2art.decompress(rom.asm.run(label))
                maps[label] = b"".join(chunks)
        label = rec[10].sym.label
        if label not in colls:
            colls[label] = rom.region(label, next_label(rom, label)).table
        label = rec[14].sym.label
        if label not in inters:
            cells = rom.region(label).cells
            n = 0
            while n < len(cells) and cells[n] != 0xFF:
                n += 6
            inters[label] = cells[:n + 1]
    rom._level_data = (maps, colls, inters)
    return rom._level_data


def resolve(own, candidates, same):
    """The label whose data `same` accepts: the record's own first, then
    the first in record order; None when none does."""
    if own in candidates and same(candidates[own]):
        return own
    return next((label for label, data in candidates.items() if same(data)),
                None)


def tileset_slots():
    """The tileset a level's two files were written from (extract_levels,
    asset_names.TILESET_NAMES): file name -> slot of loc_9342."""
    import asset_names
    out = {}
    for n, name in asset_names.TILESET_NAMES.items():
        out[("tiles", f"tileset_{name}.png")] = n
        out[("blocks", f"blocks_{name}.txt")] = n
    return out


def pack_one_level(assets, docs, n, rec, entry, art_list, rom_data, errors):
    """One record, 20 cells: see the section's head."""
    maps, colls, inters = rom_data
    if entry is None:
        errors.append(f"level {n}: no levels.json entry (the level does "
                      "not load)")
        return [0] * LEVEL_RECORD
    stem = entry["stem"]
    tmx = docs.tmx(assets, f"levels/{stem}.tmx", TMX_GROUP)
    if tmx.text is None:
        errors.append(f"{stem}.tmx: missing (the level does not load)")
        return [0] * LEVEL_RECORD

    def byte(name, fallback=0, top=0xFF):
        v = tmx.prop(name, fallback)
        if not 0 <= v <= top:
            errors.append(f"{stem}: {name} {v} does not fit its field "
                          f"(0-{top})")
        return v & top

    if tmx.prop("index", 0) != n:
        errors.append(f"{stem}: index {tmx.prop('index', 0)} in level "
                      f"{n}'s place (the ROM's index is the place)")
    tileset = byte("tileset", top=0xF)
    palette, music, flags = byte("palette"), byte("music"), byte("flags")
    groups = {"enemy_group": byte("enemy_group"),
              "enemy_group_late": byte("enemy_group_late")}
    extra = tmx.prop("extra_art", -1)
    if extra < 0:
        # level.c: a .tmx from before build 46 has no extra_art; the
        # level's .json beside it then gives the byte.
        meta = docs.get(assets, f"levels/{stem}.json")
        meta.group = LEVEL_JSON_GROUP
        extra = jint(meta.root, "extra_art", 0)
    if not 0 <= extra <= 0xFF:
        errors.append(f"{stem}: extra_art {extra} does not fit a byte")
    cols, rows = tmx.prop("cols", 0), tmx.prop("rows", 0)
    for name, v in (("cols", cols), ("rows", rows)):
        if v % 8 or not 8 <= v <= 0x78:
            errors.append(f"{stem}: {name} {v} is not a size nibble's "
                          "(8 to 120 blocks, by 8)")
    if entry["planet"] > 1:
        errors.append(f"{stem}: planet {entry['planet']} (one bit)")
    slots = tileset_slots()
    for kind in ("tiles", "blocks"):
        if slots.get((kind, entry[kind])) != tileset:
            errors.append(f"{stem}: tileset {tileset}, but its {kind} file "
                          f"is {entry[kind] or 'none'} -- the record's one "
                          "nibble picks the level type and the art")

    cells = [(entry["planet"] & 1) << 7 | tileset,
             ((rows // 8) & 0xF) << 4 | ((cols // 8) & 0xF)]
    for name, bit, off, group in LEVEL_PLANES:
        if flags & bit:
            doc = docs.tmx(assets, f"levels/{stem}_{name[5]}.tmx",
                           TMX_BACKDROP_GROUP)
            want = (LEVEL_BACKDROP_COLS, LEVEL_BACKDROP_ROWS)
        else:
            doc, want = tmx, (cols, rows)
        got = doc.layer(name)
        if got is None:
            errors.append(f"{stem}: no {name} layer in {doc.path.name} "
                          "(the level does not load)")
            cells += [0] * 4
            continue
        w, h, ids = got
        if (w, h) != want:
            errors.append(f"{stem}: {name} is {w}x{h}, the record's "
                          f"{want[0]}x{want[1]}")
        label = resolve(rec[off].sym.label, maps,
                        lambda data: data[:len(ids)] == ids
                        and len(data) >= len(ids))
        if label is None:
            errors.append(f"{stem}: {name}'s blocks are none of the ROM's "
                          "level maps")
            cells += [0] * 4
            continue
        cells += ptr_with(label, groups[group])

    table = game_collision(assets / f"levels/{stem}.collision.txt")
    label = None if table is None else resolve(
        rec[10].sym.label, colls,
        lambda data: table[:len(data)] == data and not any(table[len(data):]))
    if label is None:
        why = "missing" if table is None else "none of the ROM's"
        errors.append(f"{stem}: its collision table is {why}")
        cells += [0] * 4
    else:
        cells += ptr_with(label, flags)

    doors = game_door_list(assets, docs, stem, [])
    label = resolve(rec[14].sym.label, inters, lambda data: data == doors)
    if label is None:
        errors.append(f"{stem}: its doors are none of the ROM's interaction "
                      "lists")
        cells += [0] * 4
    else:
        cells += ptr_with(label, palette)
    cells += [(extra & 0x80) | (art_list & 0x7F), music]
    return cells


def ptr_with(label, top):
    """A long `top << 24 | label`, as the records write `$41000000|loc_X`."""
    return [PtrCell(Sym(label, (top & 0xFF) << 24), 4, k) for k in range(4)]


def rom_level_records(rom):
    return [c for rec in level_records(rom) for c in rec]


def pack_level_records(assets, rom, docs):
    """Every level in the ROM's order: load_level's levels.json entry, the
    .tmx's properties and layers as level_load reads them, the collision
    table and doors as enter_level_walk loads them, and the people's art
    list (npc_spawn) for bits 0-6 of +18 -- 20 bytes a level. The tmx's
    extra_art is read whole, but only its bit 7 is used (walk_begin);
    its bits 0-6 come from level_sprites.json, as they do in the game. An
    index that is not the level's place, a size that is not a nibble's, a
    planet past one bit, tileset files that are another tileset's than the
    level type, a plane of another size than the record's, or a file whose
    data is none of the ROM's fails."""
    entries = game_level_entries(assets, docs)
    _, art = level_sprite_lists(assets, docs)
    rom_data = rom_level_data(rom)
    out = Packed()
    for n, rec in enumerate(level_records(rom)):
        out.cells += pack_one_level(assets, docs, n, rec, entries.get(n),
                                    art.get(n, 0), rom_data, out.errors)
    return out


def edit_level_records(assets):
    p = assets / "levels/04_Paseo.tmx"
    text = p.read_text()
    old = '<property name="palette" type="int" value="5"/>'
    assert old in text
    p.write_text(text.replace(old, old.replace('"5"', '"6"'), 1),
                 newline="\n")
    return "04_Paseo.tmx palette 5 -> 6"


# ----------------------------------------------------------------------
# The fight's tables (extract_battle.py; build 131c) -- data/battle.json
# ----------------------------------------------------------------------
#
# battle.c battle_tables_load reads the whole file once. What it keeps,
# and the ROM tables it was made from:
#
#   enemies        EnemyData, 64 bytes a record, by "id" (i + 1 if absent);
#                  the name into EnemyNames' ten codes (win_charset, $C4
#                  after a short name)
#   formations     EnemyBattleFormationData, five bytes, at most 260
#   groups         EnemyFormationTable, eight bytes, at most 96
#   regions        the encounter zones (loc_23C3A .. loc_23DBA)
#   enemy_x        EnemyXPosArray, five words a layout, at most 16
#   char_pos       Battle_CharCoordinates, eight words
#   char_sprites   Battle_CharObjectPtrs: map (palette line in bits 29-30)
#                  and art, eight
#   maps, effect_maps, sprites
#                  every mapping table they name, by its label: frames of
#                  sprite pieces (BuildSprites, loc_526) or nametable
#                  blocks (loc_572)
#   anim_tables    the eight animation-script tables Battle_AnimateSprites
#                  runs
#   weapon_props   WeaponProperties, 12 bytes, at most 48
#   tech_props     TechniqueProperties, 10 bytes, at most 56
#   effect_art     WeaponTechArtPtrs' lists: art and VRAM address, two a
#                  list at most
#   success_rate, target_chances, hit_anim
#                  EnemyTechSuccessRate, Battle_CharTargetChances, loc_34EE
#
# Where a ROM table's size is set by what the file holds -- a mapping
# frame, a script -- the ROM side is read at the packed side's length
# (Packed.rom_side), so a frame of the wrong kind or a count that differs
# shows as a difference, never as a shorter read.

BATTLE_JSON = "data/battle.json"
ENEMY_MAX, FORMATION_MAX, GROUP_MAX, MAP_SETS_MAX = 128, 260, 96, 96
CHAR_MAPS, WEAPON_MAX, TECH_MAX_PROPS, EFFECT_ART_MAX = 8, 48, 56, 32
PIECES_MAX, BATTLE_NAME_LEN, TXT_END = 40, 10, 0xC4    # battle.h (131c: 16)
ENEMY_RECORD = 64
# battle.c's region keys, each with its loader cap, and the ROM label and
# length of the table it is: the two "late" tables are loc_23CFA's halves
# (the scooter's offset adds to the late one's, ProcessRandomBattle).
REGIONS = (("motavia", 96, "loc_23C3A", 0), ("motavia_scooter", 96,
                                              "loc_23C9A", 0),
           ("motavia_late", 96, "loc_23CFA", 0),
           ("motavia_late_scooter", 96, "loc_23CFA", 0x60),
           ("skure", 64, "loc_23DBA", 0))
# battle.c's TABLE_KEY order and the ROM table each key's scripts come from.
ANIM_TABLES = (("idle", "loc_2601A"), ("enemy_attack", "loc_26118"),
               ("enemy_tech", "loc_26216"), ("enemy_child_a", "loc_26314"),
               ("enemy_child_b", "loc_26414"),
               ("chars", "CharBattle_AnimOffsets"),
               ("effect", "loc_27364"), ("effect2", "loc_27226"))
ANIM_SCRIPT_MAX = 256


def win_charset(ch):
    """window.c win_charset: the digits, the letters, eight marks; 0 for
    anything else."""
    if "0" <= ch <= "9":
        return 1 + ord(ch) - ord("0")
    if "A" <= ch <= "Z":
        return 11 + ord(ch) - ord("A")
    if "a" <= ch <= "z":
        return 37 + ord(ch) - ord("a")
    return {",": 0x3F, ".": 0x40, ";": 0x41, '"': 0x42, "?": 0x43,
            "!": 0x44, "'": 0x45, "-": 0x46, ":": 0x77}.get(ch, 0)


def c_strtoul16(text):
    """strtoul(s, NULL, 16) cut to a u16."""
    m = re.match(r"\s*([+-]?)(?:0[xX])?([0-9A-Fa-f]+)", text)
    if m is None:
        return 0
    v = int(m.group(2), 16)
    return u16(-v if m.group(1) == "-" else v)


def jnum_at(arr, i, where, errors):
    """`json_at(arr, i)->number`: a number's value, 0 for another kind of
    value; a missing element is a NULL the loader dereferences -- a crash,
    reported."""
    v = jat(arr, i)
    if v is None:
        errors.append(f"{where}[{i}] missing: the loader reads it "
                      "unchecked (a crash)")
        return 0
    return int(v) if is_num(v) else 0


def jbytes(arr, cap):
    """battle.c read_bytes: json_length elements, at most `cap`, each cut
    to a byte; the rest of the loader's array stays 0."""
    out = [0] * cap
    for i in range(min(jlen(arr), cap)):
        v = jat(arr, i)
        out[i] = u8(int(v)) if is_num(v) else 0
    return out


def battle_root(assets, docs):
    return docs.get(assets, BATTLE_JSON).root


def battle_map_sets(assets, docs):
    """read_maps over "maps", "effect_maps", "sprites" in that order: each
    member a set keyed by its label (char[40]), at most 96 sets in all;
    map_index finds the first with a label. {label: frames}."""
    doc = docs.get(assets, BATTLE_JSON)
    root = doc.root
    out, count = {}, 0
    for key in ("maps", "effect_maps", "sprites"):
        group = jget(root, key)
        doc.reads.add(f"{key}.<map set>")    # read_maps walks the members
        for label, frames in (group if isinstance(group, JObj) else []):
            if count >= MAP_SETS_MAX:
                break
            count += 1
            out.setdefault(cstr(label, 40), frames)
    return out


def rom_offset_entries(rom, table):
    """A `dc.w X-table` / `dc.w (X+N)-table` offset table: [(label, N)],
    one a line, to the first line that is not one."""
    out = []
    pat = re.compile(r"^\s*dc\.w\s+\(?(\w+)(?:\+(\$[0-9A-Fa-f]+|\d+))?\)?\s*-\s*"
                     + re.escape(table) + r"\b")
    for line in rom.lines[rom.asm.label_line[table] + 1:]:
        m = pat.match(line)
        if m:
            add = m.group(2) or "0"
            out.append((rom.label(m.group(1)),
                        int(add[1:], 16) if add.startswith("$")
                        else int(add)))
            continue
        if line.strip() and not line.strip().startswith(";"):
            break
    return out


def rom_bytes_at(rom, label, addend, n):
    """n cells from label + addend, None past the end of what the reader
    can assemble (never equal to a packed cell)."""
    cells = rom.cells_from(label, addend + n)[addend:]
    return cells + [None] * (n - len(cells))


def rom_frame_length(rom, label, addend):
    """A frame's length read the extractor's way, for a ROM frame the file
    has no counterpart for: a count byte below $80 is sprite pieces, else a
    nametable block."""
    head = rom.cells_from(label, addend + 6)[addend:]
    if head and isinstance(head[0], int) and 0 < head[0] < 0x80:
        return 1 + 5 * head[0]
    if len(head) >= 6:
        return 6 + 2 * (head[4] + 1) * (head[5] + 1)
    return 0


def s8_ok(v):
    return -128 <= v <= 127


def pack_frame(frame, where, out):
    """read_frame: {pieces: [...]} or a bare list of pieces -- each "y",
    "size", "tile", "x" (s16, u8, u16, s16), at most 16 -- packed as the
    count and five bytes a piece; else a nametable block {x, y, w, h,
    cells} -- x and y words, w - 1 and h - 1, h rows of w words. Returns
    the frame's cells."""
    cells = []
    pieces = jget(frame, "pieces")
    if pieces is None and isinstance(frame, JArr):
        pieces = frame
    if pieces is not None:
        n = min(jlen(pieces), PIECES_MAX)
        cells.append(n)
        for k in range(n):
            p = jat(pieces, k)
            y, x = jint(p, "y", 0), jint(p, "x", 0)
            if not (s8_ok(y) and s8_ok(x)):
                out.errors.append(f"{where}: piece {k} at {x},{y}, past a "
                                  "signed byte")
            tile = u16(jint(p, "tile", 0))
            cells += [u8(y), u8(jint(p, "size", 0)), tile >> 8, tile & 0xFF,
                      u8(x)]
        return cells
    x, y = u16(jint(frame, "x", 0)), u16(jint(frame, "y", 0))
    w, h = u8(jint(frame, "w", 0)), u8(jint(frame, "h", 0))
    if w == 0 or h == 0:
        out.errors.append(f"{where}: a block {w}x{h} (the ROM's is w - 1 "
                          "by h - 1)")
    cells += [x >> 8, x & 0xFF, y >> 8, y & 0xFF, u8(w - 1), u8(h - 1)]
    rows = jget(frame, "cells")
    for r in range(h):
        row = jat(rows, r)
        for c in range(w):
            v = jat(row, c)
            word = u16(int(v)) if is_num(v) else 0
            cells += [word >> 8, word & 0xFF]
    return cells


def pack_battle_maps(assets, rom, docs):
    """Every map set the game keeps, in the file's order: each frame as
    read_frame holds it, in the order of the ROM table's offset words (the
    offsets are layout, not packed). A set whose label is no ROM label, or
    a frame count other than the ROM table's, fails."""
    out = Packed()
    shape = []
    for label, frames in battle_map_sets(assets, docs).items():
        lens = []
        for f in range(jlen(frames)):
            cells = pack_frame(jat(frames, f), f"{label} frame {f}", out)
            out.cells += cells
            lens.append(len(cells))
        if rom.label(label) is None:
            out.errors.append(f"map set {label}: no such ROM label")
        shape.append((label, lens))

    def rom_side(rom):
        cells = []
        for label, lens in shape:
            if rom.label(label) is None:
                cells += [None] * sum(lens)
                continue
            entries = rom_offset_entries(rom, rom.label(label))
            for i, (flabel, add) in enumerate(entries):
                n = lens[i] if i < len(lens) else rom_frame_length(
                    rom, flabel, add)
                cells += rom_bytes_at(rom, flabel, add, n)
        return cells
    out.rom_side = rom_side
    return out


def edit_battle_maps(assets):
    p = assets / BATTLE_JSON
    j = json.loads(p.read_text())
    j["effect_maps"]["Map_TechFoi"][0]["pieces"][0]["x"] += 1
    p.write_text(json.dumps(j, indent=1), newline="\n")
    return "effect_maps.Map_TechFoi[0].pieces[0].x +1"


def battle_art_labels(rom):
    """The enemy art's PNG name -> the Battle_*Art labels written under it:
    a binclude's file stem by asset_names.art_name, inline art named from
    its label (battle_<label less Battle_ and Art, lower case>_art), as
    extract_battle's art_file_names names them."""
    import asset_names
    out = {}
    for label, line_no in rom.asm.label_line.items():
        if not (label.startswith("Battle_") and label.endswith("Art")):
            continue
        m = re.search(r'binclude\s+"([^"]*)"', rom.lines[line_no])
        if m:
            stem = m.group(1).replace("\\", "/").split("/")[-1]
            name = asset_names.art_name(stem.rsplit(".", 1)[0])
        else:
            name = "battle_" + label[len("Battle_"):-len("Art")].lower() \
                + "_art"
        out.setdefault(name, []).append(label)
    return out


def battle_enemies(assets, docs, errors):
    """battle_tables_load's g_enemy: {id: entry}, the last entry for an id
    (a later one overwrites), ids from 0 up to count - 1."""
    enemies = jget(battle_root(assets, docs), "enemies")
    found, count = {}, 0
    for i in range(jlen(enemies)):
        if i + 1 >= ENEMY_MAX:
            break
        e = jat(enemies, i)
        eid = jint(e, "id", i + 1)
        if not 0 <= eid < ENEMY_MAX:
            errors.append(f"enemies[{i}]: id {eid} outside g_enemy "
                          f"(0-{ENEMY_MAX - 1}), written unchecked")
            continue
        found[eid] = e
        count = max(count, eid + 1)
    return found, count


def rom_enemy_records(rom):
    start = rom.asm.label_line["EnemyData"]
    end = next(rom.lines[j].split(":")[0].strip() for j in rom.asm.marks
               if j > start and not rom.lines[j].startswith("Enemy_"))
    return rom.region("EnemyData", end).cells


def pack_battle_enemies(assets, rom, docs):
    """g_enemy[1 .. count - 1], 64 bytes each as loc_8D4 reads EnemyData:
    the 15 palette words ("palette", hex strings by strtoul, 0 for
    anything else), the art (the Battle_*Art label its "art_file" PNG was
    written from), exp, meseta, attack, defense (u16), type, agility (u8),
    hp (u16), technique, technique_rate, success_index, escape, object
    (default $F), anim_duration, the four anim_bytes, sfx_attack,
    sfx_technique (u8), the map (the set "map" names, by label), anim
    (u8). The last byte is a hole."""
    out = Packed()
    found, count = battle_enemies(assets, docs, out.errors)
    sets = battle_map_sets(assets, docs)
    arts = battle_art_labels(rom)
    for eid in range(1, count):
        e = found.get(eid)
        where = f"enemy {eid}"
        pal = jget(e, "palette")
        for k in range(15):
            v = jat(pal, k)
            out.word(c_strtoul16(v) if isinstance(v, str) else 0)
        art = cstr(jstr(e, "art_file", ""), 32)
        labels = arts.get(art, [])
        if len(labels) != 1:
            out.errors.append(f"{where}: art {art or '(none)'} is "
                              f"{'no ROM art' if not labels else 'several'}")
            out.cells += [0] * 4
        else:
            out.cells += ptr(labels[0])
        for k in ("exp", "meseta", "attack", "defense"):
            out.word(u16(jint(e, k, 0)))
        out.byte(jint(e, "type", 0))
        out.byte(jint(e, "agility", 0))
        out.word(u16(jint(e, "hp", 0)))
        for k, d in (("technique", 0), ("technique_rate", 0),
                     ("success_index", 0), ("escape", 0), ("object", 0xF),
                     ("anim_duration", 0)):
            out.byte(jint(e, k, d))
        out.cells += jbytes(jget(e, "anim_bytes"), 4)
        out.byte(jint(e, "sfx_attack", 0))
        out.byte(jint(e, "sfx_technique", 0))
        label = jstr(e, "map", "")
        if label not in sets:
            out.errors.append(f"{where}: map {label or '(none)'} is no map "
                              "set the game keeps")
            out.cells += [0] * 4
        else:
            out.cells += ptr(label)
        out.byte(jint(e, "anim", 0))
        out.hole("loc_8D4 reads 63 bytes of a record; the 64th (the id's "
                 "copy) nothing reads")
    return out


def edit_battle_enemies(assets):
    p = assets / BATTLE_JSON
    j = json.loads(p.read_text())
    j["enemies"][9]["hp"] += 1
    p.write_text(json.dumps(j, indent=1), newline="\n")
    return f"enemies[9].hp ({j['enemies'][9]['name'].strip()}) +1"


def rom_enemy_names(rom):
    return rom.region("EnemyNames", next_label(rom, "EnemyNames")).table


def pack_enemy_names(assets, rom, docs):
    """battle_enemy_name(id): the ten codes battle_tables_load makes of
    "name" (win_charset, $C4 after a short name, zeros); entry 0 is the
    port's own blank (no enemy has id 0), a hole."""
    out = Packed()
    found, count = battle_enemies(assets, docs, [])
    out.hole("no enemy has id 0: battle_enemy_name(0) is the port's own "
             "blank", BATTLE_NAME_LEN)
    for eid in range(1, count):
        name = jstr(found.get(eid), "name", "")
        codes = [win_charset(ch) for ch in name[:BATTLE_NAME_LEN]]
        if len(name) < BATTLE_NAME_LEN:
            codes += [TXT_END] + [0] * (BATTLE_NAME_LEN - len(name) - 1)
        out.cells += codes
    return out


def edit_enemy_names(assets):
    p = assets / BATTLE_JSON
    j = json.loads(p.read_text())
    j["enemies"][2]["name"] = "MOSQUITA"
    p.write_text(json.dumps(j, indent=1), newline="\n")
    return "enemies[2].name MOSQUITO -> MOSQUITA"


def rom_formations(rom):
    return rom.region("EnemyBattleFormationData",
                      next_label(rom, "EnemyBattleFormationData")).table


def pack_formations(assets, rom, docs):
    """g_formation[i] for i < 260: enemy1, count1, enemy2, count2, layout
    (u8)."""
    forms = jget(battle_root(assets, docs), "formations")
    out = Packed()
    for i in range(min(jlen(forms), FORMATION_MAX)):
        f = jat(forms, i)
        for k in ("enemy1", "count1", "enemy2", "count2", "layout"):
            out.byte(jint(f, k, 0))
    return out


def edit_formations(assets):
    p = assets / BATTLE_JSON
    j = json.loads(p.read_text())
    j["formations"][5]["count1"] += 1
    p.write_text(json.dumps(j, indent=1), newline="\n")
    return "formations[5].count1 +1"


def rom_groups(rom):
    return rom.region("EnemyFormationTable",
                      next_label(rom, "EnemyFormationTable")).table


def pack_groups(assets, rom, docs):
    """g_group[i] for i < 96: read_bytes, eight."""
    groups = jget(battle_root(assets, docs), "groups")
    out = Packed()
    for i in range(min(jlen(groups), GROUP_MAX)):
        out.cells += jbytes(jat(groups, i), 8)
    return out


def edit_groups(assets):
    p = assets / BATTLE_JSON
    j = json.loads(p.read_text())
    j["groups"][3][2] += 1
    p.write_text(json.dumps(j, indent=1), newline="\n")
    return "groups[3][2] +1"


def rom_regions(rom):
    return [c for _, cap, label, off in REGIONS
            for c in rom.cells_from(label, off + cap)[off:]]


def pack_regions(assets, rom, docs):
    """g_region: each of the five lists by read_bytes, cut at its cap
    (96, 96, 96, 96, 64 -- the ROM tables' sizes)."""
    regions = jget(battle_root(assets, docs), "regions")
    out = Packed()
    for key, cap, _, _ in REGIONS:
        out.cells += jbytes(jget(regions, key), cap)
    return out


def edit_regions(assets):
    p = assets / BATTLE_JSON
    j = json.loads(p.read_text())
    j["regions"]["motavia_late"][7] += 1
    p.write_text(json.dumps(j, indent=1), newline="\n")
    return "regions.motavia_late[7] +1"


def rom_enemy_x(rom):
    return rom.region("EnemyXPosArray", next_label(rom, "EnemyXPosArray")).table


def pack_enemy_x(assets, rom, docs):
    """g_enemy_x[i] for i < 16, five words (a missing one 0)."""
    rows = jget(battle_root(assets, docs), "enemy_x")
    out = Packed()
    for i in range(min(jlen(rows), 16)):
        row = jat(rows, i)
        for k in range(5):
            v = jat(row, k)
            out.word(u16(int(v)) if is_num(v) else 0)
    return out


def edit_enemy_x(assets):
    p = assets / BATTLE_JSON
    j = json.loads(p.read_text())
    j["enemy_x"][2][1] += 8
    p.write_text(json.dumps(j, indent=1), newline="\n")
    return "enemy_x[2][1] +8"


def rom_char_pos(rom):
    return rom.region("Battle_CharCoordinates",
                      next_label(rom, "Battle_CharCoordinates")).table


def pack_char_pos(assets, rom, docs):
    """g_char_pos[0..7]: each a number or 0."""
    cp = jget(battle_root(assets, docs), "char_pos")
    out = Packed()
    for i in range(8):
        v = jat(cp, i)
        out.word(u16(int(v)) if is_num(v) else 0)
    return out


def edit_char_pos(assets):
    p = assets / BATTLE_JSON
    j = json.loads(p.read_text())
    j["char_pos"][3] += 8
    p.write_text(json.dumps(j, indent=1), newline="\n")
    return "char_pos[3] +8"


def rom_char_sprites(rom):
    return rom.region("Battle_CharObjectPtrs",
                      next_label(rom, "Battle_CharObjectPtrs")).table


def pack_char_sprites(assets, rom, docs):
    """g_char[i] for i < 8: "map" (char[32], a set the game keeps) with
    "palette_line" in bits 29-30 of its long, as Battle_CharObjectPtrs
    writes `$20000000|Map_X`; "art" (char[32], the label char_art makes
    the PNG's name from)."""
    chars = jget(battle_root(assets, docs), "char_sprites")
    sets = battle_map_sets(assets, docs)
    out = Packed()
    for i in range(min(jlen(chars), CHAR_MAPS)):
        c = jat(chars, i)
        m, art = cstr(jstr(c, "map", ""), 32), cstr(jstr(c, "art", ""), 32)
        line = jint(c, "palette_line", 0)
        if not 0 <= line <= 3:
            out.errors.append(f"char_sprites[{i}]: palette line {line}")
        if m not in sets:
            out.errors.append(f"char_sprites[{i}]: {m} is no map set")
        if rom.label(art) is None:
            out.errors.append(f"char_sprites[{i}]: {art} is no ROM label")
        out.cells += [PtrCell(Sym(m, (line & 3) << 29), 4, k)
                      for k in range(4)]
        out.cells += ptr(art)
    return out


def edit_char_sprites(assets):
    p = assets / BATTLE_JSON
    j = json.loads(p.read_text())
    j["char_sprites"][2]["palette_line"] = 2
    p.write_text(json.dumps(j, indent=1), newline="\n")
    return "char_sprites[2].palette_line -> 2"


def script_end(cells):
    """The first $FF or $FE at a command position, or None.
    Battle_AnimateSprites: $F7 takes the byte after it; $FF sends the
    script back to its start (loc_32C2); $FE sets bit 0 of the object's
    byte 2 (loc_32EE), and RunObject answers that by clearing the object
    (loc_3DC, loc_6D38). Nothing after either is read by that entry."""
    i = 0
    while i < len(cells):
        if cells[i] in (0xFF, 0xFE):
            return i
        i += 2 if cells[i] == 0xF7 else 1
    return None


SCRIPT_TAIL = ("past a script's $FF or $FE: Battle_AnimateSprites goes back "
               "to its start (loc_32C2) or the object is cleared (loc_32EE, "
               "loc_3DC), and nothing reads on; the extractor keeps 256 "
               "bytes from the label")


def pack_anim_scripts(assets, rom, docs):
    """read_anim_table for each of the eight tables: "index" in order,
    each key's script from "scripts" (at most 256 bytes, read_bytes). Per
    table, the offset words (a key names label or label+N; its offset is
    that place less the table's), then each entry's script to its $FF --
    the bytes held after that are holes."""
    tables = jget(battle_root(assets, docs), "anim_tables")
    out = Packed()
    shape = []
    for key, table in ANIM_TABLES:
        t = jget(tables, key)
        if t is None:
            out.errors.append(f"anim_tables.{key} missing (the game has "
                              "no scripts there)")
            shape.append((table, []))
            continue
        index, scripts = jget(t, "index"), jget(t, "scripts")
        keys = [jat(index, i) for i in range(jlen(index))]
        for k in keys:
            name = k if isinstance(k, str) else ""
            lab, _, add = name.partition("+")
            if rom.label(lab) is None or (add and not add.isdigit()):
                out.errors.append(f"anim_tables.{key}: {name!r} is no ROM "
                                  "place")
                out.word(0)
                continue
            out.word(u16(rom.distance(table, rom.label(lab))
                         + int(add or 0)))
        lens = []
        for k in keys:
            got = jget(scripts, k) if isinstance(k, str) else None
            n = min(jlen(got), ANIM_SCRIPT_MAX) if got is not None else 0
            held = jbytes(got, n)
            end = script_end(held)
            keep = n if end is None else end + 1
            out.cells += held[:keep]
            out.hole(SCRIPT_TAIL, n - keep)
            lens.append(n)
        shape.append((table, lens))

    def rom_side(rom):
        cells = []
        for table, lens in shape:
            entries = rom_offset_entries(rom, table)
            cells += rom.cells_from(table, 2 * len(entries))
            full = rom.region(table).cells
            for i, (lab, add) in enumerate(entries):
                off = rom.distance(table, lab) + add
                n = lens[i] if i < len(lens) else 0
                got = full[off:off + n] if 0 <= off else []
                cells += got + [None] * (n - len(got))
        return cells
    out.rom_side = rom_side
    return out


def edit_anim_scripts(assets):
    p = assets / BATTLE_JSON
    j = json.loads(p.read_text())
    key = j["anim_tables"]["enemy_attack"]["index"][4]
    j["anim_tables"]["enemy_attack"]["scripts"][key][1] ^= 1
    p.write_text(json.dumps(j, indent=1), newline="\n")
    return f"anim_tables.enemy_attack {key}[1] ^1"


def pack_effect_long(p, n, where, sets, out):
    """A property's effect long: art (7 bits) | pal (bit 7) in the top
    byte, over the map set the label names (or no address for "")."""
    art = jint(p, "art" + n, 0)
    pal = jint(p, "pal" + n, 0) != 0
    label = cstr(jstr(p, "map" + n, ""), 32)
    if not 0 <= art <= 0x7F:
        out.errors.append(f"{where}: art{n} {art} past seven bits")
    top = (0x80 if pal else 0) | (art & 0x7F)
    if label == "":
        out.cells += [top, 0, 0, 0]
        return
    if label not in sets:
        out.errors.append(f"{where}: map{n} {label} is no map set the game "
                          "keeps")
    out.cells += [PtrCell(Sym(label, top << 24), 4, k) for k in range(4)]


def props_region(rom, table, prefix):
    start = rom.asm.label_line[table]
    end = next(rom.lines[j].split(":")[0].strip() for j in rom.asm.marks
               if j > start and not rom.lines[j].startswith(prefix))
    return rom.region(table, end).cells


def rom_weapon_props(rom):
    return props_region(rom, "WeaponProperties", "WeaponProp_")


def pack_weapon_props(assets, rom, docs):
    """g_weapon[i] for i < 48: "bytes" 0-2 (kind, spread, sound; read
    unchecked), byte 3 a hole, then the two effect longs ("art", "pal",
    "map"; "art2", "pal2", "map2")."""
    wp = jget(battle_root(assets, docs), "weapon_props")
    sets = battle_map_sets(assets, docs)
    out = Packed()
    for i in range(min(jlen(wp), WEAPON_MAX)):
        w = jat(wp, i)
        b = jget(w, "bytes")
        for k in range(3):
            out.byte(jnum_at(b, k, f"weapon_props[{i}].bytes", out.errors))
        out.hole("the weapon's fourth byte: loc_1F30 steps over it "
                 "(addq.w #1, a6), and the port's loader reads three")
        for n in ("", "2"):
            pack_effect_long(w, n, f"weapon_props[{i}]", sets, out)
    return out


def edit_weapon_props(assets):
    p = assets / BATTLE_JSON
    j = json.loads(p.read_text())
    j["weapon_props"][4]["bytes"][2] += 1
    p.write_text(json.dumps(j, indent=1), newline="\n")
    return "weapon_props[4].bytes[2] (the sound) +1"


def rom_tech_props(rom):
    return props_region(rom, "TechniqueProperties", "TechProp_")


def pack_tech_props(assets, rom, docs):
    """g_tech[i] for i < 56: "bytes" 0-1 (rate, sound; read unchecked),
    then the two effect longs."""
    tp = jget(battle_root(assets, docs), "tech_props")
    sets = battle_map_sets(assets, docs)
    out = Packed()
    for i in range(min(jlen(tp), TECH_MAX_PROPS)):
        t = jat(tp, i)
        b = jget(t, "bytes")
        for k in range(2):
            out.byte(jnum_at(b, k, f"tech_props[{i}].bytes", out.errors))
        for n in ("", "2"):
            pack_effect_long(t, n, f"tech_props[{i}]", sets, out)
    return out


def edit_tech_props(assets):
    p = assets / BATTLE_JSON
    j = json.loads(p.read_text())
    j["tech_props"][6]["art"] += 1
    p.write_text(json.dumps(j, indent=1), newline="\n")
    return "tech_props[6].art +1"


def effect_art_labels(rom):
    """An effect art file -> the labels written under it: a binclude's
    stem by asset_names.art_name, anything else by its own label (as
    extract_battle's effect_art_file)."""
    import asset_names
    out = {}
    for label in rom_effect_art_parts(rom):
        m = re.search(r'binclude\s+"([^"]*)"',
                      rom.lines[rom.asm.label_line[label]])
        if m:
            stem = m.group(1).replace("\\", "/").split("/")[-1]
            name = asset_names.art_name(stem.rsplit(".", 1)[0])
        else:
            name = label
        out.setdefault(name, [])
        if label not in out[name]:
            out[name].append(label)
    return out


def rom_effect_art_parts(rom):
    """Every art label WeaponTechArtPtrs' lists name."""
    out = []
    ptrs = rom.region("WeaponTechArtPtrs",
                      next_label(rom, "WeaponTechArtPtrs")).table
    for k in range(0, len(ptrs), 4):
        cells = rom_effect_art_list(rom, ptrs[k].sym.label)
        for p in range(2, len(cells), 6):
            out.append(cells[p].sym.label)
    return out


def rom_effect_art_list(rom, label):
    """loc_608A's list: the count less one, then a long (the art) and a
    word (the VRAM address) each."""
    head = rom.cells_from(label, 2)
    count = (head[0] << 8 | head[1]) + 1
    return rom.cells_from(label, 2 + 6 * count)


def rom_effect_art(rom):
    ptrs = rom.region("WeaponTechArtPtrs",
                      next_label(rom, "WeaponTechArtPtrs")).table
    return [c for k in range(0, len(ptrs), 4)
            for c in rom_effect_art_list(rom, ptrs[k].sym.label)]


def pack_effect_art(assets, rom, docs):
    """g_effect_art[i] for i < 32: "parts", two at most, each "file"
    (char[32], loaded as assets/tiles/<file>.png) and "vram" (default
    $8000) as a tile, vram / $20. Packed as loc_608A reads a list: the
    count less one, then the art's label and tile * $20 each. The pointer
    table's order is the index; the pointers themselves are layout."""
    ea = jget(battle_root(assets, docs), "effect_art")
    names = effect_art_labels(rom)
    out = Packed()
    for i in range(min(jlen(ea), EFFECT_ART_MAX)):
        parts = jget(jat(ea, i), "parts")
        n = min(jlen(parts), 2)
        out.word(u16(n - 1))
        for k in range(n):
            p = jat(parts, k)
            f = cstr(jstr(p, "file", ""), 32)
            labels = names.get(f, [])
            if len(labels) != 1:
                out.errors.append(f"effect_art[{i}] part {k}: {f} is "
                                  f"{'no ROM art' if not labels else 'several'}")
                out.cells += [0] * 4
            else:
                out.cells += ptr(labels[0])
            vram = jint(p, "vram", 0x8000)
            tile = int(vram / 0x20)
            out.word(u16(tile * 0x20))
    return out


def edit_effect_art(assets):
    p = assets / BATTLE_JSON
    j = json.loads(p.read_text())
    j["effect_art"][3]["parts"][0]["vram"] += 0x20
    p.write_text(json.dumps(j, indent=1), newline="\n")
    return "effect_art[3].parts[0].vram +$20"


FIGHT_BYTES = (("success_rate", 32, "EnemyTechSuccessRate"),
               ("target_chances", 8, "Battle_CharTargetChances"),
               ("hit_anim", 28, "loc_34EE"))


def rom_fight_bytes(rom):
    return [c for _, _, label in FIGHT_BYTES
            for c in rom.region(label, next_label(rom, label)).table]


def pack_fight_bytes(assets, rom, docs):
    """read_bytes into g_success_rate (32), g_target_chances (8),
    g_hit_anim (28)."""
    root = battle_root(assets, docs)
    out = Packed()
    for key, cap, _ in FIGHT_BYTES:
        out.cells += jbytes(jget(root, key), cap)
    return out


def edit_fight_bytes(assets):
    p = assets / BATTLE_JSON
    j = json.loads(p.read_text())
    j["target_chances"][1] += 1
    p.write_text(json.dumps(j, indent=1), newline="\n")
    return "target_chances[1] +1"


# ----------------------------------------------------------------------
# The dissolve order (extract_tables.py; build 136a)
# ----------------------------------------------------------------------

DISSOLVE_JSON = "tables/dissolve_order.json"
DISSOLVE_STEPS = 64             # core/table.h


def rom_dissolve(rom):
    return rom.region("FadeEffectArray",
                      next_label(rom, "FadeEffectArray")).table


def dissolve_coord(v):
    """table.c dissolve_coord: a whole number 0-7, else -1."""
    if not is_num(v) or v < 0 or v > 7 or v != int(v):
        return -1
    return int(v)


def pack_dissolve(assets, rom, docs):
    """table_load_dissolve: "order", exactly 64 positions, each [x, y] with
    x and y whole numbers 0-7, to the byte y * 8 + x; anything else refused
    (the file does not load)."""
    root = docs.get(assets, DISSOLVE_JSON).root
    order = jget(root, "order")
    out = Packed()
    if not isinstance(order, JArr) or jlen(order) != DISSOLVE_STEPS:
        out.errors.append(f'"order" holds {jlen(order)} positions, not '
                          f'{DISSOLVE_STEPS}: refused')
        return out
    for i in range(jlen(order)):
        pos = jat(order, i)
        x = dissolve_coord(jat(pos, 0)) if (isinstance(pos, JArr)
                                              and jlen(pos) == 2) else -1
        y = dissolve_coord(jat(pos, 1)) if x >= 0 else -1
        if y < 0:
            out.errors.append(f'"order" position {i} is not [x, y], each '
                              f'0-7: refused')
            return out
        out.byte(y * 8 + x)
    return out


def edit_dissolve(assets):
    p = assets / DISSOLVE_JSON
    j = json.loads(p.read_text())
    j["order"][1][0] = (j["order"][1][0] + 1) % 8
    p.write_text(json.dumps(j, indent=1), newline="\n")
    return "order[1] x +1"


# ----------------------------------------------------------------------
# The note frequencies (extract_music.py; build 136c)
# ----------------------------------------------------------------------

FREQS_JSON = "music/note_frequencies.json"
FMFREQS_WORDS = 85              # FMFreqs' dc.w run; a track's dc.b follows
FREQS_MIN, FREQS_MAX = 64, 128  # audio/smps.h


def rom_freqs(rom):
    return rom.region("FMFreqs",
                      next_label(rom, "FMFreqs")).table[:FMFREQS_WORDS * 2]


def freq_field(e, key, top):
    """smps.c freq_field: a whole number 0..top, else -1."""
    v = jget(e, key)
    if not is_num(v) or v < 0 or v > top or v != int(v):
        return -1
    return int(v)


def pack_freqs(assets, rom, docs):
    """smps_load's load_freqs: "entries", 64-128 of them, each block 0-7
    and fnum 0-2047 to the big-endian word block << 11 | fnum; anything
    else refused."""
    root = docs.get(assets, FREQS_JSON).root
    entries = jget(root, "entries")
    out = Packed()
    if not isinstance(entries, JArr) or not \
            FREQS_MIN <= jlen(entries) <= FREQS_MAX:
        out.errors.append(f'"entries" holds {jlen(entries)} frequencies, '
                          f'not {FREQS_MIN}-{FREQS_MAX}: refused')
        return out
    for i in range(jlen(entries)):
        e = jat(entries, i)
        block, fnum = freq_field(e, "block", 7), freq_field(e, "fnum", 0x7FF)
        if block < 0 or fnum < 0:
            out.errors.append(f"entry {i} needs a block 0-7 and an fnum "
                              f"0-2047: refused")
            return out
        out.word(block << 11 | fnum)
    return out


def edit_freqs(assets):
    p = assets / FREQS_JSON
    j = json.loads(p.read_text())
    j["entries"][5]["fnum"] += 1
    p.write_text(json.dumps(j, indent=1), newline="\n")
    return "entries[5].fnum +1"


# ----------------------------------------------------------------------
# The FM instruments (extract_music.py; build 136d)
# ----------------------------------------------------------------------
#
# The ROM side: the register order (loc_BA818, 26 register numbers), then
# 26 bytes from each label FMInsPtrs names, in its order, read straight on
# past any label as SetFMIns reads them. The packed side: the register
# numbers "upload_order" names, then each instrument's 26 bytes composed
# from its fields as instruments.c composes them.

INSTRUMENTS_JSON = "music/fm_instruments.json"
FM_INS_REGISTERS = 26           # audio/instruments.h
INSTRUMENTS_MAX = 256
FM_REGISTERS = {"feedback_algorithm": (0xB0, False), "pan_lfo": (0xB4, False),
                "detune_multiple": (0x30, True), "total_level": (0x40, True),
                "rate_scaling_attack": (0x50, True), "am_decay": (0x60, True),
                "sustain_rate": (0x70, True),
                "sustain_level_release": (0x80, True)}
FM_OP_OFFSET = (0x0, 0x8, 0x4, 0xC)          # operators 1-4
FM_CHANNEL_FIELDS = (("algorithm", 0xB0, 0, 3), ("feedback", 0xB0, 3, 3),
                     ("pan", 0xB4, 6, 2), ("lfo_am_sensitivity", 0xB4, 4, 2),
                     ("lfo_fm_sensitivity", 0xB4, 0, 3))
FM_OPERATOR_FIELDS = (("multiple", 0x30, 0, 4), ("detune", 0x30, 4, 3),
                      ("total_level", 0x40, 0, 7), ("attack_rate", 0x50, 0, 5),
                      ("decay_rate", 0x60, 0, 5),
                      ("sustain_level", 0x80, 4, 4),
                      ("sustain_rate", 0x70, 0, 5),
                      ("release_rate", 0x80, 0, 4),
                      ("rate_scaling", 0x50, 6, 2), ("lfo_am", 0x60, 7, 1))
FM_PAN = {"none": 0, "right": 1, "left": 2, "center": 3}


def fm_instrument_labels(rom):
    start = rom.asm.label_line["FMInsPtrs"]
    labels = []
    for line in rom.lines[start + 1:]:
        if re.match(r"[A-Za-z_]\w*:", line):
            break
        m = re.search(r"z80ptr\((\w+)\)", line)
        if m:
            labels.append(m.group(1))
    return labels


def rom_instruments(rom):
    cells = list(rom.region("loc_BA818", "SetFMIns").table)
    for label in fm_instrument_labels(rom):
        cells += rom.cells_from(label, FM_INS_REGISTERS)
    return cells


def fm_register(name):
    """instruments.c register_number: channel 1's register, else -1."""
    if not isinstance(name, str):
        return -1
    op = 0
    if re.match(r"op[1-4]\.", name):
        op, name = int(name[2]), name[4:]
    if name not in FM_REGISTERS:
        return -1
    base, per_op = FM_REGISTERS[name]
    if per_op != (op != 0):
        return -1
    return base + (FM_OP_OFFSET[op - 1] if per_op else 0)


def fm_read_mask(base):
    mask = 0
    for _, b, shift, bits in FM_CHANNEL_FIELDS + FM_OPERATOR_FIELDS:
        if b == base:
            mask |= ((1 << bits) - 1) << shift
    return mask


def fm_whole(v, top):
    """instruments.c whole: a whole number 0..top, else -1."""
    if not is_num(v) or v < 0 or v > top or v != int(v):
        return -1
    return int(v)


def fm_instrument(ins, i, out):
    """One instrument's register values, or None (refused, out.errors)."""
    val = {}

    def put(reg, x, shift):
        val[reg] = val.get(reg, 0) | (x << shift)

    if fm_whole(jget(ins, "number"), INSTRUMENTS_MAX - 1) != i:
        out.errors.append(f'instrument {i}\'s "number" is not {i}: refused')
        return None
    for name, base, shift, bits in FM_CHANNEL_FIELDS:
        v = jget(ins, name)
        if name == "pan":
            x = FM_PAN.get(v, -1) if isinstance(v, str) else -1
        else:
            x = fm_whole(v, (1 << bits) - 1)
        if x < 0:
            out.errors.append(f'instrument {i}\'s "{name}" is missing or out '
                              f'of range: refused')
            return None
        put(base, x, shift)
    for name, base, shift, bits in FM_OPERATOR_FIELDS:
        row = jget(ins, name)
        if not isinstance(row, JArr) or jlen(row) != 4:
            out.errors.append(f'instrument {i}\'s "{name}" is not a row of '
                              f'four: refused')
            return None
        for op in range(4):
            v = jat(row, op)
            if name == "lfo_am":
                x = int(v) if isinstance(v, bool) else -1
            else:
                x = fm_whole(v, (1 << bits) - 1)
            if x < 0:
                out.errors.append(f'instrument {i}\'s "{name}" for operator '
                                  f'{op + 1} is out of range: refused')
                return None
            put(base + FM_OP_OFFSET[op], x, shift)
    ign = jget(ins, "ignored_bits")
    if ign is not None:
        if not isinstance(ign, JObj):
            out.errors.append(f'instrument {i}\'s "ignored_bits" is not an '
                              f'object: refused')
            return None
        ign.doc.reads.add(f"{ign.path}.<register>")  # the C walks members
        for key, bits in ign:
            reg = fm_register(key)
            if reg < 0 or not isinstance(bits, JArr):
                out.errors.append(f'instrument {i}\'s ignored bits "{key}" '
                                  f'name no register: refused')
                return None
            base = reg if reg >= 0xB0 else reg & 0xF0
            for k in range(jlen(bits)):
                bit = fm_whole(jat(bits, k), 7)
                if bit < 0 or (fm_read_mask(base) >> bit) & 1:
                    out.errors.append(f'instrument {i}\'s ignored bits '
                                      f'"{key}" name a bit not 0-7 or one '
                                      f'the chip reads: refused')
                    return None
                put(reg, 1, bit)
    return val


def pack_instruments(assets, rom, docs):
    """instruments_load: "upload_order", the 26 registers by name, each
    once; "instruments", 1-256, each "number" its place, every field in
    range, the operators' rows of four, ignored bits only where the chip
    reads none; anything else refused (the file does not load)."""
    root = docs.get(assets, INSTRUMENTS_JSON).root
    out = Packed()
    names = jget(root, "upload_order")
    if not isinstance(names, JArr) or jlen(names) != FM_INS_REGISTERS:
        out.errors.append(f'"upload_order" holds {jlen(names)} registers, '
                          f'not {FM_INS_REGISTERS}: refused')
        return out
    order = []
    for i in range(jlen(names)):
        reg = fm_register(jat(names, i))
        if reg < 0 or reg in order:
            out.errors.append(f'"upload_order" entry {i} names no register, '
                              f'or one named before: refused')
            return out
        order.append(reg)
    bank = jget(root, "instruments")
    if not isinstance(bank, JArr) or not 1 <= jlen(bank) <= INSTRUMENTS_MAX:
        out.errors.append(f'"instruments" holds {jlen(bank)}, not '
                          f'1-{INSTRUMENTS_MAX}: refused')
        return out
    rows = []
    for i in range(jlen(bank)):
        val = fm_instrument(jat(bank, i), i, out)
        if val is None:
            return out
        rows.append(val)
    for reg in order:
        out.byte(reg)
    for val in rows:
        for reg in order:
            out.byte(val.get(reg, 0))
    return out


def edit_instruments(assets):
    p = assets / INSTRUMENTS_JSON
    j = json.loads(p.read_text())
    tl = j["instruments"][65]["total_level"]
    tl[0] = (tl[0] + 1) % 128
    p.write_text(json.dumps(j, indent=1), newline="\n")
    return "instruments[65].total_level[0] +1"


# ----------------------------------------------------------------------
# The level animations and the map patches (extract_levels.py; 131c)
# ----------------------------------------------------------------------
#
# Neither is a ROM table. Both are the operands of one routine each,
# which the extractor writes out as constants: the level screen VBlank's
# tile animations (loc_118F4) and LoadLevelData's tail (loc_9258-loc_9300)
# with the four run lists it reads (loc_9302). As 131a's object types, the
# ROM side is those operands read fresh from the routine's lines, laid out
# as a table of their own beside what the file packs to; the run lists
# are ROM tables and pack as themselves. The animation frames' pixels are
# level 2's tile sheets already (anim_*.png).

TILE_ANIMS_MAX, TILE_ANIM_FRAMES, TILE_ANIM_SEQ = 4, 4, 8     # game.c


def routine_lines(rom, start, end):
    """The code lines from label `start` to label `end`, comments off,
    each (label or None, instruction text)."""
    out = []
    for line in rom.lines[rom.asm.label_line[start]:rom.asm.label_line[end]]:
        body = line.split(";")[0].rstrip()
        if not body.strip():
            continue
        m = re.match(r"^(\w+):\s*(.*)$", body)
        out.append((m.group(1), m.group(2).strip()) if m
                   else (None, body.strip()))
    return out


def vram_of(control):
    """A VDP write command's address: bits 16-29 and 0-1."""
    return ((control >> 16) & 0x3FFF) | ((control & 3) << 14)


def rom_tile_anims(rom):
    """loc_118F4 read branch by branch: the level (a `bne` on
    level_index is level 0, then `cmpi.w #N, d0`), the wait
    (`move.w #X, (a2)`, a period of X + 1), the step mask (`andi.w #M,
    d1`) and, with `lea (T).l, a0` / `move.b (a0,d1.w), d1`, a table of
    steps; `lsl.w #8` makes each step a byte offset; the art (`lea (A).l,
    a0` before the VDP command), the VRAM address (`move.l #C,
    (vdp_control_port)`) and the frame's longs (`moveq`/`move.w #N, d0`
    and a dbf). Per branch: level, period, tile, frame bytes, the art's
    label, then each step's offset."""
    out, cur = [], None
    for _, ins in routine_lines(rom, "loc_118F4", "loc_11978"):
        m = re.match(r"move\.w\s+\(level_index\)\.w,\s*d0", ins)
        if m:
            cur = {"level": 0}
            continue
        m = re.match(r"cmpi\.w\s+#([^,]+),\s*d0", ins)
        if m:
            cur = {"level": rom.eval(m.group(1))}
            continue
        if cur is None:
            continue
        m = re.match(r"move\.w\s+#([^,]+),\s*\(a2\)", ins)
        if m:
            cur["period"] = rom.eval(m.group(1)) + 1
        m = re.match(r"andi\.w\s+#([^,]+),\s*d1", ins)
        if m:
            cur["mask"] = rom.eval(m.group(1))
        m = re.match(r"lea\s+\((\w+)\)\.l,\s*a0", ins)
        if m:
            cur.setdefault("labels", []).append(rom.label(m.group(1)))
        if re.match(r"move\.b\s+\(a0,d1\.w\),\s*d1", ins):
            cur["table"] = cur["labels"].pop()
        m = re.match(r"move\.l\s+#([^,]+),\s*\(vdp_control_port\)", ins)
        if m:
            cur["vram"] = vram_of(rom.eval(m.group(1)))
        m = re.match(r"(?:moveq|move\.w)\s+#([^,]+),\s*d0", ins)
        if m:
            cur["longs"] = rom.eval(m.group(1)) + 1
        if ins.startswith("rts") and "longs" in cur:
            out.append(cur)
            cur = None
    cells = []
    for a in out:
        steps = a["mask"] + 1
        table = rom.cells_from(a["table"], steps) if "table" in a \
            else list(range(steps))
        cells += [a["level"] & 0xFF] + word_cells(a["period"]) \
            + word_cells(a["vram"] // 32) + word_cells(a["longs"] * 4) \
            + ptr(a["labels"][0])
        for s in table:
            cells += word_cells((s << 8) & 0xFFFF)
    return cells


def anim_frame_file(name):
    """anim_<name>_<f>.png -> (the art's label, f), by
    asset_names.LEVEL_ANIM_NAMES, or None."""
    import asset_names
    for label, n in asset_names.LEVEL_ANIM_NAMES.items():
        m = re.fullmatch(re.escape(f"anim_{n}_") + r"(\d+)\.png", name)
        if m:
            return label, int(m.group(1))
    return None


def png_tile_count(path):
    """tileset_load's count: (width / 8) * (height / 8), or None."""
    try:
        head = Path(path).read_bytes()[:24]
    except OSError:
        return None
    w, h = int.from_bytes(head[16:20], "big"), int.from_bytes(head[20:24],
                                                             "big")
    return (w // 8) * (h // 8) if w % 8 == 0 and h % 8 == 0 else None


def pack_tile_anims(assets, rom, docs):
    """game.c tile_anims_load: "anims" in order, at most 4; each "level"
    (-1), "tile", "period" ($20), the "frames" (4 at most, each "file"
    under assets/tiles/, an entry with one that will not load is dropped)
    and "sequence" (8 at most); an entry with no frame or no step is
    dropped. Per entry: level, period, tile, the frame's bytes (its PNG's
    tiles by 32), the art (the label its frame files were named from,
    frame k the art's k-th frame), then each step's offset, the frame
    number by the frame's bytes -- as loc_118F4 shifts its step."""
    d = docs.get(assets, "data/level_anims.json")
    out = Packed()
    kept = 0
    for i in range(jlen(jget(d.root, "anims"))):
        if kept >= TILE_ANIMS_MAX:
            break
        a = jat(jget(d.root, "anims"), i)
        frames, seq = jget(a, "frames"), jget(a, "sequence")
        files = [cstr(jstr(jat(frames, k), "file", ""), 147)
                 for k in range(min(jlen(frames), TILE_ANIM_FRAMES))]
        steps = [jat(seq, k) for k in range(min(jlen(seq), TILE_ANIM_SEQ))]
        counts = [png_tile_count(Path(assets) / "tiles" / f) for f in files]
        if not files or not steps or None in counts:
            continue            # the loader drops it
        kept += 1
        where = f"anims[{i}]"
        arts = [anim_frame_file(f) for f in files]
        label = arts[0][0] if arts[0] else None
        for k, art in enumerate(arts):
            if art is None or art != (label, k):
                out.errors.append(f"{where}: frame {k} ({files[k]}) is not "
                                  "frame k of one ROM art")
        if len(set(counts)) != 1:
            out.errors.append(f"{where}: frames of {counts} tiles (the "
                              "routine copies one size)")
        size = counts[0] * 32
        out.byte(jint(a, "level", -1))
        out.word(u16(jint(a, "period", 0x20)))
        out.word(u16(jint(a, "tile", 0)))
        out.word(u16(size))
        out.cells += ptr(label) if label else [0] * 4
        for s in steps:
            k = int(s) if is_num(s) else 0
            if not 0 <= k < len(files):
                out.errors.append(f"{where}: step {k} names no frame")
            out.word(u16(k * size))
    return out


def edit_tile_anims(assets):
    p = assets / "data/level_anims.json"
    j = json.loads(p.read_text())
    j["anims"][1]["sequence"][3] = 0
    p.write_text(json.dumps(j, indent=1), newline="\n")
    return "level_anims anims[1].sequence[3] 1 -> 0"


# The map patches' layout, both sides: per patch the level, the flags (a
# count, then each $C7xx byte's low byte), the item ($FF for none), the
# plane (0 A, 1 B), then a kind byte and its operands -- 1 replace (from,
# to, bytes), 2 set (block, the count, each place), 3 runs (the list's
# label, stride, block, the list's entries and its $FF).
PATCH_REPLACE, PATCH_SET, PATCH_RUNS = 1, 2, 3
PLANE_BASE = {0xFF9000: 1, 0xFFA800: 0}       # $FF9000 plane B, $A800 A


def rom_map_patches(rom):
    """loc_9258-loc_9300 read instruction by instruction: a level test
    (`move.w (level_index).w, d0` then `bne`, level 0; `cmpi.w #N, d0`),
    a flag test (`tst.b $FFFFC7xx.w`; the first under a level holds for
    the rest of it), the Prism test (`moveq #ItemID_X, d2` before
    CheckItemExistInventory), the replace loop (`move.w #N, d1` its dbf
    count, `move.w #B, d0` the block, `cmpi.b #F, (a1)`), a write
    `move.b dN|#B, $FFFFxxxx.w` (a place in the plane its address falls
    in) and `lea (L).l, a2` / `bsr loc_9302` (a run list, entries to its
    $FF, rows of $60 -- `mulu.w #$60` -- and block $87, loc_9322)."""
    patches, level, base_flags, flags, item = [], None, [], [], None
    regs, stride, run_block, loop = {}, None, None, {}
    for label, ins in routine_lines(rom, "loc_9302", "loc_9342"):
        m = re.match(r"mulu\.w\s+#([^,]+),", ins)
        if m:
            stride = rom.eval(m.group(1))
        m = re.match(r"move\.b\s+#([^,]+),\s*\(a1,d1\.w\)", ins)
        if m:
            run_block = rom.eval(m.group(1))
    lines = routine_lines(rom, "loc_9258", "loc_9302")
    for label, ins in lines:
        if re.match(r"move\.w\s+\(level_index\)\.w,\s*d0", ins):
            level, base_flags, flags, item = 0, [], [], None
            continue
        m = re.match(r"cmpi\.w\s+#([^,]+),\s*d0", ins)
        if m:
            level, base_flags, flags, item = rom.eval(m.group(1)), [], [], None
            continue
        m = re.match(r"tst\.b\s+\$FFFF(C7[0-9A-Fa-f]{2})\.w", ins)
        if m:
            flag = int(m.group(1), 16)
            if not base_flags and not flags:
                base_flags = [flag]
            flags = base_flags + ([flag] if flag not in base_flags else [])
            continue
        m = re.match(r"moveq\s+#(\w+),\s*d2", ins)
        if m:
            item = rom.eval(m.group(1))
            continue
        m = re.match(r"move(?:q|\.w)\s+#([^,]+),\s*(d[0-7])", ins)
        if m:
            regs[m.group(2)] = rom.eval(m.group(1))
            continue
        m = re.match(r"cmpi\.b\s+#([^,]+),\s*\(a1\)", ins)
        if m:
            loop = {"from": rom.eval(m.group(1))}
            continue
        if ins.startswith("dbf") and "from" in loop:
            patches.append((level, list(flags), item, 1, PATCH_REPLACE,
                            [loop["from"], regs["d0"], regs["d1"] + 1]))
            loop = {}
            continue
        m = re.match(r"move\.b\s+(d[0-7]|#[^,]+),\s*\$FFFF([0-9A-Fa-f]{4})"
                     r"\.w", ins)
        if m:
            src = m.group(1)
            block = regs[src] if src.startswith("d") else rom.eval(src[1:])
            addr = 0xFF0000 | int(m.group(2), 16)
            base = max(b for b in PLANE_BASE if b <= addr)
            key = (level, tuple(flags), item, PLANE_BASE[base], PATCH_SET,
                   block)
            last = patches[-1] if patches else None
            if last and last[:5] == (level, flags, item, PLANE_BASE[base],
                                      PATCH_SET) and last[5][0] == block:
                last[5][1].append(addr - base)
            else:
                patches.append((level, list(flags), item, PLANE_BASE[base],
                                PATCH_SET, [block, [addr - base]]))
            continue
        m = re.match(r"lea\s+\((\w+)\)\.l,\s*a2", ins)
        if m:
            patches.append((level, list(flags), item, 1, PATCH_RUNS,
                            rom.label(m.group(1))))
    cells = []
    for level, flags, item, plane, kind, ops in patches:
        cells += [level & 0xFF, len(flags)] + [f & 0xFF for f in flags] \
            + [0xFF if item is None else item & 0xFF, plane, kind]
        if kind == PATCH_REPLACE:
            cells += [ops[0], ops[1]] + word_cells(ops[2])
        elif kind == PATCH_SET:
            cells += [ops[0], len(ops[1])]
            for at in ops[1]:
                cells += word_cells(at)
        else:
            run = rom.region(ops).cells
            n = 0
            while n < len(run) and run[n] != 0xFF:
                n += 3
            cells += ptr(ops) + word_cells(stride) + [run_block] \
                + run[:n + 1]
    return cells


def pack_map_patches(assets, rom, docs):
    """game.c level_map_patches: "patches" in order; each "level" (-1),
    "flags" (strings "$C7xx": a flag the scanf cannot read, or outside
    $C700-$C7FF, and the patch never applies), "item" (when present, the
    party must hold it), "plane" ("B" else A), then "replace" (from, to,
    bytes), "set" (block, "at"), "runs" (stride $60, block $87, entries
    of three) -- a patch with several packs each. A runs patch's list is
    the ROM label named by "source", which the loader does not read: the
    list is found by its entries (the run list whose bytes they are)."""
    d = docs.get(assets, "data/map_patches.json")
    out = Packed()
    lists = {}
    for _, ins in routine_lines(rom, "loc_9258", "loc_9302"):
        m = re.match(r"lea\s+\((\w+)\)\.l,\s*a2", ins)
        if m:
            label = rom.label(m.group(1))
            run = rom.region(label).cells
            n = 0
            while n < len(run) and run[n] != 0xFF:
                n += 3
            lists[label] = run[:n]
    for i in range(jlen(jget(d.root, "patches"))):
        p = jat(jget(d.root, "patches"), i)
        where = f"patches[{i}]"
        flags = []
        for k in range(jlen(jget(p, "flags"))):
            f = jat(jget(p, "flags"), k)
            m = re.match(r"\$([0-9A-Fa-f]+)", f if isinstance(f, str) else "")
            addr = int(m.group(1), 16) if m else -1
            if not 0xC700 <= addr <= 0xC7FF:
                out.errors.append(f"{where}: flag {f!r} is no $C7xx byte "
                                  "(the patch never applies)")
            flags.append(addr & 0xFF)
        item = jint(p, "item", -1) if jget(p, "item") is not None else None
        plane = 1 if jstr(p, "plane", "A")[:1] == "B" else 0
        head = [u8(jint(p, "level", -1)), len(flags)] + flags \
            + [0xFF if item is None else u8(item), plane]
        rep = jget(p, "replace")
        if rep is not None:
            out.cells += head + [PATCH_REPLACE, u8(jint(rep, "from", -1)),
                                 u8(jint(rep, "to", 0))] \
                + word_cells(u16(jint(rep, "bytes", 0)))
        st = jget(p, "set")
        if st is not None:
            at = jget(st, "at")
            places = [jat(at, k) for k in range(jlen(at))]
            out.cells += head + [PATCH_SET, u8(jint(st, "block", 0)),
                                 len(places)]
            for v in places:
                out.cells += word_cells(u16(int(v)) if is_num(v) else 0)
        runs = jget(p, "runs")
        if runs is not None:
            entries = []
            for k in range(jlen(jget(runs, "entries"))):
                e = jat(jget(runs, "entries"), k)
                if jlen(e) != 3:
                    continue        # the loader skips it
                row = [u8(int(jat(e, x))) if is_num(jat(e, x)) else 0
                       for x in range(3)]
                if row[0] == 0xFF:
                    out.errors.append(f"{where}: an entry at row $FF would "
                                      "end the ROM's list")
                entries += row
            label = next((l for l, run in lists.items() if run == entries),
                         None)
            if label is None:
                out.errors.append(f"{where}: its runs are none of the ROM's "
                                  "run lists")
            out.cells += head + [PATCH_RUNS] + (ptr(label) if label
                                                else [0] * 4) \
                + word_cells(u16(jint(runs, "stride", 0x60))) \
                + [u8(jint(runs, "block", 0x87))] + entries + [0xFF]
    return out


def edit_map_patches(assets):
    p = assets / "data/map_patches.json"
    j = json.loads(p.read_text())
    j["patches"][5]["set"]["at"][2] += 1
    p.write_text(json.dumps(j, indent=1), newline="\n")
    return "map_patches patches[5].set.at[2] +1 (a Dezolis dungeon door)"


# ----------------------------------------------------------------------
# The palette entries and the script banks (131c): two of the JSON files
# outside handover_131's list that the game reads
# ----------------------------------------------------------------------

PALETTE_ENTRY_MAX = 96      # palette.c
SCRIPT_BANKS = 32           # game.c


def rom_palette_ptrs(rom):
    """PalettePtrs, eight bytes an entry: the palette's address, the
    colour RAM address, the count of longs less one -- to the first label
    that is not a PtrPal_ one."""
    start = rom.asm.label_line["PalettePtrs"]
    end = next(rom.lines[j].split(":")[0].strip() for j in rom.asm.marks
               if j > start and not rom.lines[j].startswith("PtrPal_"))
    return rom.region("PalettePtrs", end).table


def pack_palette_ptrs(assets, rom, docs):
    """palette.c read_entry_table: each entry with 0 <= "index" < 96 sets
    that entry's "file" (char[64]), a later one winning; palette_load_entry
    loads assets/palettes/<file> into the line its caller names. Per entry,
    0 up to the highest index given: the palette's address as the ROM
    label whose colours the file holds (palette_load's reading, the
    colours' bits only -- a stray bit colour RAM drops is level 2's note;
    the entry's own label where several hold the same), the colour RAM
    address a hole (the callers pass the line), and the count of longs
    less one from the file's colours."""
    from roundtrip import CRAM_BITS, pack_palette
    d = docs.get(assets, "palettes/paletteptrs.json")
    files = {}
    for e in (d.root if isinstance(d.root, JArr) else []):
        i = jint(e, "index", -1)
        if 0 <= i < PALETTE_ENTRY_MAX:
            files[i] = cstr(jstr(e, "file", ""), 64)
    table = rom_palette_ptrs(rom)
    words = {}
    for k in range(0, len(table), 8):
        label = table[k].sym.label
        n = ((table[k + 6] << 8 | table[k + 7]) + 1) * 2
        cells = rom.cells_from(label, 2 * n)
        words.setdefault(label, [(cells[j] << 8 | cells[j + 1]) & CRAM_BITS
                                 for j in range(0, len(cells) - 1, 2)])
    out = Packed()
    for i in range(max(files, default=-1) + 1):
        own = table[8 * i].sym.label if 8 * i < len(table) else None
        path = Path(assets) / "palettes" / files.get(i, "")
        if not files.get(i) or not path.is_file():
            out.errors.append(f"palette entry {i}: no file "
                              f"({files.get(i) or 'none'}) -- the entry "
                              "does not load")
            out.cells += [0] * 4
            out.hole("the colour RAM address: the callers pass the line "
                     "(palette_load_entry(p, line, index))", 2)
            out.word(0)
            continue
        mine = pack_palette(path)
        label = resolve(own, words, lambda w: w == mine)
        if label is None:
            out.errors.append(f"palette entry {i}: {files[i]}'s colours are "
                              "none of the ROM's palettes")
            out.cells += [0] * 4
        else:
            out.cells += ptr(label)
        out.hole("the colour RAM address: the callers pass the line "
                 "(palette_load_entry(p, line, index))", 2)
        if len(mine) % 2:
            out.errors.append(f"palette entry {i}: {len(mine)} colours, not "
                              "whole longs")
        out.word(u16(len(mine) // 2 - 1))
    return out


def edit_palette_ptrs(assets):
    p = assets / "palettes/paletteptrs.json"
    j = json.loads(p.read_text())
    j[7]["file"] = j[8]["file"]
    p.write_text(json.dumps(j, indent=1), newline="\n")
    return f"paletteptrs[7].file -> entry 8's ({j[8]['file']})"


def rom_script_banks(rom):
    return rom.region("GameScriptPtrs", next_label(rom, "GameScriptPtrs")).table


def pack_script_banks(assets, rom, docs):
    """game.c script_bank: each member of script.json whose "bank" is the
    bank loads assets/script/<"file">; banks 0 up to the highest given.
    The file is named by the block's ROM label (extract_script.py): the
    pointer packs as that label. The blocks' text is level 2's scripts;
    "count" is not read."""
    d = docs.get(assets, "script/script.json")
    files = {}
    for _, e in (d.root if isinstance(d.root, JObj) else []):
        b = jint(e, "bank", -1)
        if 0 <= b < SCRIPT_BANKS:
            files[b] = jstr(e, "file", "")
    out = Packed()
    for b in range(max(files, default=-1) + 1):
        stem = files.get(b, "")
        stem = stem[:-4] if stem.endswith(".txt") else ""
        if not stem or rom.label(stem) is None \
                or not (Path(assets) / "script" / files[b]).is_file():
            out.errors.append(f"script bank {b}: {files.get(b) or 'no file'} "
                              "is no ROM script block")
            out.cells += [0] * 4
            continue
        out.cells += ptr(rom.label(stem))
    return out


def edit_script_banks(assets):
    p = assets / "script/script.json"
    j = json.loads(p.read_text())
    keys = list(j)
    j[keys[3]]["bank"], j[keys[4]]["bank"] = j[keys[4]]["bank"], \
        j[keys[3]]["bank"]
    p.write_text(json.dumps(j, indent=1), newline="\n")
    return f"script.json {keys[3]} and {keys[4]} banks swapped"


# ----------------------------------------------------------------------
# The table list
# ----------------------------------------------------------------------

TABLES = [
    Table("stores", "the shop stock table", "StoreEquipItemArray",
          ["data/stores.json", "data/items.json"], "extract-stores",
          rom_stores, pack_stores, edit_stores),
    Table("steal", "Shir's steal lists", "StealItemArray",
          ["data/steal.json", "data/items.json"], "extract-stores",
          rom_steal, pack_steal, edit_steal),
    Table("techs", "the technique table", "TechniqueData",
          ["data/techniques.json"], "extract-techs",
          rom_techs, pack_techs, edit_techs),
    Table("tech_avail", "the techniques each character learns",
          "CharAvailableTechniques", ["data/techniques.json"],
          "extract-techs", rom_avail, pack_avail, edit_avail),
    Table("items", "the item table", "InventoryData",
          ["data/items.json"], "extract-gamedata",
          rom_items, pack_items, edit_items),
    Table("char_names", "the characters' names", "CharNames",
          ["data/characters.json"], "extract-gamedata",
          rom_names, pack_names, edit_names),
    Table("char_setup", "the starting equipment and items",
          "CharInitialSetup",
          ["data/characters.json", "data/items.json"], "extract-gamedata",
          rom_setup, pack_setup, edit_setup),
    Table("char_levels", "the level tables", "CharExperiencePtrs",
          ["data/characters.json"], "extract-gamedata",
          rom_levels, pack_levels, edit_levels),
    Table("agility", "the footwear agility bonuses", "AgilityIncreasesArray",
          ["data/agility_bonus.json", "data/items.json"], "extract-gamedata",
          rom_agility, pack_agility, edit_agility),
    Table("portraits", "the portrait table", "PortraitTable",
          ["data/portraits.json"], "extract-buildings",
          rom_portraits, pack_portraits, edit_portraits),
    Table("building_music", "the building music", "BuildingMusicPtrs",
          ["data/buildings.json"], "extract-buildings",
          rom_building_music, pack_building_music, edit_building_music),
    Table("portrait_windows", "the portrait windows",
          "WindowArtLayoutPtrs from PtrWin_PortraitStart",
          ["data/portrait_windows.json"], "extract-buildings",
          rom_portrait_windows, pack_portrait_windows,
          edit_portrait_windows),
    Table("doors", "the levels' doors and entrances",
          "LevelData +14 interaction lists",
          ["levels/*.doors.json", "levels/levels.json"],
          "extract-interactions", rom_doors, pack_doors, edit_doors),
    Table("level_records", "the level records", "LevelData",
          ["levels/*.tmx", "levels/levels.json"], "extract-levels",
          rom_level_records, pack_level_records, edit_level_records),
    Table("tile_anims", "the level animations",
          "loc_118F4's operands (the VBlank)", ["data/level_anims.json"],
          "extract-levels", rom_tile_anims, pack_tile_anims, edit_tile_anims),
    Table("map_patches", "the map patches",
          "loc_9258's operands, the run lists loc_9302 reads",
          ["data/map_patches.json"], "extract-levels", rom_map_patches,
          pack_map_patches, edit_map_patches),
    Table("palette_ptrs", "the palette entries", "PalettePtrs",
          ["palettes/paletteptrs.json"], "extract-paletteptrs",
          rom_palette_ptrs, pack_palette_ptrs, edit_palette_ptrs),
    Table("script_banks", "the script banks", "GameScriptPtrs",
          ["script/script.json"], "extract-script", rom_script_banks,
          pack_script_banks, edit_script_banks),
    Table("exits", "the edge exits", "loc_28546",
          ["levels/exits.json"], "extract-interactions",
          rom_exits, pack_exits, edit_exits),
    Table("chests", "the chest contents", "TreasureChestContentArray",
          ["data/chests.json", "data/items.json"], "extract-npcs",
          rom_chests, pack_chests, edit_chests),
    Table("level_sprites", "who stands where", "LevelSpriteData",
          ["data/level_sprites.json"], "extract-npcs",
          rom_level_sprites, pack_level_sprites, edit_level_sprites),
    Table("level_art", "each level's art list", "LevelData +18 bits 0-6",
          ["data/level_sprites.json"], "extract-npcs",
          rom_level_art, pack_level_art, edit_level_art),
    Table("art_lists", "the people's art lists", "loc_283EE",
          ["data/level_sprites.json"], "extract-npcs",
          rom_art_lists, pack_art_lists, edit_art_lists),
    Table("dialogue", "the townspeople's dialogue numbers",
          "loc_E6B0, loc_E70C, loc_E730", ["data/people_dialogue.json"],
          "extract-npcs", rom_dialogue, pack_dialogue, edit_dialogue),
    Table("battle_enemies", "the enemies", "EnemyData", [BATTLE_JSON],
          "extract-battle", rom_enemy_records, pack_battle_enemies,
          edit_battle_enemies),
    Table("enemy_names", "the enemies' names", "EnemyNames", [BATTLE_JSON],
          "extract-battle", rom_enemy_names, pack_enemy_names,
          edit_enemy_names),
    Table("formations", "the enemy formations", "EnemyBattleFormationData",
          [BATTLE_JSON], "extract-battle", rom_formations, pack_formations,
          edit_formations),
    Table("groups", "the encounter groups", "EnemyFormationTable",
          [BATTLE_JSON], "extract-battle", rom_groups, pack_groups,
          edit_groups),
    Table("regions", "the encounter regions", "loc_23C3A-loc_23DBA",
          [BATTLE_JSON], "extract-battle", rom_regions, pack_regions,
          edit_regions),
    Table("enemy_x", "the enemies' x positions", "EnemyXPosArray",
          [BATTLE_JSON], "extract-battle", rom_enemy_x, pack_enemy_x,
          edit_enemy_x),
    Table("char_pos", "the party's battle positions",
          "Battle_CharCoordinates", [BATTLE_JSON], "extract-battle",
          rom_char_pos, pack_char_pos, edit_char_pos),
    Table("char_sprites", "the party's battle sprites",
          "Battle_CharObjectPtrs", [BATTLE_JSON], "extract-battle",
          rom_char_sprites, pack_char_sprites, edit_char_sprites),
    Table("battle_maps", "the battle mappings",
          "the Battle_*Map, Map_*Battle, Map_Weapon*, Map_Tech* tables",
          [BATTLE_JSON], "extract-battle", None, pack_battle_maps,
          edit_battle_maps),
    Table("anim_scripts", "the battle animation scripts",
          "loc_2601A .. loc_27226, CharBattle_AnimOffsets", [BATTLE_JSON],
          "extract-battle", None, pack_anim_scripts, edit_anim_scripts),
    Table("weapon_props", "the weapons' effects", "WeaponProperties",
          [BATTLE_JSON], "extract-battle", rom_weapon_props,
          pack_weapon_props, edit_weapon_props),
    Table("tech_props", "the techniques' effects", "TechniqueProperties",
          [BATTLE_JSON], "extract-battle", rom_tech_props, pack_tech_props,
          edit_tech_props),
    Table("effect_art", "the effects' art lists", "WeaponTechArtPtrs",
          [BATTLE_JSON], "extract-battle", rom_effect_art, pack_effect_art,
          edit_effect_art),
    Table("fight_bytes", "the fight's small byte tables",
          "EnemyTechSuccessRate, Battle_CharTargetChances, loc_34EE",
          [BATTLE_JSON], "extract-battle", rom_fight_bytes, pack_fight_bytes,
          edit_fight_bytes),
    Table("dissolve_order", "the dissolve order", "FadeEffectArray",
          [DISSOLVE_JSON], "extract-tables", rom_dissolve, pack_dissolve,
          edit_dissolve),
    Table("note_frequencies", "the note frequencies", "FMFreqs",
          [FREQS_JSON], "extract-music", rom_freqs, pack_freqs, edit_freqs),
    Table("fm_instruments", "the FM instruments and their upload order",
          "loc_BA818, FMInsPtrs", [INSTRUMENTS_JSON], "extract-music",
          rom_instruments, pack_instruments, edit_instruments),
    Table("people_types", "the townspeople's types",
          "each object's init and mappings", ["data/npc_types.json"],
          "extract-npcs", None, pack_people_types, edit_people_types),
    Table("scene_types", "the scene objects",
          "each object's init and mappings", ["data/scene_objects.json"],
          "extract-npcs", None, pack_scene_types, edit_scene_types),
    Table("chest_types", "the chest objects",
          "each object's init and mappings", ["data/chests.json"],
          "extract-npcs", None, pack_chest_types, edit_chest_types),
]

TYPE_ROM = {"people_types": rom_people_types,
            "scene_types": rom_scene_types,
            "chest_types": rom_chest_types}
TYPE_OBJECTS = {
    "people_types": lambda a, d: list(type_entries(
        d.get(a, "data/npc_types.json").root, NPC_TYPES_MAX)),
    "scene_types": lambda a, d: list(type_entries(
        d.get(a, "data/scene_objects.json").root, SCENE_TYPES_MAX)),
    "chest_types": lambda a, d: list(type_entries(
        jget(d.get(a, "data/chests.json").root, "types"), CHEST_TYPES_MAX)),
}

# Files only part of which the tables above pack yet: their "not packed"
# list is not only annotations.
PARTLY_PACKED = {}

# (build 131b) Why a file's unread fields are unread, where the list alone
# would not say.
FIELD_NOTES = {
    "levels/levels.json":
        "the index, name, planet and the tileset's two files are what "
        "load_level reads; each level's other fields are copies of what the "
        "game reads from its .tmx, and its map files the game names from "
        "the stem itself; the tilesets block describes the tileset "
        "pointers (loc_9342), which the game takes by the files' names -- "
        "the port's own names, each checked to be the level type's tileset",
    TMX_BACKDROP_GROUP:
        "level_load reads only a backdrop's layer; its properties are "
        "copies of its level's",
}

# (build 131b) Files that describe ROM data the game reads from elsewhere:
# level 1 only.
NOT_READ = {
    "music/songs.json":
        "read by no loader: the song and effect headers decoded (tracks, "
        "channels, streams) for reading; the game plays the sound driver's "
        "flat image, music/*.bin, level 1's",
    "nametables/nametables.json":
        "read by no loader: each nametable's size; the game reads the .tmx "
        "files (nametables/), which carry their own",
    "palettes/palettes.json":
        "read by no loader: the named palettes' colour words, for reading; "
        "the game loads the palette PNGs, level 2's",
    "winart.json":
        "read by no loader: the window maps as rows, for reading; the game "
        "reads the .tmx files (winart/)",
    LEVEL_JSON_GROUP:
        "read by no loader: the extractor's description of each level "
        "record (its labels, sizes in pixels, the music's name). The game "
        "reads the record from the level's .tmx and levels.json, this file "
        "only for extra_art when a .tmx has none, and every .tmx has it",
}
LEVEL_JSON_RE = re.compile(r"levels/[0-9a-f]{2}_\w+\.json")

# (build 136b) JSON files roundtrip.py packs at level 2 itself, with the
# files they name: their ROM side is not ps2.asm's.
PACKED_ELSEWHERE = {
    "music/drums.json": "packed at level 2 with its WAVs (the drum sets "
                        "above), against the Z80 drum file's RAM image",
}

# Files whose content is the port's own, not the ROM's: level 1 only.
NOT_ROM_DATA = {
    "data/flag_names.json": "the port's names for the story flags "
                            "(tools/extract_flag_names.py); the ROM has "
                            "no such table",
    "music/q_patches.json": "Q's hand-voiced patch overrides (build 112), "
                            "written by hand; no extractor, no ROM table",
}


# ----------------------------------------------------------------------
# Running
# ----------------------------------------------------------------------

@dataclass
class Result:
    table: Table
    rom_len: int = 0
    packed_len: int = 0
    same: int = 0
    holes: dict = field(default_factory=dict)
    diffs: list = field(default_factory=list)      # (offset, rom, packed)
    notes: list = field(default_factory=list)
    errors: list = field(default_factory=list)

    @property
    def ok(self):
        return not self.diffs and not self.errors


def compare(table, rom_cells, packed):
    res = Result(table, len(rom_cells), len(packed.cells))
    res.errors += packed.errors
    for off in range(max(len(rom_cells), len(packed.cells))):
        r = rom_cells[off] if off < len(rom_cells) else None
        p = packed.cells[off] if off < len(packed.cells) else None
        if isinstance(p, Hole):
            res.holes[p.why] = res.holes.get(p.why, 0) + 1
            continue
        if r == p:
            res.same += 1
            continue
        chk = CHECKED.get((table.key, off))
        if chk and chk[0] == r and chk[1] == p:
            if not (isinstance(r, PtrCell) and r.k):    # one a pointer
                res.notes.append(f"{chk[3]} (+{off}), checked: {chk[2]}")
            continue
        res.diffs.append((off, r, p))
    if len(rom_cells) != len(packed.cells):
        res.errors.append(f"the ROM table is {len(rom_cells)} bytes, the "
                          f"packed one {len(packed.cells)}")
    return res


def run(assets, rom, only=None):
    docs = Docs()
    results = []
    for t in TABLES:
        if only and t.key not in only:
            continue
        try:
            packed = t.pack(Path(assets), rom, docs)
            if t.rom is None and packed.rom_side is not None:
                rom_cells = packed.rom_side(rom)
            elif t.rom is None:
                # The object types: the ROM side is each object's init and
                # mappings, for the objects the file names (a type's
                # object id is where its init is found).
                rom_cells = TYPE_ROM[t.key](rom, TYPE_OBJECTS[t.key](
                    Path(assets), docs))
            else:
                rom_cells = t.rom(rom)
        except (AsmDataError, KeyError) as e:
            res = Result(t)
            res.errors.append(f"could not read: {e}")
            results.append(res)
            continue
        results.append(compare(t, rom_cells, packed))
    return results, docs


def field_report(docs, assets):
    """Per file (or group of files): the key paths the loaders read, and
    the ones in the files no loader reads."""
    groups = {}
    for path, d in docs.cache.items():
        name = d.group or Path(path).relative_to(assets).as_posix()
        g = groups.setdefault(name, [set(), set(), 0])
        g[0] |= d.present()
        g[1] |= d.reads
        g[2] += 1
    out = []
    for name, (present, reads, n) in sorted(groups.items()):
        label = f"{name} ({n} files)" if n > 1 else name
        out.append((label, sorted(reads & present),
                    sorted(present - reads)))
    return out


def report(results, docs, assets, verbose=True, stream=sys.stdout):
    bad = 0
    for res in results:
        t = res.table
        status = "same" if res.ok else "DIFFERENT"
        holes = sum(res.holes.values())
        extra = f", {holes} not packed" if holes else ""
        print(f"json  {t.title} ({t.rom_label}): {res.same} of "
              f"{res.rom_len} bytes {status}{extra}", file=stream)
        for why, n in res.holes.items():
            print(f"        not packed, {n} bytes: {why}", file=stream)
        for note in res.notes:
            print(f"        note {note}", file=stream)
        for e in res.errors:
            print(f"        FAIL {e}", file=stream)
        for off, r, p in res.diffs[:8]:
            print(f"        FAIL +{off}: ROM {cell_str(r)}, packed "
                  f"{cell_str(p)}", file=stream)
        if len(res.diffs) > 8:
            print(f"        ... {len(res.diffs) - 8} more", file=stream)
        if not res.ok:
            bad += 1
    if verbose:
        for rel, read, ignored in field_report(docs, Path(assets)):
            print(f"json  {rel}: packs {', '.join(read) or '-'}",
                  file=stream)
            if rel in PARTLY_PACKED:
                print(f"        {PARTLY_PACKED[rel]}", file=stream)
            elif ignored:
                print(f"        not packed: {', '.join(ignored)}",
                      file=stream)
            if base_group(rel) in FIELD_NOTES:
                print(f"        ({FIELD_NOTES[base_group(rel)]})",
                      file=stream)
        for rel, why in NOT_ROM_DATA.items():
            print(f"json  {rel}: level 1 only -- {why}", file=stream)
        for rel, why in NOT_READ.items():
            print(f"json  {rel}: level 1 only -- {why}", file=stream)
    return bad


def to_report(results, docs, assets, report):
    """The results in roundtrip.py's report: a line a table, its notes and
    holes, a FAIL for each difference or error; then the fields each file
    packs and ignores, and the JSON files not packed yet."""
    same = 0
    for res in results:
        t = res.table
        holes = sum(res.holes.values())
        extra = f", {holes} not packed" if holes else ""
        report.say(f"    {t.title} ({t.rom_label}): {res.same} of "
                   f"{res.rom_len} bytes the same{extra}")
        for why, n in res.holes.items():
            report.say(f"      not packed, {n} bytes: {why}")
        for note in res.notes:
            report.note(f"{t.title} {note}")
        for e in res.errors:
            report.fail(f"{t.title}: {e}")
        for off, r, p in res.diffs[:8]:
            report.fail(f"{t.title} +{off}: ROM {cell_str(r)}, packed "
                        f"{cell_str(p)}")
        if len(res.diffs) > 8:
            report.fail(f"{t.title}: {len(res.diffs) - 8} more differences")
        same += res.ok
    report.say("  JSON fields (packs: what the loaders read; not packed: "
               "in the file, read by no loader -- names, labels, ids that "
               "are only an entry's place):")
    covered = set()
    for rel, read, ignored in field_report(docs, Path(assets)):
        if rel in PARTLY_PACKED:
            report.say(f"    {rel}: packs {', '.join(read) or '-'} -- "
                       f"{PARTLY_PACKED[rel]}")
            continue
        report.say(f"    {rel}: packs {', '.join(read) or '-'}")
        if ignored:
            report.say(f"      not packed: {', '.join(ignored)}")
        if base_group(rel) in FIELD_NOTES:
            report.say(f"      ({FIELD_NOTES[base_group(rel)]})")
    for path in docs.cache:
        covered.add(Path(path).relative_to(assets).as_posix())
    for rel, why in PACKED_ELSEWHERE.items():
        report.say(f"    {rel}: {why}")
    for rel, why in NOT_ROM_DATA.items():
        report.say(f"    {rel}: level 1 only -- {why}")
    for rel, why in NOT_READ.items():
        if rel != LEVEL_JSON_GROUP:
            report.say(f"    {rel}: level 1 only -- {why}")
    rest, level_json = [], 0
    for p in sorted(Path(assets).rglob("*.json")):
        rel = p.relative_to(assets).as_posix()
        if rel in covered or rel in NOT_ROM_DATA or rel in NOT_READ \
                or rel in PACKED_ELSEWHERE:
            continue
        if LEVEL_JSON_RE.fullmatch(rel):
            level_json += 1     # one level record's description each
            continue
        rest.append(rel)
    if level_json:
        report.say(f"    {LEVEL_JSON_GROUP} ({level_json} files): level 1 "
                   f"only -- {NOT_READ[LEVEL_JSON_GROUP]}")
    return same, rest


def base_group(label):
    """A field_report label less its " (N files)"."""
    return re.sub(r" \(\d+ files\)$", "", label)


def extractor_cmd(target, image, out_root):
    """The extractor for a Makefile target, writing under out_root (an
    assets/-shaped directory). (build 185) Each reads the cartridge image,
    `image`, as the Makefile's rule runs it."""
    data = out_root / "data"
    cmds = {
        "extract-stores": ["extract_stores.py", image, "-o", data],
        "extract-techs": ["extract_techs.py", image, "-o", data],
        "extract-gamedata": ["extract_gamedata.py", image, "-o", data],
        "extract-buildings": ["extract_buildings.py", image, "-o", data],
        "extract-interactions": ["extract_interactions.py", image, "-o",
                                 out_root / "levels"],
        "extract-npcs": ["extract_npcs.py", image, "-o", out_root],
        "extract-levels": ["extract_levels.py", image, "-o", out_root],
        "extract-battle": ["extract_battle.py", image, "-o", data],
        "extract-paletteptrs": ["extract_paletteptrs.py", image, "-o",
                                out_root / "palettes"],
        "extract-script": ["extract_script.py", image, "-o",
                           out_root / "script"],
        # (build 136a) the dissolve order; the ramps go to palettes/ beside
        "extract-tables": ["extract_tables.py", image, "-o",
                           out_root / "tables"],
        # (build 136c) the note frequencies (and the sound image, songs.json)
        "extract-music": ["extract_music.py", image, "-o",
                          out_root / "music"],
    }
    tool, *args = cmds[target]
    return [sys.executable, str(TOOLS / tool)] + [str(a) for a in args]


def fingerprint(res):
    return ({(off, cell_str(r), cell_str(p)) for off, r, p in res.diffs},
            set(res.errors))


def prove_edits(assets, rom, image):
    """Step f: one edit a table, both levels must fail. Level 2 is caught
    when the edit adds a difference the unedited table does not have (a
    table that already differs still has to see the edit). `rom` is the
    disassembly's tables (asmdata.Rom), `image` the cartridge image the
    extractors read (build 185)."""
    failures = 0
    image = Path(image).resolve()
    baseline, _ = run(assets, rom)
    base = {res.table.key: fingerprint(res) for res in baseline}
    for t in TABLES:
        if t.edit is None:
            continue
        with tempfile.TemporaryDirectory() as tmp:
            scratch = Path(tmp) / "assets"
            shutil.copytree(assets, scratch)
            what = t.edit(scratch)
            fresh = Path(tmp) / "fresh"
            # extract_interactions reads the level list it writes beside
            for sub in ("levels", "tiles", "data"):
                (fresh / sub).mkdir(parents=True)
            shutil.copy(assets / "levels/levels.json", fresh / "levels")
            subprocess.run(extractor_cmd(t.extractor, image, fresh),
                           check=True, capture_output=True,
                           cwd=scratch.parent)
            files = [p.relative_to(scratch).as_posix()
                     for f in t.files for p in scratch.glob(f)]
            level1 = [f for f in files
                      if f != "levels/levels.json"
                      and (fresh / f).exists()
                      and (fresh / f).read_bytes() != (scratch / f).read_bytes()]
            results, _ = run(scratch, rom, only={t.key})
            diffs, errors = fingerprint(results[0])
            level2 = bool(diffs - base[t.key][0] or errors - base[t.key][1])
            ok = bool(level1) and level2
            failures += not ok
            print(f"edit  {t.key}: {what}: level 1 "
                  f"{'fails (' + ', '.join(level1) + ')' if level1 else 'PASSES'}"
                  f", level 2 {'fails' if level2 else 'PASSES'}"
                  f"{'' if ok else '  <-- NOT CAUGHT'}")
    return failures


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--ps2-dir", type=Path, required=True,
                    help="the disassembly: ps2.asm and the constants")
    ap.add_argument("--assets", type=Path, default=Path("assets"))
    ap.add_argument("--only", nargs="*")
    ap.add_argument("--prove-edits", action="store_true")
    ap.add_argument("--rom", type=Path,
                    help="(build 185) the cartridge image, which the "
                         "extractors read: needed with --prove-edits")
    ap.add_argument("--quiet", action="store_true",
                    help="leave out the per-file field lists")
    args = ap.parse_args()

    asm = args.ps2_dir / "ps2.asm"
    const = next((args.ps2_dir / n for n in ("ps2.constants.asm",
                                              "ps2_constants.asm")
                  if (args.ps2_dir / n).exists()), None)
    if not asm.exists() or const is None:
        sys.exit(f"no ps2.asm / constants under {args.ps2_dir}")
    rom = Rom(asm, const, args.ps2_dir / "art")

    if args.prove_edits:
        if args.rom is None:
            ap.error("--prove-edits needs --rom, the cartridge image")
        bad = prove_edits(args.assets, rom, args.rom)
        print(f"edits: {'every one caught' if not bad else f'{bad} NOT caught'}")
        sys.exit(1 if bad else 0)

    results, docs = run(args.assets, rom, set(args.only) if args.only else None)
    bad = report(results, docs, args.assets, verbose=not args.quiet)
    total = len(results)
    print(f"json level 2: {total - bad} of {total} tables the same"
          + ("" if not bad else f", {bad} DIFFERENT"))
    sys.exit(1 if bad else 0)


if __name__ == "__main__":
    main()
