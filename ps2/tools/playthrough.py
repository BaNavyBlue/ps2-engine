#!/usr/bin/env python3
"""(build 98) The whole game, a new game to the ending screen, by story.py's
rules -- one seed, one game.

    make route_probe
    python3 tools/playthrough.py --seed 1
    python3 tools/playthrough.py --seed 1 --from dams     # this run's own
                                                           # checkpoint
    python3 tools/playthrough.py --seed 2 --to climatrol
    python3 tools/playthrough.py --seeds 1-12 --jobs 12   # (build 114) each
                                                           # as --seed plays it
    PS2_TEXT_CHECK=1 python3 tools/playthrough.py --seeds 1-12 --jobs 12 \
        --language ja                                      # (build 168f)

The rules are story.py's stages, unchanged: grind and gear in every town,
the chests, the party chosen by the numbers, fight_smart in every fight,
the pipes and the hospital when hurt, the bosses with BOSS_PARTY. Two
things differ from `story.py`:

  The seed. play.Session.jitter is a player's pauses: before a walk or a
  press on the map or in a fight, one time in four, 1-24 ticks with nothing
  pressed (never inside a .route file). The RNG turns every frame, so
  every fight met, every blow and every theft after a pause differs from
  the next seed's. Each stage try draws from its own stream
  ("seed/stage/try"), so a retry is not a replay.

  Lean grinding (play.LEAN). A grind that runs out of trips, or a stage's
  time budget, short of its target goes on with the levels it has, healed
  and geared in town, instead of failing the stage; Shir's thefts are a
  bonus, not a wall. The Prism dungeons' time budget is lifted: the Nei
  items are the story, not a grind.

A stage that does not finish is played again from the checkpoint before it
(a game over included), up to --tries times, each try on a new stream. A
probe that stalls or dies is restarted and counted as a try, and reported:
that is the port's, not the harness's.

(build 168f) --language KEY plays it in that language (lang.json's keys:
us, retranslation, ja), the probe told at each start, into
route_out/playthrough_s<seed>_<KEY>/ (us keeps the old name). With
PS2_TEXT_CHECK set, the game says on stderr each line too wide for its
window and each empty message asked for (window.c's text check): the
report's port stderr.

Everything goes under route_out/playthrough_s<seed>/: report.txt (a line a
stage, the tries that did not finish, harness errors, probe stalls, the
lean go-ons, the port's stderr), story.txt (every line the stages logged),
checkpoints/story_<stage>.json (CONTINUE any of them; a --from run writes
into the same directory), and the probe's session, probe/ (fail_<stage>_
<try>.png for each try that did not finish, the bosses' and the ending's
shots; its log.txt is scratch, not packed). A run --from STAGE keeps its
own: report_from_<stage>.txt, story_from_<stage>.txt, probe_from_<stage>/.
A seed replays exactly (build 99): the same seed from the same save plays
the same game, fight for fight. Nothing is decided by the clock -- the Kain
and Noah grinds count trips (story.KAIN_TRIPS, NOAH_TRIPS), Shir's thefts
count store exits (SHIR_EXITS, NOAH_THEFT_EXITS), the Prism dungeons have
no limit -- and the run pins PYTHONHASHSEED (it restarts itself with 0),
since a few choices take the first of equals from a set of names. Up to
build 98 the wall-clock budgets made a seed reproducible only up to where
one ran out, and the hash seed changed tie-breaks from run to run.
"""
import argparse
import concurrent.futures
import json
import os
import subprocess
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import play                                                 # noqa: E402
import story                                                # noqa: E402

HASH_SEED = "0"


def open_session(base, tag, restarts, pace="probe", language=None):
    """The probe's session: route_out/<base>/probe<tag>[_r<n>] -- a run
    --from a stage keeps its own, so it never overwrites the first run's.
    (build 168f) `language`: the probe's from its first tick, as the menu's
    LANGUAGE sets it (a new game and a CONTINUE keep it)."""
    name = f"{base}/probe{tag}" + (f"_r{restarts}" if restarts else "")
    s = play.Session(name)
    s.set_pace(pace)
    if language is not None:
        s.send(f"language {language}")
    return s


def tick_totals(s):
    """(build 101) The report's tick line: ticks played, in fights, on the
    victory's pages, and the pauses (all, and in fights)."""
    fight = sum(f["ticks"] for f in s.fights)
    vic = [f["victory_ticks"] for f in s.fights
           if f.get("victory_ticks") is not None]
    return (f"ticks {s.ticks}; in fights {fight}"
            + (f" ({round(fight / len(s.fights))} a fight)" if s.fights else "")
            + f"; victory pages {sum(vic)}"
            + (f" ({round(sum(vic) / len(vic))} a win)" if vic else "")
            + f"; paused {s.jitter_ticks} ({s.jitter_fight_ticks} in fights)"
            + f"; pace {s.pace}")


def port_stderr(s):
    """The port's own stderr lines in a session's log, distinct."""
    return set(ln.split("|", 1)[1].strip() for ln in s.events
               if "    |     " in ln)


def raise_dead(s, log):
    """Nobody dead before a checkpoint or a retry: Rever or a Moon Dew on
    the spot, then the clone lab of the town the party is in, or a retreat
    to the last save's town (clone lab and hospital). A checkpoint written
    with the dead in it sent seeds 1 and 3 on with Rolf dead -- the Esper
    mansion's door Espers and the Prism dungeons' walks both stop there."""
    party = s.state.get("party") or []
    if not any(m["dead"] for m in party):
        return True
    level = s.state.get("level")
    if s.state.get("screen") == "level" and level is not None and level < 0x10 \
            and play.BUILDING_CLONE_LAB in play.town_buildings(level):
        return story.revive_party(s, level, log)
    play.revive(s)
    if any(m["dead"] for m in s.state.get("party") or []):
        play.retreat(s, log=log)
    ok = not any(m["dead"] for m in s.state.get("party") or [])
    log("raise_dead:", "all alive" if ok else "still dead", "--",
        story.party_line(s))
    return ok


def resume_point(name, cp):
    """(build 103) Where a retry of stage `name` starts: its last save on
    the way (mid_<name>.json, play.trip_save) when this try's stage made
    one, else the checkpoint before it."""
    mid = story.SAVES / f"mid_{name}.json"
    return mid if mid.exists() else cp


def stage_begins(s, name, attempt, resumed=None):
    """(build 103) The session told which stage it plays (trip saves are
    per stage) and what the party is at its start; a first try clears a
    save on the way this stage left in an earlier run. `resumed`: the
    save on the way this try continued, whose note says what the stage
    had done by then (s.progress)."""
    if attempt == 0 and resumed is None:
        (story.SAVES / f"mid_{name}.json").unlink(missing_ok=True)
        (story.SAVES / f"mid_{name}.note.json").unlink(missing_ok=True)
    s.stage = name
    s.save_marks = play.trip_marks(s)
    s.looted = []                   # (build 106) a stage starts from a save
    s.continued = False
    s.story_point = False
    s.progress = set()
    if resumed is not None:
        note = Path(str(resumed)[:-len(".json")] + ".note.json")
        try:
            s.progress = set(json.loads(note.read_text())["progress"])
        except (OSError, ValueError, KeyError, TypeError):
            pass


def party_levels(s):
    return " ".join(f"{m['name']} L{m['level']}"
                    for m in s.state.get("party") or [])


def pin_hash_seed():
    """Restart under PYTHONHASHSEED=HASH_SEED unless already there: str
    hashes -- and so the order a set of item or member names is walked in,
    and which of two equal choices is taken -- differ per process
    otherwise, and a seed would not replay.

    (build 113) Windows has no exec: os.execve there starts a new process
    and ends this one at once, so whatever waits on this one sees it exit
    while the run goes on (and on MSYS2's Python it can crash). There the
    run is a child, waited on, its exit status passed on."""
    if os.environ.get("PYTHONHASHSEED") != HASH_SEED:
        env = dict(os.environ, PYTHONHASHSEED=HASH_SEED)
        if os.name == "nt":
            sys.exit(subprocess.call([sys.executable] + sys.argv, env=env))
        os.execve(sys.executable, [sys.executable] + sys.argv, env)


def without(argv, options):
    """(build 114) argv less each of `options` and its value, given as
    `--opt V` or `--opt=V`."""
    out = []
    skip = False
    for a in argv:
        if skip:
            skip = False
            continue
        if a.split("=", 1)[0] in options:
            skip = "=" not in a
            continue
        out.append(a)
    return out


def seed_list(text):
    """(build 114) `1,2,5-8` -> [1, 2, 5, 6, 7, 8]."""
    out = []
    for part in text.split(","):
        a, _, b = part.partition("-")
        out += list(range(int(a), int(b) + 1)) if b else [int(a)]
    return out


def run_seeds(seeds, jobs, argv):
    """(build 114) Each seed a child playthrough, `--seed N` with the rest
    of this command line, up to `jobs` at once: the same process a seed
    run alone is, writing only under its own route_out/playthrough_s<N>/,
    so a seed plays the same game whatever runs beside it. A child's
    stdout (every line the stages log, which story<tag>.txt keeps too) is
    held, and its report's first line printed as it ends, in seed order."""
    env = dict(os.environ, PYTHONHASHSEED=HASH_SEED)

    def play_seed(seed):
        return subprocess.run([sys.executable, __file__, "--seed", str(seed)]
                              + argv, env=env, capture_output=True, text=True)

    status = 0
    with concurrent.futures.ThreadPoolExecutor(max_workers=max(1, jobs)) as pool:
        for seed, p in zip(seeds, pool.map(play_seed, seeds)):
            head = [ln for ln in p.stdout.splitlines()
                    if ln.startswith("playthrough seed ")]
            print(f"seed {seed}: exit {p.returncode} -- "
                  + (head[-1] if head else "no report"), flush=True)
            if p.returncode != 0 and p.stderr.strip():
                print("\n".join("   " + ln
                                for ln in p.stderr.strip().splitlines()[-8:]),
                      flush=True)
            status = max(status, p.returncode)
    return status


def main():
    names = [n for n, _ in story.STAGES]
    ap = argparse.ArgumentParser()
    ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("--seeds", default=None, metavar="LIST",
                    help="(build 114) several seeds, `1,2,5-8`, each a "
                         "playthrough of its own as --seed plays it, --jobs "
                         "at once; the other options go to every one")
    ap.add_argument("--jobs", type=int, default=1,
                    help="(build 114) with --seeds: seeds played at once")
    ap.add_argument("--from", dest="start", choices=names,
                    help="CONTINUE this seed's checkpoint of that stage and "
                         "go on from the stage after it")
    ap.add_argument("--to", choices=names, default=names[-1])
    ap.add_argument("--tries", type=int, default=None,
                    help="tries a stage (4; 6 with --squeak)")
    ap.add_argument("--no-lean", action="store_true",
                    help="story.py's grind targets as walls")
    ap.add_argument("--squeak", action="store_true",
                    help="(build 118) squeak-by, the default: every level "
                         "target lower (story.squeak), a failed try's retry "
                         "after a top-up of the latest grind (story.top_up)")
    ap.add_argument("--no-squeak", action="store_true",
                    help="(build 118) the story's own targets, as before")
    ap.add_argument("--no-kain", action="store_true",
                    help="(build 119) Noah and the bosses with Rolf, Rudo, "
                         "Amy and Anna: no Kain (story.no_kain)")
    ap.add_argument("--pace", choices=sorted(play.PACES), default="probe",
                    help="the fight's pace (play.PACES): probe, as every "
                         "route and seed since build 60, or watch, COMPUTER "
                         "PLAYS's -- the window's game on the probe")
    ap.add_argument("--fail-once", default=None, metavar="STAGE",
                    help="(build 103) a test's: STAGE's first try is counted "
                         "as not finished after it plays, to force the retry "
                         "(as computer_plays.py's)")
    ap.add_argument("--language", default=None, metavar="KEY",
                    help="(build 168f) the game's language, lang.json's key "
                         "(us, retranslation, ja): route_out/playthrough_s"
                         "<seed>_<KEY>/")
    args = ap.parse_args()
    if args.seeds:
        return run_seeds(seed_list(args.seeds), args.jobs,
                         without(sys.argv[1:], ("--seed", "--seeds", "--jobs")))
    pin_hash_seed()
    if not args.no_squeak:
        story.squeak()
    if args.no_kain:
        story.no_kain()
    tries = args.tries if args.tries is not None \
        else (story.SQUEAK_TRIES if story.SQUEAK else 4)

    base = f"playthrough_s{args.seed}" \
        + (f"_{args.language}" if args.language not in (None, "us") else "")
    out = Path("route_out") / base
    out.mkdir(parents=True, exist_ok=True)
    story.SAVES = out / "checkpoints"
    story.PRISM_BUDGET = None           # (build 99) no clock: a seed replays
    play.LEAN = not args.no_lean
    play.MID_SAVES = story.SAVES        # (build 103) saves on the way

    tag = f"_from_{args.start}" if args.start else ""
    log = story.Report()
    restarts = 0
    s = open_session(base, tag, restarts, args.pace, args.language)
    first = 0
    cp = None
    if args.start:
        first = names.index(args.start) + 1
        cp = story.SAVES / f"story_{args.start}.json"
        if not play.continue_game(s, str(cp)):
            log(f"could not continue {cp}")
            return 2
        log(f"continued {cp}:", story.party_line(s))
    last = names.index(args.to)
    rows = []
    stalls = []
    failed_tries = []
    earlier_stderr = set()          # from sessions a restart closed
    harness_errors = []
    status = 0
    try:
        for name, fn in story.STAGES[first:last + 1]:
            done = False
            for attempt in range(tries):
                s.jitter = play.Jitter(f"{args.seed}/{name}/{attempt}")
                if attempt > 0:
                    rp = resume_point(name, cp)
                    if rp is None:
                        pass                    # paseo starts a new game
                    elif not play.continue_game(s, str(rp)):
                        log(f"{name}: could not continue {rp}")
                        break
                    elif rp is not cp:
                        log(f"{name}: resumed from its last save on the way,"
                            f" {rp} -- {story.party_line(s)}")
                    s.careful = s.hurt = False
                    raise_dead(s, log)
                    if story.SQUEAK and story.top_up(s, name, log, attempt):
                        # (build 119) A top-up can end with the dead, or
                        # wiped out (seed 2's Dark Force, Noah 39/28): the
                        # save again if wiped, its levels gone; the dead
                        # raised either way before the stage.
                        if s.state.get("screen") in play.WIPED \
                                and rp is not None:
                            play.continue_game(s, str(rp))
                        raise_dead(s, log)
                stage_begins(s, name, attempt,
                             rp if attempt > 0 and rp is not cp else None)
                log(f"--- {name} (try {attempt + 1}) ---")
                t0 = time.time()
                f0, j0, k0 = len(s.fights), s.jitter_ticks, (s, s.ticks)
                ok = False
                try:
                    ok = fn(s, log)
                    if ok and attempt == 0 and name == args.fail_once:
                        log(f"{name}: --fail-once: this try counted as not "
                            "finished")
                        ok = False
                except (KeyError, TypeError, ValueError, IndexError,
                        AttributeError) as e:
                    # A harness bug, not the port's: this try is lost, the
                    # next starts from the checkpoint as any other would.
                    import traceback
                    where = traceback.extract_tb(e.__traceback__)[-1]
                    log(f"{name}: harness error {type(e).__name__}: {e} at "
                        f"{Path(where.filename).name}:{where.lineno}")
                    harness_errors.append((name, attempt + 1,
                                           f"{type(e).__name__} {e} at "
                                           f"{Path(where.filename).name}:"
                                           f"{where.lineno}"))
                except (play.Stall, EOFError) as e:
                    stalls.append((name, attempt + 1, str(e)[:300]))
                    log(f"{name}: the probe stalled or died: {e}")
                    try:
                        s.p.kill()
                    except OSError:
                        pass
                    restarts += 1
                    earlier_stderr |= port_stderr(s)
                    jit = s.jitter
                    s = open_session(base, tag, restarts, args.pace,
                                     args.language)
                    s.jitter = jit
                    rp = resume_point(name, cp)
                    if rp is not None and not play.continue_game(s, str(rp)):
                        log(f"{name}: could not continue {rp} after a restart")
                        break
                secs = round(time.time() - t0, 1)
                if ok and name != names[-1]:
                    raise_dead(s, log)
                    want = story.SAVES / f"story_{name}.json"
                    story.checkpoint(s, name, s.name, log)
                    # (build 105) The checkpoint walks to a Data Memory when
                    # the level has none, and its fights can kill: seed 7's
                    # `nido` ended alive in Nido, walked to Paseo's and saved
                    # Rolf dead, so `darum`'s first try failed at once. Raise
                    # them there and save again (no walk this time).
                    if any(m["dead"] for m in s.state.get("party") or []):
                        log(f"{name}: the walk to the save left the dead")
                        if raise_dead(s, log):
                            story.checkpoint(s, name, s.name, log)
                    if want.exists() and want.stat().st_mtime >= t0:
                        cp = want
                    else:
                        # No save: the next stage would start from nothing
                        # (seed 2's Dark Force, won and stuck on Noah).
                        log(f"{name}: finished, but no checkpoint written")
                        ok = False
                if ok:
                    (story.SAVES / f"mid_{name}.json").unlink(missing_ok=True)
                    (story.SAVES / f"mid_{name}.note.json").unlink(
                        missing_ok=True)
                    rows.append((name, attempt + 1, secs, len(s.fights) - f0,
                                 s.ticks - (k0[1] if k0[0] is s else 0),
                                 s.jitter_ticks - j0, party_levels(s)))
                    done = True
                    break
                shot = f"fail_{name}_{attempt + 1}"
                try:
                    s.send(f"shot {shot}")
                except (play.Stall, EOFError):
                    pass
                failed_tries.append((name, attempt + 1, secs,
                                     s.state.get("screen"),
                                     story.party_line(s)))
                log(f"{name}: try {attempt + 1} did not finish --",
                    s.state.get("screen"), story.party_line(s))
            if not done:
                log(f"stage {name} did not finish in {tries} tries")
                status = 1
                break
    finally:
        rep = [f"playthrough seed {args.seed}"
               + (f" from {args.start}" if args.start else " from a new game")
               + f", lean {'on' if play.LEAN else 'off'}"
               + (", squeak-by" if story.SQUEAK else "")
               + (", no Kain" if story.NO_KAIN else "")
               + (f", language {args.language}" if args.language else "")
               + ", "
               f"{round(time.time() - log.t0)} s", ""]
        rep.append("| stage | try | s | fights | ticks | paused ticks "
                   "| party after |")
        rep.append("|---|---|---|---|---|---|---|")
        for r in rows:
            rep.append("| " + " | ".join(str(x) for x in r) + " |")
        rep.append("")
        rep.append(f"fights {len(s.fights)}; paused ticks {s.jitter_ticks}; "
                   f"members down {len(s.downs)}, brought back "
                   f"{len(s.revivals)}")
        rep.append(tick_totals(s))
        for how, n in sorted(story.revival_tally(s).items(),
                             key=lambda kv: -kv[1]):
            rep.append(f"   {n} x {how}")
        rep.append(f"tries that did not finish: {len(failed_tries)}")
        for f in failed_tries:
            rep.append(f"   {f[0]} try {f[1]} ({f[2]} s): {f[3]} -- {f[4]}")
        rep.append(f"harness errors: {len(harness_errors)}")
        for h in harness_errors:
            rep.append(f"   {h[0]} try {h[1]}: {h[2]}")
        rep.append(f"probe stalls or deaths: {len(stalls)}")
        for st in stalls:
            rep.append(f"   {st[0]} try {st[1]}: {st[2]}")
        lean = [ln for ln in log.lines if ln.startswith("lean:")]
        rep.append(f"lean go-ons: {len(lean)}")
        for ln in lean:
            rep.append("   " + ln)
        stderr = sorted(earlier_stderr | port_stderr(s))
        rep.append(f"port stderr (distinct): {len(stderr)}")
        for ln in stderr:
            rep.append("   " + ln)
        rep.append("finished: " + ("the ending" if status == 0
                                   and last == len(names) - 1 else
                                   f"through {args.to}" if status == 0
                                   else "no"))
        play.write_lf(out / f"report{tag}.txt", "\n".join(rep) + "\n")
        play.write_lf(out / f"story{tag}.txt", "\n".join(log.lines) + "\n")
        print("\n".join(rep[:2]), flush=True)
        try:
            s.close()
        except (OSError, ValueError):
            pass
    return status


if __name__ == "__main__":
    sys.exit(main())
