#!/usr/bin/env python3
"""(build 103) What each grind buys a tick: playthrough.py with a ledger.

    python3 tools/grind_ledger.py --seed 3 --to lab     # playthrough's args
    python3 tools/grind_ledger.py --summary route_out/playthrough_s*/ledger.json

Run mode is `playthrough.py` unchanged -- the same arguments, the same game,
the same report -- with one thing added: after every fight_smart the
ledger notes the stage, the try, the session's ticks, the screen and each
member's level, and at each stage's start and end the same. It reads the
state the harness already holds and sends nothing, so the game played is
playthrough's own. The ledger goes to route_out/playthrough_s<N>/ledger.json
(scratch: run it in a scratch directory, not the tree).

--summary reads ledgers and prints, a grind stage a line: fights, ticks,
the members' levels before and after, levels gained a 100 k ticks (all
members, and the lowest member's), and the dry tail -- the ticks after
the stage's last level-up of anyone, and after the lowest member's last.
"""
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import play                                                 # noqa: E402
import story                                                # noqa: E402
import playthrough                                          # noqa: E402

GRINDS = ("arima", "gear", "oputa", "amy", "amy_grind", "lab_grind",
          "lab_in", "lab", "lab_b1", "zema", "climatrol", "dam_grind")

LEDGER = []
_NOW = {"stage": None, "try": 0}


def _levels(s):
    return [[m["name"], m["level"]] for m in s.state.get("party") or []]


def _note(s, kind):
    LEDGER.append({"kind": kind, "stage": _NOW["stage"], "try": _NOW["try"],
                   "session": id(s), "ticks": s.ticks,
                   "screen": s.state.get("screen"), "levels": _levels(s)})


def _wrap_stage(name, fn):
    def run(s, log):
        _NOW["try"] = _NOW["try"] + 1 if _NOW["stage"] == name else 1
        _NOW["stage"] = name
        _note(s, "start")
        try:
            return fn(s, log)
        finally:
            _note(s, "end")
    return run


def _wrap_fight(fn):
    def fight(s, *a, **k):
        out = fn(s, *a, **k)
        _note(s, "fight")
        return out
    return fight


def run():
    seed = 1
    if "--seed" in sys.argv:
        seed = int(sys.argv[sys.argv.index("--seed") + 1])
    story.STAGES = [(n, _wrap_stage(n, f)) for n, f in story.STAGES]
    play.fight_smart = _wrap_fight(play.fight_smart)
    story.fight_smart = play.fight_smart
    try:
        return playthrough.main()
    finally:
        out = Path("route_out") / f"playthrough_s{seed}" / "ledger.json"
        out.parent.mkdir(parents=True, exist_ok=True)
        out.write_text(json.dumps({"seed": seed, "argv": sys.argv[1:],
                                   "ledger": LEDGER}), newline="\n")


def summarise(paths):
    rows = []
    for p in paths:
        d = json.loads(Path(p).read_text())
        led = d["ledger"]
        i = 0
        while i < len(led):
            e = led[i]
            if e["kind"] != "start":
                i += 1
                continue
            j = i + 1
            while j < len(led) and led[j]["kind"] != "end":
                j += 1
            if j >= len(led):
                break
            seg = led[i:j + 1]
            i = j + 1
            if e["stage"] not in GRINDS or len({x["session"]
                                               for x in seg}) != 1:
                continue
            t0, t1 = seg[0]["ticks"], seg[-1]["ticks"]
            names = [n for n, _ in seg[-1]["levels"]]
            if not names or not seg[0]["levels"]:
                continue
            lv0 = dict((n, v) for n, v in seg[0]["levels"])
            lv1 = dict((n, v) for n, v in seg[-1]["levels"])
            gained = sum(lv1[n] - lv0.get(n, lv1[n]) for n in names)
            low0 = min(lv0.get(n, lv1[n]) for n in names)
            low1 = min(lv1.values())
            last_any = last_low = t0
            prev = seg[0]
            for x in seg[1:]:
                a = dict((n, v) for n, v in prev["levels"])
                b = dict((n, v) for n, v in x["levels"])
                if any(b.get(n, 0) > a.get(n, b.get(n, 0)) for n in b):
                    last_any = x["ticks"]
                if b and a and min(b.values()) > min(a.values()):
                    last_low = x["ticks"]
                prev = x
            fights = sum(1 for x in seg if x["kind"] == "fight")
            wipes = sum(1 for x in seg if x["kind"] == "fight"
                        and x["screen"] in ("gameover", "sega", "title"))
            dt = max(t1 - t0, 1)
            rows.append((d["seed"], e["stage"], e["try"], fights, t1 - t0,
                         " ".join(f"{n}{lv0.get(n, '?')}>{lv1[n]}"
                                  for n in names),
                         round(gained * 1e5 / dt, 2),
                         round((low1 - low0) * 1e5 / dt, 2),
                         t1 - last_any, t1 - last_low, wipes))
    print("| seed | stage | try | fights | ticks | levels | levels/100k "
          "| lowest/100k | dry (any) | dry (lowest) | wipes |")
    print("|---|---|---|---|---|---|---|---|---|---|---|")
    for r in rows:
        print("| " + " | ".join(str(x) for x in r) + " |")


if __name__ == "__main__":
    if len(sys.argv) > 1 and sys.argv[1] == "--summary":
        summarise(sys.argv[2:])
    else:
        sys.exit(run())
