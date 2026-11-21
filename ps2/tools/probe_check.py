#!/usr/bin/env python3
"""Checks of route_probe itself that a .route cannot express (build 91).

  python3 tools/probe_check.py [--probe ./route_probe]

continue_own_slot: `start slot 1 FILE` with FILE the session's own
saves/slot1.json -- story.py's recover() after a wipe. Before build 91
copy_file opened the destination for writing first, which emptied the
file before it was read: "will not load" and "json: ...: syntax error
near byte 0". A route allows one start, so this is a session (-i).

ready_at_the_wipe_screens (build 108): Session.ready() gives up at once on
the SEGA screen and the title, the screens a wipe-out leaves.

continue_another_file (build 113): a session's second `start slot 1 FILE`,
from another checkpoint, CONTINUEs that file. Build 91's copy_file skipped
the copy when the two files had one device and inode, and Windows has no
inode (st_ino is 0 for every file): the slot kept the first save, and the
second CONTINUE loaded it again.

missing_asset (build 157): a scratch tree with a sheet the game loads at
start (title_copyright_art.png) left out. route_probe exits 2 with
game_init's message, and the log names the file. Until 157 the loader's
message reached the interpreter before route_probe had attached it, a
NULL call: a segmentation fault and an empty log.

driver_on_python38 (build 162): COMPUTER PLAYS's driver and the modules it
imports parse as Python 3.8 and call none of the newer Pythons' methods
this file knows (Path.write_text's newline, str.removeprefix, zip's
strict, ...). A player's Python 3.8 first on the PATH ran build 161's
driver, which stopped at its first Data Memory save on write_text's
newline, and the game handed the pad back with no word. No probe needed.

gameover_twice (build 157a): two lost fights in one session. The tick the
battle hands over to game over draws before the screen is entered, and
render_gameover took the phase the last game over left: on the second,
one frame of game over's background under the battle's windows. Now that
frame is the battle, the same bytes both times. A session for the two
starts.

new_game_languages (build 168e): the driver's new game, Session.new_game(),
in each language. In ENGLISH 1989 the route new_game.route tick for tick;
in the retranslation and JAPANESE, on the street with Rolf and Nei. Until
168e the driver ran the route, whose C presses are counted in the
cartridge's English: the retranslation's Governor still talked after the
twelfth (COMPUTER PLAYS at AUTO stood there at 60 Hz), and the stock
board's moves typed letters on the Japanese board.

start_new_again (build 170): `start new` once a tick has run presses the
console's reset, as COMPUTER PLAYS's always has: the SEGA screen at once,
then the title. Until 170 the probe did nothing there, so playthrough.py's
second try at Paseo went on with the first try's game -- seed 5 in
JAPANESE, Nei dead and 11 meseta, never out of Paseo in six tries. A
route allows one start, its first command, so this is a session.
"""
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import play                                                 # noqa: E402


def continue_own_slot(probe):
    s = play.Session("probe_check", probe=probe)
    try:
        first = play.continue_game(s, "routes/saves/story_kain.json")
        lv1 = s.state.get("level")
        own = f"route_out/{s.name}/saves/slot1.json"
        size = Path(own).stat().st_size if Path(own).exists() else 0
        second = play.continue_game(s, own)
        lv2 = s.state.get("level")
        after = Path(own).stat().st_size if Path(own).exists() else 0
    finally:
        s.close()
    ok = first and second and lv1 == lv2 == 0x0B and after == size > 0
    print(f"{'ok  ' if ok else 'FAIL'} continue_own_slot: first {first} "
          f"level {lv1}, own slot {size} bytes, second {second} level {lv2}, "
          f"after {after} bytes")
    return ok


def continue_another_file(probe):
    """(build 113) Kain's checkpoint on Dezolis, then the Biosystems Lab's:
    the second CONTINUE lands in the lab, not on Dezolis again."""
    s = play.Session("probe_check_another", probe=probe)
    try:
        first = play.continue_game(s, "routes/saves/story_kain.json")
        lv1 = s.state.get("level")
        second = play.continue_game(s, "routes/saves/biolab_recorder.json")
        lv2 = s.state.get("level")
    finally:
        s.close()
    ok = first and second and lv1 == 0x0B and lv2 == 0x32
    print(f"{'ok  ' if ok else 'FAIL'} continue_another_file: first {first} "
          f"level {lv1} (want 11), second {second} level {lv2} (want 50)")
    return ok


def missing_asset(probe, sheet="title_copyright_art.png"):
    """(build 157) A sheet missing at start: exit 2, game_init's message on
    stderr, the file named in the route's log."""
    probe = str(Path(probe).resolve())
    with tempfile.TemporaryDirectory(prefix="probe_check_") as scratch:
        tree = Path(scratch)
        shutil.copytree("assets", tree / "assets",
                        ignore=lambda d, names: [n for n in names
                                                 if n == sheet])
        (tree / "routes").mkdir()
        shutil.copy("routes/new_game.route", tree / "routes")
        run = subprocess.run([probe, "routes/new_game.route"], cwd=tree,
                             capture_output=True, text=True, timeout=120)
        log_path = tree / "route_out/new_game/log.txt"
        log = log_path.read_text() if log_path.exists() else ""
    said = "game_init failed" in run.stderr
    named = f"cannot read assets/tiles/{sheet}" in log
    ok = run.returncode == 2 and said and named
    print(f"{'ok  ' if ok else 'FAIL'} missing_asset: exit {run.returncode} "
          f"(want 2), message {'given' if said else 'not given'}, "
          f"{sheet} {'named' if named else 'not named'} in the log")
    return ok


def gameover_twice(probe):
    """(build 157a) The frame the battle hands over to game over on, shot
    in two game overs one after the other: the battle both times."""
    s = play.Session("probe_check_gameover", probe=probe)
    floors = []
    try:
        for n in (1, 2):
            s.send("start battle $103", "party 1 1", "limit 4000",
                   "wait idle", "press c", "wait idle", "press c")
            for _ in range(3000):
                s.send("wait ticks 1")
                if s.state.get("screen") == "gameover":
                    break
            out = s.send(f"shot switch_{n}",
                         "expect picture 160 112 battle_floor")
            floors.append(not any("FAIL" in line for line in out))
            s.send("wait screen gameover", "wait ticks 400", "press c",
                   "wait screen sega")
    finally:
        s.close()
    shots = [Path(f"route_out/{s.name}/switch_{n}.png") for n in (1, 2)]
    same = all(p.exists() for p in shots)         and shots[0].read_bytes() == shots[1].read_bytes()
    ok = same and all(floors)
    print(f"{'ok  ' if ok else 'FAIL'} gameover_twice: the battle's floor "
          f"{'drawn' if all(floors) else 'not drawn'} at both hand-overs, "
          f"the two frames {'the same' if same else 'different'}")
    return ok


def start_new_again(probe):
    """(build 170) A second `start new`, at the title's menu: the reset,
    the SEGA screen, then the title."""
    s = play.Session("probe_check_start_new", probe=probe)
    try:
        s.send("start new", "wait screen title", "wait idle", "press start",
               "wait screen intro")
        before = s.state.get("screen")
        s.send("start new")
        after = s.state.get("screen")
        s.send("wait screen title")
        title = s.state.get("screen")
    finally:
        s.close()
    ok = before == "intro" and after == "sega" and title == "title"
    print(f"{'ok  ' if ok else 'FAIL'} start_new_again: {before}, then "
          f"{after} after the second start new (want sega), then {title}")
    return ok


class _TooMany(Exception):
    pass


def ready_at_the_wipe_screens(probe, most=5):
    """(build 108) Session.ready() on the screens a wipe-out leaves -- the
    SEGA screen (`start new` powers on there) and the title -- gives up
    within `most` commands. Before, it handed the title to press_through
    up to 40 times, each up to 200 C presses the title never answers: a
    COMPUTER PLAYS run sat at the title that way after a wipe-out. The
    sends are counted, and past `most` the check stops with a FAIL rather
    than play the thousands out."""
    s = play.Session("probe_check_wiped", probe=probe)
    sent = [0]
    send = s.send

    def counted(*cmds):
        sent[0] += 1
        if sent[0] > most:
            raise _TooMany()
        return send(*cmds)

    seen = []
    try:
        for target in ("sega", "title"):
            send("start new") if target == "sega" else \
                send("wait screen title")
            where = s.state.get("screen")
            sent[0] = 0
            s.send = counted
            try:
                got = s.ready()
            except _TooMany:
                got = f"still going after {most} commands"
            s.send = send
            seen.append((target, where, got, sent[0]))
    finally:
        s.send = send
        s.close()
    ok = all(w == t and g is False for t, w, g, _ in seen)
    print(f"{'ok  ' if ok else 'FAIL'} ready_at_the_wipe_screens: "
          + "; ".join(f"{w}: ready() {g}" if isinstance(g, str)
                      else f"{w}: ready() {g} after {n} commands"
                      for _, w, g, n in seen))
    return ok


def new_game_languages(probe):
    """(build 168e) Session.new_game() in each language: ENGLISH 1989 the
    route new_game.route tick for tick (the same tick and state at the
    street), and the retranslation and JAPANESE on the street too, the
    party Rolf and Nei, the Governor's mission ($1105; the Japanese
    cartridge's $1106 is empty, its words all in $1105) and the house's
    last ($0907) shown. On 168d the driver ran the route in every language:
    the retranslation's Governor still talking at its twelfth C, and the
    Japanese name board typed over with the stock board's moves."""
    def run(language, how):
        s = play.Session(f"probe_check_new_{language}", probe=probe)
        try:
            if language != "us":
                s.send(f"language {language}")
            how(s)
            st = s.state
            return {"tick": st.get("tick"), "screen": st.get("screen"),
                    "wait": st.get("wait"),
                    "party": [m["name"] for m in st.get("party") or []],
                    "cell": (st.get("level"), st.get("x"), st.get("y")),
                    "said": 0x1105 in s.shown and 0x0907 in s.shown}
        finally:
            s.close()

    route = run("us", lambda s: s.route("routes/new_game.route"))
    seen = [("us", run("us", lambda s: s.new_game()))]
    for language in ("retranslation", "ja"):
        seen.append((language, run(language, lambda s: s.new_game())))
    street = all(r["screen"] == "level" and r["wait"] == "control"
                 and r["party"] == ["Rolf", "Nei"] and r["said"]
                 for _, r in seen)
    ok = street and seen[0][1] == route
    print(f"{'ok  ' if ok else 'FAIL'} new_game_languages: the route at "
          f"tick {route['tick']}; "
          + "; ".join(f"{lang} tick {r['tick']} {r['screen']} {r['wait']} "
                      f"{'+'.join(r['party'])}"
                      + ("" if r["said"] else " (not all said)")
                      for lang, r in seen))
    return ok


# (build 162) What COMPUTER PLAYS's driver runs, and what each newer
# Python added that 3.8 lacks: a method by name, or a keyword of a call.
DRIVER_MODULES = ("computer_plays.py", "play.py", "story.py",
                  "playthrough.py", "gen_script_ids.py")
NEWER_METHODS = {"removeprefix": "3.9", "removesuffix": "3.9",
                 "is_relative_to": "3.9", "with_stem": "3.9",
                 "readlink": "3.9", "randbytes": "3.9",
                 "bit_count": "3.10", "hardlink_to": "3.10"}
NEWER_KEYWORDS = {("write_text", "newline"): "3.10",
                  ("read_text", "newline"): "3.13",
                  ("zip", "strict"): "3.10",
                  ("dataclass", "slots"): "3.10",
                  ("dataclass", "kw_only"): "3.10"}


def driver_on_python38():
    """(build 162) The driver's modules as Python 3.8 reads them: each
    parses with ast's feature_version (3, 8), and none calls a method or
    passes a keyword NEWER_METHODS or NEWER_KEYWORDS names."""
    import ast
    here = Path(__file__).resolve().parent
    found = []
    for name in DRIVER_MODULES:
        text = (here / name).read_text(encoding="utf-8")
        try:
            tree = ast.parse(text, name, feature_version=(3, 8))
        except SyntaxError as e:
            found.append(f"{name}:{e.lineno}: not Python 3.8 ({e.msg})")
            continue
        for node in ast.walk(tree):
            if not isinstance(node, ast.Call):
                continue
            f = node.func
            called = f.attr if isinstance(f, ast.Attribute) else (
                f.id if isinstance(f, ast.Name) else None)
            if isinstance(f, ast.Attribute) and called in NEWER_METHODS:
                found.append(f"{name}:{node.lineno}: .{called}() is "
                             f"Python {NEWER_METHODS[called]}'s")
            for kw in node.keywords:
                need = NEWER_KEYWORDS.get((called, kw.arg))
                if need:
                    found.append(f"{name}:{node.lineno}: {called}'s "
                                 f"{kw.arg} is Python {need}'s")
    print(f"{'ok  ' if not found else 'FAIL'} driver_on_python38: "
          + (f"{len(DRIVER_MODULES)} modules" if not found
             else "; ".join(found)))
    return not found


if __name__ == "__main__":
    probe = "./route_probe"
    if "--probe" in sys.argv:
        probe = sys.argv[sys.argv.index("--probe") + 1]
    good = continue_own_slot(probe)
    good = ready_at_the_wipe_screens(probe) and good
    good = continue_another_file(probe) and good
    good = missing_asset(probe) and good
    good = gameover_twice(probe) and good
    good = driver_on_python38() and good
    good = new_game_languages(probe) and good
    good = start_new_again(probe) and good
    print("PASS" if good else "FAIL")
    sys.exit(0 if good else 1)
