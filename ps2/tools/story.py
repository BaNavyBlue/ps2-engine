#!/usr/bin/env python3
"""Play the opening story the way a person does, fast, and report.

    make route_probe
    python3 tools/story.py                  # new game through Nido
    python3 tools/story.py --from shure     # CONTINUE a checkpoint
    python3 tools/story.py --to arima       # stop after that stage

No `party N LEVEL`: every level is fought for, every meseta earned,
and every time the party is back in town the stores are checked for
the best weapon and armour each member can wear and afford, put on,
and the old piece sold. Chests are opened and their finds worn when
they beat the shelf. The stages:

    paseo   new game; Paseo's shops; grind at the gate to Rolf L5
    arima   the tube to Arima; its weapon store; grind there for a
            Sword; back to Rolf's house for Rudo; grind to Rolf L8
    shure   Shure: F3's body for the Small Key and the Letter, the
            five ground-floor chests (two Dynamite), back to Paseo
    nido    Nido: Dynamite on the door, the F1 chests, F2's scene
    darum, oputa, amy
            Teim to Darum; Oputa's shops; Amy at Rolf's house
    lab_in, lab, lab_b1
            the Biosystems Lab: the door, the grind on its ground floor
            (build 103: from Amy's joining, by long walks), the chests,
            F2's door, the Recorder; B1's chests
    hugh    Hugh at Rolf's house; the Recorder to the Governor and the
            library, for the Key Tube
    key_tube, zema, roron
            who goes (play.choose_party: the core, then the fourth place by
            the numbers, with a catch-up rotation); the Key Tube on the
            tube's locked door and on to Zema; Zema's shops and field;
            Roron's floors, their chests and the jet scooter's maker

    dam_party, cards, dam_grind, dams
            Rolf, Rudo, Amy, Anna; Musik and the tower's four cards;
            Piata's field to dam_trials' levels; the four dams with
            Trimates, Escapipes and s.careful, the Army Eyes, Gaira,
            Tyler's spaceship and the Governor

    roof    the tower's roof and the flight to Skure (a save-here there)

    ...     (builds 85-89: skure, dezolis_towns, esper, dezolis_grind,
            motavia, shir, menobe, ikuto, guaron, naval -- see
            docs/history.md)

    nei_sword
            (build 90) the Esper mansion with the eight Nei items: Lutz
            ($1690-$1694), F1's chest (the Nei Sword, on Rolf), Lutz
            again and NO ($1912, loc_B9D8's HEAL); the nearest town
    kain, kain_grind, noah_grind, dark_force
            (build 91) Kain in Amy's place, raised at Zosa to 20; Noah
            from Lutz's YES to Rolf L41; Pandora's box and Dark Force

    skure   Skure B1's chests and the Magic Cap on Rolf; B1's cats; Zosa
            (gear, mates, Escapipes, the hospital, Data Memory) and its
            field to Rolf L30; B2's chests and cats; Zosa again, shopping
            in the Mogic Cap (a Magic Cap doubles the prices); Zosa's
            Dezolians; Zosa's Data Memory

A checkpoint (Data Memory, slot 1, copied to routes/saves/story_<stage>
.json) is written after each stage. The report at the end -- fights,
levels, purchases, deaths, everything the port printed on stderr, and
every script the scene showed -- is what to check against the ROM.
"""

import argparse
import shutil
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from play import (WIN_YESNO2, Session, PASEO, PASEO_DOORS, bags, chests, enter_town,   # noqa: E402
                  find_item, gear_up, grind_and_gear, roam_and_gear, heal_here,
                  level_map, level_objects, rolf_house, save, search,
                  stock_mates, travel, use_item, continue_game, talk,
                  needs_healing, retreat, geared, town_buildings,
                  town_arrivals, search_event, visit, PASEO_DOORS,
                  stock_mates, resupply, BUILDING_ITEM, dungeon_grind,
                  count_mates, pick_party, talk_to, roster, choose_party,
                  CATCH_UP_GAP, leave_building, board_scooter,
                  ride_ashore, door_cells, ride_to_face, event_block,
                  fight_smart, wander, stock_item, mend, goto,
                  ride_into, use_tech, clone_lab, town_exit,
                  BUILDING_CLONE_LAB, equip, worn_head)
import play                                                 # noqa: E402

ARIMA = 0x05
OPUTA = 0x06
DARUM_TUBE = 0x0E
SHURE_G, SHURE_F1, SHURE_F2, SHURE_F3 = 0x17, 0x18, 0x19, 0x1A
NIDO_G, NIDO_F1, NIDO_F2 = 0x1B, 0x1C, 0x1D
LAB_B1, LAB_G, LAB_F1, LAB_F2 = 0x32, 0x33, 0x34, 0x35
ZEMA = 0x07
KUERI = 0x08
UZO = 0x12
UNDERWATER = 0x13
CLIMATROL_G = 0x36
MOTAVIA = 0x00
INVENTOR_DOOR = (39, 15)           # Kueri's building 13 (y, x)
UZO_RECORD = (97, 173)             # Motavia's door record for Uzo
KUERI_RECORD = (85, 37)
TUBE_DOOR = 0x0F
RORON_F5, RORON_F4, RORON_F3, RORON_F2, RORON_F1, RORON_G = \
    0x1E, 0x1F, 0x20, 0x21, 0x22, 0x23
SAVES = Path("routes/saves")


class Report:
    def __init__(self):
        self.lines = []
        self.t0 = time.time()

    def __call__(self, *parts):
        line = " ".join(str(p) for p in parts)
        self.lines.append(line)
        print(line, flush=True)


def revival_how(r):
    """(build 90) Where a member came back, from Session.revivals: the
    clone lab (building 3), in a fight or on the map, with the last two
    scripts shown before -- a technique's or an item's own lines."""
    tail = play.scripts_text(r["scripts"][-2:])
    if r["screen"] == "building":
        where = ("clone lab" if r["building"] == 3
                 else f"building {r['building']}")
        return where
    if r["screen"] == "battle":
        return f"in a fight after {tail}"
    return f"on the map after {tail}"


def revival_tally(s):
    out = {}
    for r in s.revivals:
        k = revival_how(r)
        out[k] = out.get(k, 0) + 1
    return out


def party_line(s):
    p = s.state.get("party") or []
    return " ".join(f"{m['name']} L{m['level']} {m['hp']}/{m['max_hp']}"
                    + (" dead" if m["dead"] else "") for m in p) \
        + f"; meseta {s.state.get('meseta')}"


def checkpoint(s, name, session, log):
    """Save in slot 1 at the nearest Data Memory (Paseo's), then keep a
    copy under routes/saves/."""
    if s.state.get("screen") == "building":
        leave_building(s)
    if s.state.get("screen") not in ("level", "building"):
        log(f"checkpoint: none for {name} -- the screen is "
            f"{s.state.get('screen')}")
        return
    # (build 176) Stale by the slot's own mtime before the save, not by
    # the wall clock: Windows stamps a file from its coarse clock (a tick
    # of up to 15.6 ms) and time.time() is precise (3.14), so a save-here
    # written within a tick of the clock read was "older" than it -- seed
    # 1's shir checkpoint, saved, counted as failed and the stage retried.
    src = s.saves_dir / "slot1.json"      # route_out/<session>/saves
    before = src.stat().st_mtime_ns if src.exists() else None
    town = s.state.get("level")
    b = town_buildings(town) if town is not None and town < 0x10 else {}
    if town == 3:
        # DezolisSkure (build 84): no Data Memory saves there without a
        # Magic Cap, so the probe's save-here -- a Visiphone save.
        s.send("save-here 1")
        s.looted = []                   # (build 106) the save keeps them
    elif 2 in b:                        # BuildingID_DataMemory here
        save(s, 1, level=town, door=b[2][0])
    else:
        if town != PASEO:
            enter_town(s, PASEO)
        save(s, 1)
    if src.exists() and before is not None \
            and src.stat().st_mtime_ns <= before:
        # An older save from earlier in the session: this one failed.
        log(f"checkpoint: no save written for {name} (slot 1 is stale)")
        return
    if src.exists():
        SAVES.mkdir(parents=True, exist_ok=True)
        shutil.copy(src, SAVES / f"story_{name}.json")
        log(f"checkpoint: {SAVES / f'story_{name}.json'} -- {party_line(s)}")
    else:
        log(f"checkpoint: no save written for {name}")


LOOT_WIPES = 2          # (build 99) CONTINUEs one loot() call may make


def shut_again(s, level, log, wanted=()):
    """(build 106) After a CONTINUE: the chests holding a `wanted` item
    nobody carries, on the floors loot() has worked since the last save --
    other than `level`, whose own list goes again anyway -- that the save
    has shut again, as (floor, cell) in the order the floors were looted.
    A CONTINUE loads the save as it was, so a chest opened after it is
    shut once more (seeds 4, 7, 9 and 11 in build 105: Menobe's ground
    floor or F1 chest, opened after the town's save, shut by a wipe-out on
    F2, and the stage a Nei item short). Each chest's own byte says, so a
    floor the save came after costs nothing. Only wanted items: going back
    for every chest cost seed 1's Ikuto its try (a first cut of this
    build: two floors of meseta walked again after each wipe-out, and
    more wipe-outs)."""
    s.continued = False
    again = [(lv, c) for lv in s.looted if lv != level
             for c in chests(lv)
             if play.chest_item(lv, c) in wanted
             and not play.carried(s, play.chest_item(lv, c))
             and play.chest_shut(s, lv, c)]
    if again:
        log(f"loot {play.level_name(level)}: after the CONTINUE, shut again: "
            + ", ".join(f"{play.level_name(lv)} {c}" for lv, c in again))
    return again


def loot(s, level, log, key=True, town=PASEO, tired=None, wanted=()):
    """Open every chest on a level, travelling there first. Returns the
    scripts each answered with. Hurt with no mates left, back to `town`
    and on to the next chest from there. (build 106) `wanted`: the items
    the stage needs (a Prism dungeon's Nei items) -- a chest holding one
    that a CONTINUE shut again on an earlier floor, or that was left
    unreached for hurt, is gone back to."""
    out = {}
    cells = list(chests(level))
    if level not in s.looted:
        s.looted.append(level)
    queue = [(level, c) for c in cells]
    if s.continued:
        queue = shut_again(s, level, log, wanted) + queue
    wipes = 0
    # (build 106) A chest with a wanted item left unreached because the
    # party was hurt on the way -- a retreat, and hurt again -- goes once
    # more to the end of the list: seed 9's first Menobe try in build 105
    # left F2's (41, 47) and its Nei item behind. Not one no route leads to
    # ("no way" leaves the hurt flag down), nor one with nothing wanted.
    again_once = set()
    # (build 102b) Until the list is done AND the party is standing: a
    # wipe-out on the way to the last chest used to end the loop with the
    # game at the title, and the stage after it gave up (seed 4's Skure,
    # seed 12's Menobe in build 101b's window runs; the retry covered it).
    while queue or s.state.get("screen") in ("title", "sega", "gameover"):
        if s.state.get("screen") in ("title", "sega", "gameover"):
            if not queue:
                log(f"loot {play.level_name(level)}: wiped out on the way to the last chest")
            # (build 98) Wiped out on the way: CONTINUE the last save and
            # go on with the list from there. Before, every chest after a
            # game over was "unreached" and the stage gave up (the
            # playthrough's seed 5 in Ikuto, four tries).
            # (build 99) From the top of the list: the CONTINUE shuts every
            # chest opened since that save, and the one being walked to when
            # the party fell was left "unreached" -- seed 8's Naval opened
            # one chest, was wiped out on the way to the next, and never
            # went back to either (a try lost). A chest still open answers
            # "There is nothing inside" ($1706). At most LOOT_WIPES a call.
            if wipes >= LOOT_WIPES or not recover(s, log):
                break
            wipes += 1
            log(f"loot {play.level_name(level)}: the list again after a CONTINUE")
            queue = shut_again(s, level, log, wanted) + [(level, c) for c in cells]
        lv, cell = queue.pop(0)
        shown = search(s, lv, cell, log=log, key=key)
        if not shown and getattr(s, "hurt", False):
            log("hurt with nothing left on the way; retreating")
            retreat(s, town, log=log)
            shown = search(s, lv, cell, log=log, key=key)
        if shown and 0x1704 in shown and play.make_room(s, log=log):
            # (build 99) "But they can't hold any more": the find stays in
            # the chest; a mate tossed and the chest again. Seed 6's Naval
            # lost a try to a Nei item left behind this way.
            again = search(s, lv, cell, log=log, key=key)
            shown = shown + (again or [])
        if lv == level:
            out[cell] = out.get(cell, []) + (shown or [])
        log(f"chest on {play.level_name(lv)} {cell}: "
            + (play.scripts_text(shown) if shown else "unreached"))
        if not shown and getattr(s, "hurt", False) \
                and play.chest_item(lv, cell) in wanted \
                and (lv, cell) not in again_once \
                and s.state.get("screen") not in ("title", "sega", "gameover"):
            again_once.add((lv, cell))
            queue.append((lv, cell))
        if needs_healing(s) and not find_item(s, "Monomate") \
                and not find_item(s, "Dimate"):
            log("hurt with nothing left; retreating")
            retreat(s, town, log=log)
        elif tired is not None and tired(s):
            # (build 89) A caller's own line: Menobe's first run went on
            # with Rolf's and Amy's TP at 2 and was wiped out between F1
            # and F2 by three 365-HP enemies (formation 235).
            log("worn out; retreating --", party_line(s))
            retreat(s, town, log=log)
    return out


# (build 101) The first grind is the walk (The owner): the ground between Paseo
# and Arima is mild, and reaching Arima is what brings Rudo to Rolf's door
# (the town's arrival knock, LoadSpritesInLevel's tail), so the pair walks
# there at once rather than grinding Paseo's field to L5 and Arima's to L7
# first. Hurt with nothing left to mend with on the way, back to Paseo's
# hospital and its Monomates, and out again; a wipe-out CONTINUEs Paseo's
# save, made before the first walk. ARIMA_WALKS tries at the walk.
ARIMA_WALKS = 8


def walk_to_arima(s, log, tries=ARIMA_WALKS):
    """(build 101) Paseo to Arima's edge, fighting what comes; True there."""
    goals = level_map(ARIMA).town_edges()
    for n in range(tries):
        careful, s.careful = s.careful, True
        try:
            ok = travel(s, ARIMA, goals, log=log)
        finally:
            s.careful = careful
        if ok:
            log(f"the walk to Arima: there, walk {n + 1} --", party_line(s))
            return True
        if s.state.get("screen") in ("title", "sega", "gameover"):
            if not recover(s, log):
                return False
            continue
        log(f"the walk to Arima: walk {n + 1} back to Paseo --",
            party_line(s))
        if not retreat(s, PASEO, log=log):
            return False
    log("the walk to Arima: not there in", tries, "walks")
    return False


def stage_paseo(s, log):
    # (build 101) Nei's weapon, a save, and the walk to Arima as the grind:
    # its hospital and its weapon store, then back to Paseo (the checkpoint,
    # Paseo's Data Memory: Arima has none).
    s.new_game()
    log("Paseo, a new game:", party_line(s))
    gear_up(s, PASEO, log=log)
    save(s, 1)
    s.save_town = PASEO
    if not walk_to_arima(s, log):
        return False
    heal_here(s, ARIMA)
    gear_up(s, ARIMA, log=log)
    log("Arima:", party_line(s))
    log("bags:", bags(s))
    return True


# (build 102) The grinds' ground around Paseo and Arima (The owner: a much
# larger area with the same level of creatures runs from Paseo to Darum's,
# and the tube to Arima). Motavia's encounter groups 0, 1 and 2 -- the
# regions of Paseo's column, Arima's and the one east of it, and Darum's
# tube's -- all meet Mosquitoes, Fire Ants, Bees, Poisoners, Froggies and
# Locusts, 18-34 HP a formation (the groups beside them, 3-5, three times
# that: Armorants, Waspies, Buzzers). North of Paseo's tube they are one
# piece of 1,258 open cells, x 67-111, y 10-63, Arima's gate inside it;
# Paseo's own gate is a 60-cell pocket whose only way out is that tube
# (no encounters in the tube before Climatrol). The walks go round its
# far points: Darum's approach, the north-east, the south-east, the
# tube's north end, Arima's gate.
MILD_GROUPS = (0, 1, 2)
MILD_FROM = (73, 46)                    # the tube near Paseo, its north end
MILD_ANCHORS = [(66, 22), (112, 10), (112, 63), (73, 46), (89, 52)]
_MILD = None


# The Paseo grind with Rudo: the leader to this level (build 101a's L8;
# squeak() lowers it, build 118).
ARIMA_TARGET = 8


def mild_ground():
    global _MILD
    if _MILD is None:
        _MILD = play.Ground(MOTAVIA, MILD_GROUPS, MILD_FROM, MILD_ANCHORS)
    return _MILD


def stage_arima(s, log):
    # (build 101) Rudo from Rolf's house -- the knock came with Arima,
    # reached in stage paseo -- then the three-member grind in Paseo's field,
    # as before build 101.
    if not enter_town(s, PASEO):
        log("could not get back to Paseo")
        return False
    # The regroup list is everyone joined bar Rolf and Nei, and each pick
    # leaves it (loc_A4E4 counts through the members not yet placed):
    # stop 0 is Rudo, then stop 0 again is the newcomer.
    if any(m["name"] == "Rudo" for m in s.state.get("party") or []):
        # (build 103) A save on the way (the grind's own): Rudo is in.
        log("Rudo in the party already: the grind goes on")
    else:
        shown = rolf_house(s, take=[0, 0])
        log("Rolf's house:", play.scripts_text(shown or []))
    if not any(m["name"] == "Rudo" for m in s.state.get("party") or []):
        # A save from before Arima (COMPUTER PLAYS from a player's slot):
        # no knock yet. The walk there and back, then the house again.
        log("no Rudo at Rolf's house: Arima not reached yet")
        if not walk_to_arima(s, log) or not enter_town(s, PASEO):
            return False
        shown = rolf_house(s, take=[0, 0])
        log("Rolf's house:", play.scripts_text(shown or []))
    log("party:", party_line(s))
    gear_up(s, PASEO, log=log)
    # (build 102) Long walks north of the tube, not the square by the gate.
    ok = roam_and_gear(s, PASEO, ARIMA_TARGET, mild_ground(), log=log)
    log("Paseo grind with Rudo:",
        f"reached L{ARIMA_TARGET}" if ok else "gave up", "--", party_line(s))
    log("bags:", bags(s))
    return ok


def stage_gear(s, log, rounds=6):
    # Arima's weapon store before Shure (Arima has no armour store).
    # Rounds: a stretch of Arima's field, gearing up each time through,
    # then Paseo -- its item store buys back what the new pieces
    # replaced (Arima has no item store either), its armour store, and
    # the bags handed round (a chest's SilRibbon is Nei's whoever picked
    # it up) -- and back while Arima's shelf still holds an upgrade.
    # enter_town, not travel to the edges: a party standing on an edge
    # cell leaves again with its next step.
    ok = False
    for rnd in range(rounds):
        if not enter_town(s, ARIMA):
            log("could not reach Arima")
            return False
        # (build 102) Long walks over the same ground as Paseo's grind.
        roam_and_gear(s, ARIMA, 0, mild_ground(), trips=30, log=log,
                      until_geared=True)
        log(f"Arima, round {rnd}:", party_line(s))
        if not enter_town(s, PASEO):
            log("could not get back to Paseo")
            return False
        heal_here(s, PASEO)
        gear_up(s, PASEO, log=log)
        if geared(s, ARIMA) and geared(s, PASEO):
            ok = True
            break
    log("Arima gear:", "geared" if ok else "not yet", "--", party_line(s))
    log("bags:", bags(s))
    return ok


def stage_shure(s, log):
    # F3's pocket body first: the Small Key opens the locked chests.
    body = next((c for c, (o, _, i) in level_objects(SHURE_F3).items()
                 if o == 0x2F and i == 0x11), None)
    if body is None:
        log("no body on Shure F3?")
        return False
    shown = search(s, SHURE_F3, body, log=log, key=False)
    log("Shure F3 body:", play.scripts_text(shown or []))
    shown = search(s, SHURE_F3, body, log=log, key=False)
    log("Shure F3 body again:", play.scripts_text(shown or []))
    for level in (SHURE_F3, SHURE_F2, SHURE_F1, SHURE_G):
        loot(s, level, log)
    log("after Shure:", party_line(s))
    log("bags:", bags(s))
    return bool(find_item(s, "Dynamite"))


def stage_nido(s, log):
    # Blasters (formation 44: two, technique $EB -- loc_2CEA, 20 to the
    # whole party every turn at rate 255) wipe an under-geared party.
    # Arima's shelf again with Shure's money, then Paseo for the bags.
    stage_gear(s, log)
    # The door: stand south of it, face it, Dynamite from the menu.
    if not travel(s, NIDO_G, {(53, 46)}, log=log):
        log("could not reach Nido's door")
        return False
    s.send("press up", "wait idle")
    s.ready()
    shown = talk(s)
    log("A on Nido's door:", play.scripts_text(shown))
    stick = find_item(s, "Dynamite")
    if not stick:
        log("no Dynamite in any bag")
        return False
    shown = use_item(s, stick[0], stick[1]) or []
    log("Dynamite:", play.scripts_text(shown))
    s.send("wait idle")
    s.ready()
    log("after the blast:", play.level_name(s.state.get("level")), s.state.get("x"),
        s.state.get("y"), "bags", bags(s))
    # Through the gap: the party is on 54,48 after backing away.
    if not travel(s, NIDO_G, {(53, 44)}, log=log):
        log("could not walk through the blown door")
        return False
    log("through the door:", party_line(s))
    loot(s, NIDO_F1, log)
    # F2: Teim's scene object is at 47,31 (object $2A, event $17).
    scene = next((c for c, (o, _, _) in level_objects(NIDO_F2).items()
                  if o == 0x2A), None)
    if scene is None:
        log("no scene object on Nido F2?")
        return False
    # Teim is an A-button event (Obj_Teim's init sets $2C to 4, which
    # AButton_ObjTypeIndex masks to ObjType_Events): walk up to her,
    # face her, A, and C through the scene.
    before = len(s.shown)
    shown = search(s, NIDO_F2, scene, log=log, key=False)
    if shown is None:
        log("could not reach Teim's room")
        return False
    for _ in range(20):
        if s.state.get("kind") == "waiting" and s.state.get("wait") == "control":
            break
        s.press_through()
    log("Nido F2 scene:", play.scripts_text(s.shown[before:]))
    log("after Nido:", play.level_name(s.state.get("level")), party_line(s))
    log("bags:", bags(s))
    return True


def stage_darum(s, log):
    # Teim in anyone's bag and the tube ($0E) loaded: LoadSpritesInLevel's
    # tail (ps2.asm:896) takes her out, sets $DE6E = $302 and runs demo 2
    # (Demo_TeimScene) with "A Prologue". Walk in from the east and play
    # the scene through.
    if not find_item(s, "Teim"):
        log("nobody carries Teim")
        return False
    before = len(s.shown)
    if not enter_town(s, DARUM_TUBE) and s.state.get("level") != DARUM_TUBE:
        log("could not reach the tube:", play.level_name(s.state.get("level")))
        return False
    for _ in range(40):
        st = s.state
        if st.get("screen") == "level" and st.get("kind") == "waiting" \
                and st.get("wait") == "control":
            break
        s.press_through()
    log("the tube:", play.scripts_text(s.shown[before:]))
    log("after the scene:", play.level_name(s.state.get("level")), s.state.get("x"),
        s.state.get("y"), party_line(s))
    return find_item(s, "Teim") is None


def stage_oputa(s, log):
    # West through the tube, past where Darum stood, to Oputa: its armour
    # store (Fibergear, FiberVest for Nei), its weapons, an item store to
    # sell to, and its Data Memory for the checkpoint.
    if not enter_town(s, OPUTA):
        log("could not reach Oputa:", play.level_name(s.state.get("level")), s.state.get("x"),
            s.state.get("y"))
        return False
    log("Oputa:", party_line(s))
    heal_here(s, OPUTA)
    gear_up(s, OPUTA, log=log)
    ok = grind_and_gear(s, OPUTA, 0, trips=60, log=log, until_geared=True)
    log("Oputa gear:", "geared" if ok else "not yet", "--", party_line(s))
    log("bags:", bags(s))
    return ok


def stage_amy(s, log):
    # Oputa seen: LoadSpritesInLevel's tail bumped party_member_join_next,
    # so the next member knocks at Rolf's house. Take them into the
    # party (four places), then gear everyone in Oputa again.
    if len(s.state.get("party") or []) >= 4:
        # (build 103) A save on the way (the grind's own, in Oputa): Amy
        # is in already.
        log("four in the party already: the grind goes on")
    else:
        if not enter_town(s, PASEO):
            log("could not get back to Paseo")
            return False
        # The regroup list is everyone joined bar Rolf and Nei, and each
        # pick leaves it (loc_A4E4 counts through the members not yet
        # placed): stop 0 is Rudo, then stop 0 again is the newcomer.
        shown = rolf_house(s, take=[0, 0])
        log("Rolf's house:", play.scripts_text(shown or []))
        log("party:", party_line(s))
        if len(s.state.get("party") or []) < 4:
            log("nobody new joined")
            return False
        gear_up(s, PASEO, log=log)
    if not enter_town(s, OPUTA):
        log("could not reach Oputa")
        return False
    heal_here(s, OPUTA)
    ok = grind_and_gear(s, OPUTA, 0, trips=60, log=log, until_geared=True)
    log("Oputa gear with four:", "geared" if ok else "not yet", "--",
        party_line(s))
    log("bags:", bags(s))
    return ok


# (build 103) The Biosystems Lab's ground floor as a ground for long walks:
# every cell reached from inside the blown door without a stair or hole --
# the entrance hall, the east hall, the west corridor and its south rooms,
# the north-west hall and the north rooms, 1,133 cells -- one encounter
# group (the level's own byte, 23: 90 HP and 116 EXP a formation on
# average; F1 78/110, F2 67/68, B1 94/134). Anchors from the entrance hall
# round the floor clockwise; legs of about 25-60 steps.
LAB_SPOT = (47, 70)                     # inside the blown door (47,74)
LAB_ANCHORS = [(47, 64), (75, 57), (22, 57), (2, 36), (12, 22), (37, 12)]
_LAB_GROUND = None


def lab_ground():
    global _LAB_GROUND
    if _LAB_GROUND is None:
        _LAB_GROUND = play.Ground(LAB_G, {play._levels()[LAB_G]["enemy_group"]},
                                  LAB_SPOT, LAB_ANCHORS)
    return _LAB_GROUND


LAB_TARGET = 17
LAB_TRIPS = 400


LAB_DOOR_FLAG = 0xC729      # biosystems_lab_door_blown (flag_names.json)


def lab_door_open(s):
    """The ground floor's door blown: Dynamite's event DYNAMITE_DOOR_2
    ($19) sets its byte, and the door (Obj_DestroyableDoor) reads it --
    what a player knows from having blown it."""
    return flag_set(s, LAB_DOOR_FLAG)


def stage_lab_in(s, log):
    # "The Biosystems Lab is located in the southern part of this town"
    # (Oputa's $29). The ground floor's door is object $3C, index $19
    # (Dynamite, as Nido's). (build 103) The whole grind from Amy's
    # joining is inside it: Oputa's field (encounter group 4, 31 EXP a
    # formation) paid a member 290-308 EXP a 100 k ticks, the ground floor
    # (group 23, 116 EXP) 849-901, walked from the entrance cell or over
    # the whole floor alike, with no wipe-out from Amy at L6
    # (tools/grind_trials.py, docs/history.md Build 103). Build 102b's
    # amy_grind and lab_grind (Oputa's field, to L14 and L16) are gone:
    # 9.7 M ticks bought the lowest member L6 -> L17 there and here.
    # Long walks over the floor (lab_ground), gear in Oputa at every
    # return, until everyone is L17 -- what the three grinds reached in
    # all twelve of 102b's seeds, and `lab` went through at it every time.
    if not enter_town(s, OPUTA):
        log("could not reach Oputa")
        return False
    heal_here(s, OPUTA)
    gear_up(s, OPUTA, log=log)
    resupply(s, door=town_buildings(OPUTA)[BUILDING_ITEM][0], level=OPUTA)
    if not lab_door_open(s) and not blow_door(s, LAB_G, (47, 74), log):
        return False
    log("inside the lab:", party_line(s))
    ok = dungeon_grind(s, LAB_G, LAB_SPOT, OPUTA, LAB_TARGET, trips=LAB_TRIPS,
                       log=log, ground=lab_ground(), gear=True)
    log(f"lab grind: everyone L{LAB_TARGET}" if ok else "lab grind: gave up",
        "--", party_line(s))
    retreat(s, OPUTA, log=log)
    gear_up(s, OPUTA, log=log)
    # The level is a target, not a gate: the grind stops at its trip
    # limit and the lab goes on with what it reached.
    return s.state.get("level") == OPUTA


def stage_lab(s, log):
    # The floors' chests, F2's door (index $1A, at 47,47: the way to the
    # holes that drop to B1 is behind it, and its Dynamite is in F2's
    # chest at 47,65, $3F), then the Recorder -- ObjType_Events $12 in
    # B1 (loc_DBF8), which with party_member_join_next at 3 raises it
    # to 4 -- and straight back out.
    resupply(s, door=town_buildings(OPUTA)[BUILDING_ITEM][0], level=OPUTA)
    s.careful = True
    try:
        for level in (LAB_F1, LAB_F2):
            loot(s, level, log, town=OPUTA)
        if not blow_door(s, LAB_F2, (47, 48), log):
            return False
        for attempt in range(6):
            if needs_healing(s) and count_mates(s) == 0:
                retreat(s, OPUTA, log=log)
            shown = search_event(s, LAB_B1, 0x12, log=log)
            log(f"the Recorder, try {attempt}:",
                play.scripts_text(shown or []), "--",
                party_line(s))
            if find_item(s, "Recorder"):
                break
            if s.state.get("screen") not in ("level", "battle"):
                log("lost:", s.state.get("screen"))
                return False
            retreat(s, OPUTA, log=log)
    finally:
        s.careful = False
    retreat(s, OPUTA, log=log)
    log("after the lab:", party_line(s))
    log("bags:", bags(s))
    return find_item(s, "Recorder") is not None


def stage_lab_b1(s, log):
    # B1's three chests (a Trimate, an Antidote, a PoisonShot), from
    # Oputa each time the mates run out.
    resupply(s, door=town_buildings(OPUTA)[BUILDING_ITEM][0], level=OPUTA)
    s.careful = True
    try:
        out = loot(s, LAB_B1, log, town=OPUTA)
    finally:
        s.careful = False
    retreat(s, OPUTA, log=log)
    log("after B1:", party_line(s))
    # The chests are worth having, not the way on: back in Oputa is done.
    missed = [c for c, v in out.items() if not v]
    if missed:
        log("B1 chests unreached:", missed)
    return s.state.get("level") == OPUTA


def blow_door(s, level, stand, log):
    """Obj_DestroyableDoor: stand below it, face it, A ($170D), and
    Dynamite from the menu (ItemAction_Dynamite: demo 4, the burst)."""
    stick = find_item(s, "Dynamite")
    if not stick:
        log(f"no Dynamite for the door on {play.level_name(level)}")
        return False
    if not travel(s, level, {stand}, log=log):
        log(f"could not reach the door on {play.level_name(level)}")
        return False
    s.send("press up", "wait idle")
    s.ready()
    shown = talk(s)
    log(f"A on the door on {play.level_name(level)}:",
        play.scripts_text(shown))
    stick = find_item(s, "Dynamite")
    shown = use_item(s, stick[0], stick[1]) or []
    log("Dynamite:", play.scripts_text(shown))
    play.mark_story(s)                  # (build 103) a save on the way
    s.send("wait idle")
    s.ready()
    return True


def gov_policy(s):
    # YES to anything the Governor asks; C through the rest.
    return 0


def stage_hugh(s, log):
    # The Recorder raised party_member_join_next to 4: Hugh knocks at
    # Rolf's house. Keep Rudo and Amy (stop 0 twice), then take the
    # Recorder to the Governor: loc_C9BE, $1107 and on to the library
    # (loc_CA22), whose loc_C3E6 plays it -- $1010 to $1014, the graph
    # between -- and loc_C4CA trades it for the Key Tube. The debrief
    # ($110C-$1110, $C736 and $C712) waits for $C737, later.
    if not enter_town(s, PASEO):
        log("could not get back to Paseo")
        return False
    shown = rolf_house(s, take=[0, 0])
    log("Rolf's house:", play.scripts_text(shown or []))
    knocked = any(x in (0x090C, 0x0927) for x in shown or [])
    log("party:", party_line(s))
    shown = visit(s, PASEO, PASEO_DOORS["tower"], gov_policy)
    log("the Governor:", play.scripts_text(shown or []))
    # (build 168f) $1013, the message the library asks for last: the US
    # runs it on into $1014, the Japanese ends it at $1013 (its words all
    # there), so $1014 is never looked up in Japanese.
    return knocked and 0x1013 in (shown or []) \
        and find_item(s, "KeyTube") is not None \
        and find_item(s, "Recorder") is None


def stage_key_tube(s, log):
    # Who goes first: Rolf, Nei and Rudo, and the fourth place by
    # play.choose_party -- Hugh joined at L1, sixteen levels behind Amy,
    # so he takes it to catch up. Then the Key Tube on the tube's locked
    # door (level $0F, Obj_OpenableDoor, index $1B, at 9,15: A is $170E,
    # ItemAction_KeyTube $0018 and demo 5), through, and on to Zema.
    party = pick_party(s, log=log)
    log("party:", " ".join(party), "--", party_line(s))
    if s.state.get("level") == PASEO:
        heal_here(s, PASEO)
        gear_up(s, PASEO, log=log)
    shown = search(s, TUBE_DOOR, (9, 15), log=log, key=False) or []
    log("the tube's door:", play.scripts_text(shown))
    kt = find_item(s, "KeyTube")
    if kt is not None:
        shown = use_item(s, kt[0], kt[1]) or []
        log("Key Tube:", play.scripts_text(shown))
        s.send("wait idle")
        s.ready()
    if not enter_town(s, ZEMA):
        log("could not reach Zema:", play.level_name(s.state.get("level")), s.state.get("x"),
            s.state.get("y"))
        return False
    log("Zema:", party_line(s))
    return True


def stage_zema(s, log):
    # Zema's armour (Titanigear, Titanimet, MirShield) and weapons
    # (CrmcSword, LaserKnife, Slasher, Cannon): the money from the lab
    # buys the shelf at once. Then its field until the member catching
    # up is within CATCH_UP_GAP of the others, gearing each time through.
    if not enter_town(s, ZEMA):
        log("could not reach Zema")
        return False
    heal_here(s, ZEMA)
    gear_up(s, ZEMA, log=log)
    p = s.state.get("party") or []
    others = sorted(m["level"] for m in p)
    target = others[1] - CATCH_UP_GAP + 1 if len(others) > 1 else 1
    ok = grind_and_gear(s, ZEMA, target, trips=150, log=log,
                        until_geared=True, lowest=True)
    log(f"Zema grind to L{target}:", "done" if ok else "gave up", "--",
        party_line(s))
    log("bags:", bags(s))
    return ok


def stage_roron(s, log):
    # The party chosen again (the catch-up may be over), then Roron:
    # every floor's chests, from Zema each time the mates run out, and
    # the jet scooter's maker on F5 -- object $1B with dialogue $87,
    # loc_E690: $1685 and $C716 = 1, the scooter parked outside Paseo's
    # side of the ocean at $590,$730. The cake on F4 ($85) is left alone.
    pick_party(s, log=log)
    if not enter_town(s, ZEMA):
        log("could not reach Zema")
        return False
    heal_here(s, ZEMA)
    gear_up(s, ZEMA, log=log)
    resupply(s, door=town_buildings(ZEMA)[BUILDING_ITEM][0], level=ZEMA)
    s.careful = True
    got = False
    try:
        for level in (RORON_G, RORON_F1, RORON_F2, RORON_F3, RORON_F4,
                      RORON_F5):
            loot(s, level, log, town=ZEMA)
        for attempt in range(4):
            if needs_healing(s) and count_mates(s) == 0:
                retreat(s, ZEMA, log=log)
            if not travel(s, RORON_F5, {(19, 55)}, log=log):
                log(f"the maker, try {attempt}: could not reach F5 --",
                    party_line(s))
                if s.state.get("screen") not in ("level", "battle"):
                    return False
                retreat(s, ZEMA, log=log)
                continue
            shown = talk_to(s, 0x87, want={0x1685}, log=log) or []
            log(f"the maker, try {attempt}:",
                play.scripts_text(shown))
            if 0x1685 in shown:
                got = True
                break
    finally:
        s.careful = False
    retreat(s, ZEMA, log=log)
    log("after Roron:", party_line(s))
    log("bags:", bags(s))
    return got


def yes_policy(s):
    # YES to anything asked (the inventor's "Please give it to me!").
    return 0


def stage_uzo(s, log):
    """From Roron's end (in Zema, the scooter parked at 89,115): on foot
    to the scooter, A on it; ride to Uzo's island (the town's door is on
    the island's ground, not a push from the water) and get off; the
    Maruera tree, event $10 ($1710, $1702, $1703, the leaf); back to the
    scooter; ride to ground connected to Kueri, get off, and the
    inventor at 15,39 turns the leaf into Mruragum ($B05, YES, $B06)."""
    pick_party(s, log=log)
    if not board_scooter(s, log=log):
        log("uzo: could not board the scooter:", s.state)
        return False
    if not ride_ashore(s, set(door_cells(MOTAVIA, *UZO_RECORD)), log=log):
        log("uzo: no landing on Uzo's island:", s.state)
        return False
    got = False
    for attempt in range(3):
        shown = search_event(s, UZO, 0x10, log=log) or []
        log(f"the Maruera tree, try {attempt}:",
            play.scripts_text(shown))
        if find_item(s, "MruraLeaf"):
            got = True
            break
        if needs_healing(s) and count_mates(s) == 0:
            log("uzo: hurt with nothing left")
            break
    if not got:
        return False
    log("the leaf:", find_item(s, "MruraLeaf"), party_line(s))
    if not board_scooter(s, log=log):
        log("uzo: could not get back on the scooter:", s.state)
        return False
    if not ride_ashore(s, set(door_cells(MOTAVIA, *KUERI_RECORD)), log=log):
        log("uzo: no landing for Kueri:", s.state)
        return False
    if not enter_town(s, KUERI):
        log("could not reach Kueri:", s.state)
        return False
    heal_here(s, KUERI)
    shown = visit(s, KUERI, INVENTOR_DOOR, yes_policy) or []
    log("the inventor:", play.scripts_text(shown))
    gum = find_item(s, "MruraGum")
    log("the gum:", gum, "bags:", bags(s))
    if gum:
        gear_up(s, KUERI, log=log)
        b = town_buildings(KUERI)
        if BUILDING_ITEM in b:
            resupply(s, door=b[BUILDING_ITEM][0], level=KUERI)
    return bool(gum)


def stage_gum(s, log):
    """From Kueri with the gum: on the scooter to the ocean floor's
    event $F block (158-159, 12-13), Mruragum used facing it ($19, then
    loc_9CD6: the Underwater Passage at 112,23, the way out recorded as
    the scooter's place), through the passage to Climatrol's ground
    floor (82,8), and back to Kueri."""
    if any(m["dead"] for m in s.state.get("party") or []):
        retreat(s, KUERI, log=log)
    pick_party(s, log=log)
    if not board_scooter(s, log=log):
        log("gum: could not board the scooter:", s.state)
        return False
    if not ride_to_face(s, event_block(MOTAVIA, 0x0F), log=log):
        log("gum: could not face the ocean floor's spot:", s.state)
        return False
    gum = find_item(s, "MruraGum")
    if not gum:
        log("gum: no gum")
        return False
    shown = use_item(s, gum[0], gum[1]) or []
    s.ready()
    log("the gum:", play.scripts_text(shown),
        (s.state.get("level"), s.state.get("x"), s.state.get("y")))
    if s.state.get("level") != UNDERWATER:
        return False
    loot(s, UNDERWATER, log, town=KUERI)
    ok = travel(s, CLIMATROL_G, {(82, 8)}, log=log)
    log("Climatrol:", ok, (play.level_name(s.state.get("level")), s.state.get("x"),
                           s.state.get("y")), party_line(s))
    if ok:
        s.send("shot climatrol_arrival")
    retreat(s, KUERI, log=log)
    return ok


CLIMATROL_F7 = 0x3D
NEIFIRST_SPOT = (48, 24)          # facing down at her, 48,25
# tools/party_trials.py, build 79 (mates working, 10 repeats a point):
# Rudo and Anna are the pair -- 5/10 against Neifirst at today's levels,
# 9/10 after ~300 F2 fights, 10/10 after ~450 (Rolf 22, Rudo 22, Anna
# 19, 52% HP left). The house puts Nei in slot 1 itself before Climatrol
# (RolfHouseOption_Reorganize), and she grinds alive: 190 EXP a member
# a fight on F2 with nobody down in 30, against Anna down by the eighth
# with Nei at 0 HP (tools/grind_rate.py).
CLIMATROL_PARTY = ("Rolf", "Nei", "Rudo", "Anna")
CLIMATROL_TARGET = {"Rolf": 22, "Rudo": 22, "Anna": 19}


# The Underwater Passage beside its pipe out (110,24): 114 EXP a member
# a fight with Rolf, Nei, Rudo and Anna, nobody down in 30
# (tools/grind_rate.py --floor 0x13 --cell 104,25). F2 pays 190, but its
# way out is on foot through F1, the ground floor and the whole passage
# (play.WALK_OUT: no Hinas in this party, no Escapipe sold before
# Climatrol), and two build-79 runs were wiped out on that walk.
GRIND_FLOOR = UNDERWATER
GRIND_SPOT = (104, 25)             # 101-107 on row 25, clear of the pipe


def to_climatrol(s, log):
    """From Kueri: the scooter, the gum facing the ocean floor's event
    $F block, the Underwater Passage. The gum is not eaten
    (ItemAction_MruraGum, ps2.asm:14375: the script and a step, no
    RemoveItemFromInventory), so one lasts. True on the passage."""
    if not board_scooter(s, log=log):
        log("to_climatrol: could not board the scooter:", s.state)
        return False
    if not ride_to_face(s, event_block(MOTAVIA, 0x0F), log=log):
        log("to_climatrol: could not face the ocean floor's spot:", s.state)
        return False
    gum = find_item(s, "MruraGum")
    if not gum:
        log("to_climatrol: no gum")
        return False
    use_item(s, gum[0], gum[1])
    s.ready()
    if s.state.get("level") != UNDERWATER:
        log("to_climatrol: the gum did not open the passage:", s.state)
        return False
    return True


def back_to_climatrol(s, log):
    """From anywhere but Climatrol or the passage: Kueri first -- a
    retreat's Ryuka or Telepipe lands in the last save's town, Kueri
    since the gum stage -- mended, geared and stocked there, then
    to_climatrol. True on Climatrol, the passage, or once there."""
    lv = s.state.get("level")
    if lv is not None and (0x36 <= lv <= 0x3D or lv == UNDERWATER):
        return True
    if lv != KUERI and not retreat(s, KUERI, log=log):
        return False
    heal_here(s, KUERI)
    gear_up(s, KUERI, log=log)
    # Trimates: in-battle mates are what carried the trials, and the way
    # out of Climatrol is a long walk (play.WALK_OUT). Kueri's item store
    # offers five items, and the sixth on its list, the Escapipe, is not
    # one of them.
    stock_mates(s, KUERI, want=12, log=log, trimates=True)
    stock_item(s, KUERI, "Antidote", 2, log=log)
    # Kueri's Data Memory: a wipe goes back to here, not to the stage's
    # start (climatrol_grind).
    b = town_buildings(KUERI)
    if 2 in b:
        save(s, 1, level=KUERI, door=b[2][0])
        play.trip_save(s, KUERI, log=log, saved=True)     # (build 103)
    return to_climatrol(s, log)


def reached(s, target):
    """Everyone named in `target` ({name: level}) at their level."""
    p = {m["name"]: m["level"] for m in (s.state.get("party") or [])}
    return all(p.get(n, 0) >= lv for n, lv in target.items())


@play.grinding
def climatrol_grind(s, log, target, trips=120, fights_per_trip=8):
    """dungeon_grind with a way back: F2's landing pays what Kueri's
    field cannot (Rolf L19 stayed L19 over sixty trips there), and the
    hospital is in Kueri, from which the only way here is to_climatrol
    -- which plan() cannot walk. True once the core three (the first
    three members) are at `target` -- or, for a {name: level} target,
    each named member at theirs."""
    if not isinstance(target, dict):
        target = {m["name"]: target
                  for m in (s.state.get("party") or [])[:3]}
    wipes = 0
    for trip in range(trips):
        if s.state.get("screen") in ("title", "sega", "gameover"):
            # Wiped out: CONTINUE from the last save, Kueri's.
            wipes += 1
            slot = s.saves_dir / "slot1.json"
            if wipes > 5 or not slot.exists() \
                    or not continue_game(s, str(slot)):
                log("climatrol_grind: wiped out, and no way back in")
                return False
            log(f"climatrol_grind: wiped out ({wipes}); continued the last "
                "save --", party_line(s))
        if reached(s, target):
            return True
        if not back_to_climatrol(s, log):
            return False
        here = (s.state.get("x"), s.state.get("y"))
        if s.state.get("level") != GRIND_FLOOR or here != GRIND_SPOT:
            if not travel(s, GRIND_FLOOR, {GRIND_SPOT}, log=log):
                log("climatrol_grind: could not reach the landing:", s.state)
                if not (0x36 <= (s.state.get("level") or 0) <= 0x3D):
                    continue
                return False
        before = len(s.fights)
        wander(s, ["walk left 3", "walk right 3"], fights_per_trip,
               mates=True, name="climatrol")
        mend(s, below=0.8)
        p = s.state.get("party") or []
        log(f"trip {trip}: {len(s.fights) - before} fights; "
            + " ".join(f"{m['name']} L{m['level']} {m['hp']}/{m['max_hp']}"
                       + (" dead" if m["dead"] else "") for m in p)
            + f"; meseta {s.state.get('meseta')}")
        if s.state.get("screen") not in ("level", "battle"):
            log("climatrol_grind: lost: " + s.state.get("screen", "?"))
            return False
        # Out while there is still something in the bags: the way out
        # is on foot through F1, the ground floor and the passage.
        if any(m["dead"] for m in p) or needs_healing(s, frac=0.7) \
                or count_mates(s) < 6:
            if not retreat(s, KUERI, log=log):
                if s.state.get("screen") in ("title", "sega", "gameover"):
                    continue                    # the top of the loop
                log("climatrol_grind: stuck on the way out")
                return False
    if play.LEAN and s.state.get("screen") == "level":
        # (build 98) Out of trips short of the target: F7 with what it has.
        log("lean: climatrol_grind short of", target, "--", party_line(s))
        return True
    return False


def face_neifirst(s, log, shots=True):
    """Standing on F7's 48,24: face Neifirst, A, C through the talk to
    the question, YES, and both fights -- Nei alone (she loses: the
    story), then the party once the picture's $183D/$183E have run
    (ObjType_Events $26's second visit, the party back from $C618, Nei
    among them at 0 HP). Stops back on the level with $1827 up, or on
    the SEGA/title screen. Returns {won, first, second, ticks, party}:
    the scripts of each fight, the ticks the party's fight took, and the
    party as it stood after it."""
    out = {"won": False, "first": [], "second": [], "ticks": 0,
           "party": []}
    s.send("press down", "wait idle")
    if shots:
        s.send("shot neifirst")
    before = len(s.shown)
    s.send("press a", "wait idle")
    for _ in range(20):
        st = s.state
        if st.get("menu") == 0x1F:
            break
        if st.get("screen") != "level" or 0x180E in s.shown[before:] \
                or 0x1822 in s.shown[before:]:
            log("neifirst: no fight offered:",
                play.scripts_text(s.shown[before:]))
            return out
        s.send("press c", "wait idle")
    s.send("press c", "wait idle")               # YES: $1823
    s.send("press c", "limit 3000", "wait screen battle", "wait idle")
    out["first"] = fight_smart(s, run_below=0.0, limit=400,
                               stop_on_level=True)
    s.send("limit 3000", "wait idle")
    if s.state.get("screen") != "level":
        log("neifirst: after Nei's fight:", s.state.get("screen"))
        return out
    # The picture: $183D, $183E (Nei lost) -- C through them to the
    # party's fight -- or $1826 (she won; not the story).
    for _ in range(12):
        if s.state.get("screen") != "level" or 0x1826 in s.shown[before:]:
            break
        s.send("press c", "limit 3000", "wait idle")
    if s.state.get("screen") == "battle":
        t0 = s.state.get("tick") or 0
        out["second"] = fight_smart(s, run_below=0.0, limit=600,
                                    stop_on_level=True)
        s.send("limit 3000", "wait idle")
        out["ticks"] = (s.state.get("tick") or 0) - t0
    st = s.state
    out["party"] = st.get("party") or []
    out["won"] = st.get("screen") == "level" and 0x1827 in s.shown[before:]
    return out


def stage_climatrol(s, log):
    """From Kueri with the gum: the scooter to the ocean floor, the gum,
    the Underwater Passage to Climatrol, the chests on F2, F3, F5 and F6
    (mending in Kueri when the mates run out), F7 and A on Neifirst
    (ObjType_Events $25): the talk, YES, Nei alone against her, and
    whichever way that goes the death of Nei -- $1826 or $183D/$183E and
    the party's own fight -- through loc_7E02's reloads to the clone lab
    in Paseo ($408), where the port's story ends this stage."""
    if any(m["dead"] for m in s.state.get("party") or []):
        retreat(s, KUERI, log=log)
    pick_party(s, log=log, want=CLIMATROL_PARTY)
    if not back_to_climatrol(s, log):
        return False
    # The chests once: a --redo from story_climatrol.json (written by a
    # retreat's save after them) starts with them taken.
    # (build 103) Nor when this try resumes a save on the way made after
    # them (play.trip_save's note).
    if not getattr(s, "redo", False) \
            and "climatrol_chests" not in getattr(s, "progress", set()):
        for floor in (0x38, 0x39, 0x3B, 0x3C):
            loot(s, floor, log, town=KUERI)
            log(f"{play.level_name(floor)} looted:", party_line(s))
        s.progress = getattr(s, "progress", set()) | {"climatrol_chests"}
        play.mark_story(s)
    if not reached(s, CLIMATROL_TARGET):
        ok = climatrol_grind(s, log, CLIMATROL_TARGET)
        log("Climatrol grind:", "reached" if ok else "gave up",
            CLIMATROL_TARGET, "--", party_line(s))
        if not ok:
            return False
    # Full, and the bags topped up, before F7.
    retreat(s, KUERI, log=log)
    if needs_healing(s):
        retreat(s, KUERI, log=log)
    if not back_to_climatrol(s, log):
        return False
    ok = travel(s, CLIMATROL_F7, {NEIFIRST_SPOT}, log=log)
    log("F7:", ok, (play.level_name(s.state.get("level")), s.state.get("x"), s.state.get("y")),
        party_line(s))
    if not ok:
        return False
    result = face_neifirst(s, log)
    if not result["won"]:
        return False
    # The scene: C through $1827 / $1825 / $1828 to the clone lab.
    for i in range(30):
        st = s.state
        if st.get("screen") == "building":
            break
        if st.get("kind") == "waiting" and st.get("wait") == "text":
            if 0x1826 in s.shown[-2:] or 0x1827 in s.shown[-2:]:
                s.send(f"shot nei_death_{i}")
            s.send("press c")
        s.send("limit 12000", "wait idle")
    st = s.state
    log("after the scene:", st.get("screen"), st.get("building"),
        play.scripts_text(s.shown[-6:]), party_line(s))
    s.send("shot clone_lab")
    return st.get("screen") == "building" and 0x0408 in s.shown[-4:]


# After Climatrol: tools/dam_trials.py (build 80) ran every party of
# Rolf and three of Rudo, Amy, Hugh, Anna and Kain through forty fights on
# Red Dam F2 -- the next dungeon, since the next boss, the Army Eyes
# (formation $101), cannot be won (loc_2E24 carries the party to Gaira in
# round three). Rudo+Amy+Anna went furthest on a trip at 0, 300 and 450
# grind fights and second at 150; Anna is in every top party.
DAM_PARTY = ("Rolf", "Rudo", "Amy", "Anna")


def stage_dam_party(s, log):
    """From the Climatrol checkpoint (Paseo): the debrief is already done
    -- demo 7 walks the party from the clone lab to the Governor, whose
    loc_CA42/loc_CA58 set $C736 and $C712 -- and with the Recorder gone
    the tower's desk ($0D01) offers ROOM, LIBRARY and OUTSIDE, not him.
    So the flags are checked, then Rolf's house for Rudo, Amy and Anna,
    the hospital and a save. The dams are the next stage: the Control
    Tower's cards, each dam's panel (ItemAction_*Card's loc_9D70, ported
    in build 80) and the Army Eyes."""
    out = s.send("expect flag $C736 1", "expect flag $C712 1")
    if any("FAIL" in ln for ln in out):
        log("the debrief has not happened:", out)
        return False
    party = pick_party(s, log=log, want=DAM_PARTY)
    heal_here(s, PASEO)
    save(s, 1)
    log("party:", " ".join(party), "--", party_line(s))
    names = [m["name"] for m in s.state.get("party") or []]
    return tuple(names) == DAM_PARTY


PIATA = 0x09
TOWER_F1 = 0x3F
GAIRA = 0x41
# Control Tower F1's four boxes, ObjType_Events $13-$16 (loc_DC1E): each
# gives its card once (route tower_cards).
TOWER_CARDS = ((0x13, "GreenCard"), (0x14, "BlueCard"),
               (0x15, "YellowCard"), (0x16, "RedCard"))
# Each dam: its card; the floor the scooter's push lands on, with the
# card's door there (Obj_OpenableDoor, index $1C/$1E/$20/$22); the floor
# with the panel (Obj_InvisibleBlock2, index + 1). ItemAction_*Card
# (ps2.asm:14406-14446): the door opens by demo 5, the panel is loc_9D70.
DAMS = (("GreenCard", 0x30, (95, 25), 0x31, 0x1D),
        ("BlueCard", 0x2B, (53, 27), 0x2E, 0x1F),
        ("YellowCard", 0x24, (17, 27), 0x26, 0x21),
        ("RedCard", 0x29, (47, 17), 0x2A, 0x23))
GAIRA_PANEL = (53, 26)             # below $37 at $360,$1A0 (box row 9)


def ids(shown):
    return play.scripts_text(shown or [])


PIATA_RECORD = (83, 91)            # Motavia's door record for Piata
PIATA_TOWER_STAND = (9, 24)        # below the tower's door


def to_piata(s, log):
    """Piata is not on Paseo's side of the water: the scooter, ashore by
    its door record, and in."""
    if s.state.get("level") == PIATA:
        return True
    if s.state.get("level", 0) < 0x10 and s.state.get("level") != PIATA:
        if not s.state.get("riding") and not board_scooter(s, log=log):
            log("to_piata: could not board the scooter:", s.state)
            return False
        if not ride_ashore(s, set(door_cells(MOTAVIA, *PIATA_RECORD)),
                           log=log):
            log("to_piata: no landing for Piata:", s.state)
            return False
    return enter_town(s, PIATA)


OPUTA_USTVESTIA = (7, 7)           # Oputa's building 12, Ustvestia
KEYBOARD_STAND = (47, 27)          # below Control Tower F1's keyboard


def ustvestia_policy(s):
    # NO to hearing him play ($A01); YES to the rest; Rolf in the list.
    return 1 if s.last_script == 0x0A01 else 0


def learn_musik(s, log):
    """Ustvestia's lesson in Oputa (route ustvestia_lesson): Rolf, 2000
    meseta, Musik -- which the tower's keyboard needs: TechEffect_Musik
    faced at event 7 sets $C734 and demo 5 slides F1's shutter (the
    Obj_OpenableDoor at 47,15, index $24) open. No item opens it."""
    if 0x0A0C in s.shown:
        return True
    if not enter_town(s, OPUTA):
        log("musik: could not reach Oputa")
        return False
    shown = visit(s, OPUTA, OPUTA_USTVESTIA, ustvestia_policy) or []
    log("Ustvestia:", ids(shown))
    return 0x0A0C in shown


def play_keyboard(s, log):
    """Face the keyboard from 47,27 and cast Musik, Rolf's last map
    technique (a learned Musik goes last: loc_112C4)."""
    if not goto(s, {KEYBOARD_STAND}, log=log):
        log("keyboard: could not reach it:", s.why)
        return False
    s.send("press up", "wait idle")
    s.ready()
    s.send("press c", "wait idle")
    s.send("press down", "press down", "press c", "wait idle")   # TECH
    s.send("press c", "wait idle")                               # Rolf
    stops = s.state.get("menu_stops") or 1
    s.send("press b", "wait idle", "press b", "wait idle", "press b",
           "wait idle")
    s.ready()
    shown = use_tech(s, 0, stops - 1) or []
    s.send("wait idle")
    s.ready()
    log("Musik at the keyboard:", ids(shown), f"(stop {stops - 1})")
    out = s.send("expect flag $C734 1")
    return not any("FAIL" in ln for ln in out)


def stage_cards(s, log):
    """From Paseo with $C712 set: Piata, mended and geared, and Control
    Tower F1's four boxes for the Green, Blue, Yellow and Red cards; back
    to Piata."""
    if not learn_musik(s, log):
        return False
    if not to_piata(s, log):
        log("cards: could not reach Piata:", play.level_name(s.state.get("level")))
        return False
    heal_here(s, PIATA)
    gear_up(s, PIATA, log=log)
    # The tower's door (record $3E, building 1 in the table, which
    # interact() sets aside) is not in play's door list: stand at 9,24
    # and push up -- ControlTowerGroundF at 46,41 (route
    # piata_tower_open).
    if not goto(s, {PIATA_TOWER_STAND}, log=log):
        log("cards: could not reach the tower's door:", s.why)
        return False
    s.send("walk up 1")
    s.ready()
    log("the tower:", (play.level_name(s.state.get("level")), s.state.get("x"),
                       s.state.get("y")))
    if not travel(s, TOWER_F1, {KEYBOARD_STAND}, log=log) \
            or not play_keyboard(s, log):
        return False
    for event, card in TOWER_CARDS:
        if find_item(s, card):
            continue
        shown = search_event(s, TOWER_F1, event, log=log) or []
        s.ready()
        log(f"the {card}'s box:", ids(shown), find_item(s, card))
    got = [c for _, c in TOWER_CARDS if find_item(s, c)]
    log("cards:", " ".join(got), "--", party_line(s))
    if not enter_town(s, PIATA):
        log("cards: could not get back to Piata")
        return False
    heal_here(s, PIATA)
    return len(got) == 4


def flag_set(s, addr):
    """One byte of the $C700 block is 1 (the probe's expect, whose FAIL
    is only logged in -i mode)."""
    return not any("FAIL" in ln for ln in s.send(f"expect flag ${addr:04X} 1"))


def dam_flags(dam):
    """The dam's door and panel flags: bit 0 of event_flags + index,
    $C72C + index - $1C (ItemAction_*Card, loc_9D70) -- the door's
    index is the panel's minus one."""
    panel = 0xC72C + dam[4] - 0x1C
    return panel - 1, panel


def open_dam(s, dam, log):
    """Scooter to the dam, the card on its door (unless an earlier trip
    opened it), the card on its panel. "done" with the panel's flag set;
    "hurt" when s.careful made goto give up on the way (the caller takes
    the party out and back); "" on anything else."""
    card, entry, door, panel_level, panel_event = dam
    door_flag, panel_flag = dam_flags(dam)
    if flag_set(s, panel_flag):
        return "done"
    if not find_item(s, card):
        log(f"{card}: not carried")
        return ""
    s.hurt = False
    if (s.state.get("level") or 0) < 0x10:
        if not s.state.get("riding") and not board_scooter(s, log=log):
            log(f"{card}: could not board the scooter:", s.state)
            return ""
        if not ride_into(s, entry, log=log):
            log(f"{card}: could not ride into {play.level_name(entry)}:",
                (s.state.get("level"), s.state.get("x"), s.state.get("y")))
            return ""
    if not flag_set(s, door_flag):
        shown = search(s, entry, door, log=log, key=False) or []
        if s.hurt:
            return "hurt"
        held = find_item(s, card)
        if held is None:
            log(f"{card}: gone before the door")
            return ""
        shown += use_item(s, held[0], held[1]) or []
        s.send("wait idle")
        s.ready()
        log(f"{card} door:", ids(shown))
        if not flag_set(s, door_flag):
            log(f"{card}: the door did not open --", party_line(s))
            return "hurt" if s.hurt else ""
    shown = search_event(s, panel_level, panel_event, log=log) or []
    if s.hurt:
        return "hurt"
    held = find_item(s, card)
    if held is None:
        log(f"{card}: gone before the panel")
        return ""
    before = len(s.shown)
    shown += use_item(s, held[0], held[1]) or []
    log(f"{card} panel:", ids(shown + s.shown[before:]), party_line(s))
    return "done" if flag_set(s, panel_flag) else ""


# Where the scooter's push lands in each dam; one step down from it is
# back out on the water, riding (route jet_scooter_dam). The way out
# when nothing else is: Piata's item store sells the Escapipe (build 83;
# no store before it does), which out_of_dam uses first.
DAM_ARRIVAL = {0x30: (93, 17), 0x2B: (54, 31), 0x24: (10, 25),
               0x29: (47, 25)}


def leave_dam(s, dam, log):
    entry = dam[1]
    if not travel(s, entry, {DAM_ARRIVAL[entry]}, log=log):
        log(f"leave_dam: could not get back to {play.level_name(entry)}'s way out")
        return False
    s.send("walk down 1")
    s.ready()
    log("out of the dam:", (play.level_name(s.state.get("level")), s.state.get("x"),
                            s.state.get("y")), party_line(s))
    return s.state.get("level") == MOTAVIA


def army_eyes_to_gaira(s, log, limit=400):
    """loc_81E6's battle ($101) after the fourth panel: nobody wins it;
    the ninth use of their technique is $1219 and Gaira. C through."""
    for _ in range(limit):
        st = s.state
        if st.get("screen") == "level" and st.get("level") == GAIRA:
            return True
        if st.get("screen") == "battle" and st.get("kind") != "waiting":
            s.send("wait ticks 240")
        elif st.get("kind") == "waiting":
            s.send("press c")
        else:
            s.send("wait idle")
    return False


def escape_gaira(s, log):
    """The prison (build 81): $1839, 30 s, $1812; to the panel with only
    RUN in the fights (no weapon, no TP); A on it; the control panel and
    Tyler's spaceship C'd through; Paseo and the Governor ($1111)."""
    s.send("limit 4000")
    for _ in range(60):
        st = s.state
        if 0x1812 in s.shown:
            break
        if st.get("kind") == "waiting" and st.get("wait") != "control":
            s.send("press c")
        else:
            s.send("wait ticks 120")
    log("the prison:", ids([x for x in s.shown if 0x1800 <= x < 0x1900][-4:]),
        "bags:", bags(s))
    s.ready()
    s.fight_opts = {"run_below": 2.0, "use_techs": False}
    try:
        ok = False
        for _ in range(12):
            if goto(s, {GAIRA_PANEL}, log=log):
                # (build 98) A fight can start on the last step: goto is
                # done, but the party is on the battle screen. RUN out of
                # it and walk back if it moved; the old code C'd through
                # it -- FIGHT with no weapons -- and seed 3 died there on
                # all four tries.
                if s.state.get("screen") == "battle":
                    play.fight_smart(s, run_below=2.0, use_techs=False,
                                     stop_on_level=True,
                                     stop_on_gameover=True)
                    if s.state.get("screen") != "level":
                        break                   # wiped out, or still in it
                    s.ready()
                    if (s.state.get("x"), s.state.get("y")) != GAIRA_PANEL:
                        continue
                ok = s.state.get("screen") == "level"
                break
            if s.state.get("screen") == "gameover":
                break
    finally:
        s.fight_opts = None
    log("Gaira's panel:", ok, (s.state.get("x"), s.state.get("y")),
        party_line(s))
    if not ok:
        return False
    s.send("press up", "wait idle", "press a", "limit 3000")
    where = GAIRA                       # the battle state names no level
    for _ in range(200):
        st = s.state
        if st.get("level") is not None:
            where = st["level"]
        if st.get("screen") == "level" and st.get("level") == PASEO \
                and 0x1111 in s.shown and st.get("kind") == "waiting" \
                and st.get("wait") == "control":
            break
        if st.get("screen") == "battle" and where == GAIRA:
            # (build 98) Still on Gaira, a fight: RUN, not FIGHT.
            play.fight_smart(s, run_below=2.0, use_techs=False,
                             stop_on_level=True, stop_on_gameover=True)
            continue
        s.send("press c" if st.get("kind") == "waiting" else "wait idle")
    log("home:", ids([x for x in s.shown if 0x1900 <= x < 0x1920
                      or x == 0x1111][-6:]), "bags:", bags(s))
    return s.state.get("level") == PASEO and 0x1111 in s.shown


def out_of_dam(s, dam, log):
    """Out of a dam onto the water. The Escapipe first: the scooter's push
    went in through loc_6F1C (JetScooter_ChkCanMove reaches the door
    list as a step does), which set $C64C-$C651 to Motavia and the
    leader's $E40E/$E40A -- the scooter's own place while riding
    (loc_4AE2) -- and ItemAction_Escapipe (ps2.asm:14544) loads those
    and leaves jet_scooter_flag alone: the party is back on the scooter
    at the dam's mouth (route dam_escapipe). Without one, leave_dam's
    walk."""
    if (s.state.get("level") or 0) < 0x10:
        return True
    pipe = find_item(s, "Escapipe")
    if pipe:
        shown = use_item(s, pipe[0], pipe[1]) or []
        s.ready()
        log("Escapipe:", ids(shown), (play.level_name(s.state.get("level")), s.state.get("x"),
                                      s.state.get("y"), s.state.get("riding")))
    if (s.state.get("level") or 0) >= 0x10:
        return leave_dam(s, dam, log)
    return s.state.get("level") == MOTAVIA


def recover(s, log):
    """Wiped out: CONTINUE the session's last save (Piata's, from
    piata_supply)."""
    slot = s.saves_dir / "slot1.json"
    s.careful = s.hurt = False
    if not slot.exists() or not continue_game(s, str(slot)):
        log("wiped out, and no save to continue")
        return False
    s.continued = True                  # (build 106) loot() looks again
    log("wiped out; continued the last save --", party_line(s))
    return True


# The grind before the dams (build 83). dam_trials
# (docs/dam_trials_build80.md) ranks Rudo+Amy+Anna first at every grind
# cost -- the party dam_party already took -- so it is not permuted.
# Fights per trip rose to 22.6 by 300 grind fights at 190 EXP and no
# further by 450. Piata's field pays about 467 a member a fight
# (formations $7D and $81-$84), so the 450 row's levels are about 180
# fights there; Amy and Anna, the two newest, gain most from them.
DAM_GRIND_TARGET = {"Rolf": 24, "Rudo": 24, "Amy": 20, "Anna": 22}
PIATA_FIELD = (94, 85)      # open three either side; up from 91,82 is Piata
DAM_TRIMATES = 12
DAM_ESCAPIPES = 2
WALKS_FIELD = ["walk left 3", "walk right 3"]


def piata_supply(s, log, save_it=True):
    """Piata between trips: the clone lab for anyone dead, the hospital,
    Trimates and Escapipes from building 7 (stock_item fills either page
    of a bag -- every bag holds eight already, so stock_mates' first
    page has no room), and a save at the Data Memory, so a wipe
    continues here and a Telepipe lands here ($C646)."""
    if not to_piata(s, log):
        return False
    b = town_buildings(PIATA)
    if any(m["dead"] for m in s.state.get("party") or []) \
            and BUILDING_CLONE_LAB in b:
        clone_lab(s, level=PIATA, door=b[BUILDING_CLONE_LAB][0])
    heal_here(s, PIATA)
    stock_item(s, PIATA, "Trimate", DAM_TRIMATES, log=log)
    stock_item(s, PIATA, "Escapipe", DAM_ESCAPIPES, log=log)
    if save_it and 2 in b:                  # BuildingID_DataMemory
        save(s, 1, level=PIATA, door=b[2][0])
        s.save_town = PIATA
    return s.state.get("level") == PIATA


@play.grinding
def stage_dam_grind(s, log, target=None, trips=80,
                    fights_per_trip=10):
    """From Piata: the field by its gate, ten fights a trip (fewer when
    someone needs the hospital), the hospital between, until each member
    is at dam_trials' 450-row level. Then Trimates, Escapipes, a save.
    (build 118) `target` None: DAM_GRIND_TARGET as it stands when called
    (squeak() lowers it), not as it stood when this was defined."""
    if target is None:
        target = DAM_GRIND_TARGET
    if not piata_supply(s, log):
        return False
    wipes = 0
    for trip in range(trips):
        if s.state.get("screen") in ("title", "sega", "gameover"):
            wipes += 1
            if wipes > 5 or not recover(s, log):
                return False
        if reached(s, target):
            break
        if s.state.get("level") == PIATA:
            heal_here(s, PIATA)
            if not town_exit(s):
                log("dam_grind: could not leave Piata")
                return False
        if not goto(s, {PIATA_FIELD}):
            log("dam_grind: could not reach the field:", s.why)
            if s.state.get("screen") == "level" and not enter_town(s, PIATA):
                return False
            continue
        before = len(s.fights)
        wander(s, WALKS_FIELD, fights_per_trip, mates=True, name="dam_grind")
        log(f"trip {trip}: {len(s.fights) - before} fights --", party_line(s))
        if s.state.get("screen") == "level" and not enter_town(s, PIATA):
            log("dam_grind: could not get back into Piata")
            return False
    ok = reached(s, target)
    log("dam_grind:", "reached" if ok else "short of", target, "--",
        party_line(s))
    if not ok and play.LEAN and s.state.get("screen") == "level":
        log("lean: going on to the dams")          # (build 98)
        ok = True
    return ok and piata_supply(s, log)


def stage_dams(s, log, tries=8):
    """The four dams by jet scooter from Piata, each trip supplied there
    first, with s.careful so goto hands back when someone is hurt and
    nothing is left to mend with: out by Escapipe, back to Piata, and in
    again (a door opened on an earlier trip stays open). The Army Eyes
    after the fourth panel; Gaira and Tyler's spaceship; Paseo."""
    s.save_town = PIATA
    for n, dam in enumerate(DAMS):
        for attempt in range(tries):
            if s.state.get("screen") in ("title", "sega", "gameover"):
                if not recover(s, log):
                    return False
            if not piata_supply(s, log):
                log(f"{dam[0]}: could not get back to Piata")
                return False
            s.careful = True
            try:
                r = open_dam(s, dam, log)
            finally:
                s.careful = False
            if r == "done":
                log(f"{dam[0]}: panel open on trip {attempt + 1} --",
                    party_line(s))
                break
            if s.state.get("screen") not in ("level", "battle"):
                continue
            if r != "hurt":
                return False
            log(f"{dam[0]}: hurt on trip {attempt + 1}, out and back --",
                party_line(s))
            if not out_of_dam(s, dam, log):
                return False
        else:
            return False
        if n < len(DAMS) - 1 and not out_of_dam(s, dam, log):
            return False
    if not army_eyes_to_gaira(s, log):
        log("the Army Eyes: not carried off --", s.state)
        return False
    return escape_gaira(s, log)


ROOF_DESK_MENU = 0x36           # WinID_TowerOptions2: ROOM LIBRARY ROOF OUTSIDE
DEZOLIS_SKURE = 3


def stage_roof(s, log):
    """From story_dams.json (Paseo's street after $1111): the Central
    Tower's desk (door 15,32), ROOF ($C73F, set by Gaira's $2F), $F01's
    three pages, YES, $F02 and the flight (Building_Roof, build 84) to
    DezolisSkure 60,79. The checkpoint is written where the party lands
    (the probe's save-here, a Visiphone save): a Dezolis Data Memory
    without a Magic Cap is $810 and no save."""
    def policy(s):
        m = s.state.get("menu")
        if m == ROOF_DESK_MENU:
            return 2                            # ROOF
        if m == WIN_YESNO2:
            return 0                            # YES
        return None
    shown = visit(s, PASEO, (15, 32), policy, limit=40) or []
    log("the tower's desk and roof:", play.scripts_text(shown))
    st = s.state
    landed = (st.get("level") == DEZOLIS_SKURE
              and (st.get("x"), st.get("y")) == (60, 79))
    log("landed:" if landed else "not landed:", play.level_name(st.get("level")),
        st.get("x"), st.get("y"), party_line(s))
    return landed and 0x0F02 in shown and 0x0F03 not in shown


SKURE_B2, SKURE_B1 = 0x01, 0x02
AUKBA, ZOSA, RYUON = 0x0A, 0x0B, 0x0C


def wear(s, who, item, log):
    """`who` (a name) puts on the unworn `item` from his own bag, as a
    person would with EQP. True when he wears it after."""
    party = s.state.get("party") or []
    for m, member in enumerate(party):
        if member["name"] != who:
            continue
        bag = bags(s)[m]
        slots = [i for i, (n, worn) in enumerate(bag) if n == item and not worn]
        if not slots:
            return worn_head(s, who) == item
        shown = equip(s, m, slots[0]) or []
        log(f"{who} puts on {item}:", play.scripts_text(shown))
        return worn_head(s, who) == item
    return False


def cap_for_shops(s, level, log, prefer="MogicCap"):
    """(build 168f) Rolf alive and in a cap before a Dezolis town's errands:
    its shops, hospital and Data Memory answer only to a live Rolf in a
    Magic or Mogic Cap (CheckPlanetAndCaps, ps2.asm:19188). Dead, the
    town's clone lab first (play.rolf_alive_for_shops, which gear_up only
    reached after the cap was tried); then the cap `prefer`red, or the
    other, fetched to him from whoever carries it and put on. Until 168f
    the callers put on only a cap already in Rolf's own bag: a Magic Cap
    handed to him while dead stayed with Rudo, and the retranslation's seed
    10 ground Skure's retries with Zosa refusing him -- no gear, no
    hospital. The cap he wears after, or None."""
    play.rolf_alive_for_shops(s, level)
    other = "MagicCap" if prefer == "MogicCap" else "MogicCap"
    for cap in (prefer, other):
        if worn_head(s, "Rolf") == cap:
            return cap
        if fetch_to(s, "Rolf", cap, log) and wear(s, "Rolf", cap, log):
            return cap
    return None


def caps_carried(s):
    """(build 168f) The Magic and Mogic Caps anywhere in the party's bags,
    worn or not, a dead member's too (find_item sees neither)."""
    return {n for bag in bags(s) for n, _ in bag
            if n in ("MagicCap", "MogicCap")}


def fetch_to(s, who, item, log):
    """`item` into `who`'s bag from whoever carries it (GIVE)."""
    party = s.state.get("party") or []
    target = next((m for m, p in enumerate(party) if p["name"] == who), None)
    found = find_item(s, item)
    if target is None or found is None:
        return False
    holder, slot = found
    if holder == target:
        return True
    shown = play.give_item(s, holder, slot, target) or []
    log(f"{item} from {party[holder]['name']} to {who}:",
        play.scripts_text(shown))
    return any(n == item for n, _ in bags(s)[target])


def talk_round(s, indices, log):
    """Talk to each dialogue number on this level; the scripts shown,
    logged by who said them (play.person_name)."""
    out = {}
    level = s.state.get("level")        # the round's, wiped out or not
    for index in indices:
        shown = talk_to(s, index, log=log) or []
        out[index] = shown
        who = play.person_name(level, index)
        log(f"  {who}:", play.scripts_text(shown) or "not reached")
    return out


def stage_skure(s, log):
    """From story_roof.json (DezolisSkure, where the flight lands):
    Skure's two floors under the spaceport and their chests -- the Magic
    Caps (chests 4 on B2 and 10 on B1), the Mogic Cap (2 on B2), meseta
    -- then the cap on Rolf: the Dezolians, the musk cats, the shops,
    the hospital and Data Memory answer only to a live Rolf in a Magic
    or Mogic Cap (loc_E584, CheckPlanetAndCaps). The cats of both floors
    talked to; then Zosa: the shelves bought from in the Mogic Cap (a
    Magic Cap doubles the prices, CheckIfDoublePrice, and a sale fetches
    10), the Magic Cap back on, every Zosa Dezolian talked to. The
    checkpoint is Zosa's Data Memory."""
    play.PIN_HEAD.add("Rolf")
    b1 = loot(s, SKURE_B1, log, town=ZOSA)
    fetch_to(s, "Rolf", "MagicCap", log)
    capped = wear(s, "Rolf", "MagicCap", log)
    log("Rolf in the Magic Cap:" if capped else "no Magic Cap on Rolf:",
        party_line(s))
    log("B1's cats:")
    talk_round(s, (0x7D, 0x7E, 0x7F), log)

    # B2 killed the party from story_roof's supplies (build 85's first
    # run: seven fights ending with someone dead, then the title). Zosa
    # first, in the cap: gear at double prices (the money is there),
    # mates and Escapipes, the hospital, a save for the pipes to land
    # at, and a grind until the leader is DEZOLIS_GRIND.
    if not zosa_visit(s, log, shop_cap="MagicCap"):
        return False
    grind_and_gear(s, ZOSA, DEZOLIS_GRIND, log=log)
    zosa_visit(s, log, shop_cap="MagicCap")
    log("after the grind:", party_line(s))

    b2 = loot(s, SKURE_B2, log, town=ZOSA)
    log("B2's cats:")
    talk_round(s, (0x7B, 0x7C), log)
    fetch_to(s, "Rolf", "MogicCap", log)
    fetch_to(s, "Rolf", "MagicCap", log)
    log("chests:", len(b1) + len(b2), "bags:", bags(s))

    if not zosa_visit(s, log, shop_cap="MogicCap"):
        return False
    capped = wear(s, "Rolf", "MagicCap", log)
    log("Zosa's Dezolians:")
    said = talk_round(s, range(0x5D, 0x65), log)
    heal_here(s, ZOSA)
    sense = sum(1 for v in said.values() if v and 0x1651 not in v)
    log(f"made sense: {sense} of {len(said)}", party_line(s))
    return capped and s.state.get("level") == ZOSA


DEZOLIS_GRIND = 30


def zosa_visit(s, log, shop_cap):
    """Into Zosa; Rolf in `shop_cap` for the shelves (a Magic Cap doubles
    the prices, CheckIfDoublePrice), the Magic Cap back on after; mates,
    Trimates if sold, Escapipes; the hospital; Data Memory."""
    if not enter_town(s, ZOSA):
        log("could not reach Zosa:", play.level_name(s.state.get("level")),
            s.state.get("x"), s.state.get("y"))
        return False
    shop = cap_for_shops(s, ZOSA, log, prefer=shop_cap) == shop_cap
    log(f"shopping in the {shop_cap}" if shop else "shopping in the cap on")
    got = gear_up(s, ZOSA, log=log)
    log("Zosa gear:", got)
    stock_mates(s, ZOSA, want=8, log=log, trimates=True)
    stock_item(s, ZOSA, "Escapipe", 3, log=log)
    wear(s, "Rolf", "MagicCap", log)
    heal_here(s, ZOSA)
    b = town_buildings(ZOSA)
    if 2 in b:
        save(s, 1, level=ZOSA, door=b[2][0])
    log("Zosa:", party_line(s))
    return True


# ---------------------------------------------------------------------
# Dezolis after Skure (build 88): Aukba and Ryuon, the Esper mansion,
# the Prism. Build 86's stage (story_dezolis_towns.json) never reached
# The owner; this is played again from story_skure.json.
# ---------------------------------------------------------------------

ESPER_F1, ESPER_B1 = 0x11, 0x10


def people_here(s):
    """Dialogue numbers of the people (ObjType_People, type 1) on this
    level, in order."""
    return sorted({o["index"] for o in play.objects(s) if o["type"] == 1})


def dezolis_town_visit(s, town, log, talk=True):
    """A Dezolis town as the standing rules have it: in, Rolf in the
    Mogic Cap for the shelves (a Magic Cap doubles prices,
    CheckIfDoublePrice; a sale fetches 10), mates, Trimates, Escapipes,
    the hospital, every Dezolian talked to in the Mogic Cap (its lines
    are each one's own; the Magic Cap's three stock ones), then the
    Magic Cap back on and Data Memory. The scripts said, by dialogue."""
    if not enter_town(s, town):
        log(f"could not reach {play.level_name(town)}:", play.level_name(s.state.get("level")),
            s.state.get("x"), s.state.get("y"))
        return None
    cap = cap_for_shops(s, town, log)
    which = {"MogicCap": "Mogic", "MagicCap": "Magic"}.get(cap, "wrong")
    log(f"{play.level_name(town)}: Rolf in the {which} Cap --",
        party_line(s))
    got = gear_up(s, town, log=log)
    log(f"{play.level_name(town)} gear:", got)
    stock_mates(s, town, want=8, log=log, trimates=True)
    stock_item(s, town, "Escapipe", 3, log=log)
    heal_here(s, town)
    said = {}
    if talk:
        if s.state.get("screen") == "building":
            leave_building(s)
        s.ready()
        said = talk_round(s, people_here(s), log)
        sense = sum(1 for v in said.values() if v and 0x1651 not in v)
        log(f"{play.level_name(town)}: made sense {sense} of {len(said)}")
    fetch_to(s, "Rolf", "MagicCap", log)
    wear(s, "Rolf", "MagicCap", log)
    heal_here(s, town)
    b = town_buildings(town)
    if 2 in b:
        save(s, 1, level=town, door=b[2][0])
    return said


def stage_dezolis_towns(s, log):
    """From story_skure.json (Zosa): Aukba, then Ryuon, each as
    dezolis_town_visit. The checkpoint is Ryuon's Data Memory."""
    play.PIN_HEAD.add("Rolf")
    for town in (AUKBA, RYUON):
        if dezolis_town_visit(s, town, log) is None:
            return False
    return s.state.get("level") == RYUON


def revive_party(s, town, log):
    """(build 98) Nobody left dead: Rever or a Moon Dew where the party
    stands (play.revive), else `town`'s clone lab. True when all live.
    A person does this before the Esper mansion's door Espers, who keep
    the doorway shut while Rolf is dead (route esper_door_rolf_dead), and
    before saving."""
    def dead():
        return any(m["dead"] for m in s.state.get("party") or [])
    if not dead():
        return True
    back = play.revive(s)
    if dead() and enter_town(s, town):
        b = play.town_buildings(town)
        if play.BUILDING_CLONE_LAB in b:
            clone_lab(s, level=town, door=b[play.BUILDING_CLONE_LAB][0])
        heal_here(s, town)
    log(f"revive_party: {back} on the spot, then {play.level_name(town)} --",
        party_line(s))
    return not dead()


def stage_esper(s, log):
    """The Esper mansion (ported in build 87): F1's door Espers ($88:
    $1687 and $C742 with Rolf alive and standing), B1's Esper ($8F) twice
    -- $1697 or $1698, $C743 to 1 then 2 and event_flags $D00 -- the
    scene, Lutz ($190B .. $190E), F1 16,16 with $C743 3 and $C78E 0, and
    F1's chest: the Prism (TreasureChestContentArray 14). Then out to the
    nearest town's Data Memory."""
    play.PIN_HEAD.add("Rolf")
    for _ in range(3):
        if not travel(s, ESPER_F1, play.town_arrivals(ESPER_F1), log=log) \
                and s.state.get("level") != ESPER_F1:
            log("could not reach the Esper mansion:", play.level_name(s.state.get("level")),
                s.state.get("x"), s.state.get("y"))
            return False
        party = s.state.get("party") or []
        if party and not party[0]["dead"]:
            break
        # (build 98) Rolf dead at the door: the door Espers stay put.
        revive_party(s, nearest_town(ESPER_F1), log)
    log("Esper mansion F1:", s.state.get("x"), s.state.get("y"))
    door = talk_to(s, 0x88, want=[0x1687, 0x1688], log=log) or []
    log("door Esper:", play.scripts_text(door))
    s.send("wait ticks 60")
    if not travel(s, ESPER_B1, {(16, 14)}, log=log) \
            and s.state.get("level") != ESPER_B1:
        log("could not reach B1:", play.level_name(s.state.get("level")),
            s.state.get("x"), s.state.get("y"))
        return False
    for n in range(2):
        shown = talk_to(s, 0x8F, log=log) or []
        log(f"B1's Esper, talk {n + 1}:",
            play.scripts_text(shown))
        if 0x1698 in shown:
            # loc_E07A's second idle: $C743 = 2 and event_flags $D00,
            # which holds the leader while objects $23-$27 play the
            # scene (about $2C0 frames) and $27 opens the building. The
            # pad is the scene's, not the party's: wait for the screen.
            s.send("limit 4000", "wait screen building")
            log("the scene:", s.state.get("screen"), "at tick",
                s.state.get("tick"))
            s.ready(limit=200)
            s.send("limit 3600")
        if s.state.get("level") == ESPER_F1:
            break
    log("after Lutz:", play.level_name(s.state.get("level")), s.state.get("x"),
        s.state.get("y"))
    if s.state.get("level") != ESPER_F1:
        log("no scene: still on", play.level_name(s.state.get("level")))
        return False
    # The chest is LoadSpritesInLevel's tail's (slot $20E while $C78E
    # is 0, SpriteData_EspMansion), not in the level's chest list:
    # search it by its cell, 13,15.
    shown = search(s, ESPER_F1, (13, 15), log=log) or []
    log("F1's chest:", play.scripts_text(shown))
    prism = find_item(s, "Prism") is not None
    log("the Prism:", "in the bags" if prism else "not found", "--",
        bags(s))
    if not prism:
        return False
    for town in (AUKBA, ZOSA, RYUON):
        if enter_town(s, town):
            break
    if s.state.get("level") is None:
        # (build 168f) Wiped out on the way: no level, and heal_here's
        # doors lookup was a harness error (KeyError None) -- four tries
        # in three of the 168f grind's 36 games. A try that did not
        # finish, as any wipe.
        log("the Prism: wiped out on the way --", s.state.get("screen"))
        return False
    fetch_to(s, "Rolf", "MagicCap", log)
    wear(s, "Rolf", "MagicCap", log)
    heal_here(s, s.state.get("level"))
    # (build 98) Not saved with anyone dead: the playthrough's first run
    # walked on from here with Rolf and Anna dead and was wiped out.
    revive_party(s, s.state.get("level"), log)
    return prism


# The Prism's dungeons: their entrances on DezolisSkure open only to a
# party holding the Prism (route dezolis_prism). Floors in the order the
# chests are reached; the Nei item each holds (TreasureChestContentArray,
# flag_names.json); a grind target for the leader first.
PRISM_DUNGEONS = {
    "menobe": ((0x48, 0x49, 0x4A, 0x4B), ("NeiCrown", "Neimet"), 32),
    "ikuto": ((0x52, 0x51, 0x50, 0x4F, 0x4E, 0x4D), ("NeiSlasher", "NeiShot"),
              34),
    "guaron": ((0x53,) + tuple(range(0x54, 0x63)), ("NeiCape", "NeiArmor"),
               36),
    "naval": ((0x43, 0x44), ("NeiShield", "NeiEmel"), 36),
}


PRISM_BUDGET = 170          # seconds before a floor is started (build 89);
                            # None: no limit (playthrough.py, build 99)


def nearest_town(level):
    """The Dezolis town the fewest level changes from the dungeon's
    ground floor (plan(), build 89). Build 88 went by straight-line
    distance between the door records, which put Ikuto with Aukba:
    DezolisSkure is several pieces, and Ikuto's door is on Zosa's --
    Aukba to Ikuto is through Skure B1 and B2."""
    arrive = play.town_arrivals(level) if hasattr(play, "town_arrivals") \
        else None
    best = None
    for town in (ZOSA, AUKBA, RYUON):
        goals = play.town_arrivals(town)
        # (build 170) The least cell, not the set's first (every caller's
        # set has one cell).
        start = min(arrive) if arrive else None
        if start is None:
            continue
        legs = play.plan(level, start, town, goals)
        if legs is not None and (best is None or len(legs) < best[0]):
            best = (len(legs), town)
    return best[1] if best else ZOSA


def worn_out(s):
    """(build 89) Time to go back to town from a Prism dungeon: anyone
    dead, the party under half its HP, or the members with TP (the
    menders and the technique users) under a third of it between them."""
    party = s.state.get("party") or []
    if not party or any(m["dead"] for m in party):
        return bool(party)
    hp = sum(m["hp"] for m in party) / max(1, sum(m["max_hp"] for m in party))
    tp_max = sum(m["max_tp"] for m in party)
    tp = sum(m["tp"] for m in party) / tp_max if tp_max else 1.0
    return hp < 0.5 or tp < 0.34


def prism_dungeon(s, name, log):
    """One of the Prism's dungeons, as the standing rules play it: the
    nearest town for gear, mates and a save; a grind by it until the
    leader reaches the target (grind_and_gear: money and EXP, the best
    gear bought as often as it can be); every floor's chests (loot:
    back to the town when hurt with nothing to mend); the town again."""
    levels, wanted, target = PRISM_DUNGEONS[name]
    play.PIN_HEAD.add("Rolf")
    if find_item(s, "Prism") is not None:
        play.HELD.add("Prism")
    town = nearest_town(levels[0])
    log(f"{name}: floors {' '.join(play.level_name(l) for l in levels)}, "
        f"town {play.level_name(town)}, grind to L{target}")
    t0 = time.time()
    s.careful = True                    # goto gives up hurt with nothing left
    if dezolis_town_visit(s, town, log, talk=False) is None:
        return False
    # (build 89) The grind the docstring promised and build 88 left out:
    # from open ground by the town's gate (field_spot), as dezolis_grind.
    gate = next(((e["x"], e["y"]) for e in play.level_doors(0x03).values()
                 if e.get("to_level") == town), None)
    spot = play.field_spot(0x03, gate) if gate else None
    grind_and_gear(s, town, target, fights_per_trip=8, trips=12, log=log,
                   spot=spot)
    dezolis_town_visit(s, town, log, talk=False)
    log(f"{name} after the grind:", party_line(s))
    found = {}
    for lv in levels:
        if PRISM_BUDGET is not None and time.time() - t0 > PRISM_BUDGET:
            log(f"{name}: out of time before {play.level_name(lv)}; "
                f"rerun with --from {name} --failed")
            return False
        got = loot(s, lv, log, town=town, tired=worn_out, wanted=wanted)
        found[lv] = got
    have = [w for w in wanted if play.carried(s, w)]   # worn counts (build 89)
    log(f"{name}: Nei items {have} of {list(wanted)} --", party_line(s))
    s.careful = False                   # the walk home is the retreat
    # (build 89) Back in town or not done: Guaron's first run had both
    # Nei items and no way out, reported success, and its checkpoint --
    # taken on GuaronF12 -- was no save at all.
    if dezolis_town_visit(s, town, log, talk=False) is None:
        return False
    return len(have) == len(wanted)


# Shir's thefts before the Prism's dungeons (build 89). ProcessStealItem
# runs only as she walks out of a store or the Central Tower's room
# (play.py, "Shir's thefts"), and only Paseo's are known to the harness:
# home by Skure's roof door, then Paseo. The room's list is all
# Visiphones -- a save anywhere, which Dezolis's overworld has none of --
# and wants her at level 10; the item store's has the Star Mist and the
# Moon Dew, one in eight of its thefts each. What she takes is in her
# own bag, so it is given to Rolf before she leaves the party again; so
# is the Prism before anyone does (LoadLevelData's tail asks the party,
# not the roster: whoever stays at Rolf's house takes it with them).
SHIR_WANTS = {"Visiphone": 1, "StarMist": 1, "MoonDew": 1}
# (build 99) Exits, not seconds: a wall clock made a seed replay only until
# it ran out. Build 98's five seeds took 10-295 exits here.
SHIR_EXITS = 400
MOTAVIA_KEEP = ("Prism", "MogicCap")


def keep_on_rolf(s, items, log):
    """Each of `items` carried by someone else into Rolf's bag."""
    for item in items:
        if play.carried(s, item) and not any(
                n == item for n, _ in (play.bags(s) or [[]])[0]):
            fetch_to(s, "Rolf", item, log)


def stage_motavia(s, log):
    """From Zosa (story_dezolis_grind.json): the Prism and the Mogic Cap to
    Rolf, home to Paseo by Skure's roof door (the flight, Paseo 31,16),
    Shir into the party (shir_join: Rolf's house, REORGANIZE), and raised
    to 10 beside Paseo for the Central Tower's room. Checkpoint Paseo's
    Data Memory."""
    play.PIN_HEAD.add("Rolf")
    if find_item(s, "Prism") is not None:
        play.HELD.add("Prism")
    keep_on_rolf(s, MOTAVIA_KEEP, log)
    if not enter_town(s, PASEO):
        log("motavia: could not get home to Paseo:", s.state)
        return False
    log("motavia: in Paseo --", party_line(s))
    if not play.shir_join(s, log=log):
        return False
    lv = next((m["level"] for m in s.state.get("party") or []
               if m["name"] == "Shir"), 0)
    if lv < 10 and not play.raise_member(s, "Shir", 10, log=log):
        log("motavia: Shir not raised to 10 --", party_line(s))
        return False
    enter_town(s, PASEO)
    play.heal(s)
    log("motavia:", party_line(s))
    return True


STOLEN = ("Visiphone", "StarMist", "MoonDew", "Hidapipe", "Trimate")


# (build 99) What makes room for a Moon Dew when every bag is full: the
# commonest things a bag holds, handed to Shir in its place.
SWAP_OUT = ("Trimate", "Hidapipe", "Dimate", "Monomate", "Antidote",
            "Escapipe", "Telepipe")


def empty_shir(s, log, first=()):
    """What Shir stole (STOLEN, not what she wears) out of her bag to
    whoever has room, Rolf first: when she leaves the party her bag
    stays with her at Rolf's house. (build 99) The names in `first` go
    first, and one of them finds room when there is none -- a SWAP_OUT
    item goes from a full bag to hers -- so the Moon Dews are the last
    thing left behind, not the first (seed 6's restock: every bag full of
    Trimates and Hidapipes, the dew stayed with her)."""
    for _ in range(16):
        names = [m["name"] for m in s.state.get("party") or []]
        if "Shir" not in names:
            return
        h = names.index("Shir")
        b = play.bags(s)
        order = sorted((i for i, (n, w) in enumerate(b[h])
                        if not w and n in STOLEN),
                       key=lambda i: (b[h][i][0] not in first, i))
        slot = order[0] if order else None
        if slot is None:
            return
        tgt = next((t for t in range(len(names))
                    if t != h and len(b[t]) < 16), None)
        if tgt is None and b[h][slot][0] in first:
            # Room for it: a SWAP_OUT item to Shir while she has room, else
            # one tossed from the fullest other bag (seed 7's rerun: her bag
            # full again after the theft, the dew left with her).
            swap = next(((t, i) for t in range(len(names)) if t != h
                         for i in range(len(b[t]) - 1, -1, -1)
                         if not b[t][i][1] and b[t][i][0] in SWAP_OUT), None)
            if swap is not None and len(b[h]) < 16:
                shown = play.give_item(s, swap[0], swap[1], h) or []
                log(f"{b[swap[0]][swap[1]][0]} from {names[swap[0]]} to Shir, "
                    f"for room:", play.scripts_text(shown))
                continue
            if swap is not None and play.make_room(s, log=log, who=swap[0]):
                continue
        if tgt is None:
            log("shir: nobody has room for", b[h][slot][0])
            return
        shown = play.give_item(s, h, slot, tgt) or []
        log(f"{b[h][slot][0]} from Shir to {names[tgt]}:",
            play.scripts_text(shown))


def stage_shir(s, log):
    """From story_motavia.json (or its own failed checkpoint, --failed):
    Shir in and out of Paseo's item store and the Central Tower's room
    until the party carries SHIR_WANTS, fetched from Rolf's house after
    each theft (play.shir_thefts), up to SHIR_EXITS store exits. Done,
    the stolen things go to Rolf, the party is chosen for Menobe
    (pick_party, ahead= its floors) and the tower's roof flies it to
    DezolisSkure 60,79."""
    play.PIN_HEAD.add("Rolf")
    if find_item(s, "Prism") is not None:
        play.HELD.add("Prism")
    tally = play.shir_thefts(s, SHIR_WANTS, log=log, max_exits=SHIR_EXITS,
                             on_full=lambda: empty_shir(s, log))
    have = {i: play.carried(s, i) for i in SHIR_WANTS}
    log("shir: carried", have, "--", party_line(s))
    if any(have[i] < n for i, n in SHIR_WANTS.items()):
        if not play.LEAN:
            log("shir: not all stolen yet; rerun with --from shir --failed")
            return False
        # (build 98) The thefts are a bonus, not the story: on without them.
        log("lean: not all stolen, going on with", have)
    empty_shir(s, log)
    keep_on_rolf(s, MOTAVIA_KEEP, log)
    # Nei is in the roster after Climatrol but never in the party again:
    # pick_party's default core has her, and build 89's first run came out
    # Rolf, Rudo and Kain -- three. catch_up=False: the party that fights
    # best on the mix (build 88's trials: Rolf, Rudo, Amy, Anna on either);
    # catch_up's choice, a low member to raise, is logged beside it.
    ahead = list(PRISM_DUNGEONS["menobe"][0])
    members = play.pickable(s)
    alt, why = play.choose_party(members, catch_up=True,
                                 mix=play.dungeon_mix(ahead), core=("Rolf",))
    log("shir: catch_up would take", alt, f"({why})")
    got = pick_party(s, log=log, ahead=ahead, catch_up=False, core=("Rolf",))
    keep_on_rolf(s, MOTAVIA_KEEP, log)
    log("shir: party for Menobe", got, "--", party_line(s))
    ok = stage_roof(s, log)
    if not ok:
        return False
    return find_item(s, "Prism") is not None


DEZOLIS_GRIND_2 = 33
ZOSA_GATE = (119, 24)


@play.grinding
def stage_dezolis_grind(s, log, trips=25):
    """Zosa's field before the Prism's dungeons, the standing grind --
    money and EXP, the best gear bought each time through town -- from
    open ground near the gate (field_spot): walked from the gate's own
    cell the trips fought nothing (the first Menobe runs: 0 fights a
    trip at Aukba and at Zosa). As far as `trips` goes; the next stage
    starts from wherever it got."""
    play.PIN_HEAD.add("Rolf")
    spot = play.field_spot(0x03, ZOSA_GATE)
    log("grind spot on DezolisSkure:", spot)
    if dezolis_town_visit(s, ZOSA, log, talk=False) is None:
        return False
    ok = grind_and_gear(s, ZOSA, DEZOLIS_GRIND_2, fights_per_trip=8,
                        trips=trips, log=log, spot=spot)
    dezolis_town_visit(s, ZOSA, log, talk=False)
    log("reached" if ok else "short of", f"L{DEZOLIS_GRIND_2}:",
        party_line(s))
    return s.state.get("level") == ZOSA


def stage_menobe(s, log):
    return prism_dungeon(s, "menobe", log)


def stage_ikuto(s, log):
    return prism_dungeon(s, "ikuto", log)


def stage_guaron(s, log):
    return prism_dungeon(s, "guaron", log)


def stage_naval(s, log):
    return prism_dungeon(s, "naval", log)


# ---------------------------------------------------------------------
# After the Prism's dungeons (build 90)
# ---------------------------------------------------------------------
#
# Building_EsperMansion (ps2.asm 18940), entered by A on F1's $33 (event
# $34, loc_E0AA) once $C743 is 3:
#   $C744 clear: $168F, or -- all eight of NeiEquipmentArray in the
#     party's bags, worn or not (CheckItemExistInventory) -- $1690-$1694,
#     $C744 = 1 and $C78F = 0, which puts the Nei Sword in the chest slot
#     LoadSpritesInLevel's tail places at 13,15 ($20F).
#   $C744 set, no Nei Sword carried: $1913 ("Remove the Nei-sword").
#   $C744 set, the Nei Sword carried: $1911 and YES/NO (WinID_YesNo2).
#     YES: $1695, then loc_CFC0 -- NoahGroundF, x $200, y $6B0.
#     NO: $1912, then loc_B9D8 (the hospital's HEAL: HP and TP full, no
#     charge) -- the mansion is a free inn from here on.
# NeiSword_ItemSelected (14868): USE on level $63 or later takes the party
# to EsperMansionF1 16,16; anywhere else it is $27.
LUTZ_STAND = (17, 16)               # below $33 at 17,15 (esper_lutz_nei)
NEI_SWORD_CHEST = (13, 15)
NOAH_G = 0x63


def lutz(s, answer, log, limit=120):
    """A on F1's $33 from 17,16 and the building played to the end: C
    through the text, `answer` (0 YES, 1 NO) at $1911's YES/NO. Returns
    the scripts shown. YES ends on NoahGroundF."""
    if s.state.get("level") != ESPER_F1 or \
            (s.state.get("x"), s.state.get("y")) != LUTZ_STAND:
        if not travel(s, ESPER_F1, {LUTZ_STAND}, log=log):
            log("lutz: could not stand below him:", play.level_name(s.state.get("level")),
                s.state.get("x"), s.state.get("y"))
            return None
    s.ready()
    before = len(s.shown)
    s.send("press up", "wait idle", "press a", "wait idle")
    inside = False
    for _ in range(limit):
        st = s.state
        if st.get("screen") == "building":
            inside = True
        if st.get("screen") == "level" and inside \
                and st.get("kind") == "waiting" and st.get("wait") == "control":
            break
        if st.get("screen") == "level" and not inside \
                and st.get("kind") == "waiting" and st.get("wait") == "control":
            log("lutz: A found nobody at", st.get("x"), st.get("y"))
            break
        if st.get("kind") != "waiting":
            s.send("wait idle")
        elif st.get("wait") == "menu" and st.get("menu") == WIN_YESNO2:
            play.menu_pick(s, answer)
            s.send("wait idle")
        else:
            s.send("press c", "wait idle")
    return s.shown[before:]


KEEP_ON_ROLF = {"Prism", "Visiphone", "MagicCap", "MogicCap", "NeiSword",
                "SmallKey"}


def make_room(s, who, log, keep=KEEP_ON_ROLF):
    """(build 90) One free slot in `who`'s bag (16 is full): an unworn
    item not in `keep` -- a spare of a kept one counts as spare -- given to
    whoever carries least. The first run's Nei Sword stayed in Anna's bag:
    Rolf's held sixteen, among them two LaserKnives Shir had stolen."""
    party = s.state.get("party") or []
    m = next((i for i, p in enumerate(party) if p["name"] == who), None)
    if m is None:
        return False
    b = bags(s)
    if len(b[m]) < 16:
        return True
    seen = set()
    spare = None
    for i, (n, worn) in enumerate(b[m]):
        if worn:
            continue
        if n in keep and n not in seen:
            seen.add(n)
            continue
        spare = i
        break
    to = min((t for t in range(len(party)) if t != m and len(b[t]) < 16),
             key=lambda t: len(b[t]), default=None)
    if spare is None or to is None:
        log(f"make_room: nothing to move out of {who}'s bag")
        return False
    shown = play.give_item(s, m, spare, to) or []
    log(f"{b[m][spare][0]} from {who} to {party[to]['name']}:",
        play.scripts_text(shown))
    return len(bags(s)[m]) < 16


def wear_hand(s, who, item, log):
    """`who` puts on a one-hand `item` from his bag into the empty hand
    if one is empty (gear_up's rule), else over the first."""
    party = s.state.get("party") or []
    m = next((i for i, p in enumerate(party) if p["name"] == who), None)
    if m is None:
        return False
    bag = bags(s)[m]
    slots = [i for i, (n, w) in enumerate(bag) if n == item and not w]
    if not slots:
        return any(n == item and w for n, w in bag)
    hands = [n for n, w in bag if w and play.kind_of(n) in
             (play.KIND_HAND, play.KIND_BOTH)]
    hand = 1 if len(hands) == 1 and play.kind_of(item) == play.KIND_HAND \
        else 0
    shown = equip(s, m, slots[0], hand) or []
    log(f"{who} puts on {item} (hand {hand}):",
        play.scripts_text(shown))
    return any(n == item and w for n, w in bags(s)[m])


def lutz_heal(s, log):
    """NO to Lutz with the Nei Sword carried: $1912 and a free HEAL."""
    shown = lutz(s, 1, log) or []
    log("Lutz, NO:", play.scripts_text(shown), "--",
        party_line(s))
    return 0x1912 in shown


def stage_nei_sword(s, log):
    """From Ryuon (story_naval.json), the eight Nei items in the bags:
    the Esper mansion, Lutz ($1690-$1694 and $C744), F1's chest (the Nei
    Sword, Rolf's only: equip mask 1), Rolf in it, Lutz again and NO
    ($1912 and the HEAL), then the nearest town as the standing rules
    have it and its Data Memory in the Magic Cap."""
    play.PIN_HEAD.add("Rolf")
    if not travel(s, ESPER_F1, play.town_arrivals(ESPER_F1), log=log) \
            and s.state.get("level") != ESPER_F1:
        log("nei_sword: could not reach the Esper mansion:",
            play.level_name(s.state.get("level")), s.state.get("x"), s.state.get("y"))
        return False
    shown = lutz(s, 1, log) or []
    log("Lutz:", play.scripts_text(shown))
    if 0x1690 not in shown and 0x1911 not in shown and 0x1913 not in shown:
        log("nei_sword: not all eight --", bags(s))
        return False
    if not play.carried(s, "NeiSword"):
        got = search(s, ESPER_F1, NEI_SWORD_CHEST, log=log) or []
        log("F1's chest:", play.scripts_text(got))
    if not play.carried(s, "NeiSword"):
        log("nei_sword: no Nei Sword --", bags(s))
        return False
    make_room(s, "Rolf", log)
    fetch_to(s, "Rolf", "NeiSword", log)
    if not wear_hand(s, "Rolf", "NeiSword", log):
        log("nei_sword: Rolf is not wearing it --", bags(s)[0])
        return False
    log("Rolf wears:", [n for n, w in bags(s)[0] if w])
    lutz_heal(s, log)
    town = nearest_town(ESPER_F1)
    if dezolis_town_visit(s, town, log, talk=False) is None:
        return False
    return play.carried(s, "NeiSword") > 0


# ---------------------------------------------------------------------
# Kain, and the grind before Noah (build 91)
# ---------------------------------------------------------------------
#
# docs/noah_trials_build90.md: on Noah, Rudo+Anna+Kain behind Rolf is
# behind Rudo+Amy+Anna up to ~80 grind fights and ahead from ~160 (Kain
# L28), and Kain's parties manage ~3.4 fights a trip at L10 -- so he is
# raised first somewhere cheaper, then the party grinds on NoahGroundF
# from Lutz's YES: out by the Nei Sword when worn out, NO at Lutz for the
# free HEAL (loc_B9D8 leaves the dead dead: those go to a clone lab).
# The party can only change at Rolf's house in Paseo
# (RolfHouseOption_Reorganize), so the trip home is also Shir's: a
# bounded round of thefts for the Moon Dews and Star Mists Noah wants.

KAIN_PARTY = ("Rolf", "Rudo", "Anna", "Kain")
KAIN_TARGET = 20
# (build 119) no_kain(): Noah and the bosses with the party the Nei Sword
# came with -- no Kain raised, no swap before the bosses (The owner's
# question, the Noah trial's other leader).
NO_KAIN = False
NO_KAIN_PARTY = ("Rolf", "Rudo", "Amy", "Anna")
NOAH_THEFTS = {"MoonDew": 2, "StarMist": 4}
NOAH_THEFT_EXITS = 300              # (build 99) store exits; 116-254 in build 98
CONSUMABLES = ("Monomate", "Dimate", "Trimate", "StarMist", "MoonDew",
               "Antidote", "Escapipe", "Telepipe", "Hidapipe", "Visiphone")


def keep_from(s, leaving, joining, log):
    """Before `leaving` stays at Rolf's house -- a bag stays with its
    owner -- their unworn consumables, and every unworn piece no store
    sells that one of `joining` can wear (Amy's Neimet is Kain's), go to
    whoever stays, the emptiest bag first. Returns what stayed behind."""
    ids = [play.CHAR_ID[j] for j in joining]
    common = play.sold_anywhere()
    tried = set()
    for _ in range(24):
        party = s.state.get("party") or []
        names = [p["name"] for p in party]
        if leaving not in names:
            return []
        h = names.index(leaving)
        b = bags(s)
        slot = None
        for i in range(len(b[h]) - 1, -1, -1):
            n, worn = b[h][i]
            if worn or (i, n) in tried:
                continue
            unique = n not in common and any(play.wearable(n, c) for c in ids)
            if n in CONSUMABLES or unique:
                slot = i
                break
        if slot is None:
            break
        tried.add((slot, b[h][slot][0]))
        to = min((t for t in range(len(party)) if t != h
                  and party[t]["name"] != "Shir" and len(b[t]) < 16),
                 key=lambda t: len(b[t]), default=None)
        if to is None:
            log(f"keep_from: no room for {b[h][slot][0]}")
            break
        shown = play.give_item(s, h, slot, to) or []
        log(f"{b[h][slot][0]} from {leaving} to {party[to]['name']}:",
            play.scripts_text(shown))
    b = bags(s)
    names = [p["name"] for p in s.state.get("party") or []]
    if leaving not in names:
        return []
    return [n for n, w in b[names.index(leaving)] if not w
            and n in CONSUMABLES]


def in_party(s, name):
    return any(m["name"] == name for m in s.state.get("party") or [])


def stage_kain(s, log):
    """From story_nei_sword.json (Aukba): home to Paseo by the roof door;
    whoever KAIN_PARTY leaves out hands over the consumables and anything
    unique Kain or Shir could wear (keep_from); Shir in for a bounded round
    of thefts (NOAH_THEFTS, empty_shir after); then Rolf, Rudo, Anna and
    Kain, Kain geared in Paseo, the tower's roof back to DezolisSkure and
    Zosa's shelves in the Mogic Cap. Checkpoint Zosa's Data Memory."""
    play.PIN_HEAD.add("Rolf")
    if find_item(s, "Prism") is not None:
        play.HELD.add("Prism")
    keep_on_rolf(s, MOTAVIA_KEEP, log)
    # (build 119) Under no_kain() the party is already KAIN_PARTY: home all
    # the same, for Shir's thefts.
    if NO_KAIN or not in_party(s, "Kain"):
        if not enter_town(s, PASEO):
            log("kain: could not get home to Paseo:", play.level_name(s.state.get("level")))
            return False
        gear_up(s, PASEO, log=log)          # sells what nobody wants
        r = play.roster(s)
        now = [n for n, v in r.items() if v["in_party"]]
        for n in now:
            if n not in KAIN_PARTY and n != "Shir":
                left = keep_from(s, n, ("Kain", "Shir"), log)
                if left:
                    log(f"kain: {n} keeps {left}")
        stays = [n for n in now if n in KAIN_PARTY]
        if "Shir" not in now and len(stays) >= 4:
            # (build 119) Nobody leaves for good (no_kain): the last but
            # Rolf out for Shir's round, back after it.
            left = keep_from(s, stays[-1], ("Shir",), log)
            if left:
                log(f"kain: {stays[-1]} keeps {left}")
            stays = stays[:-1]
        if "Shir" not in now:
            pick_party(s, want=stays + ["Shir"], log=log)
        if in_party(s, "Shir"):
            tally = play.shir_thefts(s, NOAH_THEFTS, log=log,
                                     max_exits=NOAH_THEFT_EXITS,
                                     on_full=lambda: empty_shir(s, log))
            log("kain: thefts", tally.get("items"), "--",
                {i: play.carried(s, i) for i in NOAH_THEFTS})
            empty_shir(s, log)
        got = pick_party(s, want=list(KAIN_PARTY), log=log)
        if any(n not in got for n in KAIN_PARTY):
            log("kain: the party is", got)
            return False
        keep_on_rolf(s, MOTAVIA_KEEP, log)
        gear_up(s, PASEO, log=log)
        log("kain: in Paseo --", party_line(s))
    if s.state.get("level") not in play.DEZOLIS_LEVELS:
        if s.state.get("level") != PASEO and not enter_town(s, PASEO):
            return False
        if not stage_roof(s, log):
            return False
    if dezolis_town_visit(s, ZOSA, log, talk=False) is None:
        return False
    log("kain: at Zosa --", party_line(s))
    return all(in_party(s, n) for n in KAIN_PARTY)


# (build 99) Trips, not seconds (build 98's seeds: 16-24 trips to L20).
KAIN_TRIPS = 40


@play.grinding
def stage_kain_grind(s, log):
    """Kain to KAIN_TARGET on the field outside Zosa (field_spot, as
    dezolis_grind): grind_and_gear by the party's lowest level -- the
    others carry him, EXP is split by the living (loc_F2A4) -- with the
    best gear bought each time through town. Resumable (--failed)."""
    play.PIN_HEAD.add("Rolf")
    if NO_KAIN:
        log("kain_grind: no Kain in the party (no_kain)")
        return True
    spot = play.field_spot(0x03, ZOSA_GATE)
    lv = next((m["level"] for m in s.state.get("party") or []
               if m["name"] == "Kain"), None)
    if lv is None:
        log("kain_grind: Kain is not in the party")
        return False
    trips = 0
    while lv < KAIN_TARGET and trips < KAIN_TRIPS:
        grind_and_gear(s, ZOSA, KAIN_TARGET, fights_per_trip=8, trips=4,
                       log=log, spot=spot, lowest=True)
        trips += 4
        lv = next((m["level"] for m in s.state.get("party") or []
                   if m["name"] == "Kain"), 0)
    dezolis_town_visit(s, ZOSA, log, talk=False)
    log(f"kain_grind: Kain L{lv} after {trips} trips --", party_line(s))
    if lv < KAIN_TARGET:
        if play.LEAN:
            log(f"lean: Kain short of L{KAIN_TARGET}, going on")  # (build 98)
            return True
        log("kain_grind: out of trips; rerun with --from kain_grind --failed")
        return False
    return True


NOAH_TARGET = {"Rolf": 41, "Kain": 30}
NOAH_TRIP_FIGHTS = 20
# (build 99) Trips onto Noah, a wipe-out counted, not seconds (build 98's
# seeds: 42-51 trips to NOAH_TARGET).
NOAH_TRIPS = 80
NOAH_WALKS = ["walk down 3", "walk left 3", "walk up 3", "walk right 3"]


def nei_sword_out(s, log):
    """USE the Nei Sword on Noah (NeiSword_ItemSelected, loc_A21E): Esper
    mansion F1 16,16."""
    s.ready()
    # (build 98) Its holder dead -- Rolf, who wears it -- the dead come back
    # first (Rever, a Moon Dew): the playthrough's first Noah grind lost
    # Rolf on a trip, found no live holder and could not leave Noah.
    holder = next((h for h, bag in enumerate(bags(s))
                   for n, _ in bag if n == "NeiSword"), None)
    party = s.state.get("party") or []
    if holder is not None and holder < len(party) and party[holder]["dead"]:
        back = play.revive(s, spend_reserve=True)     # (build 99) the reserve
        log(f"nei_sword_out: the sword's holder was dead; {back} revived --",
            party_line(s))
    # Worn counts: Rolf wears it, and find_item looks only at what is not
    # worn. ITEM lists the whole bag, worn pieces marked, and USE on one
    # runs its action (NeiSword_ItemSelected does not ask).
    party = s.state.get("party") or []
    found = next(((h, i) for h, bag in enumerate(bags(s))
                  if h < len(party) and not party[h]["dead"]
                  for i, (n, _) in enumerate(bag) if n == "NeiSword"), None)
    if found is None:
        log("nei_sword_out: no Nei Sword --", bags(s))
        return False
    shown = use_item(s, found[0], found[1]) or []
    s.ready()
    ok = s.state.get("level") == ESPER_F1
    if not ok:
        log("nei_sword_out:", play.scripts_text(shown), "->",
            play.level_name(s.state.get("level")), s.state.get("x"), s.state.get("y"))
    return ok


def noah_trip(s, log, fights=NOAH_TRIP_FIGHTS):
    """From the mansion: YES, fights on Noah until `fights` or worn_out,
    the Nei Sword back. Returns the fights, or None when wiped out."""
    shown = lutz(s, 0, log) or []
    if s.state.get("level") != NOAH_G:
        log("noah: not on Noah after", play.scripts_text(shown),
            play.level_name(s.state.get("level")))
        return 0
    n = 0
    while n < fights and not worn_out(s):
        got = play.wander(s, NOAH_WALKS, 1, mates=True)
        if s.state.get("screen") not in ("level", "battle"):
            return None
        if not got:
            break
        n += len(got)
    if s.state.get("level") in (NOAH_G, NOAH_G + 1):
        nei_sword_out(s, log)
    return n


def mansion_mend(s, town, log):
    """After the Nei Sword: anyone dead to `town`'s clone lab (loc_B9D8's
    HEAL skips the dead), everyone else to Lutz's NO."""
    if any(m["dead"] for m in s.state.get("party") or []):
        if not enter_town(s, town):
            return False
        b = play.town_buildings(town)
        if play.BUILDING_CLONE_LAB in b:
            clone_lab(s, level=town, door=b[play.BUILDING_CLONE_LAB][0])
        heal_here(s, town)
        return not any(m["dead"] for m in s.state.get("party") or [])
    return lutz_heal(s, log)


# (build 99) Moon Dews carried onto Noah and kept back from the walk's
# revivals (play.revive, Session.dew_reserve): the Nei Sword's holder -- Rolf,
# who wears it -- dead with nobody able to cast Rever leaves the party no way
# off Noah. No shop sells one; Shir steals them in Paseo.
DEW_RESERVE = 1


def sword_holder(s):
    """The party position whose bag holds the Nei Sword, or None."""
    return next((h for h, bag in enumerate(bags(s))
                 for n, _ in bag if n == "NeiSword"), None)


def reserve_dews(s):
    """Moon Dews the reserve can count on: those outside the Nei Sword
    holder's bag. A dew in Rolf's own bag is no use to him dead --
    find_item looks only at the living (seed 11's Noah grind: the dew
    handed to Rolf, Rolf dead, "0 revived", two tries lost)."""
    holder = sword_holder(s)
    return sum(1 for h, bag in enumerate(bags(s)) if h != holder
               for n, _ in bag if n == "MoonDew")


def dews_off_holder(s, log):
    """Hand the holder's Moon Dews to a living member with room (tossing
    something of theirs when every bag is full) until the reserve is
    outside his bag."""
    for _ in range(4):
        if reserve_dews(s) >= DEW_RESERVE:
            return
        holder = sword_holder(s)
        party = s.state.get("party") or []
        b = bags(s)
        if holder is None or holder >= len(b):
            return
        slot = next((i for i, (n, w) in enumerate(b[holder])
                     if n == "MoonDew" and not w), None)
        others = [h for h in range(min(len(b), len(party)))
                  if h != holder and not party[h]["dead"]]
        if slot is None or not others:
            return
        to = min(others, key=lambda h: len(b[h]))
        if len(b[to]) >= 16 and not play.make_room(s, log=log, who=to):
            return
        shown = play.give_item(s, holder, slot, to) or []
        log(f"dews: a Moon Dew from {party[holder]['name']} to "
            f"{party[to]['name']}, off the sword's holder:",
            play.scripts_text(shown))


def restock_dews(s, town, log):
    """Before a trip onto Noah: at least DEW_RESERVE Moon Dews carried, or
    home for them -- the tower's roof door to Paseo, the last member but
    Rolf out for Shir (keep_from), her thefts for the dews (empty_shir
    after), the party as it was, and the roof back to `town`. False only
    when the way there or back fails; with no Shir to send it goes on
    without, logged."""
    s.dew_reserve = DEW_RESERVE
    dews_off_holder(s, log)
    have = reserve_dews(s)
    if have >= DEW_RESERVE:
        return True
    if getattr(s, "dews_gave_up", False):
        return True         # a restock came back empty: once a stage try
    r = play.roster(s)
    if "Shir" not in r:
        log("dews: none in reserve and no Shir to steal one; going on")
        return True
    names = [m["name"] for m in s.state.get("party") or []]
    log(f"dews: {have} of {DEW_RESERVE} in reserve; home for Shir's thefts --",
        party_line(s))
    keep_on_rolf(s, MOTAVIA_KEEP, log)
    if not enter_town(s, PASEO):
        log("dews: could not get home to Paseo:", play.level_name(s.state.get("level")))
        return False
    if "Shir" not in names:
        leaving = next((n for n in reversed(names) if n != "Rolf"), None)
        if leaving is not None:
            left = keep_from(s, leaving, ("Shir",), log)
            if left:
                log(f"dews: {leaving} keeps {left}")
        pick_party(s, want=[n for n in names if n != leaving] + ["Shir"],
                   log=log)
    if in_party(s, "Shir"):
        dews_first = ("MoonDew",)
        empty_shir(s, log, first=dews_first)     # any she already had
        # Room to steal into: seed 7's Shir came back from Rolf's house
        # with a full bag and every other bag full too, so no theft was
        # possible and none was tried. The commonest things go.
        for _ in range(4):
            names_now = [m["name"] for m in s.state.get("party") or []]
            h = names_now.index("Shir") if "Shir" in names_now else None
            if h is None or len(play.shir_bag(s)) < 14 \
                    or not play.make_room(s, log=log, who=h):
                break
        def room():
            empty_shir(s, log, first=dews_first)
            names_now = [m["name"] for m in s.state.get("party") or []]
            if "Shir" in names_now and len(play.shir_bag(s)) >= 16:
                play.make_room(s, log=log, who=names_now.index("Shir"))
        tally = play.shir_thefts(s, {"MoonDew": DEW_RESERVE}, log=log,
                                 max_exits=NOAH_THEFT_EXITS, on_full=room)
        empty_shir(s, log, first=dews_first)
        log("dews: thefts", tally.get("items"), "--",
            play.carried(s, "MoonDew"), "carried")
    pick_party(s, want=names, log=log)
    keep_on_rolf(s, MOTAVIA_KEEP, log)
    dews_off_holder(s, log)
    if s.state.get("level") != PASEO and not enter_town(s, PASEO):
        return False
    if not stage_roof(s, log):
        return False
    if dezolis_town_visit(s, town, log, talk=False) is None:
        return False
    now = reserve_dews(s)
    log("dews: back at", f"{play.level_name(town)} --", now, "carried --", party_line(s))
    if now < DEW_RESERVE:
        # No spinning: one empty-handed restock is the last this session.
        log("dews: none stolen; the Noah trips go on without a reserve")
        s.dews_gave_up = True
    return True


@play.grinding
def stage_noah_grind(s, log):
    """From Zosa with Kain raised: the grind on NoahGroundF until
    NOAH_TARGET -- Lutz's YES, noah_trip, mansion_mend -- and every fifth
    trip the nearest town for gear, mates and a save. Resumable."""
    play.PIN_HEAD.add("Rolf")
    town = nearest_town(ESPER_F1)
    trips = total = went = 0
    s.dews_gave_up = False              # (build 99) one restock try a stage try
    if s.state.get("level") not in (ESPER_F1, NOAH_G):
        if dezolis_town_visit(s, town, log, talk=False) is None:
            return False
    while not reached(s, NOAH_TARGET):
        if went >= NOAH_TRIPS:
            dezolis_town_visit(s, town, log, talk=False)
            if play.LEAN:
                # (build 98) Short of NOAH_TARGET: the bosses with these levels.
                log(f"lean: noah_grind short of {NOAH_TARGET} after {total} "
                    f"fights, going on --", party_line(s))
                return True
            log("noah_grind: out of trips; rerun with --from noah_grind "
                "--failed --", party_line(s))
            return False
        if not restock_dews(s, town, log):
            return False
        went += 1
        n = noah_trip(s, log)
        if n is None:
            if not recover(s, log):
                return False
            continue
        total += n
        trips += 1
        log(f"noah trip {trips}: {n} fights --", party_line(s))
        if not mansion_mend(s, town, log):
            log("noah_grind: could not mend --", party_line(s))
            return False
        if trips % 5 == 0:
            dezolis_town_visit(s, town, log, talk=False)
    dezolis_town_visit(s, town, log, talk=False)
    log(f"noah_grind: {total} fights in {trips} trips --", party_line(s))
    return True


NOAH_BOX = (33, 13)                 # object $39 at $210,$D8 (LoadSpritesInLevel)
BOX_TRIES = 4
BOX_HP, BOX_TP = 0.85, 0.6


def tp_line(s):
    return " ".join(f"{m['name']} TP {m['tp']}/{m['max_tp']}"
                    for m in s.state.get("party") or [] if m["max_tp"])


def box_ready(s):
    """Nobody down, HP at BOX_HP of the party's and TP at BOX_TP of the
    TP users' between them."""
    p = s.state.get("party") or []
    if not p or any(m["dead"] for m in p):
        return False
    hp = sum(m["hp"] for m in p) / max(1, sum(m["max_hp"] for m in p))
    tmax = sum(m["max_tp"] for m in p)
    tp = sum(m["tp"] for m in p) / tmax if tmax else 1.0
    return hp >= BOX_HP and tp >= BOX_TP


# (build 94) The party for the last two bosses, from tools/boss_trials.py:
# Dark Force 11 of 20 and Mother Brain 9 of 10 at the Noah checkpoint's
# levels with fight_smart's boss policy -- Amy's Deban is what wins; the
# party build 91 took there (Anna for Amy) won 1 of 20 and none of 9.
BOSS_PARTY = ("Rolf", "Rudo", "Amy", "Kain")
BOSS_TRIES = 4
# The trials dealt eight Trimates a member; with none the same party won
# 1 of 10 against Dark Force. Ryuon's is the Dezolis item store with them
# (the mansion's town, Aukba, has none).
BOSS_TRIMATES = 32


def boss_supplies(s, log):
    """Ryuon's item store: Trimates to BOSS_TRIMATES across the bags that
    have room, after the usual town visit."""
    if dezolis_town_visit(s, RYUON, log, talk=False) is None:
        return False
    got = play.stock_item(s, RYUON, "Trimate", BOSS_TRIMATES, log=log)
    have = sum(1 for bag in bags(s) for (n, w) in bag if n == "Trimate")
    log(f"boss supplies: {got} Trimates bought, {have} carried")
    return True


def stage_boss_party(s, log):
    """From story_noah_grind.json (the town nearest the mansion): home to
    Paseo by the roof door, whoever BOSS_PARTY leaves out hands over what
    Amy could use (keep_from), Amy in at Rolf's house, geared in Paseo,
    and back to the mansion's town. Checkpoint there."""
    play.PIN_HEAD.add("Rolf")
    if find_item(s, "Prism") is not None:
        play.HELD.add("Prism")
    keep_on_rolf(s, MOTAVIA_KEEP, log)
    town = nearest_town(ESPER_F1)
    if all(in_party(s, n) for n in BOSS_PARTY):
        return dezolis_town_visit(s, town, log, talk=False) is not None
    if not enter_town(s, PASEO):
        log("boss_party: could not get home to Paseo:", play.level_name(s.state.get("level")))
        return False
    gear_up(s, PASEO, log=log)
    r = play.roster(s)
    for n in [n for n, v in r.items() if v["in_party"]]:
        if n not in BOSS_PARTY:
            left = keep_from(s, n, ("Amy",), log)
            if left:
                log(f"boss_party: {n} keeps {left}")
    got = pick_party(s, want=list(BOSS_PARTY), log=log)
    if any(n not in got for n in BOSS_PARTY):
        log("boss_party: the party is", got)
        return False
    keep_on_rolf(s, MOTAVIA_KEEP, log)
    gear_up(s, PASEO, log=log)
    log("boss_party: in Paseo --", party_line(s))
    if not stage_roof(s, log):
        return False
    if dezolis_town_visit(s, town, log, talk=False) is None:
        return False
    log("boss_party: at the mansion's town --", party_line(s))
    return True


def stage_dark_force(s, log):
    """Pandora's box and Dark Force, up to BOSS_TRIES times: a loss goes
    back to the session's last save (the town's) and tries again."""
    for attempt in range(BOSS_TRIES):
        if dark_force_once(s, log):
            return True
        log(f"dark_force: try {attempt + 1} lost")
        if s.state.get("screen") not in ("level", "building") \
                and not recover(s, log):
            return False
    return False


def dark_force_once(s, log):
    """Pandora's box and Dark Force (route noah_box): from the town,
    mended, Lutz's YES, to 33,14 below the box, A ($171A), C through the
    reload and $1910 into the fight ($102), fought with fight_smart; then
    the Nei Sword out and the town. True when Dark Force is beaten."""
    s.dews_gave_up = False              # (build 99)
    play.PIN_HEAD.add("Rolf")
    town = nearest_town(ESPER_F1)
    if not boss_supplies(s, log):
        return False
    if dezolis_town_visit(s, town, log, talk=False) is None:
        return False
    # (build 91) The walk from Lutz's landing (32,107) to the box is
    # eleven or so fights; the first run opened it with Rolf at 187/286
    # and Kain's TP at 5 and was wiped out. TP comes back only at Lutz's
    # NO (or a town), so: walk there, top HP up with mates and Res, and
    # open it only with the party at BOX_READY -- else the Nei Sword out,
    # NO, YES and the walk again.
    for attempt in range(BOX_TRIES):
        if not restock_dews(s, town, log):          # (build 99)
            return False
        if not mansion_mend(s, town, log):
            return False
        lutz(s, 0, log)
        if s.state.get("level") != NOAH_G:
            return False
        # The walk's fights on weapons and mates only: build 91's first
        # tries arrived with Rolf's and Kain's TP spent on the way (0/146,
        # 1/108) every time. goto() fights with s.fight_opts. (Build 97)
        # No RUN, and the dead revived and everyone mended to half after
        # each fight: WALK_FIGHT and WALK_MEND, from tools/walk_trials.py
        # -- 600 seeds of this walk, 2 wiped out against build 94's 16.
        s.fight_opts = dict(WALK_FIGHT)
        s.walk_mend = WALK_MEND
        try:
            there = travel(s, NOAH_G, {(NOAH_BOX[0], NOAH_BOX[1] + 1)},
                           log=log)
        finally:
            s.fight_opts = None
            s.walk_mend = None
        if not there:
            log("dark_force: could not reach the box:",
                play.level_name(s.state.get("level")), s.state.get("x"), s.state.get("y"))
            if s.state.get("screen") not in ("level", "battle"):
                recover(s, log)
                return False
            nei_sword_out(s, log)
            continue
        play.revive(s)
        play.mend(s, below=0.9)
        log(f"below the box, try {attempt + 1} --", party_line(s),
            "--", tp_line(s), "--", play.count_mates(s), "mates")
        if box_ready(s):
            break
        nei_sword_out(s, log)
    else:
        log("dark_force: never at the box in shape")
        return False
    s.send("press up", "wait idle")
    s.send("shot dark_force_01_box")
    before = len(s.shown)
    s.send("press a", "wait idle")
    for _ in range(12):
        if s.state.get("screen") == "battle":
            break
        if s.state.get("kind") == "waiting":
            s.send("press c", "wait idle")
        else:
            s.send("wait idle")
    log("the box:", play.scripts_text(s.shown[before:]))
    if s.state.get("screen") != "battle":
        log("dark_force: no fight --", s.state.get("screen"))
        return False
    s.send("wait idle", "shot dark_force_02_fight")
    log("fight: formation", s.state.get("formation"), s.state.get("enemies"))
    play.fight_smart(s, run_below=0.0, limit=4000, stop_on_level=True,
                     stop_on_gameover=True)
    won = s.state.get("screen") == "level"
    rec = s.fights[-1] if s.fights else {}
    log("Dark Force", "beaten" if won else "not beaten", "--",
        s.state.get("screen"), party_line(s), "ticks", rec.get("ticks"),
        "scripts", play.scripts_text((rec.get("scripts") or [])[-8:]))
    if not won:
        recover(s, log)
        return False
    s.ready()
    s.send("shot dark_force_03_after")
    log("after:", play.level_name(s.state.get("level")), s.state.get("x"), s.state.get("y"),
        play.scripts_text(s.shown[before:][-6:]))
    if not nei_sword_out(s, log):
        # (build 98) The sword's holder dead and nobody to revive him: no
        # way off Noah, and nowhere to save. A lost try (the playthrough's
        # seed 2 went on from here and never left).
        log("dark_force: beaten, but no way off Noah --", party_line(s))
        return False
    mansion_mend(s, town, log)
    dezolis_town_visit(s, town, log, talk=False)
    return True


NOAH_F1 = 0x64
# The plain floor just south of NoahF1's corridor. The corridor itself is
# block $33, every cell of it with the collision table's special bit (the
# bit stairs, exits and holes carry), which LevelMap keeps out of its
# floor: here it marks the approach's trigger (loc_70D6: row 33 or north
# of it), so the stage walks the corridor itself.
MOTHER_BRAIN_WALK_FROM = (33, 40)
# (build 96c) Mending to 0.7 after every fight, and healing in a fight at
# the usual 0.5, spent all 18 Trimates and Rolf's and Amy's TP on the walk:
# three tries reached her corridor with nothing left for her (box_ready).
# The wipe itself (build 94's and this one's) was fight_smart's RUN: once
# anyone alive is under run_below (0.25) it orders STGY/RUN every round,
# and against Noah's pairs of Vorcanos it failed round after round with
# nobody attacking or healing until all four were down. The walk does
# not run.
# (build 97) tools/walk_trials.py (docs/walk_trials_build97.md): this walk
# and the Dark Force stage's, from Lutz's YES, on the same seeds for every
# policy. Weapons and mates with no RUN beat 96c's healing walk: 300 seeds
# of this one, 293 reached her corridor, 7 wiped out and 239 fit for her
# (box_ready), against 290, 10 and 195 -- 96c's heals in a fight are Res,
# so Amy arrived with 71 of 135 TP where a Trimate would have done. RUN
# costs: build 94's walk, the same with RUN, was wiped out 17 times. After
# each fight the dead revived and everyone mended to half (WALK_MEND):
# 600 seeds a walk, 2 wiped out on the Dark Force walk against 16 with
# goto's old rule, and on this one 17 against 13 (within noise); 1200
# walks pooled, 19 against 29, as often fit (963 against 979).
WALK_MEND = 0.5
WALK_FIGHT = {"use_techs": False, "type_techs": (), "run_below": 0.0}
MOTHER_BRAIN_WALK = 8
MOTHER_BRAIN = 0x103
WIN_YES_NO = 0x1F
ENDING_LIMIT = 20000                # the ending screen is ~17300 ticks


def answer(s, answers, until, limit=200):
    """C through text; at each YES/NO the next of `answers` ("yes"/"no")
    until `until(state)`. False if the limit ran out first."""
    answers = list(answers)
    for _ in range(limit):
        st = s.state
        if until(st):
            return True
        if st.get("wait") == "menu" and st.get("menu") == WIN_YES_NO:
            if answers and answers.pop(0) == "no":
                s.send("press down")
            s.send("press c", "wait idle")
            continue
        if st.get("kind") == "waiting":
            s.send("press c", "wait idle")
        else:
            s.send("wait idle")
    return False


def mother_brain_once(s, log):
    """From the town after Dark Force: mended, Lutz's YES, to the foot of
    NoahF1's corridor on weapons alone (TP kept for her), topped up and at
    box_ready, north up the corridor into the arrival, YES to fighting, NO to
    returning ("Die!"), her battle ($103) with fight_smart. True when she
    is beaten; the level is back with the aftermath to come."""
    s.dews_gave_up = False              # (build 99)
    play.PIN_HEAD.add("Rolf")
    town = nearest_town(ESPER_F1)
    if not boss_supplies(s, log):
        return False
    if dezolis_town_visit(s, town, log, talk=False) is None:
        return False
    for attempt in range(BOX_TRIES):
        if not restock_dews(s, town, log):          # (build 99)
            return False
        if not mansion_mend(s, town, log):
            return False
        lutz(s, 0, log)
        if s.state.get("level") != NOAH_G:
            return False
        # The walk's fights: WALK_FIGHT and WALK_MEND (build 97, from
        # tools/walk_trials.py) -- weapons and mates, no RUN, TP kept for
        # her; the dead revived and everyone mended to half after each
        # fight. (Build 96c healed with techniques in the fights; build 94
        # ran.)
        s.fight_opts = dict(WALK_FIGHT)
        s.walk_mend = WALK_MEND
        try:
            there = travel(s, NOAH_F1, {MOTHER_BRAIN_WALK_FROM}, log=log)
        finally:
            s.fight_opts = None
            s.walk_mend = None
        if not there:
            log("mother_brain: could not reach her corridor:",
                play.level_name(s.state.get("level")), s.state.get("x"), s.state.get("y"))
            if s.state.get("screen") not in ("level", "battle"):
                recover(s, log)
                return False
            nei_sword_out(s, log)
            continue
        play.revive(s)
        play.mend(s, below=0.9)
        log(f"below her, try {attempt + 1} --", party_line(s), "--",
            tp_line(s), "--", play.count_mates(s), "mates")
        if box_ready(s):
            break
        nei_sword_out(s, log)
    else:
        log("mother_brain: never at her corridor in shape")
        return False
    before = len(s.shown)
    s.send(f"walk up {MOTHER_BRAIN_WALK}", "wait idle", "wait ticks 2",
           "wait idle")
    if not answer(s, ["yes", "no"],
                  lambda st: st.get("screen") == "battle"):
        log("mother_brain: no fight --", s.state.get("screen"))
        return False
    log("the confrontation:", play.scripts_text(s.shown[before:]))
    if s.state.get("formation") != MOTHER_BRAIN:
        log("mother_brain: a fight, but formation", s.state.get("formation"))
        return False
    s.send("wait idle", "shot mother_brain_01_fight")
    play.fight_smart(s, run_below=0.0, limit=4000, stop_on_level=True,
                     stop_on_gameover=True)
    won = s.state.get("screen") == "level"
    rec = s.fights[-1] if s.fights else {}
    log("Mother Brain", "beaten" if won else "not beaten", "--",
        s.state.get("screen"), party_line(s), "ticks", rec.get("ticks"))
    return won


def stage_mother_brain(s, log):
    """Mother Brain, up to BOSS_TRIES times (a loss back to the last save),
    then everything after her: the aftermath, the walk, Lutz's warning,
    the walk to the Earthmen, YES and YES to them, the six on the bridge,
    and the ending screen played through to its last button and the Sega
    screen (build 93a's ending; shots of a page, the credits and the
    end). No checkpoint follows: the game is over."""
    for attempt in range(BOSS_TRIES):
        if mother_brain_once(s, log):
            break
        log(f"mother_brain: try {attempt + 1} lost")
        if s.state.get("screen") not in ("level", "building") \
                and not recover(s, log):
            return False
    else:
        return False
    before = len(s.shown)
    s.send("shot mother_brain_02_after")
    # Each press is settled with `wait idle`, which on the ending screen
    # waits out the whole limit: at 600 ticks (the aftermath's walks and
    # texts take under 300) an overshoot onto the ending lands inside the
    # first page's text, so the shots below start from page 3.
    s.send("limit 600")
    if not answer(s, ["yes", "yes", "yes"],
                  lambda st: st.get("screen") == "ending", limit=400):
        log("mother_brain: no ending --", s.state.get("screen"),
            play.scripts_text(s.shown[before:][-8:]))
        return False
    log("after her:", play.scripts_text(s.shown[before:]))
    s.send(f"limit {ENDING_LIMIT}", "wait screen ending",
           "wait ending page3_text", "wait ticks 700", "shot ending_01_page3",
           "wait ending final_days_text", "wait ticks 600",
           "shot ending_02_final_days",
           "wait ending credits", "wait ticks 4500", "shot ending_03_credits",
           "wait ending ufo", "wait ticks 24", "shot ending_04_ufo",
           "wait ending button", "shot ending_05_the_end",
           "press c", "wait screen sega", "limit 3600")
    done = s.state.get("screen") == "sega"
    log("the ending:", "played to the Sega screen" if done else
        f"stopped on {s.state.get('screen')}", "--",
        play.scripts_text(s.shown[before:][-10:]))
    return done


STAGES = [("paseo", stage_paseo), ("arima", stage_arima),
          ("gear", stage_gear), ("shure", stage_shure), ("nido", stage_nido),
          ("darum", stage_darum), ("oputa", stage_oputa), ("amy", stage_amy),
          ("lab_in", stage_lab_in), ("lab", stage_lab),
          ("lab_b1", stage_lab_b1),
          ("hugh", stage_hugh), ("key_tube", stage_key_tube),
          ("zema", stage_zema), ("roron", stage_roron), ("uzo", stage_uzo),
          ("gum", stage_gum), ("climatrol", stage_climatrol),
          ("dam_party", stage_dam_party), ("cards", stage_cards),
          ("dam_grind", stage_dam_grind), ("dams", stage_dams),
          ("roof", stage_roof), ("skure", stage_skure),
          ("dezolis_towns", stage_dezolis_towns), ("esper", stage_esper),
          ("dezolis_grind", stage_dezolis_grind),
          ("motavia", stage_motavia), ("shir", stage_shir),
          ("menobe", stage_menobe), ("ikuto", stage_ikuto),
          ("guaron", stage_guaron), ("naval", stage_naval),
          ("nei_sword", stage_nei_sword), ("kain", stage_kain),
          ("kain_grind", stage_kain_grind), ("noah_grind", stage_noah_grind),
          ("boss_party", stage_boss_party), ("dark_force", stage_dark_force),
          ("mother_brain", stage_mother_brain)]


# ---------------------------------------------------------------------
# (build 118) Squeak-by. The owner, build 117: "I don't think you should be
# grinding to assure 100% survival in 1 playthrough. That's why saves
# exist. Live on the razors edge squeek by mode. The grind is to accomplish
# bare minimum survival gear and levels. Getting best gear is better than
# level padding." squeak() lowers every grind's level target; the stages
# that grind for gear (gear, oputa, amy: until the town's shelf is bought)
# keep theirs. A try that fails is paid for with a retry from the last
# save (playthrough.py, computer_plays.py), and before the retry a top-up:
# TOP_UP_TRIPS trips of the latest grind stage's own grind, so the party is
# stronger each time and each seed finds the least it needs.
# ---------------------------------------------------------------------

SQUEAK = False
SQUEAK_TRIES = 6        # a stage's tries under squeak() (4 otherwise)
TOP_UP_TRIPS = 3        # a try already failed: about 24 fights (top_up)


def squeak():
    """Every level target down to squeak-by's: one or two levels under
    what the story ground to before, each a level every stage after it
    was measured to get through at with retries (docs/history.md, Build
    118)."""
    global SQUEAK, ARIMA_TARGET, LAB_TARGET, CLIMATROL_TARGET
    global DAM_GRIND_TARGET, DEZOLIS_GRIND, DEZOLIS_GRIND_2, KAIN_TARGET
    global NOAH_TARGET
    if SQUEAK:
        return
    SQUEAK = True
    ARIMA_TARGET = 7
    LAB_TARGET = 15
    CLIMATROL_TARGET = {"Rolf": 20, "Rudo": 20, "Anna": 17}
    DAM_GRIND_TARGET = {"Rolf": 22, "Rudo": 22, "Amy": 18, "Anna": 20}
    DEZOLIS_GRIND = 28
    DEZOLIS_GRIND_2 = 31
    KAIN_TARGET = 18
    NOAH_TARGET = {"Rolf": 35, "Kain": 24}
    for name, (levels, wanted, target) in list(PRISM_DUNGEONS.items()):
        PRISM_DUNGEONS[name] = (levels, wanted, target - 2)


# Rolf at the end of Noah's grind with Kain's party: squeak-by's seeds 1-12
# (Noah 35/24) left him at L36-37 on its first pass; the story's L41 target.
NOAH_KAIN_ROLF = {True: 37, False: 41}


def no_kain():
    """(build 119) Noah and the bosses with NO_KAIN_PARTY: the kain stage
    only Shir's thefts, kain_grind nothing, Noah's grind until Rolf is
    where Kain's grind left him (NOAH_KAIN_ROLF), boss_party nothing to
    change. After squeak(), or without it."""
    global NO_KAIN, KAIN_PARTY, BOSS_PARTY, NOAH_TARGET
    if NO_KAIN:
        return
    NO_KAIN = True
    KAIN_PARTY = BOSS_PARTY = NO_KAIN_PARTY
    NOAH_TARGET = {"Rolf": NOAH_KAIN_ROLF[SQUEAK]}


def top_up_paseo(s, log, trips):
    return enter_town(s, PASEO) and roam_and_gear(
        s, PASEO, 99, mild_ground(), trips=trips, log=log)


def top_up_arima(s, log, trips):
    return enter_town(s, ARIMA) and roam_and_gear(
        s, ARIMA, 99, mild_ground(), trips=trips, log=log)


def top_up_oputa(s, log, trips):
    return enter_town(s, OPUTA) and grind_and_gear(
        s, OPUTA, 99, trips=trips, log=log)


def top_up_lab(s, log, trips):
    if not enter_town(s, OPUTA):
        return False
    heal_here(s, OPUTA)
    ok = dungeon_grind(s, LAB_G, LAB_SPOT, OPUTA, 99, trips=trips, log=log,
                       ground=lab_ground(), gear=True)
    retreat(s, OPUTA, log=log)
    return ok


def top_up_zema(s, log, trips):
    return enter_town(s, ZEMA) and grind_and_gear(
        s, ZEMA, 99, trips=trips, log=log)


def top_up_climatrol(s, log, trips):
    ok = climatrol_grind(s, log, {"Rolf": 99}, trips=trips)
    retreat(s, KUERI, log=log)
    return ok


def top_up_piata(s, log, trips):
    return stage_dam_grind(s, log, target={"Rolf": 99}, trips=trips)


def top_up_zosa(s, log, trips):
    # (build 168f) A Skure retry starts from story_roof.json, before
    # Skure's chests: no cap in the bags, and Zosa refuses a bare-headed
    # Rolf everything (cap_for_shops). The top-up ground there with no
    # gear and no hospital, and the party came back to the stage hurt:
    # "Rolf in the wrong Cap", then "not bought?" a trip, in every
    # language's grind; JAPANESE seed 10 lost all six Skure tries. B1's
    # Magic Cap first, as the stage itself takes it, and Rolf's head kept
    # for it as the Dezolis stages keep it (gear_up swapped it for a
    # Titanimet every trip when the top-up ran in a process whose Skure
    # had not pinned it yet).
    play.PIN_HEAD.add("Rolf")
    if not caps_carried(s):
        cell = next((c for c in play.chests(SKURE_B1)
                     if play.chest_item(SKURE_B1, c) == "MagicCap"), None)
        shown = search(s, SKURE_B1, cell, log=log) if cell else None
        log("top-up: B1's Magic Cap first:",
            play.scripts_text(shown or []) or "unreached")
    if dezolis_town_visit(s, ZOSA, log, talk=False) is None:
        return False
    ok = grind_and_gear(s, ZOSA, 99, fights_per_trip=8, trips=trips, log=log,
                        spot=play.field_spot(0x03, ZOSA_GATE))
    dezolis_town_visit(s, ZOSA, log, talk=False)
    return ok


def top_up_noah(s, log, trips):
    """Noah's own grind, whoever NOAH_TARGET names (Rolf and Kain; Rolf
    alone under no_kain) a level past where they stand for each TOP_UP_TRIPS of `trips` -- a try already
    failed each (top_up): +1, then +2, +3. (build 119) It was +1 always, so
    Dark Force's retries, each from the one save, never got stronger than
    that."""
    global NOAH_TARGET
    keep = NOAH_TARGET
    up = max(1, trips // TOP_UP_TRIPS)
    NOAH_TARGET = {m["name"]: m["level"] + up
                   for m in s.state.get("party") or []
                   if m["name"] in keep}
    try:
        return stage_noah_grind(s, log)
    finally:
        NOAH_TARGET = keep


# The grind stages' top-ups, each its own grind's place and way.
TOP_UP = {"arima": top_up_paseo, "gear": top_up_arima,
          "oputa": top_up_oputa, "amy": top_up_oputa, "lab_in": top_up_lab,
          "zema": top_up_zema, "climatrol": top_up_climatrol,
          "dam_grind": top_up_piata, "skure": top_up_zosa,
          "dezolis_grind": top_up_zosa, "kain_grind": top_up_zosa,
          "noah_grind": top_up_noah}

# No top-up before these: nothing is ground before them (paseo), the last
# grind is on the other planet (motavia, shir), or the party has just
# changed and the last grind's place is not theirs (dam_party, cards,
# roof).
NO_TOP_UP = frozenset({"paseo", "motavia", "shir", "dam_party", "cards",
                       "roof"})


@play.grinding
def top_up(s, name, log, attempt=1):
    """Before try `attempt` + 1 of stage `name` under squeak(): TOP_UP_TRIPS
    trips a try already failed of the latest grind at or before it. A retry
    resumes from the same save as the one before it (the top-up's levels
    are not saved), so each is ground up from there a little further than
    the last: 3 trips, then 6, 9 (seed 4's Skure and seed 7's Neifirst,
    build 118, failed every try with the same 3). True when there was one."""
    if name in NO_TOP_UP:
        return False
    trips = TOP_UP_TRIPS * max(1, attempt)
    names = [n for n, _ in STAGES]
    for n in reversed(names[:names.index(name) + 1]):
        if n in TOP_UP:
            log(f"{name}: squeak-by's top-up before the retry -- {n}'s "
                f"grind, {trips} trips --", party_line(s))
            TOP_UP[n](s, log, trips)
            log(f"{name}: after the top-up --", party_line(s))
            return True
    return False


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--from", dest="start", choices=[n for n, _ in STAGES],
                    help="CONTINUE routes/saves/story_<stage>.json and go "
                         "on from the stage after it")
    ap.add_argument("--to", choices=[n for n, _ in STAGES], default="nido")
    ap.add_argument("--redo", action="store_true",
                    help="rerun the --from stage itself from its checkpoint")
    ap.add_argument("--failed", action="store_true",
                    help="(build 89) continue story_<from>_failed.json and "
                         "redo the --from stage: a stage played over several "
                         "runs (shir)")
    ap.add_argument("--session", default="story")
    ap.add_argument("--trace", default=None,
                    help="every probe line to this file as well")
    ap.add_argument("--saves", default=None,
                    help="(build 98) where checkpoints are written and "
                         "--from reads them (routes/saves)")
    args = ap.parse_args()
    global SAVES
    if args.saves:
        SAVES = Path(args.saves)

    log = Report()
    s = Session(args.session, trace=args.trace)
    names = [n for n, _ in STAGES]
    first = 0
    if args.start:
        first = names.index(args.start) + (0 if args.redo else 1)
        s.redo = args.redo
        cp = SAVES / f"story_{args.start}.json"
        if args.redo and (SAVES / f"story_{args.start}_start.json").exists():
            # A stage redone starts where it first started, not from a
            # save its own earlier attempts made.
            cp = SAVES / f"story_{args.start}_start.json"
        if args.failed:
            first = names.index(args.start)
            s.redo = True
            cp = SAVES / f"story_{args.start}_failed.json"
        if not continue_game(s, str(cp)):
            log(f"could not continue {cp}")
            return 2
        log(f"continued {cp}:", party_line(s))
    last = names.index(args.to)
    status = 0
    try:
        for name, fn in STAGES[first:last + 1]:
            log(f"--- {name} ---")
            ok = fn(s, log)
            # A stage that did not finish keeps its progress under its own
            # name: story_<stage>.json is what --from <stage> continues,
            # and a failed run writing over it (build 78's did) loses the
            # state every later run and trial was measured from.
            checkpoint(s, name if ok else f"{name}_failed", args.session,
                       log)
            if not ok:
                log(f"stage {name} did not finish")
                status = 1
                break
    finally:
        log("--- report ---")
        log(f"{len(s.fights)} fights, {round(time.time() - log.t0, 1)} s")
        deaths = sum(1 for f in s.fights
                     if any(m["dead"] for m in f.get("party") or []))
        log(f"fights ending with someone dead: {deaths}")
        log(f"members down: {len(s.downs)}; brought back: "
            f"{len(s.revivals)}")
        for how, n in sorted(revival_tally(s).items(), key=lambda kv: -kv[1]):
            log(f"   {n} x {how}")
        stderr = [ln for ln in s.events if "    |     " in ln]
        log(f"stderr lines: {len(stderr)}")
        for ln in sorted(set(ln.split("|", 1)[1].strip() for ln in stderr)):
            log("  ", ln)
        log("final:", party_line(s))
        s.close()
        play.write_lf(Path(f"route_out/{args.session}/story.txt"),
                      "\n".join(log.lines) + "\n")
    return status


if __name__ == "__main__":
    sys.exit(main())
