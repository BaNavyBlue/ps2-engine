#!/usr/bin/env python3
"""Every four-member party after Climatrol through a dam's fights.

With $C737 set the house leaves Rolf alone and three are chosen
(RolfHouseOption_Reorganize, ps2.asm:16330): of Rudo, Amy, Hugh, Anna
and Kain -- Shir has not joined by then -- ten triples. The next boss,
the Army Eyes (formation $101), cannot be won: their technique
(loc_2E24) carries the party off to Gaira in round three. So the
yardstick is the next dungeon: a gauntlet of fights on a dam floor with
no way out, as a run to its panel would be.

Each run: CONTINUE the Climatrol checkpoint, `members Rolf A B C`, each
raised by the real level-up rows to the level its own EXP reaches after
F grind fights at --share EXP (party_trials.levels_after), outfitted with
the best one-hand weapon twice from the stores before Climatrol, a stock
of Trimates dealt round, `warp` to the floor, then play.wander for N
fights with fight_smart (techniques on: RES/GIRES by plan_heal, and
DEFEND ordered back to ATTACK). It stops at a wipe (the SEGA screen).

    python3 tools/dam_trials.py --grind 0,150,300 --repeats 3
    python3 tools/dam_trials.py --triples Rudo+Anna+Kain --grind 0

Build 88: --type-techs (default Kain,Hugh; `none` is build 87's
fight_smart, which ordered no damage technique at all) and --late 0,
which clears $C737 in the trial's copy of the game so a Motavian
dungeon rolls its first encounter group -- Climatrol's biomonsters
rather than the robots that replace them:

    python3 tools/dam_trials.py --floor 0x3B --cell X,Y --late 0 ...

Build 90: Noah before the story goes there, Kain and Hugh against the
party that finished the Prism's dungeons. --stores all outfits from
every store (not only those before Climatrol); --outfit names who is
outfitted -- the rest keep what the save has them wearing (the Nei
items); --candidates widens the pool (Shir):

    python3 tools/dam_trials.py --save routes/saves/story_nei_sword.json \
        --floor 0x63 --cell 32,107 --share 3459 --grind 0,80,250 \
        --stores all --outfit Kain,Hugh,Shir --fights 20 --repeats 3

Results go to route_out/dam_trials.json and .md as they come in.
"""
import argparse
import itertools
import json
import sys
import tempfile
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import play                                                 # noqa: E402
from play import Session, continue_game, give_round, wander  # noqa: E402
import party_trials                                          # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
CANDIDATES = ["Rudo", "Amy", "Hugh", "Anna", "Kain"]


def run(s, save, triple, level, fights, mates, floor, cell, outfits,
        repeat, late=None):
    t0 = time.time()
    if not continue_game(s, str(save)) and not continue_game(s, str(save)):
        return {"error": "continue failed"}
    party = ["Rolf"] + list(triple)
    s.send("members " + " ".join(party))
    for who in party:
        s.send(f"member {who} {level[who]}")
        if who in outfits:
            s.send(f"outfit {who} " + " ".join(outfits[who]["dual"]))
    given = give_round(s, "Trimate", mates, [0, 1, 2, 3])
    if late is not None:
        s.send(f"flag $C737 {late}")
    s.send(f"warp {floor} {cell[0]} {cell[1]}", "wait screen level",
           "wait idle", f"wait ticks {1 + 41 * repeat}")
    if s.state.get("level") != floor:
        return {"error": f"not on {floor:#x}: {s.state.get('level')}"}
    s.fights = []
    s.tech_orders = []
    tick0 = s.state.get("tick") or 0
    wander(s, ["walk left 3", "walk right 3"], fights, mates=True,
           name="dam")
    if not s.fights:
        return {"error": "no fight: " + str((s.state.get("level"),
                                              s.state.get("x"),
                                              s.state.get("y")))}
    won = 0
    downs = 0
    wiped = s.state.get("screen") != "level"
    for f in s.fights:
        p = f.get("party") or []
        if p and all(m["dead"] for m in p):
            break
        won += 1
        downs += any(m["dead"] for m in p)
    end = s.state.get("party") or []
    hp = sum(m["hp"] for m in end) if not wiped else 0
    full = sum(m["max_hp"] for m in end) or 1
    return {"triple": "+".join(triple), "grind": None, "repeat": repeat,
            "levels": "/".join(str(level[w]) for w in party),
            "fought": len(s.fights), "won": won, "wiped": wiped,
            "all": (not wiped) and won >= fights,
            "fights_with_someone_down": downs,
            "hp_left": round(hp / full, 3) if not wiped else 0.0,
            "mates_given": given,
            "type_techs": len(s.tech_orders),
            "orders": {k: sum(1 for o in s.tech_orders if f"{o[0]} {o[1]}" == k)
                       for k in {f"{o[0]} {o[1]}" for o in s.tech_orders}},
            "didnt_work": sum(f["scripts"].count(0x1204) for f in s.fights),
            "ticks_per_fight": round(((s.state.get("tick") or 0) - tick0)
                                     / max(1, len(s.fights))),
            "secs": round(time.time() - t0, 1)}


def table(results, fights):
    groups = {}
    for r in results:
        if "error" in r:
            continue
        groups.setdefault((r["triple"], r["grind"]), []).append(r)
    rows = []
    for (triple, grind), rs in groups.items():
        clear = sum(r["all"] for r in rs)
        won = sum(r["won"] for r in rs) / len(rs)
        hp = sum(r["hp_left"] for r in rs if r["all"]) / max(1, clear)
        wiped = sum(r["wiped"] for r in rs)
        rows.append((clear / len(rs), won, hp, triple, grind, clear,
                     len(rs), rs[0]["levels"], wiped))
    rows.sort(key=lambda r: (r[4], -r[0], -r[1], -r[2]))
    lines = [f"| grind fights | party (Rolf +) | levels | cleared {fights} "
             "| fights per trip (mean) | wiped | HP left (cleared) |",
             "|---|---|---|---|---|---|---|"]
    for rate, won, hp, triple, grind, clear, n, levels, wiped in rows:
        lines.append(f"| {grind} | {triple} | {levels} | {clear}/{n} | "
                     f"{won:.1f} | {wiped}/{n} | {hp:.0%} |")
    return "\n".join(lines)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--save", default="routes/saves/story_climatrol.json")
    ap.add_argument("--triples", default="all",
                    help="all, or A+B+C,... from " + ", ".join(CANDIDATES))
    ap.add_argument("--grind", default="0,150,300",
                    help="grind fights before the dam, at --share EXP each")
    ap.add_argument("--share", type=int, default=190,
                    help="EXP a member a fight at the grind spot (build 79: "
                         "Climatrol F2's landing, 190)")
    ap.add_argument("--fights", type=int, default=12)
    ap.add_argument("--mates", type=int, default=8)
    ap.add_argument("--floor", default="0x2A", help="Red Dam F2")
    ap.add_argument("--cell", default="45,21")
    ap.add_argument("--repeats", type=int, default=2)
    ap.add_argument("--out", default="route_out/dam_trials")
    ap.add_argument("--session", default="dam_trials")
    ap.add_argument("--type-techs", default="Kain,Hugh",
                    help="members fight_smart orders type techniques for; "
                         "none = build 87")
    ap.add_argument("--stores", default="before",
                    help="before (Climatrol, build 80) or all")
    ap.add_argument("--outfit", default=None,
                    help="who is outfitted from the stores (default: "
                         "everyone); the rest keep the save's gear")
    ap.add_argument("--candidates", default=",".join(CANDIDATES))
    ap.add_argument("--late", type=int, default=None,
                    help="$C737 for the run (0: the pre-Climatrol groups)")
    args = ap.parse_args()

    # Out of the hospital: the checkpoint has Rolf and Rudo hurt and Amy
    # and Hugh poisoned (status $100), and needs_healing stops wander on
    # either before its first step.
    sv = json.loads((ROOT / args.save).read_text())
    for c in sv["characters"]:
        if c["id"] != "Nei":
            c["hp"] = c["max_hp"]
            c["status"] = 0
            if "max_tp" in c:
                c["tp"] = c["max_tp"]
    save = Path(tempfile.mkdtemp()) / "dam_trials.json"
    save.write_text(json.dumps(sv), newline="\n")

    items, sold = party_trials.load_items(
        all_stores=(args.stores == "all"))
    cands = args.candidates.split(",")
    dressed = (args.outfit.split(",") if args.outfit
               else ["Rolf"] + cands)
    outfits = {n: party_trials.loadouts(n, items, sold)
               for n in ["Rolf"] + cands if n in dressed}
    triples = (list(itertools.combinations(cands, 3))
               if args.triples == "all"
               else [tuple(t.split("+")) for t in args.triples.split(",")])
    grinds = [int(x) for x in args.grind.split(",")]
    floor = int(args.floor, 0)
    cell = [int(x) for x in args.cell.split(",")]
    out = ROOT / args.out
    out.parent.mkdir(parents=True, exist_ok=True)
    play.TYPE_TECHS = (() if args.type_techs == "none"
                       else tuple(args.type_techs.split(",")))
    s = Session(args.session)
    results = []
    total = len(triples) * len(grinds) * args.repeats
    n = 0
    for grind in grinds:
        for triple in triples:
            lv = party_trials.levels_after(ROOT / args.save, args.share,
                                           grind, ["Rolf"] + list(triple))
            for rep in range(args.repeats):
                n += 1
                r = run(s, save, triple, lv, args.fights,
                        args.mates, floor, cell, outfits, rep,
                        late=args.late)
                r["grind"] = grind
                r.setdefault("triple", "+".join(triple))
                results.append(r)
                print(f"[{n}/{total}] {grind}f {'+'.join(triple)} "
                      f"{r.get('levels', '')}: won {r.get('won')}"
                      f"/{args.fights} {'CLEAR' if r.get('all') else ''} "
                      f"hp {r.get('hp_left')} {r.get('secs')}s "
                      f"{r.get('error', '')}", flush=True)
                out.with_suffix(".json").write_text(json.dumps(
                    {"args": vars(args), "results": results}, indent=1), newline="\n")
                out.with_suffix(".md").write_text(
                    table(results, args.fights) + "\n", newline="\n")
    print(table(results, args.fights))
    s.close()


if __name__ == "__main__":
    main()
