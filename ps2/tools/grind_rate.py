#!/usr/bin/env python3
"""XP a fight pays on a Climatrol floor, with Nei alive or at 0 HP.

The victory's split (loc_F2A4, ps2.asm:22423) is the formation's EXP over
the living members, plus one, to each of them: Nei alive takes a quarter,
Nei dead none -- and she has to be alive again for A on Neifirst (event
$25 with $C042 at 0 is $180E and nothing more). This walks F2's landing
(or another floor/cell) for N fights with the party as the save has it,
outfitted with two of the best one-hand weapon, and reports the EXP each
member gained per fight, the fights with someone down, and whether it
held up.

    python3 tools/grind_rate.py --fights 24 --nei dead --pair Rudo+Amy
"""
import argparse
import json
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from play import (Session, continue_game, wander, find_item,  # noqa: E402
                  use_item, give_round)
import party_trials                                          # noqa: E402

ROOT = Path(__file__).resolve().parent.parent


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--save", default="routes/saves/story_climatrol.json")
    ap.add_argument("--pair", default="Rudo+Amy")
    ap.add_argument("--nei", choices=["alive", "dead"], default="alive")
    ap.add_argument("--floor", default="0x38")
    ap.add_argument("--cell", default="15,24")
    ap.add_argument("--fights", type=int, default=24)
    ap.add_argument("--mates", type=int, default=10)
    ap.add_argument("--session", default="grind_rate")
    args = ap.parse_args()

    save = json.loads((ROOT / args.save).read_text())
    if args.nei == "dead":
        for c in save["characters"]:
            if c["id"] == "Nei":
                c["hp"] = 0
    tmp = Path(tempfile.mkdtemp()) / "grind_rate.json"
    tmp.write_text(json.dumps(save), newline="\n")

    items, sold = party_trials.load_items()
    pair = args.pair.split("+")
    s = Session(args.session)
    if args.nei == "dead":
        s.expendable = ("Nei",)
    if not continue_game(s, str(tmp)):
        print("continue failed")
        return 1
    s.send("members " + " ".join(["Rolf", "Nei"] + pair))
    for who in ["Rolf"] + pair:
        kit = party_trials.loadouts(who, items, sold)["dual"]
        s.send(f"outfit {who} " + " ".join(kit))
    give_round(s, "Trimate", args.mates, [0, 1, 2, 3])
    give_round(s, "Antidote", 2, [0])
    # The checkpoint's Rudo is poisoned, and needs_healing counts that:
    # cured here, as Kueri's hospital would before a trip.
    for i, m in enumerate(s.state.get("party") or []):
        if m.get("poison"):
            pill = find_item(s, "Antidote")
            if pill:
                use_item(s, pill[0], pill[1], target=i)
                s.send("wait idle")
    fx, fy = args.cell.split(",")
    s.send(f"warp {int(args.floor, 0)} {fx} {fy}", "wait screen level",
           "wait idle")
    exp0 = roster_exp(s)
    t0 = s.state.get("tick") or 0
    fights = wander(s, ["walk left 3", "walk right 3"], args.fights,
                    mates=True, name="rate")
    exp1 = roster_exp(s)
    n = len(s.fights)
    downs = sum(1 for f in s.fights
                if any(m["dead"] and m["name"] != "Nei"
                       for m in (f.get("party") or [])))
    gains = {k: exp1[k] - exp0[k] for k in exp0 if exp1[k] != exp0[k]}
    party = s.state.get("party") or []
    print(json.dumps({
        "pair": args.pair, "nei": args.nei, "floor": args.floor,
        "fights": n, "stopped_early": n < args.fights,
        "exp_per_fight": {k: round(v / max(1, n)) for k, v in gains.items()},
        "fights_with_a_fighter_down": downs,
        "ticks_per_fight": round(((s.state.get("tick") or 0) - t0)
                                 / max(1, n)),
        "end": [(m["name"], m["level"], m["hp"], m["max_hp"])
                for m in party]}))
    return 0


def roster_exp(s):
    """{name: experience} from the probe's exp line."""
    out = {}
    for ln in s.send("exp"):
        words = ln.split()
        if "exp" not in words:
            continue
        for tok in words[words.index("exp") + 1:]:
            name, _, value = tok.partition(":")
            if value.isdigit():
                out[name] = int(value)
    return out


if __name__ == "__main__":
    sys.exit(main())
