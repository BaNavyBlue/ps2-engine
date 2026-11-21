#!/usr/bin/env python3
"""(build 138e) The saves for checking the port's palette scenes in the
emulator, written into a battery file beside the player's own slot
(docs/emulator_saves.md).

    python3 tools/emulator_scenes.py YOUR.srm -o SCENES.srm
    python3 tools/emulator_scenes.py --new -o SCENES.srm
    python3 tools/emulator_scenes.py --setup            # remake the Noah save

Slot 1 is left as the file has it (the player's own); slots 2-4 get:

  2  LIB   routes/saves/paseo_recorder.json -- Paseo, below the Central
           Tower's door with the Recorder: up into the tower, the Governor,
           the library and "overlap the graphs" (route
           library_graph_overlay).
  3  LAB   routes/saves/biolab_recorder.json -- the Biosystems Lab B1 at
           79,20, damage floors two steps left and two down: the white
           field and the palette as loaded after it (route
           emulator_lab_damage_floor).
  4  NOAH  routes/saves/emu_mother_brain.json -- Noah's first floor at the
           foot of Mother Brain's corridor (33,40): up the corridor into
           the arrival through white (route emulator_mother_brain_white).

The Noah save is made by the harness (--setup, or when it is missing):
walk_mother_brain.json CONTINUEd and walked to the corridor's foot as
story.py's Mother Brain stage walks it (WALK_FIGHT, WALK_MEND, fights by
fight_smart), the dead revived and everyone mended to 0.9, then the
probe's save-here -- a Visiphone save, so CONTINUE puts the party there.
The Sega screen's fade needs no save.
"""

import argparse
import shutil
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))

import play         # noqa: E402
import story        # noqa: E402
import to_srm       # noqa: E402

SCENES = [
    (1, "LIB", "paseo_recorder.json"),
    (2, "LAB", "biolab_recorder.json"),
    (3, "NOAH", "emu_mother_brain.json"),
]
NOAH_SAVE = ROOT / "routes" / "saves" / "emu_mother_brain.json"


def setup(log=print):
    """The Noah save: the walk to the corridor's foot, then save-here."""
    s = play.Session("emulator_scenes_setup")
    try:
        if not play.continue_game(s, "routes/saves/walk_mother_brain.json"):
            log("setup: CONTINUE failed")
            return False
        s.ready()
        s.fight_opts = dict(story.WALK_FIGHT)
        s.walk_mend = story.WALK_MEND
        try:
            there = play.travel(s, story.NOAH_F1, {story.MOTHER_BRAIN_WALK_FROM},
                                log=lambda *a: None)
        finally:
            s.fight_opts = None
            s.walk_mend = None
        st = s.state
        if not there or (st.get("level"), st.get("x"), st.get("y")) != \
                (story.NOAH_F1,) + tuple(story.MOTHER_BRAIN_WALK_FROM):
            log("setup: did not reach the corridor's foot:", st.get("level"),
                st.get("x"), st.get("y"), st.get("screen"))
            return False
        play.revive(s)
        play.mend(s, below=0.9)
        s.ready()
        s.send("save-here 1")
        log("setup: at", st.get("level"), st.get("x"), st.get("y"), "--",
            story.party_line(s), "--", len(s.fights), "fights on the way")
    finally:
        s.close()
    src = ROOT / "route_out" / "emulator_scenes_setup" / "saves" / "slot1.json"
    if not src.exists():
        log("setup: no save written")
        return False
    shutil.copy(src, NOAH_SAVE)
    return True


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("srm", nargs="?", help="the player's battery file (slot 1 kept)")
    ap.add_argument("--new", action="store_true", help="a fresh file, slot 1 empty")
    ap.add_argument("-o", "--output")
    ap.add_argument("--setup", action="store_true", help="remake emu_mother_brain.json")
    args = ap.parse_args()

    if args.setup or not NOAH_SAVE.exists():
        if not setup():
            return 1
    if args.srm is None and not args.new:
        return 0 if args.setup else ap.error("YOUR.srm or --new")
    if args.output is None:
        ap.error("-o SCENES.srm")

    tables = to_srm.Tables()
    srm = to_srm.Srm.new() if args.new else to_srm.Srm.load(args.srm)
    before = srm.copy()
    for slot, label, name in SCENES:
        text = (ROOT / "routes" / "saves" / name).read_text(encoding="utf-8")
        gd = to_srm.load_port_save(tables, text)
        gd["save_name"] = to_srm.name_from_ascii(label)
        block = to_srm.block_from_gamedata(gd)
        prior = srm.copy()
        srm.put_block(slot, block)
        srm.put_block(slot + to_srm.MIRROR, block)
        problems = to_srm.verify_written(tables, srm, prior, slot, block, gd)
        if problems:
            for p in problems:
                print("FAIL  %s: %s" % (name, p), file=sys.stderr)
            return 1
    if srm.block(0) != before.block(0) or srm.block(to_srm.MIRROR) != before.block(to_srm.MIRROR):
        print("FAIL  slot 1 moved", file=sys.stderr)
        return 1
    Path(args.output).write_bytes(bytes(srm.image))
    print("%s: %s" % (args.output, srm.describe()))
    checked = srm.copy()
    for s, what, why in to_srm.power_on_check(checked):
        res = to_srm.continue_slot(checked, s)
        line = "  slot %d: %s" % (s + 1, what)
        if res is not None:
            line += " -- %s; CONTINUE then %s, cell %d,%d" % (
                to_srm.slot_list_line(checked.block(s)).strip(),
                to_srm.map_name(tables, res["level"]), res["x"] // 16, res["y"] // 16)
        print(line)
    return 0


if __name__ == "__main__":
    sys.exit(main())
