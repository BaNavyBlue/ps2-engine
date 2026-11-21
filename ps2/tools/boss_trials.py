#!/usr/bin/env python3
"""The last two bosses, Dark Force ($102) and Mother Brain ($103), with each
party of Rolf and three as they stand in a save (build 91; Mother Brain and
the fight policies build 94).

Each run: CONTINUE the save, `members Rolf A B C` (each at the save's own
level and gear: a member at Rolf's house keeps their bag), Trimates dealt
round, then the way in, then fight_smart with no RUN:

  dark_force    `warp` to NoahGroundF 33,14 below Pandora's box (the save
                must have the box's flag clear), up, A, C through $171A
                and $1910 into the fight.
  mother_brain  `warp` to NoahF1 33,37 (the *mother brain fought* flag
                clear), four steps up the corridor into the arrival scene,
                YES to the first question and NO to the second ("Die!").

The fight policy (--policy, any of):

  old     build 91-93: damage techniques for Kain and Hugh only, no Deban,
          and a technique once ordered cast every round it can be paid for
  techs   every member's damage techniques against a boss, at the boss bar
  deban   Deban while half the party is without it, techniques as `old`
  both    techs and deban (fight_smart's default, build 94)
  all     both, and Shift twice a fight

Recorded: won, the boss's HP at the end, the fight's ticks, members
standing, and for each member the TP spent, the curse kinds seen (Dark
Force's: 1-3 lose every turn, 4 attack down, 5 sealed, 6 agility down),
sealed or not, and the techniques ordered.

(build 114) Each fight is a fresh probe -- power-on, the CONTINUE, then
`wait ticks` of its repeat's offset, --step apart and the same for every
triple, policy and arrival (walk_trials' way) -- so fights run at once with
--jobs N and come out the same at any N, and the policies meet the same
draws. Up to build 113 every fight ran in one session, each starting where
the last had left the RNG; --chain still draws that way (one at a time).

    python3 tools/boss_trials.py --save routes/saves/story_noah_grind.json \\
        --boss dark_force --candidates Rudo,Amy,Anna,Kain --repeats 10 \\
        --policy old,both

Arrivals (build 97b). With --arrival (or --walk-policies) the fight starts
from the save as it stands -- no `member` refill, no Trimates dealt on
top -- with the party as a walk to the boss left it: the save's Trimates
cut to the arrival's count in the order the walk spends them (goto's
mending and fight_smart take the first living holder's first, so Rolf's
go first, then Rudo's, ...) and each named member's TP set. The save is
a walk_trials start save (routes/saves/walk_<boss>.json: Noah's ground
floor, full HP and TP, 18 Trimates, BOSS_PARTY) and the triple is its
own party's.

  --arrival LABEL:MATES[:NAME=TP,...]   repeatable
  --walk-policies P,Q,...   an arrival each from walk_trials' results
                            (route_out/walk_trials_<boss>.json): the means
                            over its box_ready arrivals -- the ones the
                            stage takes into the fight -- of mates left and
                            each member's TP; --full-tp keeps the TP full
                            (the mates alone)

    python3 tools/boss_trials.py --boss mother_brain \\
        --save routes/saves/walk_mother_brain.json --candidates Rudo,Amy,Kain \\
        --walk-policies b96c,w_mend50 --repeats 100
"""
import argparse
import itertools
import json
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import jobs                                                 # noqa: E402
import play                                                 # noqa: E402

ROOT = Path(__file__).resolve().parent.parent

WIN_YES_NO = 0x1F
POLICIES = {
    "old":   {"boss_techs": False, "deban": False, "shift": False,
              "reset_techs": False},
    "techs": {"boss_techs": True,  "deban": False, "shift": False},
    "deban": {"boss_techs": False, "deban": True,  "shift": False},
    "both":  {"boss_techs": True,  "deban": True,  "shift": False},
    "all":   {"boss_techs": True,  "deban": True,  "shift": True},
}


def enter_dark_force(s):
    s.send("warp 0x63 33 14", "wait screen level", "wait idle",
           "press up", "wait idle", "press a", "wait idle")
    for _ in range(16):
        if s.state.get("screen") == "battle":
            break
        if s.state.get("kind") == "waiting":
            s.send("press c", "wait idle")
        else:
            s.send("wait idle")


def enter_mother_brain(s):
    """The corridor, the arrival scene and the confrontation's two
    questions: YES (fight), then NO (do not return) -- "Die!"."""
    s.send("warp 0x64 33 37", "wait screen level", "wait idle",
           "walk up 4", "wait idle", "wait ticks 2", "wait idle")
    # NoahF1 has random encounters: one on the corridor is fought, and
    # the walk north goes on (the arrival needs only row 33 or north).
    for _ in range(3):
        if s.state.get("screen") != "battle" \
                or s.state.get("formation") == 0x103:
            break
        play.fight_smart(s, run_below=0.0, stop_on_gameover=True)
        if s.state.get("screen") != "level":
            return
        s.send("walk up 4", "wait idle", "wait ticks 2", "wait idle")
    answers = ["yes", "no"]
    for _ in range(60):
        st = s.state
        if st.get("screen") == "battle":
            break
        if st.get("wait") == "menu" and st.get("menu") == WIN_YES_NO \
                and answers:
            if answers.pop(0) == "no":
                s.send("press down")
            s.send("press c", "wait idle")
            continue
        if st.get("kind") == "waiting":
            s.send("press c", "wait idle")
        else:
            s.send("wait idle")


BOSSES = {
    "dark_force":   {"formation": 0x102, "enter": enter_dark_force},
    "mother_brain": {"formation": 0x103, "enter": enter_mother_brain},
}


def boss_hp(formation):
    form = play.formation_of(formation) or {}
    return (play.enemy(form.get("enemy1")) or {}).get("hp", 0)


def run(s, save, boss, triple, levels, mates, policy, arrival=None,
        offset=0):
    if not play.continue_game(s, str(save)) and \
            not play.continue_game(s, str(save)):
        return {"error": "continue failed"}
    if offset:
        # (build 114) A fresh probe's draw: the RNG turns every frame.
        s.send(f"wait ticks {offset}")
    party = ["Rolf"] + list(triple)
    s.send("members " + " ".join(party))
    if arrival is None:
        for who in party:
            s.send(f"member {who} {levels[who]}")
        play.give_round(s, "Trimate", mates, [0, 1, 2, 3])
    b = BOSSES[boss]
    b["enter"](s)
    if s.state.get("screen") != "battle" \
            or s.state.get("formation") != b["formation"]:
        return {"error": f"no {boss}: {s.state.get('screen')} "
                         f"{s.state.get('formation')}"}
    tick0 = s.state.get("tick") or 0
    hp = boss_hp(b["formation"])
    n0 = len(s.events)
    nf = len(s.fights)
    # Mother Brain's fights run to 50000 ticks: the limit is loop steps,
    # and a fight cut short by it is neither won nor lost.
    play.fight_smart(s, run_below=0.0, limit=4000, stop_on_level=True,
                     stop_on_gameover=True, **POLICIES[policy])
    pat = re.compile(rf"formation {b['formation']} enemies \d+:(\d+)/")
    for ln in s.events[n0:]:
        m = pat.search(ln)
        if m:
            hp = int(m.group(1))
    won = s.state.get("screen") == "level"
    if not won and s.state.get("screen") == "battle":
        return {"error": "the fight was still going at the limit",
                "boss_hp": hp}
    st = s.state.get("party") or []
    fight = s.fights[nf] if len(s.fights) > nf else {}
    r = {"triple": "+".join(triple), "policy": policy, "won": won,
         "boss_hp": 0 if won else hp,
         "standing": sum(1 for m in st if not m["dead"]) if won else 0,
         "ticks": (s.state.get("tick") or 0) - tick0,
         "members": fight.get("members", {})}
    if arrival is not None:
        r["arrival"] = arrival["label"]
        r["mates_left"] = play.count_mates(s)
    return r


def fights(args, cands, levels, policies, arrivals, saves):
    """(build 114) Every fight of the trial, in the order they have always
    run and been reported: a triple's policies and repeats, or with
    arrivals the repeats round the policies and arrivals. A repeat's
    offset is the same for every triple, policy and arrival."""
    if arrivals:
        triple = tuple(cands[:3])
        for rep in range(args.repeats):
            for policy in policies:
                for a in arrivals:
                    yield {"save": saves[a["label"]], "boss": args.boss,
                           "triple": triple, "levels": levels, "mates": 0,
                           "policy": policy, "arrival": a,
                           "offset": rep * args.step,
                           "label": f"{a['label']} {policy} {rep}"}
    else:
        for triple in itertools.combinations(cands, 3):
            for policy in policies:
                for rep in range(args.repeats):
                    yield {"save": ROOT / args.save, "boss": args.boss,
                           "triple": triple, "levels": levels,
                           "mates": args.mates, "policy": policy,
                           "arrival": None, "offset": rep * args.step,
                           "label": f"{'+'.join(triple)} {policy} {rep}"}


def fight_result(f, r):
    r.setdefault("triple", "+".join(f["triple"]))
    r.setdefault("policy", f["policy"])
    if f["arrival"] is not None:
        r.setdefault("arrival", f["arrival"]["label"])
    return r


def fresh_fight(f):
    """(build 114) One fight in a probe of its own: this worker's session
    (route_out/boss_trials/w<N>, never two at once), started from power-on,
    so the fight comes out the same whichever worker plays it and
    whenever."""
    s = play.Session(f"boss_trials/w{jobs.worker()}")
    try:
        return fight_result(f, run(s, f["save"], f["boss"], f["triple"],
                                   f["levels"], f["mates"], f["policy"],
                                   arrival=f["arrival"], offset=f["offset"]))
    finally:
        s.close()


# ---------------------------------------------------------------------
# Arrivals (build 97b): the party as a walk to the boss leaves it.
# ---------------------------------------------------------------------

def parse_arrival(spec):
    """LABEL:MATES[:NAME=TP,...] -> {"label", "mates", "tp"}."""
    parts = spec.split(":")
    if len(parts) not in (2, 3):
        raise ValueError(f"--arrival {spec}: LABEL:MATES[:NAME=TP,...]")
    tp = {}
    if len(parts) == 3 and parts[2]:
        for kv in parts[2].split(","):
            name, v = kv.split("=")
            tp[name] = int(v)
    return {"label": parts[0], "mates": int(parts[1]), "tp": tp}


def walk_arrivals(boss, policies, full_tp):
    """An arrival per walk_trials policy: the means, over that policy's
    box_ready arrivals, of mates left and each TP user's TP, rounded."""
    path = ROOT / "route_out" / f"walk_trials_{boss}.json"
    results = json.loads(path.read_text())["results"]
    out = []
    for pol in policies:
        fit = [r for r in results if r.get("policy") == pol
               and r.get("reached") and r.get("box_ready")]
        if not fit:
            raise ValueError(f"walk_trials_{boss}: no box_ready runs of {pol}")
        mates = round(sum(r["mates"] for r in fit) / len(fit))
        tp = {}
        if not full_tp:
            for name in fit[0]["tp"]:
                v = [r["tp"][name][0] for r in fit if name in r["tp"]]
                tp[name] = round(sum(v) / len(v))
        out.append({"label": pol + ("_fulltp" if full_tp else ""),
                    "mates": mates, "tp": tp, "walk_runs": len(fit)})
    return out


def arrival_save(save, arrival, party, out_dir):
    """The save with the arrival dealt: Trimates cut from the front of
    the party (the walk's spending order) down to arrival["mates"], TP
    set. Written beside the trial's results; its path."""
    sv = json.loads(Path(save).read_text())
    chars = {c["id"]: c for c in sv["characters"]}
    have = sum(1 for n in party for e in chars[n]["inventory"]
               if e["item"] in play.MATES and not e["equipped"])
    cut = have - arrival["mates"]
    if cut < 0:
        raise ValueError(f"{arrival['label']}: the save carries {have} "
                         f"mates, fewer than {arrival['mates']}")
    for n in party:
        keep = []
        for e in chars[n]["inventory"]:
            if cut > 0 and e["item"] in play.MATES and not e["equipped"]:
                cut -= 1
                continue
            keep.append(e)
        chars[n]["inventory"] = keep
    for name, v in arrival["tp"].items():
        chars[name]["tp"] = min(v, chars[name]["max_tp"])
    path = Path(out_dir) / f"arrival_{arrival['label']}.json"
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(sv, indent=1), newline="\n")
    return path


def arrival_summary(results, arrivals, policies):
    """(build 97b) One row per arrival and fight policy."""
    rows = ["| arrival | mates dealt | TP dealt | policy | won | boss HP left "
            "(mean, lost) | mates left (mean, won) | standing (mean, won) "
            "| ticks (mean) |", "|---|---|---|---|---|---|---|---|---|"]
    for a in arrivals:
        for policy in policies:
            rs = [r for r in results if r.get("arrival") == a["label"]
                  and r.get("policy") == policy and "error" not in r]
            if not rs:
                continue
            won = [r for r in rs if r["won"]]
            lost = [r["boss_hp"] for r in rs if not r["won"]]
            tp = ", ".join(f"{n} {v}" for n, v in a["tp"].items()) or "full"
            rows.append(
                f"| {a['label']} | {a['mates']} | {tp} | {policy} | "
                f"{len(won)}/{len(rs)} | "
                + (f"{sum(lost) / len(lost):.0f}" if lost else "-") + " | "
                + (f"{sum(r['mates_left'] for r in won) / len(won):.1f}"
                   if won else "-") + " | "
                + (f"{sum(r['standing'] for r in won) / len(won):.2f}"
                   if won else "-")
                + f" | {sum(r['ticks'] for r in rs) / len(rs):.0f} |")
    return "\n".join(rows)


def summary(results, cands, levels, policies):
    rows = ["| party (Rolf +) | levels | policy | won | boss HP left (mean, "
            "lost) | ticks (mean) |", "|---|---|---|---|---|---|"]
    for triple in itertools.combinations(cands, 3):
        for policy in policies:
            rs = [r for r in results if r.get("triple") == "+".join(triple)
                  and r.get("policy") == policy and "error" not in r]
            if not rs:
                continue
            lost = [r["boss_hp"] for r in rs if not r["won"]]
            rows.append(f"| {'+'.join(triple)} | "
                        + "/".join(str(levels[n]) for n in ["Rolf"] + list(triple))
                        + f" | {policy} | {sum(r['won'] for r in rs)}/{len(rs)} | "
                        + (f"{sum(lost) / len(lost):.0f}" if lost else "-")
                        + f" | {sum(r['ticks'] for r in rs) / len(rs):.0f} |")
    # Each member: TP spent, how often cursed and with what, techniques.
    rows += ["", "| member | policy | fights | TP spent (mean) | cursed "
             "(kinds seen) | sealed | techniques ordered |",
             "|---|---|---|---|---|---|---|"]
    names = ["Rolf"] + list(cands)
    for name in names:
        for policy in policies:
            ms = [r["members"][name] for r in results
                  if r.get("policy") == policy and name in r.get("members", {})]
            if not ms:
                continue
            spent = [m["tp_first"] - m["tp_last"] for m in ms
                     if m["tp_first"] is not None]
            kinds = {}
            for m in ms:
                for k in m["curses"]:
                    kinds[k] = kinds.get(k, 0) + 1
            cursed = sum(1 for m in ms if m["curses"])
            orders = {}
            for m in ms:
                for t in m["orders"]:
                    orders[t] = orders.get(t, 0) + 1
            rows.append(f"| {name} | {policy} | {len(ms)} | "
                        + (f"{sum(spent) / len(spent):.0f}" if spent else "-")
                        + f" | {cursed} ("
                        + ", ".join(f"{k}x{n}" for k, n in sorted(kinds.items()))
                        + f") | {sum(1 for m in ms if m['sealed'])} | "
                        + (", ".join(f"{t} {n}" for t, n in
                                     sorted(orders.items(), key=lambda x: -x[1]))
                           or "-") + " |")
    return "\n".join(rows)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--save", default="routes/saves/story_noah_grind.json")
    ap.add_argument("--boss", default="dark_force", choices=sorted(BOSSES))
    ap.add_argument("--candidates", default="Rudo,Amy,Anna,Kain")
    ap.add_argument("--repeats", type=int, default=10)
    ap.add_argument("--mates", type=int, default=8)
    ap.add_argument("--policy", default="both",
                    help="comma-separated: " + ", ".join(POLICIES))
    ap.add_argument("--out", default=None)
    ap.add_argument("--arrival", action="append", default=None,
                    help="LABEL:MATES[:NAME=TP,...], repeatable (build 97b)")
    ap.add_argument("--walk-policies", default=None,
                    help="walk_trials policies, an arrival each (build 97b)")
    ap.add_argument("--full-tp", action="store_true",
                    help="with --walk-policies: the mates alone, TP full")
    ap.add_argument("--jobs", type=int, default=1,
                    help="(build 114) fights at once, each in a probe of its "
                         "own; the results the same at any number")
    ap.add_argument("--step", type=int, default=7,
                    help="(build 114) ticks between one repeat's offset and "
                         "the next's")
    ap.add_argument("--chain", action="store_true",
                    help="(build 114) the way before: every fight in one "
                         "session, in order, each starting where the last "
                         "left the RNG (--jobs and --step unused) -- how "
                         "the trials up to build 113 were drawn")
    args = ap.parse_args()
    out = args.out or f"route_out/boss_trials_{args.boss}"
    policies = args.policy.split(",")
    sv = json.loads((ROOT / args.save).read_text())
    levels = {c["id"]: c["level"] for c in sv["characters"]}
    cands = args.candidates.split(",")
    arrivals = [parse_arrival(a) for a in (args.arrival or [])]
    if args.walk_policies:
        arrivals += walk_arrivals(args.boss, args.walk_policies.split(","),
                                  args.full_tp)
    results = []
    try:
        saves = {}
        if arrivals:
            party = ["Rolf"] + cands[:3]
            saves = {a["label"]: arrival_save(ROOT / args.save, a, party,
                                              ROOT / (out + "_saves"))
                     for a in arrivals}
        plan = list(fights(args, cands, levels, policies, arrivals, saves))
        if args.chain:
            # The rounds interleave the arrivals, so each repeat meets
            # them at about the same point of the session's ticks.
            s = play.Session("boss_trials")
            try:
                for f in plan:
                    r = fight_result(f, run(s, f["save"], f["boss"],
                                            f["triple"], f["levels"],
                                            f["mates"], f["policy"],
                                            arrival=f["arrival"]))
                    results.append(r)
                    short = {k: r[k] for k in r if k != "members"}
                    print(f"{f['label']}: {short}", flush=True)
            finally:
                s.close()
        else:
            # (build 114) Each fight in a fresh probe with its repeat's
            # offset, so --jobs changes nothing but the time it takes.
            for f, r in zip(plan, jobs.ordered(fresh_fight, plan, args.jobs)):
                results.append(r)
                short = {k: r[k] for k in r if k != "members"}
                print(f"{f['label']}: {short}", flush=True)
    finally:
        if arrivals:
            table = (arrival_summary(results, arrivals, policies) + "\n\n"
                     + summary(results, cands, levels, policies))
        else:
            table = summary(results, cands, levels, policies)
        # --jobs is left out: it changes nothing in the results.
        kept = {k: v for k, v in vars(args).items() if k != "jobs"}
        Path(ROOT / out).with_suffix(".json").write_text(
            json.dumps(dict({"args": kept, "results": results},
                            **({"arrivals": arrivals} if arrivals else {})),
                       indent=1), newline="\n")
        Path(ROOT / out).with_suffix(".md").write_text(table + "\n", newline="\n")
        print(table)


if __name__ == "__main__":
    main()
