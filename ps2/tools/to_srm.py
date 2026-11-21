#!/usr/bin/env python3
"""(build 138e) The port's saves into the cartridge's battery RAM, and back.

A port save (saves/slotN.json, routes/saves/*.json) written into an
emulator's battery file (RetroArch's .srm) so the ROM can CONTINUE exactly
where the save stands, and an .srm slot read back into the port's save
format. docs/emulator_saves.md has the commands for the visual checks.

    python3 tools/to_srm.py write SAVE.json --slot N --srm FILE.srm [-o OUT.srm] [--label NAME]
    python3 tools/to_srm.py write SAVE.json --slot N --new -o OUT.srm [--label NAME]
    python3 tools/to_srm.py read FILE.srm --slot N [-o OUT.json]
    python3 tools/to_srm.py check FILE.srm
    python3 tools/to_srm.py selftest [--srm FILE.srm]

Run from anywhere; the tables (items, levels, flag names) are read from the
tree this script is in. `write` without -o rewrites FILE.srm in place after
copying it to FILE.srm.bak. Slots are numbered 1-4 as the game's slot list
numbers them.

What the ROM keeps (read in ps2.asm: the save (SaveData), the checksum's
test (loc_D676), the copyright's test (loc_D6AA), the repair copy
(loc_D6D4), the power-on set-up (loc_D700), the load (LoadSavedData), the
power-on check "Let me check the backup data" (loc_D1E0 / loc_D20E),
CONTINUE (loc_D3B6), the Data Memory's save (loc_B5A4) and the slot list
(loc_104EC)):

* The battery RAM is $200001-$203FFF, odd addresses only: 8 KB of data.
  It holds eight blocks of 1024 bytes, $800 addresses apart: slots 1-4 at
  $200001, $200801, $201001, $201801, and each slot's mirror copy $2000
  above it. A save writes the slot and its mirror the same.
* A block is the RAM a save copies: the eight characters' records
  ($C000-$C1FF, 512 bytes), the flags ($C700-$C7FF, 256), the party block
  ($C600-$C69D, 158), the checksum (a word, big-endian: the 16-bit sum of
  the 926 bytes before it, stored into $C69E as well), then $C6A0-$C6FF,
  the 96-byte copyright string a new game puts there.
* $C69C is $FFFF when the slot holds a save (the Data Memory's save sets
  it); the slot list and CONTINUE test its first byte in the slot.
* A character's record ends in eight bytes the port's save has no field
  for: the name as the window tiles draw it, an upper and a lower tile a
  letter (SetCharNames, from VDPCharacterMaps). A new game and the name
  board write them; CONTINUE does not, so the ROM shows the names from the
  save. The writer makes them from the names as SetCharNames does.

The port's side is src/game/gamedata.c: gamedata_load reads raw_c600 first
and then the named fields, which win where the two disagree (build 104's
telepipe_edited route). load_port_save below follows gamedata_load to the
letter, and `selftest` checks it against the port's own C (save_resave,
`make save_resave`) on every save under routes/saves.

Layout of the battery file: RetroArch's Genesis Plus GX writes the 16 KB
the cartridge's range covers, the data on the odd bytes and $FF on the
even ones. The writer keeps the layout of the file it is given (odd bytes
or even bytes in 16 KB or more, or the 8 KB of data alone), its size, and
every byte outside the slot written and its mirror.
"""

import argparse
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile

TREE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# ---------------------------------------------------------------------------
# The battery RAM's layout

DATA_SIZE = 0x2000          # $200001-$203FFF, odd addresses: 8 KB
BLOCK = 0x400               # one slot's copy
SLOTS = 4
MIRROR = 4                  # the mirror copy is block slot + 4

OFF_RECORDS = 0x000         # $C000-$C1FF
OFF_FLAGS = 0x200           # $C700-$C7FF
OFF_PARTY = 0x300           # $C600-$C69D
PARTY_SIZE = 0x9E
OFF_CHECKSUM = 0x39E        # $C69E, big-endian
OFF_COPYRIGHT = 0x3A0       # $C6A0-$C6FF
SUMMED = 0x39E              # loc_D676 sums $39E bytes

CHARS = 8
RECORD = 0x40
NAME_LEN = 4
ITEM_SLOTS = 16
PARTY_MAX = 4

# CopyrightString (ps2.asm:19770), 96 bytes, compared by loc_D6AA.
COPYRIGHT = (b"SEGA MEGA DRIVE " b"(C)SEGA 1988.NOV" b"PHANTASY STAR 2       "
             b"BACKUP RAM" b"PROGRAMMED BY          " b"NAKA YUJI")
assert len(COPYRIGHT) == 0x60

# The party block's bytes by name (gamedata.h's GD_* and pack_c600).
P_MEMBERS_NUM = 0x00
P_JOINED = 0x04
P_JOIN_NEXT = 0x06
P_MEMBER_ID = 0x08
P_MONEY = 0x20
P_KEEPER_COUNT = 0x27
P_KEEPER_ITEMS = 0x28
P_LEVEL = 0x40
P_LEVEL_Y = 0x42
P_LEVEL_X = 0x44
P_LAST_SAVE_MAP = 0x46
P_ENTRANCE_MAP = 0x4C
P_JET_SCOOTER = 0x52
P_SCOOTER_Y = 0x54
P_SCOOTER_X = 0x56
P_PLANET = 0x58
P_LAST_SAVE_PLANET = 0x5A
P_LAST_SAVE_VISIPHONE = 0x5C
P_NAMES = 0x60
P_SLOT_NAME = 0x80
P_SLOT_LEVEL = 0x84
P_SLOT_IN_USE = 0x9C

CHAR_ID = ["Rolf", "Nei", "Rudo", "Amy", "Hugh", "Anna", "Kain", "Shir"]
EQUIP_KEY = ["head", "right_hand", "left_hand", "body", "legs"]
PLANET = ["Motavia", "Dezolis"]
SAVE_FORMAT = 2

# ---------------------------------------------------------------------------
# The names: the script charset (gamedata.c's code_from_ascii /
# ascii_from_code) and the window tiles (VDPCharacterMaps, ps2.asm:27449:
# 208 pairs, every upper tile $26; window.c's win_glyph, checked against
# the table pair by pair in build 138e).

NAME_END = 0xC4             # a name shorter than four ends here


def code_from_ascii(ch):
    if 0x30 <= ch <= 0x39:
        return 1 + (ch - 0x30)
    if 0x41 <= ch <= 0x5A:
        return 11 + (ch - 0x41)
    if 0x61 <= ch <= 0x7A:
        return 37 + (ch - 0x61)
    return 0


def ascii_from_code(code):
    if 1 <= code <= 10:
        return chr(0x30 + code - 1)
    if 11 <= code <= 36:
        return chr(0x41 + code - 11)
    if 37 <= code <= 62:
        return chr(0x61 + code - 37)
    return " "


def name_from_ascii(text):
    """gamedata.c's name_from_ascii: bytes as strlen counts them."""
    raw = text.encode("utf-8")
    out = bytearray(NAME_LEN)
    for i in range(min(NAME_LEN, len(raw))):
        out[i] = code_from_ascii(raw[i])
    if len(raw) < NAME_LEN:
        out[len(raw)] = NAME_END
    return bytes(out)


def name_to_ascii(name):
    out = ""
    for n in range(NAME_LEN):
        if name[n] == NAME_END:
            break
        out += ascii_from_code(name[n])
    return out


def name_portable(name):
    """True when name_to_ascii and name_from_ascii give these bytes back."""
    return name_from_ascii(name_to_ascii(name)) == bytes(name)


def glyph(code):
    """VDPCharacterMaps[code]: (upper, lower) tile."""
    if 1 <= code <= 10:
        return 0x26, 0x96 + code
    if 11 <= code <= 0x48:
        return 0x26, code + 28
    if code == 0x77:
        return 0x26, 0x80
    if code < 208:
        return 0x26, 0x26
    raise ValueError("name code $%02X is past VDPCharacterMaps" % code)


def name_tiles(name):
    """SetCharNames: the record's +$38-+$3B uppers, +$3C-+$3F lowers."""
    out = bytearray(8)
    for i in range(NAME_LEN):
        upper, lower = glyph(name[i])
        out[i] = upper
        out[4 + i] = lower
    return bytes(out)


# ---------------------------------------------------------------------------
# The tables the save names things by, as gamedata_load_tables reads them.

class Tables:
    def __init__(self, tree=TREE):
        def load(rel):
            with open(os.path.join(tree, rel), "rb") as fh:
                return json.loads(fh.read().decode("utf-8"),
                                  object_pairs_hook=first_wins)

        items = load("assets/data/items.json")
        self.items = [str(it.get("key", "None"))[:23]
                      if isinstance(it.get("key"), str) else "None"
                      for it in items[:128]]

        self.level_name = {}
        levels = load("assets/levels/levels.json")["levels"]
        self.level_count = 0
        for i, lv in enumerate(levels[:128]):
            index = c_int(lv, "index", i)
            if 0 <= index < 128:
                self.level_name[index] = c_str(lv, "name", "")[:31]
                self.level_count = max(self.level_count, index + 1)

        self.flag_name = [""] * 0x100
        try:
            flags = load("assets/data/flag_names.json")
        except OSError:
            flags = {}
        for f in children(c_get(flags, "flags")):
            addr = c_str(f, "addr", "")
            name = c_str(f, "name", "")
            a = int(addr[1:], 16) if re.fullmatch(r"\$[0-9A-Fa-f]+", addr) else -1
            if 0xC700 <= a <= 0xC7FF:
                self.flag_name[a - 0xC700] = name[:63]

    def item_key(self, item):
        item &= 0x7F
        return self.items[item] if item < len(self.items) else "None"

    def item_by_key(self, key, warn):
        if key is not None:
            for i, k in enumerate(self.items):
                if k == key:
                    return i
            warn('unknown item "%s"' % key)
        return 0

    def level_label(self, level):
        name = self.level_name.get(level, "")
        if level < self.level_count and name:
            return name
        return None

    def level_id(self, name):
        folded = ascii_fold(name)
        for i in range(self.level_count):
            n = self.level_name.get(i, "")
            if n and ascii_fold(n) == folded:
                return i
        return -1


def ascii_fold(s):
    return "".join(chr(ord(c) + 32) if "A" <= c <= "Z" else c for c in s)


# ---------------------------------------------------------------------------
# JSON as src/core/json.c gives it: a key's first member, numbers, booleans
# and strings by type only.

def first_wins(pairs):
    out = {}
    for k, v in pairs:
        if k not in out:
            out[k] = v
    return out


def c_get(obj, key):
    return obj.get(key) if isinstance(obj, dict) else None


def c_has(obj, key):
    """json_get(obj, key) != NULL: a member there, a JSON null included."""
    return isinstance(obj, dict) and key in obj


def is_number(v):
    return isinstance(v, (int, float)) and not isinstance(v, bool)


def c_int(obj, key, fallback):
    v = c_get(obj, key)
    return int(v) if is_number(v) else fallback


def c_bool(obj, key, fallback):
    v = c_get(obj, key)
    return v if isinstance(v, bool) else fallback


def c_str(obj, key, fallback):
    v = c_get(obj, key)
    return v if isinstance(v, str) else fallback


def children(v):
    """A value's children as json.c links them: an array's items, an
    object's members."""
    if isinstance(v, list):
        return v
    if isinstance(v, dict):
        return list(v.values())
    return []


def c_strtol_hex(s):
    """strtol(s, NULL, 16) on what follows '$'."""
    m = re.match(r"\s*([+-]?)(0[xX](?=[0-9A-Fa-f]))?([0-9A-Fa-f]*)", s)
    digits = m.group(3)
    if not digits:
        return 0
    v = int(digits, 16)
    return -v if m.group(1) == "-" else v


def get16(raw, off):
    return raw[off] << 8 | raw[off + 1]


def put16(raw, off, v):
    raw[off] = (v >> 8) & 0xFF
    raw[off + 1] = v & 0xFF


# ---------------------------------------------------------------------------
# GameData (gamedata.h), and gamedata_load / gamedata_save mirrored.

def new_char():
    return {"status": 0, "hp": 0, "max_hp": 0, "tp": 0, "max_tp": 0,
            "level": 0, "experience": 0, "strength": 0, "mental": 0,
            "agility": 0, "luck": 0, "dexterity": 0, "attack_base": 0,
            "attack": 0, "defense": 0, "equip": [0] * 5, "techs_a": 0,
            "techs_b": 0, "item_count": 0, "items": [0] * ITEM_SLOTS,
            "name": bytes(NAME_LEN)}


def new_gamedata():
    return {"chr": [new_char() for _ in range(CHARS)],
            "party_members_num": 0, "party_members_joined": 0,
            "party_member_join_next": 0, "party_member_id": [0] * PARTY_MAX,
            "money": 0, "level_index": 0, "level_y": 0, "level_x": 0,
            "jet_scooter": 0, "planet": 0, "save_name": bytes(NAME_LEN),
            "save_level": 0, "c600": bytearray(PARTY_SIZE),
            "c700": bytearray(0x100)}


def pack_c600(gd):
    raw = bytearray(gd["c600"])
    put16(raw, P_MEMBERS_NUM, gd["party_members_num"])
    put16(raw, P_JOINED, gd["party_members_joined"])
    put16(raw, P_JOIN_NEXT, gd["party_member_join_next"])
    for i in range(PARTY_MAX):
        put16(raw, P_MEMBER_ID + i * 2, gd["party_member_id"][i])
    put16(raw, P_MONEY, gd["money"] >> 16)
    put16(raw, P_MONEY + 2, gd["money"] & 0xFFFF)
    put16(raw, P_LEVEL, gd["level_index"])
    put16(raw, P_LEVEL_Y, gd["level_y"])
    put16(raw, P_LEVEL_X, gd["level_x"])
    put16(raw, P_JET_SCOOTER, gd["jet_scooter"])
    put16(raw, P_PLANET, gd["planet"])
    for c in range(CHARS):
        raw[P_NAMES + c * NAME_LEN:P_NAMES + (c + 1) * NAME_LEN] = gd["chr"][c]["name"]
    raw[P_SLOT_NAME:P_SLOT_NAME + NAME_LEN] = gd["save_name"]
    put16(raw, P_SLOT_LEVEL, gd["save_level"])
    return raw


def unpack_c600(gd):
    raw = gd["c600"]
    gd["party_members_num"] = get16(raw, P_MEMBERS_NUM)
    gd["party_members_joined"] = get16(raw, P_JOINED)
    gd["party_member_join_next"] = get16(raw, P_JOIN_NEXT)
    gd["party_member_id"] = [get16(raw, P_MEMBER_ID + i * 2) for i in range(PARTY_MAX)]
    gd["money"] = get16(raw, P_MONEY) << 16 | get16(raw, P_MONEY + 2)
    gd["level_index"] = get16(raw, P_LEVEL)
    gd["level_y"] = get16(raw, P_LEVEL_Y)
    gd["level_x"] = get16(raw, P_LEVEL_X)
    gd["jet_scooter"] = get16(raw, P_JET_SCOOTER)
    gd["planet"] = get16(raw, P_PLANET)
    for c in range(CHARS):
        gd["chr"][c]["name"] = bytes(raw[P_NAMES + c * NAME_LEN:P_NAMES + (c + 1) * NAME_LEN])
    gd["save_name"] = bytes(raw[P_SLOT_NAME:P_SLOT_NAME + NAME_LEN])
    gd["save_level"] = get16(raw, P_SLOT_LEVEL)


def read_hex(rows, data, start, count):
    n = 0
    for row in children(rows):
        if not isinstance(row, str):
            continue
        s = row
        p = 0
        while p + 1 < len(s):
            hi = int(s[p], 16) if s[p] in "0123456789abcdefABCDEF" else -1
            lo = int(s[p + 1], 16) if s[p + 1] in "0123456789abcdefABCDEF" else -1
            if hi >= 0 and lo >= 0 and n < count:
                data[start + n] = hi << 4 | lo
                n += 1
            p += 2


def read_flags(v, c700, start, count, warn):
    if v is None:
        return
    if isinstance(v, list):
        read_hex(v, c700, start, count)
        return
    if not isinstance(v, dict):
        return
    for k, m in v.items():
        i = -1
        if k[:1] in ("c", "C") and len(k.encode("utf-8")) >= 4 \
                and re.fullmatch(r"[0-9A-Fa-f]{3}", k[1:4]):
            a = int(k[1:4], 16)
            if 0x700 <= a <= 0x7FF:
                i = a - 0x700
        if i < start or i >= start + count:
            warn('flag key "%s" is not a c7XX address in this section; skipped' % k)
            continue
        b = -1
        if isinstance(m, bool):
            b = 1 if m else 0
        elif is_number(m):
            b = int(m)
        elif isinstance(m, str):
            if m == "shut":
                b = 0
            elif m == "taken":
                b = 1
            elif m == "locked":
                b = 0x80
            elif m[:1] == "$":
                b = c_strtol_hex(m[1:])
        if b < 0 or b > 0xFF:
            warn('flag "%s" has a value that is not a byte; skipped' % k)
            continue
        c700[i] = b


def read_map(tables, obj, key, fallback, warn):
    v = c_get(obj, key)
    if v is None:
        return fallback
    if is_number(v):
        return int(v) & 0xFFFF
    if isinstance(v, str):
        level = tables.level_id(v)
        if level >= 0:
            return level
        warn('"%s" is not a map\'s name; kept %u' % (v, fallback))
    return fallback


def read_recruit(party, key, old_key, fallback):
    v = c_get(party, key)
    if not c_has(party, key):
        return c_int(party, old_key, fallback) & 0xFFFF
    if not isinstance(v, str):
        return c_int(party, key, fallback) & 0xFFFF
    if v == "none":
        return 0
    if v == "Shir away":
        return CHARS
    for c in range(1, CHARS):
        if v == CHAR_ID[c]:
            return c
    return fallback


def read_char(tables, src, d, warn):
    eq = c_get(src, "equipment")
    inv = c_get(src, "inventory")
    d["name"] = name_from_ascii(c_str(src, "name", ""))
    d["level"] = c_int(src, "level", d["level"]) & 0xFFFF
    d["experience"] = c_int(src, "experience", 0) & 0xFFFFFFFF
    d["hp"] = c_int(src, "hp", 0) & 0xFFFF
    d["max_hp"] = c_int(src, "max_hp", d["hp"]) & 0xFFFF
    d["tp"] = c_int(src, "tp", 0) & 0xFFFF
    d["max_tp"] = c_int(src, "max_tp", d["tp"]) & 0xFFFF
    for k in ("strength", "mental", "agility", "luck", "dexterity", "attack_base"):
        d[k] = c_int(src, k, 0) & 0xFFFF
    d["attack"] = c_int(src, "attack", d["attack_base"]) & 0xFFFF
    d["defense"] = c_int(src, "defense", 0) & 0xFFFF
    if c_has(src, "battle_techniques_known"):
        d["techs_a"] = ((c_int(src, "battle_techniques_known", 0) & 0x7F)
                        | (0x80 if c_bool(src, "battle_musik", False) else 0)) & 0xFF
    else:
        d["techs_a"] = c_int(src, "techniques_a", 0) & 0xFF
    if c_has(src, "map_techniques_known"):
        d["techs_b"] = ((c_int(src, "map_techniques_known", 0) & 0x7F)
                        | (0x80 if c_bool(src, "knows_musik", False) else 0)) & 0xFF
    else:
        d["techs_b"] = c_int(src, "techniques_b", 0) & 0xFF
    d["status"] = c_int(src, "status", 0) & 0xFFFF
    for s in range(5):
        d["equip"][s] = tables.item_by_key(c_str(eq, EQUIP_KEY[s], "None"), warn)
    d["items"] = [0] * ITEM_SLOTS
    d["item_count"] = 0
    for e in children(inv):
        if d["item_count"] >= ITEM_SLOTS:
            break
        item = tables.item_by_key(c_str(e, "item", "None"), warn)
        d["items"][d["item_count"]] = (item | (0x80 if c_bool(e, "equipped", False) else 0)) & 0xFF
        d["item_count"] += 1


def load_port_save(tables, text, warn=None):
    """gamedata_load (gamedata.c), field for field and in its order."""
    warnings = []
    if warn is None:
        warn = warnings.append
    root = json.loads(text, object_pairs_hook=first_wins)
    gd = new_gamedata()

    read_hex(c_get(root, "raw_c600"), gd["c600"], 0, PARTY_SIZE)
    unpack_c600(gd)

    read_flags(c_get(root, "flags_c700"), gd["c700"], 0x00, 0x10, warn)
    read_flags(c_get(root, "event_flags"), gd["c700"], 0x10, 0x70, warn)
    read_flags(c_get(root, "treasure_chest_flags"), gd["c700"], 0x80, 0x80, warn)

    label = c_get(root, "slot_label")
    gd["save_name"] = name_from_ascii(c_str(label, "name", ""))
    gd["save_level"] = c_int(label, "level", gd["save_level"]) & 0xFFFF

    gd["money"] = c_int(root, "money", gd["money"]) & 0xFFFFFFFF

    loc = c_get(root, "location")
    planet = c_str(loc, "planet", PLANET[gd["planet"] & 1])
    gd["planet"] = 1 if planet == PLANET[1] else 0
    gd["level_index"] = read_map(tables, loc, "map", gd["level_index"], warn)
    gd["level_x"] = c_int(loc, "x", gd["level_x"]) & 0xFFFF
    gd["level_y"] = c_int(loc, "y", gd["level_y"]) & 0xFFFF
    gd["jet_scooter"] = 1 if c_bool(loc, "on_jet_scooter", gd["jet_scooter"] != 0) else 0
    parked = c_get(loc, "jet_scooter_parked")
    if c_has(loc, "jet_scooter_parked"):
        raw = gd["c600"]
        put16(raw, P_SCOOTER_X, c_int(parked, "x", get16(raw, P_SCOOTER_X)) & 0xFFFF)
        put16(raw, P_SCOOTER_Y, c_int(parked, "y", get16(raw, P_SCOOTER_Y)) & 0xFFFF)

    raw = gd["c600"]
    last = c_get(root, "last_save")
    if c_has(root, "last_save"):
        lp = c_str(last, "planet", PLANET[get16(raw, P_LAST_SAVE_PLANET) & 1])
        put16(raw, P_LAST_SAVE_PLANET, 1 if lp == PLANET[1] else 0)
        put16(raw, P_LAST_SAVE_VISIPHONE,
              1 if c_bool(last, "by_visiphone", get16(raw, P_LAST_SAVE_VISIPHONE) != 0) else 0)
    for key, off in (("telepipe_lands_at", P_LAST_SAVE_MAP),
                     ("dungeon_entrance", P_ENTRANCE_MAP)):
        p = c_get(root, key)
        if not c_has(root, key):
            continue
        put16(raw, off, read_map(tables, p, "map", get16(raw, off), warn))
        put16(raw, off + 2, c_int(p, "y", get16(raw, off + 2)) & 0xFFFF)
        put16(raw, off + 4, c_int(p, "x", get16(raw, off + 4)) & 0xFFFF)
    kept = c_get(root, "tower_room_items")
    if isinstance(kept, list):
        n = 0
        raw[P_KEEPER_ITEMS:P_KEEPER_ITEMS + ITEM_SLOTS] = bytes(ITEM_SLOTS)
        for e in kept:
            if n >= ITEM_SLOTS:
                break
            item = tables.item_by_key(e, warn) if isinstance(e, str) else 0
            if item <= 0:
                continue
            raw[P_KEEPER_ITEMS + n] = item
            n += 1
        raw[P_KEEPER_COUNT] = n

    party = c_get(root, "party")
    members = c_get(party, "members")
    count = len(children(members))
    if count > 0:
        count = min(count, PARTY_MAX)
        gd["party_member_id"] = [0] * PARTY_MAX
        for i in range(count):
            m = members[i] if isinstance(members, list) else None
            c = CHAR_ID.index(m) if isinstance(m, str) and m in CHAR_ID else -1
            gd["party_member_id"][i] = c if c >= 0 else 0
        gd["party_members_num"] = count - 1
    gd["party_members_joined"] = read_recruit(party, "last_recruit_joined",
                                              "recruits_joined", gd["party_members_joined"])
    gd["party_member_join_next"] = read_recruit(party, "last_recruit_sent",
                                                "next_recruit_event", gd["party_member_join_next"])

    for src in children(c_get(root, "characters")):
        cid = c_str(src, "id", None)
        if cid in CHAR_ID:
            read_char(tables, src, gd["chr"][CHAR_ID.index(cid)], warn)

    gd["c600"] = pack_c600(gd)
    gd["warnings"] = warnings
    return gd


def map_value(tables, level):
    name = tables.level_label(level)
    return '"%s"' % name if name is not None else "%d" % level


def dump_port_save(tables, gd):
    """gamedata_save (gamedata.c): the same text, byte for byte."""
    raw = pack_c600(gd)
    out = []
    w = out.append
    w("{\n")
    w('  "format": %d,\n' % SAVE_FORMAT)
    w('  "slot_label": { "name": "%s", "level": %u },\n'
      % (name_to_ascii(gd["save_name"]), gd["save_level"]))
    w('  "money": %u,\n' % gd["money"])
    w('  "location": { "planet": "%s", "map": %s, "x": %u, "y": %u, "on_jet_scooter": %s,\n'
      '                "jet_scooter_parked": { "x": %u, "y": %u } },\n'
      % (PLANET[gd["planet"] & 1], map_value(tables, gd["level_index"]), gd["level_x"],
         gd["level_y"], "true" if gd["jet_scooter"] else "false",
         get16(raw, P_SCOOTER_X), get16(raw, P_SCOOTER_Y)))
    w('  "last_save": { "planet": "%s", "by_visiphone": %s },\n'
      % (PLANET[get16(raw, P_LAST_SAVE_PLANET) & 1],
         "true" if get16(raw, P_LAST_SAVE_VISIPHONE) != 0 else "false"))
    for key, off in (("telepipe_lands_at", P_LAST_SAVE_MAP),
                     ("dungeon_entrance", P_ENTRANCE_MAP)):
        w('  "%s": { "map": %s, "x": %u, "y": %u },\n'
          % (key, map_value(tables, get16(raw, off)), get16(raw, off + 4), get16(raw, off + 2)))
    w('  "party": {\n    "members": [')
    i = 0
    while i <= gd["party_members_num"] and i < PARTY_MAX:
        w('%s"%s"' % ("" if i == 0 else ", ", CHAR_ID[gd["party_member_id"][i] % CHARS]))
        i += 1
    w("],\n")
    w('    "last_recruit_joined": ' + recruit_text(gd["party_members_joined"]))
    w(',\n    "last_recruit_sent": ' + recruit_text(gd["party_member_join_next"]))
    w("\n  },\n")
    w('  "tower_room_items": [')
    for i in range(min(raw[P_KEEPER_COUNT], ITEM_SLOTS)):
        w('%s"%s"' % ("" if i == 0 else ", ", tables.item_key(raw[P_KEEPER_ITEMS + i] & 0x7F)))
    w("],\n")
    w('  "characters": [\n')
    for c in range(CHARS):
        dump_char(tables, w, gd["chr"][c], c, c + 1 < CHARS)
    w("  ],\n")
    dump_flags(tables, w, "flags_c700", gd["c700"], 0x00, 0x10, False, True)
    dump_flags(tables, w, "event_flags", gd["c700"], 0x10, 0x70, False, True)
    dump_flags(tables, w, "treasure_chest_flags", gd["c700"], 0x80, 0x80, True, True)
    w('  "raw_c600": [\n')
    for row in range(0, PARTY_SIZE, 16):
        w('    "%s"%s\n' % (raw[row:row + 16].hex().upper(), "," if row + 16 < PARTY_SIZE else ""))
    w("  ]\n")
    w("}\n")
    return "".join(out)


def recruit_text(value):
    if value == 0:
        return '"none"'
    if value < CHARS:
        return '"%s"' % CHAR_ID[value]
    if value == CHARS:
        return '"Shir away"'
    return "%u" % value


def dump_char(tables, w, d, index, comma):
    w("    {\n")
    w('      "id": "%s",\n' % CHAR_ID[index])
    w('      "name": "%s",\n' % name_to_ascii(d["name"]))
    w('      "level": %u,\n' % d["level"])
    w('      "experience": %u,\n' % d["experience"])
    w('      "hp": %u, "max_hp": %u,\n' % (d["hp"], d["max_hp"]))
    w('      "tp": %u, "max_tp": %u,\n' % (d["tp"], d["max_tp"]))
    w('      "strength": %u, "mental": %u, "agility": %u,\n'
      % (d["strength"], d["mental"], d["agility"]))
    w('      "luck": %u, "dexterity": %u,\n' % (d["luck"], d["dexterity"]))
    w('      "attack_base": %u, "attack": %u, "defense": %u,\n'
      % (d["attack_base"], d["attack"], d["defense"]))
    w('      "battle_techniques_known": %u, "map_techniques_known": %u, "knows_musik": %s,\n'
      % (d["techs_a"] & 0x7F, d["techs_b"] & 0x7F, "true" if d["techs_b"] & 0x80 else "false"))
    if d["techs_a"] & 0x80:
        w('      "battle_musik": true,\n')
    w('      "status": %u,\n' % d["status"])
    w('      "equipment": {')
    for s in range(5):
        w('%s"%s": "%s"' % (" " if s == 0 else ", ", EQUIP_KEY[s], tables.item_key(d["equip"][s])))
    w(" },\n")
    w('      "inventory": [')
    for i in range(min(d["item_count"], ITEM_SLOTS)):
        w('%s\n        { "item": "%s", "equipped": %s }'
          % ("" if i == 0 else ",", tables.item_key(d["items"][i]),
             "true" if d["items"][i] & 0x80 else "false"))
    w("%s]\n" % ("\n      " if d["item_count"] != 0 else ""))
    w("    }%s\n" % ("," if comma else ""))


def dump_flags(tables, w, key, c700, start, count, chests, comma):
    w('  "%s": {\n' % key)
    for i in range(start, start + count):
        name = tables.flag_name[i]
        w('    "c%03X%s%s": ' % (0x700 + i, "_" if name else "", name))
        v = c700[i]
        if chests and v in (0, 0x80, 1):
            w('"%s"' % ("shut" if v == 0 else "taken" if v == 1 else "locked"))
        else:
            w("%u" % v)
        w("%s\n" % ("," if i + 1 < start + count else ""))
    w("  }%s\n" % ("," if comma else ""))


# ---------------------------------------------------------------------------
# GameData and the block SaveData writes.

def record_from_char(d):
    r = bytearray(RECORD)
    for off, key in ((0x00, "status"), (0x02, "hp"), (0x04, "max_hp"), (0x06, "tp"),
                     (0x08, "max_tp"), (0x0A, "level"), (0x10, "strength"),
                     (0x12, "mental"), (0x14, "agility"), (0x16, "luck"),
                     (0x18, "dexterity"), (0x1A, "attack_base"), (0x1C, "attack"),
                     (0x1E, "defense")):
        put16(r, off, d[key])
    put16(r, 0x0C, d["experience"] >> 16)
    put16(r, 0x0E, d["experience"] & 0xFFFF)
    r[0x20:0x25] = bytes(d["equip"])
    r[0x25] = d["techs_a"]
    r[0x26] = d["techs_b"]
    r[0x27] = d["item_count"]
    r[0x28:0x38] = bytes(d["items"])
    return r


def char_from_record(r):
    d = new_char()
    for off, key in ((0x00, "status"), (0x02, "hp"), (0x04, "max_hp"), (0x06, "tp"),
                     (0x08, "max_tp"), (0x0A, "level"), (0x10, "strength"),
                     (0x12, "mental"), (0x14, "agility"), (0x16, "luck"),
                     (0x18, "dexterity"), (0x1A, "attack_base"), (0x1C, "attack"),
                     (0x1E, "defense")):
        d[key] = get16(r, off)
    d["experience"] = get16(r, 0x0C) << 16 | get16(r, 0x0E)
    d["equip"] = list(r[0x20:0x25])
    d["techs_a"] = r[0x25]
    d["techs_b"] = r[0x26]
    d["item_count"] = r[0x27]
    d["items"] = list(r[0x28:0x38])
    return d


def checksum(block):
    """loc_D676 / SaveData: the bytes before the checksum, add.w."""
    return sum(block[:SUMMED]) & 0xFFFF


def block_from_gamedata(gd, in_use=True):
    """The RAM SaveData copies into a slot, after the Data Memory's save
    (loc_B5A4) has marked it: records, flags, party block, checksum,
    copyright."""
    party = pack_c600(gd)
    if in_use:
        put16(party, P_SLOT_IN_USE, 0xFFFF)
    block = bytearray(BLOCK)
    for c in range(CHARS):
        rec = record_from_char(gd["chr"][c])
        name = party[P_NAMES + c * NAME_LEN:P_NAMES + (c + 1) * NAME_LEN]
        rec[0x38:0x40] = name_tiles(name)
        block[OFF_RECORDS + c * RECORD:OFF_RECORDS + (c + 1) * RECORD] = rec
    block[OFF_FLAGS:OFF_FLAGS + 0x100] = gd["c700"]
    block[OFF_PARTY:OFF_PARTY + PARTY_SIZE] = party
    put16(block, OFF_CHECKSUM, checksum(block))
    block[OFF_COPYRIGHT:BLOCK] = COPYRIGHT
    return bytes(block)


def gamedata_from_block(block):
    """What a slot holds, in the port's terms (the RAM LoadSavedData fills)."""
    gd = new_gamedata()
    gd["c600"] = bytearray(block[OFF_PARTY:OFF_PARTY + PARTY_SIZE])
    unpack_c600(gd)
    for c in range(CHARS):
        rec = block[OFF_RECORDS + c * RECORD:OFF_RECORDS + (c + 1) * RECORD]
        d = char_from_record(rec)
        d["name"] = gd["chr"][c]["name"]
        gd["chr"][c] = d
    gd["c700"] = bytearray(block[OFF_FLAGS:OFF_FLAGS + 0x100])
    gd["warnings"] = []
    return gd


# ---------------------------------------------------------------------------
# The battery file.

class Srm:
    """The cartridge's 8 KB as an emulator's file lays them out."""

    def __init__(self, image, layout, filler):
        self.image = bytearray(image)
        self.layout = layout        # "odd", "even" or "packed"
        self.filler = filler

    @staticmethod
    def new():
        """Genesis Plus GX's layout, every block empty but valid: zeros, the
        copyright and the checksum, not in use."""
        srm = Srm(b"\xFF" * (2 * DATA_SIZE), "odd", 0xFF)
        empty = bytearray(BLOCK)
        put16(empty, OFF_CHECKSUM, checksum(empty))
        empty[OFF_COPYRIGHT:] = COPYRIGHT
        for b in range(2 * SLOTS):
            srm.put_block(b, empty)
        return srm

    @staticmethod
    def load(path):
        with open(path, "rb") as fh:
            image = fh.read()
        return Srm.parse(image, path)

    @staticmethod
    def parse(image, what="the file"):
        candidates = []
        if len(image) == DATA_SIZE:
            candidates.append(("packed", None))
        if len(image) > DATA_SIZE:
            candidates += [("odd", image[0::2]), ("even", image[1::2])]
        if not candidates:
            raise SystemExit("%s: %d bytes; a battery file for this game is 8 KB "
                             "(the data alone) or 16 KB and up (odd or even bytes)"
                             % (what, len(image)))
        # The layout whose blocks carry the copyright string, else the one
        # whose other bytes are all one value (a file the game never wrote).
        for layout, other in candidates:
            srm = Srm(image, layout, None if other is None else other[0])
            if any(srm.block(b)[OFF_COPYRIGHT:] == COPYRIGHT for b in range(2 * SLOTS)):
                if other is not None:
                    srm.filler = Srm.filler_of(srm)
                return srm
        for layout, other in candidates:
            if other is None or len(set(other)) == 1:
                return Srm(image, layout, None if other is None else other[0])
        raise SystemExit("%s: no block carries the copyright string and the layout "
                         "cannot be told; pass --new for a fresh file" % what)

    @staticmethod
    def filler_of(srm):
        other = srm.image[0::2] if srm.layout == "odd" else srm.image[1::2]
        return max(set(other), key=other.count)

    def offset(self, i):
        if self.layout == "packed":
            return i
        return 2 * i + (1 if self.layout == "odd" else 0)

    def block(self, b):
        start = self.offset(b * BLOCK)
        if self.layout == "packed":
            return bytes(self.image[start:start + BLOCK])
        chunk = self.image[start:start + 2 * BLOCK:2]
        return bytes(chunk) + bytes(BLOCK - len(chunk))

    def put_block(self, b, data):
        if self.layout != "packed" and len(self.image) < 2 * DATA_SIZE:
            self.image += bytes([self.filler]) * (2 * DATA_SIZE - len(self.image))
        start = self.offset(b * BLOCK)
        if self.layout == "packed":
            self.image[start:start + BLOCK] = data
        else:
            self.image[start:start + 2 * BLOCK:2] = data

    def copy(self):
        return Srm(self.image, self.layout, self.filler)

    def describe(self):
        if self.layout == "packed":
            return "%d bytes, the data alone" % len(self.image)
        return "%d bytes, the data on the %s bytes, $%02X on the others" % (
            len(self.image), self.layout, self.filler)


# ---------------------------------------------------------------------------
# The ROM's side, worked through.

def checksum_good(block):
    """loc_D676: 0 (beq) when the stored word is the sum."""
    return get16(block, OFF_CHECKSUM) == checksum(block)


def copyright_good(block):
    """loc_D6AA: the 96 bytes from $C6A0's place against CopyrightString."""
    return block[OFF_COPYRIGHT:BLOCK] == COPYRIGHT


def power_on_check(srm):
    """What the select-game screen does once a power-on (loc_D1E0): the
    set-up (loc_D700), then "Let me check the backup data" a slot at a time
    (loc_D20E). Returns per slot what happened; repairs `srm` in place as
    the ROM would. "overwritten" means the ROM writes whatever RAM holds
    over the slot (SaveData from the select screen's RAM): a lost slot."""
    report = []
    for slot in range(SLOTS):
        main, mirror = srm.block(slot), srm.block(slot + MIRROR)
        # loc_D700: neither copy has the copyright -> SaveData over both.
        if not copyright_good(main) and not copyright_good(mirror):
            report.append((slot, "overwritten", "neither copy has the copyright "
                           "string (loc_D700 saves the RAM over it)"))
            continue
        # loc_D20E: only a slot marked in use, in either copy, is checked.
        if main[OFF_PARTY + P_SLOT_IN_USE] == 0 and mirror[OFF_PARTY + P_SLOT_IN_USE] == 0:
            report.append((slot, "empty", "not in use in either copy"))
            continue
        if not checksum_good(main):
            if checksum_good(mirror):
                srm.put_block(slot, mirror)             # loc_D6D4 (slot)
                report.append((slot, "repaired", "the main copy's checksum is bad; "
                               "copied from the mirror ($130E)"))
            else:
                report.append((slot, "overwritten", "both checksums bad: SaveData "
                               "writes the RAM over the slot ($130F)"))
            continue
        if not checksum_good(mirror):
            srm.put_block(slot + MIRROR, main)          # loc_D6D4 (slot + 4)
            report.append((slot, "repaired", "the mirror's checksum is bad; copied "
                           "from the main copy ($130E)"))
            continue
        report.append((slot, "good", "both copies pass ($130C)"))
    return report


def continue_slot(srm, slot):
    """CONTINUE on a slot (loc_D3B6): the main copy's in-use byte, then
    LoadSavedData. Returns the RAM it fills ({address: bytes}) and what the
    screen does next, or None for "There is no data for that number"."""
    main = srm.block(slot)
    if main[OFF_PARTY + P_SLOT_IN_USE] == 0:
        return None
    ram = {0xC000: main[0x000:0x200],                   # 512 bytes
           0xC700: main[0x200:0x300],                   # 256
           0xC600: main[0x300:0x400]}                   # 256, $C600-$C6FF
    party = ram[0xC600]
    visiphone = get16(party, P_LAST_SAVE_VISIPHONE)
    last_planet = get16(party, P_LAST_SAVE_PLANET)
    attendant = "Dezolis's" if visiphone == 0 and last_planet != 0 else "Motavia's"
    return {"ram": ram, "attendant": attendant,
            "level": get16(party, P_LEVEL), "x": get16(party, P_LEVEL_X),
            "y": get16(party, P_LEVEL_Y)}


def slot_list_line(block):
    """The slot list (loc_104EC): $C680's four letters and the word at
    $C684."""
    party = block[OFF_PARTY:]
    name = "".join(ascii_from_code(code) for code in party[P_SLOT_NAME:P_SLOT_NAME + 4])
    return "%-4s %3d" % (name, get16(party, P_SLOT_LEVEL))


# ---------------------------------------------------------------------------
# Commands

def slot_arg(text):
    n = int(text)
    if not 1 <= n <= SLOTS:
        raise argparse.ArgumentTypeError("a slot is 1-4")
    return n - 1


def cmd_write(args):
    tables = Tables()
    with open(args.save, "rb") as fh:
        text = fh.read().decode("utf-8")
    gd = load_port_save(tables, text, warn=lambda m: print("save: " + m, file=sys.stderr))
    if args.label is not None:
        if len(args.label.encode("utf-8")) > NAME_LEN:
            raise SystemExit("--label: four letters at most")
        gd["save_name"] = name_from_ascii(args.label)
    block = block_from_gamedata(gd)

    if args.new:
        srm = Srm.new()
    elif args.srm:
        srm = Srm.load(args.srm)
    else:
        raise SystemExit("write: --srm FILE (the emulator's file, kept as it is "
                         "but for the slot) or --new")
    before = srm.copy()
    srm.put_block(args.slot, block)
    srm.put_block(args.slot + MIRROR, block)

    problems = verify_written(tables, srm, before, args.slot, block, gd)
    for p in problems:
        print("FAIL  " + p, file=sys.stderr)
    if problems:
        raise SystemExit(1)

    out = args.output or args.srm
    if out is None:
        raise SystemExit("write --new: -o OUT.srm")
    if out == args.srm and os.path.exists(out):
        shutil.copyfile(out, out + ".bak")
    with open(out, "wb") as fh:
        fh.write(srm.image)
    res = continue_slot(srm, args.slot)
    print("%s: slot %d (and its mirror) from %s -- %s" % (
        out, args.slot + 1, args.save, srm.describe()))
    print("  slot list: %s; CONTINUE: %s Data Memory, then %s at x %d, y %d"
          % (slot_list_line(block), res["attendant"],
             map_name(tables, res["level"]), res["x"], res["y"]))


def map_name(tables, level):
    name = tables.level_label(level)
    return "%s ($%02X)" % (name, level) if name else "map $%02X" % level


def verify_written(tables, srm, before, slot, block, gd):
    """The ROM's power-on check and CONTINUE on the written file: the slot
    passes untouched, CONTINUE fills RAM with the block, the RAM reads back
    as the same port save, and nothing else in the file moved."""
    problems = []
    checked = srm.copy()
    for s, what, why in power_on_check(checked):
        if s == slot and what != "good":
            problems.append("slot %d: the power-on check says %s (%s)" % (s + 1, what, why))
    if checked.image != srm.image:
        moved = [s + 1 for s in range(SLOTS)
                 if checked.block(s) != srm.block(s) or checked.block(s + MIRROR) != srm.block(s + MIRROR)]
        if slot + 1 in moved:
            problems.append("slot %d: the power-on check changed it" % (slot + 1))
    res = continue_slot(checked, slot)
    if res is None:
        problems.append("slot %d: CONTINUE says there is no data" % (slot + 1))
        return problems
    ram = res["ram"]
    if ram[0xC000] + ram[0xC700] + ram[0xC600] != block:
        problems.append("slot %d: CONTINUE's RAM is not the block written" % (slot + 1))
    back = gamedata_from_block(block)
    if dump_port_save(tables, back) != dump_port_save(tables, gd):
        problems.append("slot %d: the block does not read back as the same save" % (slot + 1))
    for i in range(min(len(srm.image), len(before.image))):
        if srm.image[i] != before.image[i] and not in_slot(srm, i, slot):
            problems.append("byte $%04X outside slot %d changed" % (i, slot + 1))
            break
    # A file Genesis Plus GX trimmed of its trailing $FF grows back to the
    # 16 KB the cartridge's range covers when a block past its end is
    # written; any other change of size is a fault.
    grown = (srm.layout != "packed" and len(before.image) < 2 * DATA_SIZE
             and len(srm.image) == 2 * DATA_SIZE)
    if len(srm.image) != len(before.image) and not grown:
        problems.append("the file's size changed: %d -> %d" % (len(before.image), len(srm.image)))
    return problems


def in_slot(srm, i, slot):
    for b in (slot, slot + MIRROR):
        start = srm.offset(b * BLOCK)
        span = BLOCK if srm.layout == "packed" else 2 * BLOCK
        if start <= i < start + span:
            return True
    return False


def losses(tables, block):
    """What a port save cannot carry from this block: every byte the
    reader's save gives back differently, by what it is."""
    gd = gamedata_from_block(block)
    text = dump_port_save(tables, gd)
    again = block_from_gamedata(load_port_save(tables, text, warn=lambda m: None),
                                in_use=False)
    out = []
    for i in range(BLOCK):
        if block[i] != again[i]:
            out.append("%s: $%02X -> $%02X" % (byte_name(i), block[i], again[i]))
    return text, out


def byte_name(i):
    if i < OFF_FLAGS:
        c, off = divmod(i, RECORD)
        if off >= 0x38:
            return "%s's record +$%02X (the name's tiles)" % (CHAR_ID[c], off)
        return "%s's record +$%02X" % (CHAR_ID[c], off)
    if i < OFF_PARTY:
        return "$%04X (a flag)" % (0xC700 + i - OFF_FLAGS)
    return "$%04X" % (0xC600 + i - OFF_PARTY)


def cmd_read(args):
    tables = Tables()
    srm = Srm.load(args.srm)
    checked = srm.copy()
    for s, what, why in power_on_check(checked):
        if s == args.slot and what not in ("good", "empty"):
            print("slot %d: the ROM's power-on check: %s -- %s" % (s + 1, what, why),
                  file=sys.stderr)
    res = continue_slot(checked, args.slot)
    if res is None:
        raise SystemExit("slot %d: CONTINUE would say there is no data for that number"
                         % (args.slot + 1))
    block = checked.block(args.slot)
    text, lost = losses(tables, block)
    tails = [c for c in range(CHARS)
             if block[c * RECORD + 0x38:c * RECORD + 0x40]
             != name_tiles(block[OFF_PARTY + P_NAMES + c * 4:OFF_PARTY + P_NAMES + c * 4 + 4])]
    for c in tails:
        print("note: %s's name tiles are not what SetCharNames makes of the name"
              % CHAR_ID[c], file=sys.stderr)
    for line in lost:
        if "name's tiles" not in line:
            print("not carried by the port's save: " + line, file=sys.stderr)
    if args.output:
        with open(args.output, "w", newline="\n") as fh:
            fh.write(text)
        print("%s: slot %d of %s (%s)" % (args.output, args.slot + 1, args.srm,
                                           slot_list_line(block).strip()))
    else:
        sys.stdout.write(text)


def cmd_check(args):
    tables = Tables()
    srm = Srm.load(args.srm)
    print("%s: %s" % (args.srm, srm.describe()))
    checked = srm.copy()
    for s, what, why in power_on_check(checked):
        res = continue_slot(checked, s)
        line = "  slot %d: %-11s %s" % (s + 1, what, why)
        if res is not None:
            line += "\n          slot list %s; CONTINUE: %s Data Memory, then %s at x %d, y %d" % (
                slot_list_line(checked.block(s)), res["attendant"],
                map_name(tables, res["level"]), res["x"], res["y"])
        print(line)


# ---------------------------------------------------------------------------
# selftest

REMOVE = object()


def field_paths(root):
    """Every member of the save's objects, and the first item of each array,
    as key paths."""
    out = []

    def walk(v, path):
        if isinstance(v, dict):
            for k, m in v.items():
                out.append(path + (k,))
                walk(m, path + (k,))
        elif isinstance(v, list) and v:
            out.append(path + (0,))
            walk(v[0], path + (0,))
    walk(root, ())
    return out


def edit_at(root, path, value):
    root = json.loads(json.dumps(root), object_pairs_hook=first_wins)
    v = root
    for k in path[:-1]:
        v = v[k]
    if value is REMOVE:
        del v[path[-1]]
    else:
        v[path[-1]] = value
    return root


def c_resave(text=None, new=False, quiet=False):
    """The port's own load and save (save_resave), or its new game."""
    exe = os.path.join(TREE, "save_resave")
    if not os.path.exists(exe) and os.path.exists(exe + ".exe"):
        exe += ".exe"
    if not os.path.exists(exe):
        raise SystemExit("selftest needs save_resave: `make save_resave` in the tree")
    with tempfile.TemporaryDirectory() as tmp:
        src, dst = os.path.join(tmp, "in"), os.path.join(tmp, "out")
        os.makedirs(src)
        if new:
            argv = [exe, "--new", dst]
        else:
            with open(os.path.join(src, "slot1.json"), "wb") as fh:
                fh.write(text.encode("utf-8"))
            argv = [exe, src, dst]
        r = subprocess.run(argv, cwd=TREE, capture_output=True, text=True)
        if r.returncode != 0:
            if quiet:
                return None
            raise SystemExit("save_resave failed: " + r.stderr)
        with open(os.path.join(dst, "slot1.json"), "rb") as fh:
            return fh.read().decode("utf-8")


def cmd_selftest(args):
    tables = Tables()
    failures = 0

    def fail(msg):
        nonlocal failures
        failures += 1
        print("FAIL  " + msg)

    # 1. The mirror of gamedata_load against the port's C, every save.
    saves_dir = os.path.join(TREE, "routes", "saves")
    names = sorted(n for n in os.listdir(saves_dir) if n.endswith(".json"))
    template = Srm.load(args.srm) if args.srm else Srm.new()
    for n in names:
        with open(os.path.join(saves_dir, n), "rb") as fh:
            text = fh.read().decode("utf-8")
        canonical = c_resave(text)
        gd = load_port_save(tables, text, warn=lambda m: None)
        if dump_port_save(tables, gd) != canonical:
            fail("%s: the Python reading differs from the port's" % n)
            continue
        # 2. Into every slot of the template and out again.
        for slot in range(SLOTS):
            srm = template.copy()
            block = block_from_gamedata(gd)
            srm.put_block(slot, block)
            srm.put_block(slot + MIRROR, block)
            problems = verify_written(tables, srm, template, slot, block, gd)
            for p in problems:
                fail("%s into slot %d: %s" % (n, slot + 1, p))
            text_back, _ = losses(tables, block)
            if c_resave(text_back) != canonical:
                fail("%s into slot %d: read back, the port makes another save" % (n, slot + 1))
    print("%d port saves: the Python reading is the port's, and each written into "
          "slots 1-4 passes the ROM's check, CONTINUEs to the same RAM and reads "
          "back as the same save" % len(names))

    # 3. A battery file the ROM wrote: every block read and written back.
    if args.srm:
        srm = Srm.load(args.srm)
        print("%s: %s" % (args.srm, srm.describe()))
        for b in range(2 * SLOTS):
            block = srm.block(b)
            names_ok = all(block[c * RECORD + 0x38:c * RECORD + 0x40]
                           == name_tiles(block[OFF_PARTY + P_NAMES + c * 4:OFF_PARTY + P_NAMES + c * 4 + 4])
                           for c in range(CHARS))
            if not names_ok:
                fail("block %d: a record's name tiles are not SetCharNames' of its name" % b)
            if not checksum_good(block):
                fail("block %d: the ROM's own checksum does not add up here" % b)
            text, lost = losses(tables, block)
            again = block_from_gamedata(load_port_save(tables, text, warn=lambda m: None),
                                        in_use=False)
            in_use = block[OFF_PARTY + P_SLOT_IN_USE] != 0
            print("  block %d (%s %d): %s; read and written back: %s" % (
                b, "slot" if b < SLOTS else "mirror of slot", b % SLOTS + 1,
                slot_list_line(block).strip() if in_use else "not in use",
                "the same 1024 bytes" if again == block else "%d bytes differ" % len(lost)))
            for line in lost:
                print("      " + line)
            if c_resave(text) != text:
                fail("block %d: the port's C reads the reader's save differently" % b)
        report = power_on_check(srm.copy())
        print("  the power-on check on it: " + ", ".join(
            "slot %d %s" % (s + 1, what) for s, what, _ in report))

        # 4. The port's new game beside the ROM's (the empty slots hold the
        # RAM a new game leaves: loc_D700 saved it at the first power-on).
        new_text = c_resave(new=True)
        empty = [b for b in range(SLOTS) if srm.block(b)[OFF_PARTY + P_SLOT_IN_USE] == 0]
        if empty:
            rom = srm.block(empty[0])
            port = block_from_gamedata(load_port_save(tables, new_text), in_use=False)
            diff = [i for i in range(BLOCK) if rom[i] != port[i]]
            print("  the port's new game against the ROM's in slot %d: %d bytes differ"
                  % (empty[0] + 1, len(diff)))
            for i in diff:
                print("      %s: ROM $%02X, port $%02X" % (byte_name(i), rom[i], port[i]))

    # 3b. Every field of two saves taken away or given the wrong kind of
    # value, one at a time: the Python reading must stay the port's where a
    # fallback, an old name or a type test decides (no route's save
    # exercises those).
    swept = 0
    for base, odd_raw in (("story_motavia.json", False), ("save_named_fields.json", False),
                          ("story_motavia.json", True)):
        with open(os.path.join(saves_dir, base), "rb") as fh:
            root = json.loads(fh.read().decode("utf-8"), object_pairs_hook=first_wins)
        if odd_raw:
            # raw_c600 full of values no game writes (a planet of $A5A5, ...),
            # so every fallback that reads it shows.
            pattern = bytes((0xA5 + 7 * i) & 0xFF for i in range(PARTY_SIZE)).hex().upper()
            root["raw_c600"] = [pattern[i:i + 32] for i in range(0, len(pattern), 32)]
            base += " (odd raw_c600)"
        for path in field_paths(root):
            for variant in (REMOVE, "x", 7, True, None, [], {}):
                edited = edit_at(root, path, variant)
                text = json.dumps(edited, indent=1)
                canonical = c_resave(text, quiet=True)
                if canonical is None:
                    continue        # the port's C refused it (a crash or no file)
                swept += 1
                gd = load_port_save(tables, text, warn=lambda m: None)
                if dump_port_save(tables, gd) != canonical:
                    fail("%s with %s %s: the Python reading differs from the port's"
                         % (base, "/".join(map(str, path)),
                            "removed" if variant is REMOVE else "= " + json.dumps(variant)))
    print("%d edited saves (a field removed or of another kind): the Python "
          "reading is the port's" % swept)

    # 5. The check catches what it should: a byte of a written slot's main
    # copy changed (repaired from the mirror), both copies changed (the slot
    # lost), and the same slot in the other layouts read back the same.
    with open(os.path.join(saves_dir, names[0]), "rb") as fh:
        gd = load_port_save(tables, fh.read().decode("utf-8"), warn=lambda m: None)
    block = block_from_gamedata(gd)
    bad = bytearray(block)
    bad[0x123] ^= 1
    for layout in ("odd", "even", "packed"):
        srm = Srm.new()
        if layout != "odd":
            data = b"".join(srm.block(b) for b in range(2 * SLOTS))
            image = data if layout == "packed" else bytes(
                x for pair in zip(data, b"\xFF" * DATA_SIZE) for x in pair)
            srm = Srm.parse(image)
            if srm.layout != layout:
                fail("a %s file was taken for %s" % (layout, srm.layout))
        srm.put_block(1, block)
        srm.put_block(1 + MIRROR, block)
        if Srm.parse(bytes(srm.image)).block(1) != block:
            fail("%s layout: the slot does not read back" % layout)
        one = srm.copy()
        one.put_block(1, bad)
        if power_on_check(one)[1][1] != "repaired" or one.block(1) != block:
            fail("%s layout: a changed main copy is not repaired from the mirror" % layout)
        both = srm.copy()
        both.put_block(1, bad)
        both.put_block(1 + MIRROR, bad)
        if power_on_check(both)[1][1] != "overwritten":
            fail("%s layout: two bad copies are not reported as a lost slot" % layout)
    print("the check: a changed main copy is repaired from the mirror, two bad "
          "copies lose the slot, in the odd, even and packed layouts")

    print("selftest: %s" % ("PASS" if failures == 0 else "%d FAILED" % failures))
    return 0 if failures == 0 else 1


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0],
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)

    w = sub.add_parser("write", help="a port save into a slot of a battery file")
    w.add_argument("save")
    w.add_argument("--slot", type=slot_arg, required=True, help="1-4")
    w.add_argument("--srm", help="the emulator's battery file: its layout and other slots")
    w.add_argument("--new", action="store_true", help="a fresh 16 KB file, other slots empty")
    w.add_argument("--label", help="the slot list's name, up to four letters")
    w.add_argument("-o", "--output")

    r = sub.add_parser("read", help="a slot of a battery file as a port save")
    r.add_argument("srm")
    r.add_argument("--slot", type=slot_arg, required=True, help="1-4")
    r.add_argument("-o", "--output")

    c = sub.add_parser("check", help="the ROM's power-on check and CONTINUE, every slot")
    c.add_argument("srm")

    t = sub.add_parser("selftest", help="round trips against the port's C (make save_resave)")
    t.add_argument("--srm", help="a battery file the ROM wrote, read and written back")

    args = ap.parse_args()
    if args.cmd == "write":
        cmd_write(args)
    elif args.cmd == "read":
        cmd_read(args)
    elif args.cmd == "check":
        cmd_check(args)
    else:
        sys.exit(cmd_selftest(args))


if __name__ == "__main__":
    main()
