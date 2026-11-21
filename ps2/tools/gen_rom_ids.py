#!/usr/bin/env python3
"""Generate src/game/rom_ids.h (build 96): every music, sound and level id
by the constants file's name (MusicID_, SXFID_, LevelID_), every named
byte of the $C700 flag block (assets/data/flag_names.json), and the hand
lists below for what the constants file does not name: the A-button
events (ObjType_Events) and their kinds, the one-shot level events
(loc_7E02), the demo walks, and the flag bytes the save knows only by a
routine's label. Run from ps2/ with the constants file:

    python3 tools/gen_rom_ids.py path/to/ps2.constants.asm

The header is checked in, so building never needs the disassembly.
"""
import json, re, sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import asset_names  # noqa: E402

def camel_to_upper(s):
    s = re.sub(r"(?<=[a-z0-9])(?=[A-Z])", "_", s)
    s = re.sub(r"(?<=[A-Z])(?=[A-Z][a-z])", "_", s)
    return s.upper()

def ids(text, prefix):
    out = []
    for m in re.finditer(r"^%s(\w+)\s*=.*?;\s*\$?([0-9A-Fa-f]+)\s*$" % prefix,
                         text, re.M):
        name, val = m.group(1), m.group(2)
        v = int(val, 16) if "$" in m.group(0).split(";")[-1] else int(val)
        out.append((camel_to_upper(name), v))
    return out

# ObjType_Events (ps2.asm:19862, EventType_DialogueIndex): what the A button
# hits, by the event object's number. Every routine was read (build 96b);
# the ROM label is the first thing in each comment. Numbers sharing a routine
# share a description.
EVENTS = [
    (0x00, "NOTHING", "loc_D9AA: \"nothing unusual\" ($1708), or getting off the jet scooter"),
    (0x01, "ARIMA_BLOWN_DOOR", "loc_DA74: the doors the scoundrels blew open ($170A)"),
    (0x02, "CONTROL_TOWER", "loc_DA80: a town's control tower ($1709)"),
    (0x03, "DEAD_BODY_BARE", "loc_DA8C: Shure's bodies with nothing on them ($170B)"),
    (0x04, "FAKE_MARUERA_TREE", "loc_DA98: \"looks like a Maruera tree, but it isn't\" ($170F)"),
    (0x05, "DARUM_SEEN", "loc_E9A8: \"I've seen that face!\" ($1801), as ObjType_TeimDarum 1"),
    (0x06, "JET_SCOOTER_PARKED", "loc_DAA4: boarding"),
    (0x07, "KEYBOARD", "loc_DAD4: the keyboard ($1711), YES back to the saved place"),
    (0x08, "SKURE_NEWSPAPER_1", "loc_DB22: the old newspaper ($1719), then its page ($1712)"),
    (0x09, "SKURE_NEWSPAPER_2", "loc_DB30: $1719, $1713"),
    (0x0A, "SKURE_NEWSPAPER_3", "loc_DB3E: $1719, $1714"),
    (0x0B, "SKURE_NEWSPAPER_4", "loc_DB4C: $1719, $1715"),
    (0x0C, "SKURE_NEWSPAPER_5", "loc_DB5A: $1719, $1716"),
    (0x0D, "SKURE_NEWSPAPER_6", "loc_DB68: $1719, $1717"),
    (0x0E, "SKURE_NEWSPAPER_7", "loc_DB76: $1719, $1718"),
    (0x0F, "WATER_COLOUR", "loc_DB84: the water around the Underwater Passage ($1720)"),
    (0x10, "MARUERA_TREE", "loc_DB90: the leaf, once"),
    (0x11, "DEAD_BODY_POCKET", "loc_DBB2: the Small Key and the Letter"),
    (0x12, "RECORDER", "loc_DBF8: the lab's Recorder, once"),
    (0x13, "TOWER_GREEN_CARD", "loc_DC1E: Control Tower F1's four boxes, each giving its card (event - $E: items 5-8)"),
    (0x14, "TOWER_BLUE_CARD", "loc_DC1E"),
    (0x15, "TOWER_YELLOW_CARD", "loc_DC1E"),
    (0x16, "TOWER_RED_CARD", "loc_DC1E"),
    (0x17, "TEIM", "loc_DC36: Teim takes the Letter and joins the bags"),
    (0x18, "DYNAMITE_DOOR_1", "loc_DC84: \"Maybe dynamite can open this door\" ($170D)"),
    (0x19, "DYNAMITE_DOOR_2", "loc_DC84"),
    (0x1A, "DYNAMITE_DOOR_3", "loc_DC84"),
    (0x1B, "STRONG_SHUTTER", "loc_DC90: \"a very strong shutter\" ($170E)"),
    (0x1C, "DAM_LOCK_1", "loc_DC9C: a dam's door or panel, \"a place to put the card in\" ($171B); its byte is FLAG_DAM_LOCKS_FIRST on"),
    (0x1D, "DAM_LOCK_2", "loc_DC9C"),
    (0x1E, "DAM_LOCK_3", "loc_DC9C"),
    (0x1F, "DAM_LOCK_4", "loc_DC9C"),
    (0x20, "DAM_LOCK_5", "loc_DC9C"),
    (0x21, "DAM_LOCK_6", "loc_DC9C"),
    (0x22, "DAM_LOCK_7", "loc_DC9C"),
    (0x23, "DAM_LOCK_8", "loc_DC9C"),
    (0x24, "STRONG_SHUTTER_2", "loc_DC90"),
    (0x25, "NEIFIRST", "loc_DCA8: the talk with Neifirst and her battle"),
    (0x26, "NEI_DEATH", "loc_DD52: after the battle, Nei's last words"),
    (0x27, "NEI_GONE", "loc_DDD0: Rolf calls her name; Nei out of the party"),
    (0x28, "CLIMATROL_SHAKES", "loc_DE1E: the weather control system shaking ($1825)"),
    (0x29, "CLIMATROL_EXPLOSIONS", "loc_DE42: sixteen explosions"),
    (0x2A, "RETURN_TO_PASEO", "loc_DEC6: the explosions, $1828, Paseo and the walk into the clone lab"),
    (0x2B, "MOUNT_UZO", "loc_DF24: \"a high rocky mountain\" ($171F)"),
    (0x2C, "GAIRA_PRISON_2", "loc_DF32: the prison on Gaira, as $2D"),
    (0x2D, "GAIRA_PRISON", "loc_DF32: \"This is the satellite Gaila\" ($1839), gear taken"),
    (0x2E, "GAIRA_EXPLOSION", "loc_DFA8: the explosion that knocks Gaira off its orbit ($1812)"),
    (0x2F, "GAIRA_CONTROL_PANEL", "loc_DFE6: into Building_GairaControlPanel"),
    (0x30, "MEMBER_DEAD", "loc_DFFE: \"{char} is dead!\" ($1721); the SEGA screen if nobody lives"),
    (0x31, "SHIR_GONE", "loc_E042: \"What? Shir is gone?\" ($1722)"),
    (0x32, "DOOR_ESPERS", "loc_E05A: the Esper mansion's door Espers"),
    (0x33, "ESPER_B1", "loc_E07A: the Esper in B1, twice"),
    (0x34, "ESPER_MANSION_LUTZ", "loc_E0AA: Lutz's room once the six Espers have spoken"),
    (0x35, "PANDORAS_BOX", "loc_E0BE: Noah's box, and Dark Force"),
    (0x36, "PANDORAS_BOX_OPENED", "loc_E0F8: the box's laugh, from the one-shot"),
    (0x37, "MOTHER_BRAIN_CONFRONTATION", "loc_E122"),
    (0x38, "MOTHER_BRAIN_AFTERMATH", "loc_E1E4"),
    (0x39, "LUTZ_WARNING", "loc_E29E"),
    (0x3A, "EARTHMEN", "loc_E2E2"),
    (0x3B, "OPENING_PAGE_1", "loc_E47C: the opening over Motavia, a page each ($1401-$1406)"),
    (0x3C, "OPENING_PAGE_2", "loc_E4B4"),
    (0x3D, "OPENING_PAGE_3", "loc_E4C0"),
    (0x3E, "OPENING_PAGE_4", "loc_E4CC"),
    (0x3F, "OPENING_PAGE_5", "loc_E4D8"),
    (0x40, "OPENING_PAGE_6", "loc_E4E4: the last page, then the SEGA screen"),
]

# What the A button's target word ($DE6E) holds: its high byte picks the
# kind (AButton_ObjTypeIndex, ps2.asm:19856), its low byte the entry.
EVENT_KINDS = [
    (0x000, "EVENTS", "ObjType_Events: the EVENT_* above"),
    (0x100, "PEOPLE", "ObjType_People: a townsperson's dialogue number"),
    (0x200, "CHESTS", "ObjType_TreasureChests: a chest's number"),
    (0x300, "TEIM_DARUM", "ObjType_TeimDarum: the TEIM_DARUM_* below"),
]

# ObjType_TeimDarum (ps2.asm:21584, loc_E99C), by its low byte.
TEIM_DARUM = [
    (1, "DARUM_SEEN", "loc_E9A8: \"I've seen that face!\" ($1801)"),
    (2, "SCENE", "loc_E9BA: Teim out of the bag and the scene with Darum"),
    (3, "ROLF_DEAD", "loc_EAC2: \"{char} is dead.\" (bank 0's 6) at Rolf's own door"),
]

# The one-shot level events: loc_7E02 (ps2.asm:11489) runs the case
# event_flags' high byte names once, on the level screen's next entry.
# Cases 8, 9 and $D do nothing ($D is still set, to hold the party).
SCENES = [
    (0x00, "NONE", "no one-shot"),
    (0x01, "NEI_DYING", "loc_7E02: the picture after the Neifirst battle, event NEI_DEATH"),
    (0x02, "NEI_DEAD", "loc_7E6A: her eyes closed, event NEI_GONE"),
    (0x03, "CLIMATROL_SHAKES", "loc_7EE4: the shaking camera, event CLIMATROL_SHAKES"),
    (0x04, "MOTAVIA_BEFORE", "loc_7F14: Motavia from above for 300 frames, no party"),
    (0x05, "CLIMATROL_EXPLODES", "loc_7F44: the big shake and the explosions; Climatrol destroyed"),
    (0x06, "MOTAVIA_AFTER", "loc_7F80: Motavia again for 300 frames, no party"),
    (0x07, "RETURN_TO_PASEO", "loc_7FB0: back on F7, event RETURN_TO_PASEO"),
    (0x0A, "GAIRA_PRISON", "loc_7FDA: after the Army Eyes' capture, event GAIRA_PRISON"),
    (0x0B, "PANDORAS_BOX_OPENED", "loc_7FF2: Noah's box's laugh, event PANDORAS_BOX_OPENED"),
    (0x0C, "SHIR_GONE", "loc_8010: after a theft, event SHIR_GONE"),
    (0x0D, "ESPER_B1_HOLD", "loc_8022: nothing; ESPER_B1's second talk sets it, and while event_flags is not 0 the party stands still"),
    (0x0E, "MOTHER_BRAIN_ARRIVAL", "loc_8028: her image and the arcs, event MOTHER_BRAIN_CONFRONTATION"),
    (0x0F, "MOTHER_BRAIN_DEFEATED", "loc_8070: event MOTHER_BRAIN_AFTERMATH"),
    (0x10, "OPENING", "loc_8082: the opening's first page, event OPENING_PAGE_1"),
]

# The demo walks (DemoScriptPtrs, ps2.asm:5401), by index.
DEMOS = [
    (0, "HOUSE_TO_TOWER", "Demo_HouseToTower"),
    (1, "TOWER_TO_HOUSE", "Demo_TowerToHouse"),
    (2, "TEIM_SCENE", "Demo_TeimScene"),
    (3, "AFTER_TEIM_DEATH", "Demo_AfterTeimDeath: Darum's walk"),
    (4, "DYNAMITE_USED", "Demo_DynamiteUsed"),
    (5, "KEY_USED", "Demo_KeyUsed"),
    (6, "CLIMATROL_TO_CLONE_LAB", "Demo_ClimatrolToCloneLab"),
    (7, "CLONE_LAB_TO_TOWER", "Demo_CloneLabToTower"),
    (8, "TYLER_SPC_TO_TOWER", "Demo_TylerSpcToTower"),
    (9, "AFTER_MOTHER_BRAIN", "loc_3AC4"),
    (10, "TO_THE_EARTHMEN", "loc_3AD8"),
]

# Names for the flag bytes flag_names.json calls after the first routine to
# touch them (flag_loc_*), each read in ps2.asm (build 96b). The save keeps
# its own keys; these are the code's names only.
FLAG_NAMES = {
    0xC700: ("TOWNS_VISITED", "LoadSpritesInLevel: c700[level] = level for each town up to Ryuon; the base of that list"),
    0xC711: ("SCENE_CAMERA", "event_flags' low byte: loc_809C's camera case"),
    0xC712: ("PIATA_TOWER_UNLOCKED", "set at the Governor's first debrief (loc_CA58); Piata's tower door stays shut until then (loc_6ED6)"),
    0xC715: ("DARUM_SCENE_DONE", "set as the Darum scene ends (loc_EAB0); Darum's init (loc_4E2A) keeps him away after"),
    0xC722: ("RECORDER_FOUND", "event RECORDER's own byte; the library's news reads it (loc_C406)"),
    0xC735: ("CLIMATROL_DESTROYED", "set by the one-shot CLIMATROL_EXPLODES (loc_7F44); Motavia's map patch (loc_9258) and loc_585C read it"),
    0xC73E: ("GAIRA_OFF_ORBIT", "event GAIRA_EXPLOSION's own byte (loc_DFD0); the ring (loc_64A6) and the drifting scroll read it"),
    0xC742: ("DOOR_ESPERS_LET_ROLF_IN", "event DOOR_ESPERS (loc_E05A) with Rolf alive; the door Espers step aside (loc_532C)"),
    0xC743: ("ESPER_B1_TALKS", "event ESPER_B1 (loc_E07A) counts the talks; Building_EsperMansion sets 3 (loc_4B40 and the Espers read it)"),
    0xC78E: ("CHEST_PRISM", "chest $0E's own byte, the Prism in the Esper mansion's F1: 1 from a new game, cleared as Lutz wakes (loc_CF0A), 1 again once taken"),
    0xC78F: ("CHEST_NEI_SWORD", "chest $0F's own byte, the Nei Sword in the same place: cleared when Lutz sees the eight Nei items (loc_CF38)"),
    0xC744: ("LUTZ_SAW_NEI_ITEMS", "set when Lutz sees all eight Nei items (loc_CF38): the Nei Sword"),
    # (build 97a) The dams' bytes, one by one. Each card (ItemAction_
    # GreenCard .. RedCard, ps2.asm:14407) carries its dam's door event;
    # loc_9C12 returns the faced event n and a1 = event_flags + n, so the
    # card sets the door's byte (loc_9D3E: demo KEY_USED, the door opens)
    # or, on event n + 1, the panel's (loc_9D70: the card used up, camera
    # case DAMS_CHECK). loc_81E6 wants all eight at 1 for the Army Eyes;
    # with Climatrol destroyed each panel's byte patches Motavia's map
    # (loc_9258: $C72F, $C733, $C731, $C72D).
    0xC72C: ("GREEN_DAM_DOOR", "event DAM_LOCK_1, Green Dam ground floor 95,25: the green card's door (loc_9D3E)"),
    0xC72D: ("GREEN_DAM_PANEL", "event DAM_LOCK_2, Green Dam F1 113,28: the green card's panel (loc_9D70); Motavia's patch loc_17DA2 (loc_92BC)"),
    0xC72E: ("BLUE_DAM_DOOR", "event DAM_LOCK_3, Blue Dam ground floor 53,27: the blue card's door"),
    0xC72F: ("BLUE_DAM_PANEL", "event DAM_LOCK_4, Blue Dam F3 21,32: the blue card's panel; Motavia's patch loc_17C94"),
    0xC730: ("YELLOW_DAM_DOOR", "event DAM_LOCK_5, Yellow Dam ground floor 17,27: the yellow card's door"),
    0xC731: ("YELLOW_DAM_PANEL", "event DAM_LOCK_6, Yellow Dam F2 51,24: the yellow card's panel; Motavia's patch loc_17D4A (loc_92AE)"),
    0xC732: ("RED_DAM_DOOR", "event DAM_LOCK_7, Red Dam F1 47,17: the red card's door"),
    0xC733: ("RED_DAM_PANEL", "event DAM_LOCK_8, Red Dam F2 45,20: the red card's panel; Motavia's patch loc_17CDA (loc_92A0)"),
}

# (build 97a) The level screen's camera cases (loc_809C, ps2.asm:11667):
# event_flags' low byte ($C711), run every frame of LevelScreenLoop.
# (build 97b) All eight read in full, with loc_8226 (the shake's roll),
# loc_8240 (demo_timer down, then loc_824C) and loc_824C (the byte cleared,
# screen_changed_flag -1: the level reloads). game.c runs 1-4 and 6
# (level_scene_camera), 7 is the opening crawl's (opening_tick), 8 the
# dams' check; 5 is not set by anything ported.
CAMERAS = [
    (0, "NONE", "nothing: loc_809C returns"),
    (1, "WAIT_THEN_RELOAD", "loc_8240: demo_timer down, then the byte cleared and the level reloaded"),
    (2, "SHAKE_THEN_RELOAD", "loc_8226 on each axis (plane A 0-7 px, plane B 0-3 a frame), then as 1"),
    (3, "JITTER", "loc_80D4: each plane 0 or 1 px an axis a frame, no timer: until something else clears it"),
    (4, "WAIT_THEN_NEXT_LEVEL", "loc_810E: as 1, and the reload goes to $F748/$F74A/$F74C"),
    (5, "BACK_TO_WAY_OUT", "loc_8130: with no window asked for or open, the level from $C64C/$C64E/$C650 (the gum's way out) and the reload"),
    (6, "GAIRA_COUNTDOWN", "loc_815C: with no event, window or window object, demo_timer down; at 0 event $2E (the explosion) and the byte cleared"),
    (7, "PAN_TO_TARGET", "loc_8192: the camera a pixel a frame toward $C648/$C64A (loc_8BBA); there, the next event from $F780 (the opening crawl)"),
    (8, "DAMS_CHECK", "loc_81E6: all eight dam bytes at 1 -> the Army Eyes' battle ($101); either way event_flags cleared. A card on its dam's panel sets it (loc_9D70)"),
]

# The runs of the block the port indexes.
FLAG_RANGES = [
    ("EVENT_SLOTS", 0x10, "event n's own byte is c700[FLAG_EVENT_SLOTS + n] (ObjType_Events: event_flags + n)"),
    ("DAM_LOCKS_FIRST", 0x2C, "$C72C-$C733: every dam's door and panel, one byte each (loc_81E6 wants all eight at 1)"),
    ("DAM_LOCKS_LAST", 0x33, ""),
    ("CHESTS", 0x80, "chest n's byte is c700[FLAG_CHESTS + n] (treasure_chest_flags)"),
]

def main():
    const = open(sys.argv[1], encoding="utf-8", errors="replace").read()
    music = ids(const, "MusicID_")
    sfx = ids(const, "SXFID_")
    levels = ids(const, "LevelID_")
    items = ids(const, "ItemID_")
    flags = json.load(open("assets/data/flag_names.json"))["flags"]
    with open("src/game/rom_ids.h", "w", encoding="utf-8", newline="\n") as o:
        o.write("#ifndef PS2_ROM_IDS_H\n#define PS2_ROM_IDS_H\n\n")
        o.write("/* The ROM's ids by name (builds 96a-96b): music and sounds (MusicID_\n"
                " * and SXFID_ in the constants file), levels (LevelID_), items\n"
                " * (ItemID_), the $C700\n"
                " * flag block's bytes, the A-button events (ObjType_Events) and\n"
                " * their kinds, the one-shot level events (loc_7E02) and the demo\n"
                " * walks (DemoScriptPtrs). The ROM's labels are in the comments.\n"
                " * Generated by tools/gen_rom_ids.py; checked in so a build never\n"
                " * needs the disassembly. */\n\n")
        o.write("/* Music (MusicPtrs, from $81). */\n")
        for n, v in music:
            o.write("#define MUSIC_%-24s 0x%02X\n" % (n, v))
        o.write("\n/* Sound effects (SFXPtrs, from $A0). */\n")
        for n, v in sfx:
            o.write("#define SFX_%-26s 0x%02X\n" % (n, v))
        o.write("\n/* Levels (LevelDataPtrs). */\n")
        for n, v in levels:
            o.write("#define LEVEL_%-24s 0x%02X\n" % (n, v))
        o.write("\n/* Items (ItemID_, the rows of ItemData). */\n")
        for n, v in items:
            o.write("#define ITEM_%-25s 0x%02X\n" % (n, v))
        o.write("\n/* The $C700 flag block, GameData.c700[FLAG_*]: the bytes with a\n"
                " * name. The save's keys (assets/data/flag_names.json) are not these:\n"
                " * a byte named there only by the routine that touches it gets its\n"
                " * name here (FLAG_NAMES in the generator), and the event objects'\n"
                " * own bytes are FLAG_EVENT_SLOTS + the event. */\n")
        for n, v, c in FLAG_RANGES:
            o.write("#define FLAG_%-38s 0x%02X%s\n" % (n, v, ("   /* %s */" % c) if c else ""))
        left = []
        for f in flags:
            addr = int(f["addr"][1:], 16)
            name = f["name"].upper()
            if name.startswith("FLAG_"):
                name = name[5:]
            if addr in FLAG_NAMES:
                name, why = FLAG_NAMES[addr]
                o.write("#define FLAG_%-38s 0x%02X   /* %s: %s */\n" % (name, addr & 0xFF, f["addr"], why))
                continue
            if f["name"].endswith("_unused"):
                continue      # (build 97f) an event's byte nothing reads
            if f["name"] == "unused" or name.startswith("LOC_") or name.startswith("EVENT"):
                if f["name"] != "unused":
                    left.append(f["addr"])
                continue
            o.write("#define FLAG_%-38s 0x%02X   /* %s */\n" % (name, addr & 0xFF, f["addr"]))
        if left:
            o.write("/* Not named yet (the save calls them by place or routine): %s. */\n"
                    % ", ".join(left))
        else:
            o.write("/* (build 97f) Every byte the ROM touches has a name; the event bytes\n"
                    " * nothing reads end \"_unused\" in the save and have no FLAG_ here. */\n")
        o.write("\n/* What the A button hits: the event object's number (ObjType_Events,\n"
                " * EventType_DialogueIndex, ps2.asm:19875). */\n")
        for v, n, c in EVENTS:
            o.write("#define EVENT_%-30s 0x%02X%s\n" % (n, v, ("   /* %s */" % c) if c else ""))
        o.write("\n/* The A button's target word: the kind in its high byte. */\n")
        for v, n, c in EVENT_KINDS:
            o.write("#define EVENT_KIND_%-25s 0x%03X   /* %s */\n" % (n, v, c))
        o.write("\n/* ObjType_TeimDarum's entries, the target word's low byte. */\n")
        for v, n, c in TEIM_DARUM:
            o.write("#define TEIM_DARUM_%-25s %d   /* %s */\n" % (n, v, c))
        o.write("\n/* The one-shot level events (loc_7E02, ps2.asm:11489): event_flags'\n"
                " * high byte, run once on the level screen's next entry. */\n")
        for v, n, c in SCENES:
            o.write("#define SCENE_%-30s 0x%02X   /* %s */\n" % (n, v, c))
        o.write("\n/* The level screen's camera cases (loc_809C, ps2.asm:11667):\n"
                " * event_flags' low byte. */\n")
        for v, n, c in CAMERAS:
            o.write("#define CAMERA_%-29s 0x%02X   /* %s */\n" % (n, v, c))
        o.write("\n/* (build 97b) PalettePtrs (ps2.asm:26819) by index, for\n"
                " * palette_load_entry: assets/palettes/<name>.png, the names\n"
                " * in tools/asset_names.py. */\n")
        for v, n in sorted(asset_names.PALETTE_NAMES.items()):
            o.write("#define PALETTE_%-28s 0x%02X\n" % (n.upper(), v))
        o.write("\n/* The demo walks (DemoScriptPtrs, ps2.asm:5401), by index. */\n")
        for v, n, c in DEMOS:
            o.write("#define DEMO_INDEX_%-25s %d%s\n" % (n, v, ("   /* %s */" % c) if c else ""))
        o.write("\n#endif /* PS2_ROM_IDS_H */\n")
    print(len(music), "music", len(sfx), "sfx", len(levels), "levels", len(items), "items")

if __name__ == "__main__":
    main()
