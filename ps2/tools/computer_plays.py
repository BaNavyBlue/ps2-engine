#!/usr/bin/env python3
"""(build 100b) COMPUTER PLAYS's driver: the campaign by the harness's rules,
played in the game's own window. The game starts this as a child process:

    python3 tools/computer_plays.py --attach 3,4 --start new --out saves/computer
    python3 tools/computer_plays.py --attach 3,4 --start saves/slot2.json ...

and runs its commands -- the route language, over file descriptor 3, the
log back on 4 -- with the probe's own interpreter, a tick at a time through
the frame loop (docs/computer_plays.md). (build 115) On Windows, where a
child has no descriptors past 2, --attach names the two pipes' handles
instead, opened here as descriptors. Everything above the pipes is the
harness as it is: story.py's stages in playthrough.py's loop, a seed for the
player's pauses, lean grinding, a stage retried from its checkpoint.

What differs from playthrough.py is only what a player could not do:

  * The game refuses every command that is not a player's; no stage sends
    one but `start slot` and `save-here` (docs/computer_plays.md, the
    audit).
  * A checkpoint the harness would have made with `save-here` -- the ends of
    the roof and shir stages, in the Skure field, where a player has no
    save -- is not made: the stage counts as played, and a retry of the
    stage after it CONTINUEs the checkpoint before and plays both, the
    lost stage before squeak-by's top-up (build 180).
  * A retry's CONTINUE is the game's own: the checkpoint copied into the
    computer's own slot (its own memory card, saves/computer/slots), the
    console reset, Start and CONTINUE on the title. --no-swap CONTINUEs
    whatever the computer last saved instead.

--start SAVE: the player's save is read, never written. Its stage is told
from its story flags, recruits and kept story items against the story's own
checkpoints (routes/saves/story_<stage>.json); --first overrides.

PS2_COMPUTER_ARGS in the game's environment is added to the arguments
(`--to arima --seed 7`, for a test). Output in --out: report.txt, story.txt,
checkpoints/; the game's saves go to --out/slots.
"""
import argparse
import json
import os
import shutil
import sys
import time
from pathlib import Path

# (build 162) The oldest Python the driver runs on (autoplay.c asks the
# same of a Python before it starts one): said once, in driver.log.
if sys.version_info < (3, 8):
    sys.exit("computer plays: needs Python 3.8 or newer, not "
             + sys.version.split()[0])
sys.path.insert(0, str(Path(__file__).resolve().parent))
# (build 125) --home DIR: the stand-alone game wrote the harness's files out
# there from its pack (autoplay.c), and every relative path the harness
# reads -- assets/, routes/, src/ -- is from there. Taken before play is
# imported, which reads assets/ as it loads; argparse knows it too.
if "--home" in sys.argv[:-1]:
    os.chdir(sys.argv[sys.argv.index("--home") + 1])
import play                                                 # noqa: E402
import playthrough                                          # noqa: E402
import story                                                # noqa: E402

# DezolisSkure: the field the roof's spaceport opens on. It has no Data
# Memory; a player saves there only with the Visiphone, which the harness
# does not carry for it. The probe's `save-here` stood in.
SKURE_FIELD = 3

# Stages no player's save can start: each begins exactly where the stage
# before it ended, in the Skure field (above). A save is played on from the
# stage before instead.
NO_PICKUP = {"skure": "roof", "menobe": "shir"}

REFS = Path("routes/saves")


# Things the story hands over once and the party keeps to the end. (build
# 180) The helmet is items.json's "Neimet": spelt "NeiMet" until 179, it
# marked no save, and menobe's signature lacked it.
STORY_ITEMS = ("Prism", "NeiCrown", "Neimet", "NeiArmor", "NeiCape",
               "NeiShield", "NeiEmel", "NeiSlasher", "NeiShot", "NeiSword")


# The recruits in their order of joining (gamedata.c, write_recruit).
RECRUITS = ("none", "Nei", "Rudo", "Amy", "Hugh", "Anna", "Kain", "Shir")


def recruits_joined(save):
    """How many recruits have come to Rolf's house: the save's
    last_recruit_joined by name (build 106), or the count older saves
    kept as recruits_joined."""
    party = save.get("party") or {}
    v = party.get("last_recruit_joined", party.get("recruits_joined")) or 0
    if isinstance(v, str):
        return RECRUITS.index(v) if v in RECRUITS else 0
    return int(v)


def story_flags(save):
    """What a save has played, as marks every game of the story shares and
    none loses: the story's flag bytes set ($C700-$C77F, by address), how
    many have joined (recruits_joined(), a mark a recruit) and the story's
    kept items. Chests and levels are left out: they differ from game to
    game. Both save formats: 2 names each byte (only the c7XX prefix counts
    -- the names changed in 97a/97f), 1 is rows of hex."""
    out = set()
    for key, base in (("flags_c700", 0xC700), ("event_flags", 0xC710)):
        block = save.get(key)
        if isinstance(block, dict):
            for k, v in block.items():
                if v:
                    out.add(int(k[1:4], 16) | 0xC000)
        elif isinstance(block, list):
            raw = bytes.fromhex("".join(block))
            out |= {base + i for i, b in enumerate(raw) if b}
    out |= {f"recruit {i + 1}" for i in range(recruits_joined(save))}
    text = json.dumps(save.get("characters") or [])
    out |= {f"item {n}" for n in STORY_ITEMS if f'"{n}' in text}
    return out


def signatures(names):
    """Each stage's own marks: in its checkpoint and not in the one before
    (routes/saves/story_<stage>.json, the story's own chain). Empty for a
    stage that marks nothing (the grinds, gear, a party chosen); None with
    no checkpoint there."""
    sig = {}
    prev = set()
    for n in names:
        ref = REFS / f"story_{n}.json"
        if not ref.exists():
            sig[n] = None
            continue
        cur = story_flags(json.loads(ref.read_text()))
        sig[n] = cur - prev
        prev = cur
    return sig


def stage_of_save(path, names):
    """(stage to play first, why not, earlier stages whose marks the save
    lacks). The save has played every stage up to the last one whose own
    marks it all has; play goes on at the stage after that -- so a save at
    the end of a stage that marks nothing plays that stage again rather
    than skip one."""
    have = story_flags(json.loads(Path(path).read_text()))
    sig = signatures(names)
    last = -1
    for i, n in enumerate(names):
        if sig[n] and sig[n] <= have:
            last = i
    unmet = [n for n in names[:last + 1] if sig[n] and not sig[n] <= have]
    if last + 1 >= len(names):
        return None, "the story is played out in this save", unmet
    pick = names[last + 1]
    if pick in NO_PICKUP:
        pick = NO_PICKUP[pick]
    return pick, None, unmet


def attach_fds(spec):
    """--attach's CMD,LOG as file descriptors: the game's 3 and 4 on POSIX.
    (build 115) On Windows they are the two pipes' handles, which the game
    handed down (autoplay.c); each is opened as a descriptor here."""
    cmd, log = (int(x) for x in spec.split(","))
    if os.name == "nt":
        import msvcrt
        return (msvcrt.open_osfhandle(cmd, os.O_WRONLY | os.O_BINARY),
                msvcrt.open_osfhandle(log, os.O_RDONLY | os.O_BINARY))
    return cmd, log


def main():
    playthrough.pin_hash_seed()
    names = [n for n, _ in story.STAGES]
    fns = dict(story.STAGES)
    ap = argparse.ArgumentParser()
    ap.add_argument("--attach", required=True,
                    help="CMD,LOG: the game's file descriptors (on "
                         "Windows, its pipes' handles)")
    ap.add_argument("--start", default="new",
                    help="new, last (build 103: the computer's last run, "
                         "OUT/last.json), or a save to CONTINUE (read only)")
    ap.add_argument("--out", default="saves/computer")
    ap.add_argument("--home", default=None,
                    help="(build 125) the folder the stand-alone game wrote "
                         "the harness out to; relative paths from there")
    ap.add_argument("--seed", type=int, default=None,
                    help="the player's pauses (default: from the clock)")
    ap.add_argument("--first", choices=names, default=None,
                    help="the stage to play first from --start SAVE")
    ap.add_argument("--to", choices=names, default=names[-1])
    ap.add_argument("--tries", type=int, default=None,
                    help="tries a stage (4; 6 with --squeak)")
    ap.add_argument("--squeak", action="store_true",
                    help="(build 118) squeak-by, as playthrough.py's and "
                         "the default: lower targets, a top-up before a "
                         "retry")
    ap.add_argument("--no-squeak", action="store_true",
                    help="(build 118) the story's own targets, as before")
    ap.add_argument("--no-swap", action="store_true",
                    help="a retry CONTINUEs the computer's last own save")
    ap.add_argument("--pace", choices=sorted(play.PACES), default="watch",
                    help="the fight's pace (play.PACES); watch by default: "
                         "a menu or a page answered the moment it is up")
    ap.add_argument("--fail-once", default=None, metavar="STAGE",
                    help="a test's: STAGE's first try is counted as not "
                         "finished after it plays, to force the retry "
                         "(build 101)")
    ap.add_argument("--classify", action="store_true",
                    help="print the stage --start SAVE would begin at, and "
                         "stop (no game needed)")
    args = ap.parse_args()

    if args.classify:
        pick, why, unmet = stage_of_save(args.start, names)
        print(pick or f"none: {why}", "(earlier stages unmet: "
              + (", ".join(unmet) or "none") + ")")
        return 0

    if not args.no_squeak:
        story.squeak()
    tries = args.tries if args.tries is not None \
        else (story.SQUEAK_TRIES if story.SQUEAK else 4)
    cmd_fd, log_fd = attach_fds(args.attach)
    out = Path(args.out)
    (out / "checkpoints").mkdir(parents=True, exist_ok=True)
    story.SAVES = out / "checkpoints"
    story.PRISM_BUDGET = None
    play.LEAN = True
    play.MID_SAVES = story.SAVES        # (build 103) saves on the way
    last_run = None
    if args.start == "last":
        # (build 103) The computer's last run, as last.json names it: a
        # save on the way (that stage again, from there) or a stage's
        # checkpoint (the stage after it). The directory was not cleared.
        try:
            last_run = json.loads((out / "last.json").read_text())
        except (OSError, ValueError):
            print("computer plays: no last run to go on from", flush=True)
            return 2
    seed = args.seed if args.seed is not None else (
        int(last_run.get("seed", 0)) if last_run
        else int(time.time()) % 100000)

    def note_last(kind, stage, path):
        """(build 103) Where a later COMPUTER PLAYS can go on from."""
        play.write_lf(out / "last.json", json.dumps(
            {"stage": stage, "kind": kind, "save": str(path),
             "seed": seed}) + "\n")

    play.ON_SAVE = note_last

    log = story.Report()
    s = play.Session("computer", attach=(cmd_fd, log_fd))
    s.set_pace(args.pace)
    s.saves_dir = out / "slots"
    rows, failed, errors = [], [], []
    status = "no"
    start_line = f"computer plays seed {seed} from {args.start}"

    def cont(cp):
        """CONTINUE the checkpoint (or, --no-swap, the last own save)."""
        s.careful = s.hurt = False
        return play.continue_game(s, None if args.no_swap else str(cp))

    resumed_first = None    # (build 103) the last run's save on the way

    def replay(pending, name, attempt, tag=""):
        """The stages played since the checkpoint that left none, again."""
        for pn in pending:
            log(f"--- {pn} again, before {name} (no checkpoint "
                f"between) ---")
            s.jitter = play.Jitter(f"{seed}/{name}/{attempt}/{pn}{tag}")
            if not fns[pn](s, log):
                return False
        return True

    try:
        first = 0
        cp = None
        if last_run is not None:
            stage = last_run.get("stage")
            if stage not in names:
                log(f"last run: stage {stage} is not one of the story's")
                status = "refused: the last run's stage is unknown"
                return 2
            cp = story.SAVES / "start.json"
            shutil.copy(last_run["save"], cp)
            note = Path(last_run["save"][:-len(".json")] + ".note.json")
            if last_run["kind"] == "mid" and note.exists():
                shutil.copy(note, story.SAVES / "start.note.json")
            first = names.index(stage) + (1 if last_run["kind"] == "end"
                                          else 0)
            if last_run["kind"] == "mid":
                resumed_first = cp
            if first >= len(names):
                status = "refused: the last run played the story out"
                return 2
            start_line += (f", the last run's {last_run['kind']} save of "
                           f"{stage}, at stage {names[first]}")
            if not cont(cp):
                log(f"could not continue {last_run['save']}")
                status = "no: the save would not continue"
                return 2
            log(f"continued the last run ({last_run['kind']} of {stage}) at "
                f"{names[first]}:", story.party_line(s))
        elif args.start != "new":
            pick, why, unmet = stage_of_save(args.start, names)
            if args.first:
                pick = args.first
            if pick is None:
                log(f"{args.start}: {why}")
                status = "refused: " + why
                return 2
            if unmet:
                log(f"{args.start}: played past {pick}'s stages with these "
                    f"stages' flags not all set: {', '.join(unmet)}")
            cp = story.SAVES / "start.json"
            shutil.copy(args.start, cp)
            first = names.index(pick)
            start_line += f", at stage {pick}"
            if not cont(cp):
                log(f"could not continue {args.start}")
                status = "no: the save would not continue"
                return 2
            log(f"continued {args.start} at {pick}:", story.party_line(s))
        last = names.index(args.to)
        pending = []        # stages played since cp that left no checkpoint
        for name in names[first:last + 1]:
            done = False
            for attempt in range(tries):
                s.jitter = play.Jitter(f"{seed}/{name}/{attempt}")
                ok = True
                if attempt > 0:
                    rp = playthrough.resume_point(name, cp)
                    if rp is not None and not cont(rp):
                        log(f"{name}: could not continue {rp}")
                        break
                    if rp is not cp:
                        log(f"{name}: resumed from its last save on the way,"
                            f" {rp} -- {story.party_line(s)}")
                        pending = []    # the save is past them
                    playthrough.raise_dead(s, log)
                    # (build 180) The lost stages first, then the top-up:
                    # skure's (menobe's) is Zosa's grind on Dezolis, and the
                    # checkpoint CONTINUEd is dams' (motavia's) on Motavia.
                    # Run before the flight, it reached nothing, and every
                    # try played the first again (seeds 7, 10 and 11, build
                    # 179). Now each starts where playthrough.py's does.
                    ok = replay(pending, name, attempt)
                    if pending:
                        s.jitter = play.Jitter(f"{seed}/{name}/{attempt}")
                    if ok and story.SQUEAK \
                            and story.top_up(s, name, log, attempt):
                        # (build 119) As playthrough.py's: a top-up wiped
                        # out goes back to the save; the dead raised.
                        if s.state.get("screen") in play.WIPED \
                                and rp is not None:
                            cont(rp)
                            if pending:
                                playthrough.raise_dead(s, log)
                                ok = replay(pending, name, attempt, "/again")
                        playthrough.raise_dead(s, log)
                    s.jitter = play.Jitter(f"{seed}/{name}/{attempt}")
                if attempt > 0:
                    resumed = rp if rp is not cp else None
                else:
                    resumed = resumed_first
                    resumed_first = None
                playthrough.stage_begins(s, name, attempt, resumed)
                log(f"--- {name} (try {attempt + 1}) ---")
                t0 = time.time()
                f0, k0 = len(s.fights), s.ticks
                try:
                    ok = ok and bool(fns[name](s, log))
                    if ok and attempt == 0 and name == args.fail_once:
                        log(f"{name}: --fail-once: this try counted as "
                            f"not finished")
                        ok = False
                except (KeyError, TypeError, ValueError, IndexError,
                        AttributeError) as e:
                    import traceback
                    where = traceback.extract_tb(e.__traceback__)[-1]
                    log(f"{name}: harness error {type(e).__name__}: {e}")
                    errors.append(f"{name} try {attempt + 1}: "
                                  f"{type(e).__name__} {e} at "
                                  f"{Path(where.filename).name}:"
                                  f"{where.lineno}")
                    ok = False
                if ok and name != names[-1]:
                    playthrough.raise_dead(s, log)
                    if s.state.get("screen") == "level" \
                            and s.state.get("level") == SKURE_FIELD:
                        log(f"{name}: played; no player's save in the Skure "
                            f"field, so no checkpoint -- a retry of the next "
                            f"stage plays this one again")
                        pending.append(name)
                    else:
                        want = story.SAVES / f"story_{name}.json"
                        story.checkpoint(s, name, s.name, log)
                        if want.exists() and want.stat().st_mtime >= t0:
                            cp = want
                            pending = []
                            note_last("end", name, want)
                        else:
                            log(f"{name}: finished, but no checkpoint written")
                            ok = False
                if ok:
                    (story.SAVES / f"mid_{name}.json").unlink(missing_ok=True)
                    (story.SAVES / f"mid_{name}.note.json").unlink(
                        missing_ok=True)
                    rows.append((name, attempt + 1,
                                 round(time.time() - t0, 1),
                                 len(s.fights) - f0, s.ticks - k0,
                                 playthrough.party_levels(s)))
                    done = True
                    break
                failed.append(f"{name} try {attempt + 1}: "
                              f"{s.state.get('screen')} -- "
                              f"{story.party_line(s)}")
                log(f"{name}: try {attempt + 1} did not finish")
            if not done:
                status = f"no: {name} did not finish in {tries} tries"
                # (build 123) the owner: out of tries, the pad goes back in the
                # game, never wherever the last try left it (a wipe-out's
                # title): the last save CONTINUEd first -- the stage's save
                # on the way, else the checkpoint before it.
                back = playthrough.resume_point(name, cp)
                if back is not None and cont(back):
                    log(f"{name}: out of tries; continued {back} for the "
                        f"pad -- {story.party_line(s)}")
                    status += f"; the pad back at {Path(back).name}"
                return 1
        status = "the ending" if last == len(names) - 1 \
            else f"through {args.to}"
        return 0
    except (EOFError, BrokenPipeError) as e:
        # The pad was taken back (the game closed the log pipe).
        status = f"handed back ({type(e).__name__})"
        return 0
    finally:
        rep = [start_line + ", lean on"
               + (", squeak-by" if story.SQUEAK else "")
               + f", {round(time.time() - log.t0)} s",
               "", "| stage | try | s | fights | ticks | party after |",
               "|---|---|---|---|---|---|"]
        rep += ["| " + " | ".join(str(x) for x in r) + " |" for r in rows]
        rep += ["", f"fights {len(s.fights)}; paused ticks {s.jitter_ticks}",
                playthrough.tick_totals(s),
                f"tries that did not finish: {len(failed)}"]
        rep += ["   " + f for f in failed]
        rep += [f"harness errors: {len(errors)}"] + ["   " + e for e in errors]
        rep.append(f"finished: {status}")
        play.write_lf(out / "report.txt", "\n".join(rep) + "\n")
        play.write_lf(out / "story.txt", "\n".join(log.lines) + "\n")
        print("\n".join(rep[:1] + rep[-1:]), flush=True)


if __name__ == "__main__":
    sys.exit(main())
