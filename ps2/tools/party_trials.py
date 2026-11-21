#!/usr/bin/env python3
"""Neifirst against every party the house allows, to choose whom to grind.

RolfHouseOption_Reorganize (ps2.asm:16330) clears the party and, while
$C737 is clear, puts Nei in slot 1 itself: before Climatrol the party is
Rolf, Nei and two of Rudo, Amy, Hugh, Anna, Kain and Shir -- fifteen
pairs. Nei always loses her fight alone (event $25's battle), and the
party's own fight (event $26's second visit) is Rolf and the pair with
Nei among them at 0 HP. After Climatrol ($C737 set) the house leaves Rolf
alone and three are chosen, so the pair taken to Climatrol is two thirds
of the party for the rest of the game.

Each trial: CONTINUE the save, `members Rolf Nei A B`, Rolf and the pair
raised to LEVEL by the real level-up rows (`member`), each outfitted from
the best the stores reachable before Climatrol sell (Paseo, Arima, Oputa,
Zema, Kueri: stores 0-12) in one of three hand loadouts, a few Trimates,
`warp` to F7's 48,24, a few frames' wait (the RNG), and
story.face_neifirst. The table ranks pair x level x loadout by wins, then
by HP left.

    python3 tools/party_trials.py --levels 19,22,25 --repeats 2
    python3 tools/party_trials.py --pairs Hugh+Kain,Rudo+Amy --levels 20

Results go to route_out/party_trials.json and .md as they come in.
"""

import argparse
import itertools
import json
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from play import Session, continue_game, give_round         # noqa: E402
from story import face_neifirst, NEIFIRST_SPOT, CLIMATROL_F7  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
CANDIDATES = ["Rudo", "Amy", "Hugh", "Anna", "Kain", "Shir"]
CHAR_BIT = {"Rolf": 0, "Nei": 1, "Rudo": 2, "Amy": 3, "Hugh": 4, "Anna": 5,
            "Kain": 6, "Shir": 7}
STORES_BEFORE_CLIMATROL = range(0, 13)

# action_flags & 7 (loc_ABB2's andi.w #7): the slot an item fills.
T_HEAD, T_ONE, T_TWO, T_BODY, T_LEGS = 1, 2, 3, 4, 5


def load_items(all_stores=False):
    items = {i["key"]: i for i in
             json.loads((ROOT / "assets/data/items.json").read_text())}
    stores = json.loads((ROOT / "assets/data/stores.json").read_text())
    sold = set()
    for st in stores["stores"]:
        if all_stores or st["index"] in STORES_BEFORE_CLIMATROL:
            sold |= {k for k in st["items"] if k != "None"}
    return items, sold


def loadouts(name, items, sold):
    """{style: (head, right, left, body, legs)} from what is sold that
    `name` may equip (equip_mask's CharID bit). dual: the best one-hand
    weapon in both hands; shield: it and the best one-hand guard;
    two: the best two-hander, when there is one worth it."""
    bit = 1 << CHAR_BIT[name]
    mine = [items[k] for k in sold if items[k]["equip_mask"] & bit]

    def best(kind, key):
        c = [i for i in mine if i["action_flags"] & 7 == kind]
        return max(c, key=key)["key"] if c else "-"

    head = best(T_HEAD, lambda i: i["defense"])
    body = best(T_BODY, lambda i: i["defense"])
    legs = best(T_LEGS, lambda i: (i["defense"] + i["attack"]))
    ones = [i for i in mine if i["action_flags"] & 7 == T_ONE]
    weapon = max((i for i in ones if i["attack"] > 0),
                 key=lambda i: (i["attack"], i["defense"]), default=None)
    guard = max((i for i in ones if i["attack"] == 0),
                key=lambda i: i["defense"], default=None)
    twos = [i for i in mine if i["action_flags"] & 7 == T_TWO]
    two = max(twos, key=lambda i: (i["attack"], i["defense"]), default=None)
    out = {}
    if weapon is not None:
        out["dual"] = (head, weapon["key"], weapon["key"], body, legs)
        if guard is not None:
            out["shield"] = (head, weapon["key"], guard["key"], body, legs)
    if two is not None and (weapon is None
                            or two["attack"] > 2 * weapon["attack"]):
        out["two"] = (head, two["key"], two["key"], body, legs)
    return out


def levels_after(save, share, fights, who):
    """{name: level} for `who` after `fights` fights at `share` EXP each,
    from the save's own experience and characters.json's table -- the
    level Character_ProcessLevelUp would reach."""
    chars = json.loads((ROOT / "assets/data/characters.json").read_text())
    sv = json.loads(Path(save).read_text())
    out = {}
    for i, c in enumerate(sv["characters"]):
        if c["id"] not in who:
            continue
        exp = c["experience"] + share * fights
        lv = c["level"]
        for row in chars[i]["levels"]:
            if row["level"] > lv and row["exp"] <= exp:
                lv = row["level"]
        out[c["id"]] = min(lv, 50)
    return out


def trial(s, save, pair, level, style, repeat, outfits, mates, log):
    t0 = time.time()
    # After a wipe the first CONTINUE can land on the SEGA screen's
    # tail; once more from there.
    if not continue_game(s, str(save)) and not continue_game(s, str(save)):
        return {"error": "continue failed"}
    party = ["Rolf", "Nei"] + list(pair)
    s.send("members " + " ".join(party))
    for who in ["Rolf"] + list(pair):
        lv = level[who] if isinstance(level, dict) else level
        s.send(f"member {who} {lv}")
        kit = outfits[who].get(style) or outfits[who]["dual"]
        s.send(f"outfit {who} " + " ".join(kit))
    given = 0
    if mates:
        # Rolf and the pair: Nei is at 0 HP in the party's fight.
        given = give_round(s, "Trimate", mates, [0, 2, 3])
    s.send(f"warp {CLIMATROL_F7} {NEIFIRST_SPOT[0]} {NEIFIRST_SPOT[1]}",
           "wait screen level", "wait idle",
           f"wait ticks {1 + 37 * repeat}")
    st = s.state
    if st.get("level") != CLIMATROL_F7:
        return {"error": f"not on F7: {st.get('level')}"}
    r = face_neifirst(s, log, shots=False)
    fighters = {m["name"]: (m["hp"], m["max_hp"]) for m in r["party"]
                if m["name"] != "Nei"}
    left = sum(hp for hp, _ in fighters.values())
    full = sum(mx for _, mx in fighters.values()) or 1
    return {"pair": "+".join(pair), "level": level, "style": style,
            "repeat": repeat, "won": r["won"], "ticks": r["ticks"],
            "mates_given": given,
            "hp_left": round(left / full, 3), "fighters": fighters,
            "second": [f"${x:04X}" for x in r["second"][-6:]],
            "secs": round(time.time() - t0, 1)}


def table(results):
    groups = {}
    for r in results:
        if "error" in r:
            continue
        k = (r["pair"], r["level"], r["style"])
        groups.setdefault(k, []).append(r)
    rows = []
    for (pair, level, style), rs in groups.items():
        wins = sum(r["won"] for r in rs)
        hp = sum(r["hp_left"] for r in rs if r["won"]) / max(1, wins)
        ticks = sum(r["ticks"] for r in rs if r["won"]) / max(1, wins)
        rows.append((wins / len(rs), hp, -ticks, pair, level, style,
                     wins, len(rs), ticks))
    rows.sort(reverse=True)
    lines = ["| pair | level | hands | won | HP left (wins) | "
             "fight ticks (wins) |",
             "|---|---|---|---|---|---|"]
    for rate, hp, _, pair, level, style, wins, n, ticks in rows:
        lines.append(f"| {pair} | {level} | {style} | {wins}/{n} | "
                     f"{hp:.0%} | {ticks:.0f} |")
    return "\n".join(lines)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--save", default="routes/saves/story_climatrol.json")
    ap.add_argument("--levels", default="19,22,25")
    ap.add_argument("--styles", default="dual,shield")
    ap.add_argument("--pairs", default="all",
                    help="all, or A+B,C+D from " + ", ".join(CANDIDATES))
    ap.add_argument("--repeats", type=int, default=2)
    ap.add_argument("--mates", type=int, default=6,
                    help="Trimates given before the fight (Kueri sells them)")
    ap.add_argument("--fights", default=None,
                    help="instead of --levels: F2 fight counts, e.g. "
                         "0,150,300; each member at the level its own EXP "
                         "reaches after that many fights at --share")
    ap.add_argument("--share", type=int, default=170,
                    help="EXP per member per F2 fight (grind_rate.py: 170 "
                         "with Nei alive, 251 with her down)")
    ap.add_argument("--out", default="route_out/party_trials")
    ap.add_argument("--session", default="trials")
    args = ap.parse_args()

    items, sold = load_items()
    outfits = {n: loadouts(n, items, sold) for n in ["Rolf"] + CANDIDATES}
    pairs = (list(itertools.combinations(CANDIDATES, 2))
             if args.pairs == "all"
             else [tuple(p.split("+")) for p in args.pairs.split(",")])
    if args.fights is not None:
        levels = [("F", int(x)) for x in args.fights.split(",")]
    else:
        levels = [int(x) for x in args.levels.split(",")]
    styles = args.styles.split(",")
    out = ROOT / args.out
    out.parent.mkdir(parents=True, exist_ok=True)

    quiet = lambda *a: None                                   # noqa: E731
    s = Session(args.session)
    results = []
    meta = {"save": args.save, "outfits": outfits, "mates": args.mates}
    total = len(pairs) * len(levels) * len(styles) * args.repeats
    n = 0
    for level in levels:
        for pair in pairs:
            lv = level
            if isinstance(level, tuple):
                lv = levels_after(ROOT / args.save, args.share, level[1],
                                  ["Rolf"] + list(pair))
            for style in styles:
                for rep in range(args.repeats):
                    n += 1
                    r = trial(s, ROOT / args.save, pair, lv, style, rep,
                              outfits, args.mates, quiet)
                    if isinstance(level, tuple) and "error" not in r:
                        r["level"] = (f"{level[1]}f "
                                      + "/".join(str(lv[w]) for w in
                                                 ["Rolf"] + list(pair)))
                    results.append(r)
                    print(f"[{n}/{total}] {'+'.join(pair)} L{level} {style} "
                          f"#{rep}: " + ("WON" if r.get("won") else "lost")
                          + f" {r.get('hp_left', '')} {r.get('ticks', '')}t "
                          f"{r.get('secs', '')}s {r.get('error', '')}",
                          flush=True)
                    (out.with_suffix(".json")).write_text(json.dumps(
                        {"meta": meta, "results": results}, indent=1), newline="\n")
                    (out.with_suffix(".md")).write_text(table(results) + "\n", newline="\n")
    print(table(results))


if __name__ == "__main__":
    main()
