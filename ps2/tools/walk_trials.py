#!/usr/bin/env python3
"""The walks across Noah to the last two bosses, many times per fight
policy (build 97), in the manner of boss_trials.py.

Each run is a fresh probe: CONTINUE the walk's start save (Noah's ground
floor just after Lutz's YES), wait a different number of ticks -- the RNG
turns every frame, so each run meets different fights -- then `travel`
to the walk's end with the policy's fight options and its mending after
each fight, exactly as story.py's stage walks it. The same offsets are
used for every policy, so policies are compared on the same seeds.

The walks (--walk):

  mother_brain  story.py's mother_brain_once: to the foot of NoahF1's
                corridor (MOTHER_BRAIN_WALK_FROM). Start save from
                story_dark_force.json.
  dark_force    story.py's dark_force_once: to the cell below Pandora's
                box on the ground floor. Start save from
                story_boss_party.json (the box's flag still clear).

The start save is made once (--setup, or when it is missing): the
stage's own preparation -- Ryuon's Trimates (boss_supplies), the town,
the mansion's mending (mansion_mend) and Lutz's YES -- then the probe's
save-here, copied to routes/saves/walk_<walk>.json.

At the walk's end every policy gets the same arrival: Rever / a Moon Dew
on the dead, then mates to 0.9 (the Mother Brain stage's own), and
story.box_ready decides whether the party is fit for the boss.

The policies (--policy, any of POLICIES): a fight_smart option set for
the fights on the way, and goto's mending after each (None: the old rule,
mates to 0.6 only once someone is under half, nobody revived).

Recorded per run: reached, wiped out (a game over on the way), gave up
(neither), box_ready at the end, each member's TP there, mates left,
members dead on arrival, members down and brought back on the way
(Session.downs / revivals), fights and ticks.

    python3 tools/walk_trials.py --walk mother_brain --repeats 12
    python3 tools/walk_trials.py --walk dark_force --policy b94,b96c
"""
import argparse
import json
import shutil
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import jobs                                                 # noqa: E402
import play                                                 # noqa: E402
import story                                                # noqa: E402

ROOT = Path(__file__).resolve().parent.parent

# Build 94's walk: weapons and mates only, RUN at fight_smart's default
# (anyone alive under a quarter). Build 96c's WALK_FIGHT and WALK_MEND,
# written out here because story.py's are set from this trial (build 97)
# and a policy's name must keep meaning the same thing. The rest vary one
# threshold of 96c's at a time.
B94 = {"use_techs": False, "type_techs": ()}
B96C = {"use_techs": True, "type_techs": (), "boss_techs": False,
        "deban": False, "run_below": 0.0, "heal_below": 0.4,
        "mate_below": 0.3}


def _v(**kw):
    d = dict(B96C)
    d.update(kw)
    return d


POLICIES = {
    "b94":       (B94, None),
    "b94_norun": (dict(B94, run_below=0.0), None),
    "b96c":      (B96C, 0.5),
    "b96c_run":  (_v(run_below=0.25), 0.5),
    "heal30":    (_v(heal_below=0.3), 0.5),
    "heal50":    (_v(heal_below=0.5), 0.5),
    "mate20":    (_v(mate_below=0.2), 0.5),
    "mate45":    (_v(mate_below=0.45), 0.5),
    "mend30":    (B96C, 0.3),
    "mend70":    (B96C, 0.7),
    "mend_old":  (B96C, None),
    # Weapons alone, no RUN, and one thing changed (build 97's second
    # round, after the first said weapons alone arrive with the TP).
    "w_mend50":  (dict(B94, run_below=0.0), 0.5),
    "w_mate30":  (dict(B94, run_below=0.0, mate_below=0.3), None),
    "w_mate60":  (dict(B94, run_below=0.0, mate_below=0.6), None),
    "w_mate60_mend50": (dict(B94, run_below=0.0, mate_below=0.6), 0.5),
}


def prepare(s, log):
    """The stage up to the walk (mother_brain_once, dark_force_once): supplies, the town, the mansion's
    mending, Lutz's YES."""
    play.PIN_HEAD.add("Rolf")
    town = story.nearest_town(story.ESPER_F1)
    return (story.boss_supplies(s, log)
            and story.dezolis_town_visit(s, town, log, talk=False) is not None
            and story.mansion_mend(s, town, log)
            and (story.lutz(s, 0, log) or True)
            and s.state.get("level") == story.NOAH_G)


WALKS = {
    "mother_brain": {"from": "story_dark_force.json",
                     "level": story.NOAH_F1,
                     "goal": story.MOTHER_BRAIN_WALK_FROM},
    "dark_force":   {"from": "story_boss_party.json",
                     "level": story.NOAH_G,
                     "goal": (story.NOAH_BOX[0], story.NOAH_BOX[1] + 1)},
}


def start_save(walk):
    return ROOT / "routes" / "saves" / f"walk_{walk}.json"


def setup(walk, log):
    """The start save: the stage's preparation, then save-here on Noah's
    ground floor where Lutz sets the party down."""
    s = play.Session("walk_setup")
    try:
        if not play.continue_game(s, str(ROOT / "routes" / "saves"
                                         / WALKS[walk]["from"])):
            log("setup: could not continue", WALKS[walk]["from"])
            return False
        if not prepare(s, log):
            log("setup: not on Noah after Lutz:", s.state.get("level"))
            return False
        s.ready()
        s.send("save-here 1")
        log("setup: at", s.state.get("level"), s.state.get("x"),
            s.state.get("y"), "--", story.party_line(s), "--",
            story.tp_line(s), "--", play.count_mates(s), "mates")
    finally:
        s.close()
    src = ROOT / "route_out" / "walk_setup" / "saves" / "slot1.json"
    if not src.exists():
        log("setup: no save written")
        return False
    shutil.copy(src, start_save(walk))
    return True


def quiet(*_):
    pass


def run(walk, policy, offset, name=None):
    fight, mend = POLICIES[policy]
    w = WALKS[walk]
    s = play.Session(name or f"walk_trials_{walk}")
    try:
        if not play.continue_game(s, str(start_save(walk))):
            return {"error": "continue failed"}
        if offset:
            s.send(f"wait ticks {offset}")
        s.ready()
        tick0 = s.state.get("tick") or 0
        s.fight_opts = dict(fight, stop_on_gameover=True)
        s.walk_mend = mend
        try:
            there = play.travel(s, w["level"], {w["goal"]}, log=quiet)
        finally:
            s.fight_opts = None
            s.walk_mend = None
        st = s.state
        at = (st.get("level"), st.get("x"), st.get("y"))
        reached = bool(there) and at == (w["level"],) + tuple(w["goal"])
        wiped = st.get("screen") == "gameover" or any(
            f.get("party") and all(m["dead"] for m in f["party"])
            for f in s.fights)
        r = {"policy": policy, "offset": offset, "reached": reached,
             "wiped": wiped and not reached, "fights": len(s.fights),
             "downs": len(s.downs), "revivals": len(s.revivals),
             "ticks": (st.get("tick") or 0) - tick0}
        if not reached:
            r["where"] = at
            r["screen"] = st.get("screen")
            return r
        p = st.get("party") or []
        r["dead_on_arrival"] = sum(1 for m in p if m["dead"])
        play.revive(s)
        play.mend(s, below=0.9)
        p = s.state.get("party") or []
        r["box_ready"] = story.box_ready(s)
        r["mates"] = play.count_mates(s)
        r["tp"] = {m["name"]: [m["tp"], m["max_tp"]] for m in p
                   if m["max_tp"]}
        r["hp"] = round(sum(m["hp"] for m in p)
                        / max(1, sum(m["max_hp"] for m in p)), 3)
        r["dead_after"] = sum(1 for m in p if m["dead"])
        return r
    finally:
        s.close()


def worker_run(task):
    """(build 114) A run in a worker (jobs.ordered): its session is the
    worker's own, route_out/walk_trials_<walk>/w<N>, never two at once."""
    walk, policy, offset, _ = task
    r = run(walk, policy, offset,
            name=f"walk_trials_{walk}/w{jobs.worker()}")
    r.setdefault("policy", policy)
    return r


def mean(xs):
    return sum(xs) / len(xs) if xs else 0.0


def summary(results, policies, members):
    rows = ["| policy | runs | reached | wiped out | gave up | box_ready "
            "| mates left (mean) | "
            + " | ".join(f"{m} TP (mean)" for m in members)
            + " | dead on arrival (mean) | down / brought back (mean) "
            "| fights (mean) |",
            "|---|---|---|---|---|---|---|" + "---|" * len(members)
            + "---|---|---|"]
    for pol in policies:
        rs = [r for r in results if r.get("policy") == pol
              and "error" not in r]
        if not rs:
            continue
        got = [r for r in rs if r["reached"]]
        wiped = sum(1 for r in rs if r["wiped"])
        tps = []
        for m in members:
            v = [r["tp"][m][0] for r in got if m in r.get("tp", {})]
            mx = next((r["tp"][m][1] for r in got if m in r.get("tp", {})),
                      0)
            tps.append(f"{mean(v):.0f}/{mx}" if v else "-")
        rows.append(
            f"| {pol} | {len(rs)} | {len(got)} | {wiped} | "
            f"{len(rs) - len(got) - wiped} | "
            f"{sum(1 for r in got if r['box_ready'])} | "
            + (f"{mean([r['mates'] for r in got]):.1f}" if got else "-")
            + " | " + " | ".join(tps) + " | "
            + (f"{mean([r['dead_on_arrival'] for r in got]):.2f}"
               if got else "-")
            + f" | {mean([r['downs'] for r in rs]):.1f} / "
            f"{mean([r['revivals'] for r in rs]):.1f} | "
            f"{mean([r['fights'] for r in rs]):.1f} |")
    return "\n".join(rows)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--walk", default="mother_brain", choices=sorted(WALKS))
    ap.add_argument("--policy", default=",".join(POLICIES),
                    help="comma-separated: " + ", ".join(POLICIES))
    ap.add_argument("--repeats", type=int, default=12)
    ap.add_argument("--step", type=int, default=7,
                    help="ticks between one run's start and the next's")
    ap.add_argument("--first", type=int, default=0,
                    help="the first run's offset index (to extend a trial)")
    ap.add_argument("--append", action="store_true",
                    help="add these policies' runs to the --out results")
    ap.add_argument("--setup", action="store_true",
                    help="remake the start save first")
    ap.add_argument("--out", default=None)
    ap.add_argument("--jobs", type=int, default=1,
                    help="(build 114) runs at once, each a probe of its own "
                         "as ever; the results the same at any number")
    args = ap.parse_args()
    out = ROOT / (args.out or f"route_out/walk_trials_{args.walk}")
    log = print
    if args.setup or not start_save(args.walk).exists():
        if not setup(args.walk, log):
            return 2
    sv = json.loads(start_save(args.walk).read_text())
    members = list(sv["party"]["members"])
    policies = args.policy.split(",")
    results = []
    jpath = out.with_suffix(".json")
    if (args.first or args.append) and jpath.exists():
        results = json.loads(jpath.read_text())["results"]
    seen = {r.get("policy") for r in results} | set(policies)
    table_policies = [p for p in POLICIES if p in seen]
    t0 = time.time()
    # (build 114) The runs in their old order, each in a worker process (a
    # fresh one: nothing --setup did here reaches a run), the results in
    # that order at any --jobs.
    plan = [(args.walk, pol, k * args.step, k)
            for k in range(args.first, args.first + args.repeats)
            for pol in policies]
    try:
        for task, r in zip(plan, jobs.ordered(worker_run, plan, args.jobs)):
            results.append(r)
            short = {x: r[x] for x in r if x not in ("tp",)}
            print(f"{task[1]} {task[3]}: {short}", flush=True)
    finally:
        # Only the members with TP get a column (Rudo has none).
        tp_users = [m for m in members
                    if any(m in r.get("tp", {}) for r in results)]
        table = summary(results, table_policies, tp_users)
        # --jobs is left out: it changes nothing in the results.
        kept = {k: v for k, v in vars(args).items() if k != "jobs"}
        jpath.write_text(json.dumps({"args": kept,
                                     "results": results}, indent=1), newline="\n")
        out.with_suffix(".md").write_text(table + "\n", newline="\n")
        print(table)
        print(f"{len(results)} runs, {time.time() - t0:.0f} s")
    return 0


if __name__ == "__main__":
    sys.exit(main())
