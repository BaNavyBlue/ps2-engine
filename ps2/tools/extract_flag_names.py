#!/usr/bin/env python3
"""Names for the save's flag bytes, $C700-$C7FF: assets/data/flag_names.json.

$C700-$C70F is a handful of loose bytes, $C710-$C77F is event_flags
(ObjType_Events n uses event_flags + n), $C780-$C7FF is
treasure_chest_flags (chest n: 0 shut, $80 locked, 1 taken). Each byte
gets a name from, in order: the hand-checked table below (every entry
read in ps2.asm), the chest or event object the levels place with that
index, or the first routine that names the address. What is left is
"unused" -- nothing in the ROM reads it directly (an indexed use can
still reach it). An event's byte that nothing reads or writes is named
for its event with "_unused" on the end (build 97f); gen_rom_ids.py gives
those no FLAG_.

(build 185) From the ROM itself (tools/rommap.py), not ps2.asm: the
routines that name a byte are found by decoding the 68000 code (see
rom_refs), a level's name is its LevelID_ constant, and the objects come
from level_sprites.json and chests.json as before (extract_npcs.py).

usage: extract_flag_names.py ROM -o assets/data
"""
import argparse
import json
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from extract_npcs import decode  # noqa: E402
from rommap import RomMap, RomMapError  # noqa: E402

# Read in ps2.asm, and in the port's own comments where it was ported.
MANUAL = {
    0xC704: ("teleport_town_1", "loc_10F2A: the towns the teleport stations list, $C704-$C709, a level id each"),
    0xC705: ("teleport_town_2", "loc_10F2A"),
    0xC706: ("teleport_town_3", "loc_10F2A"),
    0xC707: ("teleport_town_4", "loc_10F2A"),
    0xC708: ("teleport_town_5", "loc_10F2A"),
    0xC709: ("teleport_town_6", "loc_10F2A"),
    0xC70A: ("dezolis_station_1", "loc_10F2A with $F764 set: the Dezolis stations, $C70A-$C70C"),
    0xC70B: ("dezolis_station_2", "loc_10F2A"),
    0xC70C: ("dezolis_station_3", "loc_10F2A"),
    0xC716: ("jet_scooter_owned", "ObjJetSctr_Init and LoadSpritesInLevel test it (2: a second state); Ustvestia_MusicPointers gates Secret Ways and Mystery on it"),
    0xC721: ("shure_f3_body_searched", "ObjType_Events $11 (loc_DBB2): 2 once the key and the Letter are taken"),
    0xC727: ("teim_given_the_letter", "ObjType_Events $17 (loc_DC64) sets it; Obj_Teim's init removes her once set"),
    0xC736: ("governor_errand_step", "Building_CentralTowerGovernor: 0 before the Recorder is handed in, 1 after the first debrief"),
    0xC737: ("nei_gone", "the library's news and the battle music read it (after Climatrol)"),
    0xC78E: ("chest_esper_mansion_f1_13_15_first", "LoadSpritesInLevel's tail (level $11): the mansion's two chests share one slot; while this is 0 the chest there is chest $0E, the Prism. Building_EsperMansion clears it (loc_CF0A) as Lutz wakes. A new game starts it at 1"),
    0xC78F: ("chest_esper_mansion_f1_nei_sword_first", "LoadSpritesInLevel's tail (level $11): while this is 0 the chest there is $0F, the Nei Sword (TreasureChestContentArray). Building_EsperMansion clears it (loc_CF38) when Lutz sees all eight Nei items. A new game starts it at 1"),
    0xC745: ("pandora_box_opened", "Noah's box (ObjType_Events $35, loc_E0BE) sets it before Dark Force; LoadSpritesInLevel stops placing the box once set. The disassembly's SkipDarumTeimEvent also writes it"),
    0xC747: ("mother_brain_fought", "set by the confrontation (ObjType_Events $37, loc_E1B6) as her battle starts; the approach (loc_70D6) no longer fires and LoadLevelData's tail (loc_92EE) puts block $28 on NoahF1 at $1B0"),
    0xC73F: ("central_tower_roof_open", "the tower's options list ROOF once set; Ustvestia_MusicPointers gates five songs on it"),
    # (build 97a) Bytes build 96b named in rom_ids.h (gen_rom_ids.py's
    # FLAG_NAMES, each read there) but the save still called by routine.
    0xC711: ("scene_camera", "event_flags' low byte: loc_809C's camera case"),
    0xC712: ("piata_tower_unlocked", "set at the Governor's first debrief (loc_CA58); Piata's tower door stays shut until then (loc_6ED6)"),
    0xC715: ("darum_scene_done", "set as the Darum scene ends (loc_EAB0); Darum's init (loc_4E2A) keeps him away after"),
    0xC722: ("recorder_found", "event RECORDER's own byte; the library's news reads it (loc_C406)"),
    0xC735: ("climatrol_destroyed", "set by the one-shot CLIMATROL_EXPLODES (loc_7F44); Motavia's map patch (loc_9258) and loc_585C read it"),
    0xC73E: ("gaira_off_orbit", "event GAIRA_EXPLOSION's own byte (loc_DFD0); the ring (loc_64A6) and the drifting scroll read it"),
    0xC742: ("door_espers_let_rolf_in", "event DOOR_ESPERS (loc_E05A) with Rolf alive; the door Espers step aside (loc_532C)"),
    0xC743: ("esper_b1_talks", "event ESPER_B1 (loc_E07A) counts the talks; Building_EsperMansion sets 3 (loc_4B40 and the Espers read it)"),
    0xC744: ("lutz_saw_nei_items", "set when Lutz sees all eight Nei items (loc_CF38): the Nei Sword"),
    # (build 97a) The dams, one byte a door or panel: each card sets the
    # byte of the event it is used on (loc_9C12: event_flags + the event;
    # loc_9D3E the door, loc_9D70 the panel). loc_81E6 wants all eight.
    # (build 97f) The per-place event bytes: each is event n's own byte
    # (event_flags + n), named for what reads or writes it. Read in full:
    # the events' routines (EventType_DialogueIndex), loc_E84A (the item
    # an event hands over: `addq.b #1, (a0)`), every indexed use of the
    # block -- loc_5656 (a chest object placed as an event reads its
    # event's byte), Obj_OpenableDoor and Obj_DestroyableDoor ($2F(a0)),
    # loc_9C12 (the items used on a door) and Ustvestia's songs -- and
    # the objects each event is placed with. "_unused": nothing reads or
    # writes that event's byte.
    0xC713: ("shure_bare_bodies_unused", "event DEAD_BODY_BARE ($03, loc_DA8C: $170B), Shure F3 57,33 and 82,37; nothing reads or writes it"),
    0xC714: ("uzo_fake_maruera_trees_unused", "event FAKE_MARUERA_TREE ($04, loc_DA98: $170F), six on Uzo; nothing reads or writes it"),
    0xC717: ("tower_keyboard_unused", "event KEYBOARD ($07, loc_DAD4: $1711, YES back to the saved place), Control Tower F1 48,26; nothing reads or writes it"),
    0xC718: ("skure_newspaper_1_opened", "event SKURE_NEWSPAPER_1 ($08, loc_DB22), Skure B1 47,45: a Motavian chest object whose loc_5656 shows it open while this is set (bits 0-6); nothing sets it, so it stays shut"),
    0xC719: ("skure_newspaper_2_opened", "event SKURE_NEWSPAPER_2 ($09, loc_DB30), Skure B1 25,49; as $C718"),
    0xC71A: ("skure_newspaper_3_opened", "event SKURE_NEWSPAPER_3 ($0A, loc_DB3E), Skure B1 25,69; as $C718"),
    0xC71B: ("skure_newspaper_4_opened", "event SKURE_NEWSPAPER_4 ($0B, loc_DB4C), Skure B1 65,73; as $C718"),
    0xC71C: ("skure_newspaper_5_opened", "event SKURE_NEWSPAPER_5 ($0C, loc_DB5A), Skure B2 43,23; as $C718"),
    0xC71D: ("skure_newspaper_6_opened", "event SKURE_NEWSPAPER_6 ($0D, loc_DB68), Skure B2 39,23; as $C718"),
    0xC71E: ("skure_newspaper_7_opened", "event SKURE_NEWSPAPER_7 ($0E, loc_DB76), Skure B2 41,27; as $C718"),
    0xC71F: ("underwater_passage_water_unused", "event WATER_COLOUR ($0F, loc_DB84: $1720), Motavia 159,12-13; the Maruera gum tests the event's number (loc_9C12), not its byte; nothing reads or writes it"),
    0xC720: ("maruera_leaf_taken", "event MARUERA_TREE ($10, loc_DB90), Uzo 141,17: loc_E84A adds 1 as it hands over the leaf; set, the tree answers $1708"),
    0xC723: ("tower_green_card_taken", "event TOWER_GREEN_CARD ($13, loc_DC1E), Control Tower F1 41,6: loc_E84A adds 1 as it hands over the card; set, the box answers $1708"),
    0xC724: ("tower_blue_card_taken", "event TOWER_BLUE_CARD ($14, loc_DC1E), Control Tower F1 45,6; as $C723"),
    0xC725: ("tower_yellow_card_taken", "event TOWER_YELLOW_CARD ($15, loc_DC1E), Control Tower F1 49,6; as $C723"),
    0xC726: ("tower_red_card_taken", "event TOWER_RED_CARD ($16, loc_DC1E), Control Tower F1 53,6; as $C723"),
    0xC728: ("nido_door_blown", "event DYNAMITE_DOOR_1 ($18), Nido ground floor 53,45: ItemAction_Dynamite's `bset #0`; the door there (Obj_DestroyableDoor) reads it"),
    0xC729: ("biosystems_lab_door_blown", "event DYNAMITE_DOOR_2 ($19), Biosystems Lab ground floor 47,73; as $C728"),
    0xC72A: ("biosystems_lab_f2_door_blown", "event DYNAMITE_DOOR_3 ($1A), Biosystems Lab F2 47,47; as $C728"),
    0xC72B: ("tube_shutter_opened", "event STRONG_SHUTTER ($1B), the tube's locked door 9,15: ItemAction_KeyTube's `bset #0`; the door there (Obj_OpenableDoor) reads it"),
    0xC73B: ("mount_uzo_unused", "event MOUNT_UZO ($2B, loc_DF24: $171F), Motavia 173,96; nothing reads or writes it"),
    0xC72C: ("green_dam_door", "the green card on the Green Dam's door (event $1C, ground floor 95,25; loc_9D3E)"),
    0xC72D: ("green_dam_panel", "the green card on the Green Dam's panel (event $1D, F1 113,28; loc_9D70); with Climatrol destroyed, patches Motavia's map (loc_92BC)"),
    0xC72E: ("blue_dam_door", "the blue card on the Blue Dam's door (event $1E, ground floor 53,27)"),
    0xC72F: ("blue_dam_panel", "the blue card on the Blue Dam's panel (event $1F, F3 21,32); with Climatrol destroyed, patches Motavia's map (loc_9258)"),
    0xC730: ("yellow_dam_door", "the yellow card on the Yellow Dam's door (event $20, ground floor 17,27)"),
    0xC731: ("yellow_dam_panel", "the yellow card on the Yellow Dam's panel (event $21, F2 51,24); with Climatrol destroyed, patches Motavia's map (loc_92AE)"),
    0xC732: ("red_dam_door", "the red card on the Red Dam's door (event $22, F1 47,17)"),
    0xC733: ("red_dam_panel", "the red card on the Red Dam's panel (event $23, F2 45,20); with Climatrol destroyed, patches Motavia's map (loc_92A0)"),
}


def level_names(rom):
    """level id -> its LevelID_ constant's name. A value two constants
    share is left out, for the level's record name to stand in: the
    disassembly has one such slip (LevelID_GuaronF3 is id(Level_GuaronF4),
    so it and LevelID_GuaronF4 are both $57), and which of the two a
    level is would be a guess."""
    by_value = {}
    for name in sorted(rom.consts):
        if name.startswith("LevelID_"):
            by_value.setdefault(rom.const(name), []).append(name[8:])
    return {v: names[0] for v, names in by_value.items() if len(names) == 1}


def snake(s):
    s = re.sub(r"(?<=[a-z0-9])(?=[A-Z])", "_", s)
    return re.sub(r"[^a-z0-9]+", "_", s.lower()).strip("_")


FLAG_BYTES = range(0xFFC700, 0xFFC800)


def rom_refs(rom):
    """address -> [label] for direct references: every instruction with an
    absolute operand in $C700-$C7FF ($FFFFC7xx.w or .l), under the label
    whose block it is in, in the ROM's order.

    The code is found by decoding: a label's block (to the next label in
    ps2.asm's order) is code when it decodes as 68000 instructions all
    the way to the next label, and data otherwise -- art, text and tables
    stop at an opcode the 68000 does not have or end off an instruction's
    boundary. Every label is looked at, so a map kept to the names the
    extractors use keeps them all.

    event_flags and treasure_chest_flags themselves are left out: they are
    the bases the event and chest code index from (`lea (event_flags).w,
    a2` and then `(a2,d0.w)`), and `move.w #$A00, (event_flags).w` sets
    the event word, not a byte of its own. (As ps2.asm writes them, by
    name, the reader before build 185 did not count them either; it
    counted `$FFFFC7xx` and `event_flags+$xx`.)
    """
    bases = {rom.const("event_flags") & 0xFFFFFF,
             rom.const("treasure_chest_flags") & 0xFFFFFF}
    refs = {}
    labels = rom.labels
    for i, (label, start) in enumerate(labels):
        rom.index(label)
        end = labels[i + 1][1] if i + 1 < len(labels) else len(rom.rom)
        found, at = [], start
        try:
            while at < end:
                insn = decode(rom, at)
                for kind, _, value in insn.ops:
                    if kind == "abs" and value in FLAG_BYTES \
                            and value not in bases:
                        found.append(0xC700 + (value & 0xFF))
                at += insn.length
        except (ValueError, RomMapError, IndexError):
            continue
        if at != end:
            continue
        for address in found:
            refs.setdefault(address, []).append(label)
    return refs


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("rom", type=Path, help="the cartridge image")
    ap.add_argument("-o", "--out", default="assets/data")
    ap.add_argument("--assets", default="assets")
    a = ap.parse_args()

    rom = RomMap(a.rom)
    levels = level_names(rom)
    refs = rom_refs(rom)
    sprites = json.loads(Path(a.assets, "data/level_sprites.json").read_text())
    chests = json.loads(Path(a.assets, "data/chests.json").read_text())
    content = {c["index"]: c for c in chests["contents"]}

    placed = {}                     # address -> [description]
    for lv in sprites["levels"]:
        lname = snake(levels.get(lv["level"], lv.get("name", f"level_{lv['level']:02x}")))
        for o in lv["objects"]:
            where = f"{lname}_{o['x'] // 16}_{o['y'] // 16}"
            if o["interaction"] == 2 and 0 < o["index"] < 0x80:
                c = content.get(o["index"], {})
                what = (snake(c["item"]) if "item" in c else
                        f"{c['meseta']}_meseta" if "meseta" in c else "nothing")
                placed.setdefault(0xC780 + o["index"], []).append(f"chest_{where}_{what}")
            elif o["interaction"] == 0 and 0 < o["index"] < 0x70:
                placed.setdefault(0xC710 + o["index"], []).append(f"event{o['index']:02x}_{where}")

    out = []
    for addr in range(0xC700, 0xC800):
        kind = ("misc" if addr < 0xC710 else "event" if addr < 0xC780 else "chest")
        notes = []
        from_placed = False
        if addr in MANUAL:
            name, note = MANUAL[addr]
            notes.append(note)
        elif kind == "chest" and addr in placed:
            name, from_placed = placed[addr][0], True
        elif addr in refs:
            lab = refs[addr][0]
            name = "flag_" + (lab.lower() if lab.startswith("loc_") else snake(lab))
        elif addr in placed:
            name, from_placed = placed[addr][0], True
        else:
            name = "unused"
        others = placed.get(addr, [])[1:] if from_placed else placed.get(addr, [])
        if kind == "event" and placed.get(addr):
            notes.append("event objects with this index (their routine may "
                         "not read the flag): " + ", ".join(placed[addr][:6]))
        elif others:
            notes.append("also placed: " + ", ".join(others))
        seen = []
        for lab in refs.get(addr, []):
            if lab not in seen:
                seen.append(lab)
        if seen:
            notes.append("read/written in: " + "; ".join(seen[:6]))
        out.append({"addr": f"${addr:04X}", "kind": kind, "name": name,
                    "notes": " | ".join(notes)})

    Path(a.out).mkdir(parents=True, exist_ok=True)
    # "ps2.asm references": the routines are still named by the
    # disassembly's labels (the map's), though read from the ROM's code.
    Path(a.out, "flag_names.json").write_text(json.dumps(
        {"source": "ps2.asm references, level_sprites.json, chests.json",
         "flags": out}, indent=1) + "\n", newline="\n")
    named = sum(1 for f in out if f["name"] != "unused")
    print(f"flag_names.json: {named} of 256 named")


if __name__ == "__main__":
    main()
