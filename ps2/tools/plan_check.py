#!/usr/bin/env python3
"""plan() checks (build 89).

  python3 tools/plan_check.py            # the timed plans
  python3 tools/plan_check.py --equiv    # and reaches() against path()

The timed plans: from Zosa's street with the Prism held, to Aukba and to
each of the Prism's four dungeons, each inside --budget seconds (10). On
build 88's plan() the first never finished (~31,700 LevelMap.path calls).
--equiv asks, on every level with and without the Prism, whether each
arrival cell reaches each way out, both ways -- LevelMap.reaches and
path() -- and fails on any difference.

Build 90: the levels $30-$4F never finished inside a tool call's 300 s.
One level at a time (--levels 0x33-0x33) the big three still take more
than 40 s, so --part K/N checks only the K-th of N slices of a level's
arrival cells; and the Prism's pass skips a level whose map it does not
change (no item_patches) and whose arrivals are the same -- the checks
would be the very same ones:

  python3 tools/plan_check.py --no-timed --equiv --levels 0x33-0x33 --part 1/4

Ikuto B6 ($4C: 209 arrivals x 773 ways out, 26 ms a path() that finds
nothing) and B4 ($4E: 658 x 934) are an hour and half an hour of path()
calls: --sample N checks N (arrival, way out) pairs drawn with a fixed
seed instead of all of them."""
import random
import sys, time
sys.path.insert(0, "tools")
import play

ZOSA = (0x0B, (21, 6))                  # story_dezolis_grind.json's cell
TARGETS = {"Aukba": (0x0A, {(2, 9)}), "MenobeGroundF": (0x48, {(48, 61)}),
           "IkutoGroundF": (0x52, {(15, 13)}), "GuaronGroundF": (0x53, {(68, 45)}),
           "NavalGroundF": (0x43, {(5, 43)})}


def reset(held):
    play.HELD = set(held)
    play._MAPS.clear()
    play._WAYS.clear()
    play._HOLES.clear()


def timed(budget):
    bad = 0
    for held in ((), ("Prism",)):
        reset(held)
        for name, (lv, goals) in TARGETS.items():
            t = time.time()
            legs = play.plan(ZOSA[0], ZOSA[1], lv, goals)
            dt = time.time() - t
            want = bool(held) or name == "Aukba"
            ok = (legs is not None) == want and dt < budget
            bad += not ok
            print(f"{'ok  ' if ok else 'FAIL'} {'Prism' if held else 'none '} "
                  f"Zosa -> {name}: {len(legs) if legs else None} legs "
                  f"{[hex(L) for L, _ in legs] if legs else ''} {dt:.2f}s")
    return bad


def equiv(only=None, part=None, sample=None):
    bad = checks = skipped = 0
    rng = random.Random(90)
    first = {}
    for held in ((), ("Prism",)):
        reset(held)
        arrivals = {}
        for L in play._levels():
            try:
                for cells, to, arr in play.ways_out(L):
                    arrivals.setdefault(to, set()).add(arr)
            except FileNotFoundError:
                pass
        for L, arrs in sorted(arrivals.items()):
            if only is not None and L not in only:
                continue
            if held and not play.item_patches(L) and first.get(L) == arrs:
                skipped += 1                # the no-Prism pass's own checks
                continue
            first.setdefault(L, arrs)
            try:
                lm = play.level_map(L)
                outs = play.ways_out(L)
            except FileNotFoundError:
                continue
            cells_in = sorted(arrs)
            if part is not None:
                k, n = part
                cells_in = cells_in[k - 1::n]
            pairs = [(a, o) for a in cells_in for o in outs if o[0]]
            if sample is not None and len(pairs) > sample:
                pairs = rng.sample(pairs, sample)
            for a, (cells, to, arr) in pairs:
                old = a in cells or lm.path(a, cells) is not None
                new = lm.reaches(a, cells)
                checks += 1
                if old != new:
                    bad += 1
                    print(f"DIFF {held} level {L:#x} from {a} to {to:#x}: "
                          f"path {old} reaches {new}")
    print(f"equiv: {checks} checks, {bad} differences"
          + (f", {skipped} Prism passes the same as without" if skipped else ""))
    return bad


if __name__ == "__main__":
    budget = 10.0
    if "--budget" in sys.argv:
        budget = float(sys.argv[sys.argv.index("--budget") + 1])
    only = None
    if "--levels" in sys.argv:              # --levels 0x10-0x1f
        lo, hi = sys.argv[sys.argv.index("--levels") + 1].split("-")
        only = set(range(int(lo, 0), int(hi, 0) + 1))
    n = 0 if "--no-timed" in sys.argv else timed(budget)
    part = None
    if "--part" in sys.argv:                # --part 2/4
        k, m = sys.argv[sys.argv.index("--part") + 1].split("/")
        part = (int(k), int(m))
    if "--equiv" in sys.argv:
        sample = None
        if "--sample" in sys.argv:
            sample = int(sys.argv[sys.argv.index("--sample") + 1])
        n += equiv(only, part, sample)
    print("PASS" if n == 0 else f"FAIL ({n})")
    sys.exit(1 if n else 0)
