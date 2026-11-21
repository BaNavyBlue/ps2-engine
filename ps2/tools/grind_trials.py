#!/usr/bin/env python3
"""(build 103) What a grind buys a tick, policy against policy, from one save.

    python3 tools/grind_trials.py --save CHECKPOINT --policy roam \\
        --budget 1500000 --seed 1 [--out route_out/grind_trials/x.json]

From a checkpoint (a seed's story_amy.json, story_amy_grind.json, ...) the
party grinds for `--budget` ticks under one policy, at the probe's pace with
the seed's pauses, and the tool reports the EXP each member earned, the
levels, the fights, the ticks a fight, and the wipe-outs. The policies are
the Oputa grinds' (build 102b) and the ones build 103 weighed against them:

  field     Oputa's field by grind_and_gear's little square (amy_grind,
            lab_grind): encounter group 4.
  entrance  the Biosystems Lab's ground floor (door blown first) by
            dungeon_grind on the entrance cell, left 3 / right 3 (lab_in).
  roam      the same floor by dungeon_grind with its long walks
            (story.lab_ground(), play.roam).

EXP is the victory's share (loc_F270, ps2.asm:22407: the formation's EXP
over the members alive at the end, plus one, to each of them), counted from
the formation of every fight won and the party right after it.
"""
import argparse
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import play                                                 # noqa: E402
import story                                                # noqa: E402


class Budget(Exception):
    pass


def formation_exp(f):
    form = play.formation_of(f)
    if not form:
        return 0
    total = 0
    for k in (1, 2):
        e = play.enemy(form[f"enemy{k}"])
        if e and form[f"count{k}"]:
            total += form[f"count{k}"] * e["exp"]
    return total


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--save", required=True)
    ap.add_argument("--policy", choices=("field", "entrance", "roam"),
                    required=True)
    ap.add_argument("--budget", type=int, default=1_500_000)
    ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("--session", default="grind_trials/probe")
    ap.add_argument("--out", default=None)
    args = ap.parse_args()

    play.LEAN = True
    s = play.Session(args.session)
    s.jitter = play.Jitter(f"{args.seed}/grind_trials/{args.policy}")
    if not play.continue_game(s, args.save):
        print("could not continue", args.save)
        return 2
    lines = []

    def log(*a):
        lines.append(" ".join(str(x) for x in a))
    before = {m["name"]: m["level"] for m in s.state.get("party") or []}
    exp = {n: 0 for n in before}
    rec = {"fights": 0, "won": 0, "wipes": 0, "t0": s.ticks}
    real = play.fight_smart

    def fight(ss, *a, **k):
        n0 = len(ss.fights)
        out = real(ss, *a, **k)
        rec["fights"] += 1
        if len(ss.fights) > n0 and ss.fights[-1].get("victory_ticks") \
                is not None:
            rec["won"] += 1
            alive = [m["name"] for m in ss.state.get("party") or []
                     if not m["dead"]]
            if alive:
                share = formation_exp(ss.fights[-1]["formation"]) \
                    // len(alive) + 1
                for n in alive:
                    exp[n] = exp.get(n, 0) + share
        if ss.state.get("screen") in ("gameover", "sega", "title"):
            rec["wipes"] += 1
        if ss.ticks - rec["t0"] >= args.budget:
            raise Budget()
        return out

    play.fight_smart = fight
    story.fight_smart = fight
    ok = None
    try:
        if not play.enter_town(s, story.OPUTA):
            print("could not reach Oputa")
            return 2
        play.heal_here(s, story.OPUTA)
        play.gear_up(s, story.OPUTA, log=log)
        if args.policy == "field":
            ok = play.grind_and_gear(s, story.OPUTA, 99, trips=100000,
                                     log=log, lowest=True)
        else:
            play.resupply(s, door=play.town_buildings(story.OPUTA)[
                play.BUILDING_ITEM][0], level=story.OPUTA)
            if not story.blow_door(s, story.LAB_G, (47, 74), log):
                print("could not blow the door")
                return 2
            ground = story.lab_ground() if args.policy == "roam" else None
            ok = play.dungeon_grind(s, story.LAB_G, story.LAB_SPOT,
                                    story.OPUTA, 99, trips=100000, log=log,
                                    ground=ground)
    except Budget:
        pass
    ticks = s.ticks - rec["t0"]
    after = {m["name"]: m["level"] for m in s.state.get("party") or []}
    res = {"save": args.save, "policy": args.policy, "seed": args.seed,
           "budget": args.budget, "ticks": ticks, "fights": rec["fights"],
           "won": rec["won"], "wipes": rec["wipes"], "ended": ok,
           "screen": s.state.get("screen"),
           "levels": {n: [before.get(n), after.get(n)] for n in after},
           "exp": exp,
           "exp_per_100k": {n: round(v * 1e5 / max(ticks, 1))
                            for n, v in exp.items()},
           "ticks_a_fight": round(ticks / max(rec["fights"], 1))}
    print(json.dumps(res))
    if args.out:
        Path(args.out).parent.mkdir(parents=True, exist_ok=True)
        Path(args.out).write_text(json.dumps(res) + "\n", newline="\n")
    try:
        s.close()
    except (OSError, ValueError):
        pass
    return 0


if __name__ == "__main__":
    import playthrough
    playthrough.pin_hash_seed()
    sys.exit(main())
