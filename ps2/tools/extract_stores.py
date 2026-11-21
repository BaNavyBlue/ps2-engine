#!/usr/bin/env python3
"""Store stock: StoreEquipItemArray and StealItemArray, as JSON.

StoreEquipItemArray (after the technique tables) is six item ids per
store, in the order the doors index them: the door's $FFFFF766 byte picks
the store and its $FFFFF764 byte is the cursor's last index, so a
five-item stock with a trailing ItemID_None still takes six bytes.

StealItemArray is the four eight-entry lists Shir picks from when she
walks off with something (ProcessStealItem).

(build 185) From the ROM itself (tools/rommap.py), not ps2.asm. A store's
name ("Paseo Item Store") was the disassembly's comment over its block;
it is now what the comment said, read from the game: the one door that
opens the store (a level's interaction list, LevelData +14: y, x,
BuildingID_, portrait, $FFFFF764, $FFFFF766) is in a level, Level_Paseo,
and is a building, BuildingID_ItemStore.
"""

import argparse
import json
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from rommap import RomMap                                     # noqa: E402
from extract_gamedata import constant_names, id_keys, run_of  # noqa: E402

STOCK = 6               # item ids a store
STEAL = 8               # entries a steal list
LEVEL_SIZE = 20         # a LevelData record
DOOR_SIZE = 6           # an interaction-list entry
DOOR_LIST = 14          # the record's (palette << 24) | interaction list


def words(name):
    """`ItemStore` -> `Item Store`: an identifier's words."""
    return " ".join(re.findall(r"[A-Z][a-z0-9]*|[a-z0-9]+", name))


def store_doors(rom):
    """{store index: "<level> <building>"} from every door into a store.

    LoadLevelData reads a level's interaction list from its LevelData
    record; loc_6EE6 takes a door's building id, portrait, $FFFFF764 and
    $FFFFF766 from it, the last being the store. The levels are the
    Level_* records after LevelData, 20 bytes apart."""
    buildings = constant_names(rom, "BuildingID_")
    start = rom.addr("LevelData")
    levels, _ = run_of(rom, "LevelData", lambda n: n.startswith("Level_"))
    names = {}
    for i, (label, at) in enumerate(levels):
        if at != start + LEVEL_SIZE * i:
            sys.exit(f"{label} is not LevelData record {i}")
        door = rom.u32(at + DOOR_LIST) & 0xFFFFFF
        while rom.u8(door) != 0xFF:
            _, _, kind, _, _, store = rom.read(door, DOOR_SIZE)
            door += DOOR_SIZE
            building = buildings.get(kind, "")
            if kind == 0 or not building.endswith("Store"):
                continue
            name = f"{label[len('Level_'):]} {words(building)}"
            if names.setdefault(store, name) != name:
                sys.exit(f"store {store} is both {names[store]} and {name}")
    return names


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("rom", type=Path, help="the cartridge image")
    ap.add_argument("-o", "--out", type=Path, default=Path("assets/data"))
    args = ap.parse_args()

    rom = RomMap(args.rom)
    keys = id_keys(rom, "ItemID_")

    # --- stores ---
    raw = rom.blob("StoreEquipItemArray")
    if len(raw) % STOCK > 1:                # at most an `even` byte over
        sys.exit(f"StoreEquipItemArray is {len(raw)} bytes")
    names = store_doors(rom)
    stores = []
    for i in range(len(raw) // STOCK):
        if i not in names:
            sys.exit(f"no door opens store {i}, so it has no name")
        stores.append({"index": i, "name": names[i],
                       "items": [keys[b] for b in raw[i * STOCK:(i + 1) * STOCK]]})

    # --- Shir ---
    steal = {}
    lists, _ = run_of(rom, "StealItemArray",
                      lambda n: n.startswith("StealItemArray_"))
    for label, at in lists:
        if len(rom.blob(label)) < STEAL:
            sys.exit(f"{label} is {len(rom.blob(label))} entries")
        picks = rom.read(at, STEAL)
        steal[label[len("StealItemArray_"):].lower()] = [keys[b] for b in picks]

    args.out.mkdir(parents=True, exist_ok=True)
    (args.out / "stores.json").write_text(json.dumps(
        {"source": "StoreEquipItemArray", "stores": stores}, indent=1), newline="\n")
    (args.out / "steal.json").write_text(json.dumps(
        {"source": "StealItemArray", "lists": steal}, indent=1), newline="\n")
    print(f"{len(stores)} stores, {len(steal)} steal lists")


if __name__ == "__main__":
    main()
