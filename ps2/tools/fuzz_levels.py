#!/usr/bin/env python3
"""Fuzz every level, not just the one a new game starts on.

`start new` then `fuzz` rarely gets out of Paseo, so this starts a new
game on each level in turn (`start level L CX CY`) and fuzzes from there.
The cell is the first arrival any door, exit or interaction entry names
for that level -- somewhere the game itself puts the party -- with Gaira
($41, $42), which nothing enters by a door, on 24,5.

    make route_probe_asan
    python3 tools/fuzz_levels.py [--ticks 20000] [--seeds 1,2,3]
                                 [--probe ./route_probe_asan]
                                 [--levels 0,4,0x63] [--out fuzz_out]
                                 [--party N:LEVEL] [--no-party]
                                 [--jobs 12]

Each run is a route in <out>/routes/ and a log in <out>/L<l>_s<seed>/.
Afterwards it prints the runs that failed (the game busy past the
limit), every sanitizer report, and every distinct stderr line with how
many runs printed it and the first run that did, which is the route to
rerun with `-v` and a `shot`; then a coverage table: per level, the
fights, the distinct scripts shown, the levels reached from it and the
commands begun, and the levels with no fight at all.

The party starts at a fair level for the region (`party N LEVEL`, the
probe's own command): REGION_PARTY below by level index, from three at
level 8 in the first towers to four at 30 on Dezolis, so a fuzz on a
late dungeon is a fight and not a wipe on the first step. `--party
3:10` sets one for every level; `--no-party` is a new game's Rolf and
Nei at level 1 everywhere, as before build 66.
"""

import argparse
import concurrent.futures
import json
import os
import re
import subprocess
from collections import OrderedDict
from pathlib import Path

NO_DOOR_IN = {0x41: (24, 5), 0x42: (24, 5)}

# (first level index, members, level): the region a level index falls
# in, by the order LevelData lists them -- Motavia's early towers, the
# dams and the Biosystems lab, then Dezolis and its dungeons.
REGION_PARTY = [
    (0x00, 3, 8),       # Motavia, Paseo and the towns, Shure, Nido
    (0x20, 4, 14),      # Roron, the dams, the tubes
    (0x32, 4, 20),      # Biosystems lab, Climatrol, the control tower
    (0x3E, 4, 28),      # Gaira and the Dezolis towns
    (0x4A, 4, 34),      # the Dezolis dungeons
    (0x5C, 4, 40),      # Esper mansion, Noah
]


def region_party(level):
    members, lv = 2, 1
    for first, m, l in REGION_PARTY:
        if level >= first:
            members, lv = m, l
    return members, lv


def arrivals(levels_dir):
    cells = {}
    for f in sorted(levels_dir.glob("*.doors.json")):
        for e in json.loads(f.read_text())["entries"]:
            if "to_level" in e:
                cells.setdefault(e["to_level"], (e["to_x"], e["to_y"]))
    for e in json.loads((levels_dir / "exits.json").read_text())["entries"]:
        cells.setdefault(e["to_level"], (e["to_x"], e["to_y"]))
    for level, cell in NO_DOOR_IN.items():
        cells.setdefault(level, cell)
    return cells


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--ticks", type=int, default=20000)
    ap.add_argument("--seeds", default="1,2,3")
    ap.add_argument("--probe", default="./route_probe_asan")
    ap.add_argument("--levels", default="")
    ap.add_argument("--out", type=Path, default=Path("fuzz_out"))
    ap.add_argument("--party", default="",
                    help="N:LEVEL for every level, instead of REGION_PARTY")
    ap.add_argument("--no-party", action="store_true",
                    help="a new game's party at level 1 everywhere")
    ap.add_argument("--jobs", type=int, default=1,
                    help="(build 114) runs at once; the report the same at "
                         "any number")
    args = ap.parse_args()

    cells = arrivals(Path("assets/levels"))
    levels = ([int(x, 0) for x in args.levels.split(",")] if args.levels
              else sorted(cells))
    seeds = [int(s) for s in args.seeds.split(",")]
    (args.out / "routes").mkdir(parents=True, exist_ok=True)
    env = dict(os.environ, ASAN_OPTIONS="detect_leaks=0")

    # (build 114) Every run's route written first, each run its own route
    # file and its own <out>/<name>/, so they can run at once; the logs are
    # read in this order whatever order the runs end in.
    plan = []
    for level in levels:
        if level not in cells:
            plan.append((level, None, None))
            continue
        cx, cy = cells[level]
        if args.no_party:
            party = ""
        elif args.party:
            n, lv = args.party.split(":")
            party = f"party {int(n)} {int(lv)}\n"
        else:
            n, lv = region_party(level)
            party = f"party {n} {lv}\n"
        for seed in seeds:
            name = f"L{level:02X}_s{seed}"
            route = args.out / "routes" / f"{name}.route"
            route.write_text(f"start level {level} {cx} {cy}\n{party}"
                             f"limit 15000\nfuzz {args.ticks} {seed}\n", newline="\n")
            plan.append((level, name, route))

    def probe_run(item):
        _, name, route = item
        run = subprocess.run([args.probe, "-o", str(args.out / name),
                              str(route)], env=env,
                             capture_output=True, text=True)
        log = (args.out / name / "log.txt")
        return run, (log.read_text() if log.exists() else "")

    failed, sanitizer = [], []
    lines = OrderedDict()
    cover = {}
    pool = concurrent.futures.ThreadPoolExecutor(max_workers=max(1, args.jobs))
    done = pool.map(probe_run, [p for p in plan if p[1] is not None])
    for level, name, _ in plan:
        if name is None:
            print(f"level ${level:02X}: no arrival cell known, skipped")
            continue
        run, text = next(done)
        if run.returncode != 0:
            failed.append(f"{name}: {run.stdout.strip()}")
        c = cover.setdefault(level, {"fights": 0, "scripts": set(),
                                     "levels": set(), "commands": [0] * 4,
                                     "runs": 0})
        c["runs"] += 1
        c["fights"] += text.count("*     screen battle")
        c["scripts"] |= set(re.findall(r"\*\s+script (\$\w+)", text))
        c["levels"] |= {int(x, 16) for x in
                        re.findall(r"\*\s+level \$(\w+)", text)} - {level}
        m = re.search(r"commands begun: attack (\d+), tech (\d+), "
                      r"item (\d+), defend (\d+)", text)
        if m:
            for i in range(4):
                c["commands"][i] += int(m.group(i + 1))
        for line in text.splitlines():
            m = re.match(r"\s*\d+\s+\|\s+(.*)", line)
            if not m:
                continue
            msg = m.group(1)
            if "AddressSanitizer" in msg or "runtime error" in msg:
                sanitizer.append(f"{name}: {msg}")
            key = re.sub(r"\b\d{3,}\b", "N", msg)
            count, first = lines.get(key, (0, name))
            lines[key] = (count + 1, first)
        print(f"{name}: exit {run.returncode}", flush=True)
    pool.shutdown()

    print(f"\n{len(levels) * len(seeds)} runs, {len(failed)} failed")
    for f in failed:
        print("  " + f)
    print(f"{len(sanitizer)} sanitizer reports")
    for s in sanitizer:
        print("  " + s)
    print(f"{len(lines)} distinct stderr lines")
    for key, (count, first) in lines.items():
        print(f"  {count:4d}x  {key}   (first: {first})")

    print("\ncoverage: level  runs  fights  scripts  reached  "
          "attack/tech/item/defend")
    quiet = []
    for level in sorted(cover):
        c = cover[level]
        reached = " ".join(f"${x:02X}" for x in sorted(c["levels"]))
        print(f"  ${level:02X}   {c['runs']:4d}  {c['fights']:6d}  "
              f"{len(c['scripts']):7d}  {reached:20s} "
              + "/".join(str(x) for x in c["commands"]))
        if c["fights"] == 0:
            quiet.append(level)
    total = sum(c["fights"] for c in cover.values())
    shown = set().union(*(c["scripts"] for c in cover.values())) \
        if cover else set()
    print(f"  {len(cover)} levels, {total} fights, {len(shown)} distinct "
          f"scripts; no fight on: "
          + (" ".join(f"${x:02X}" for x in quiet) or "none"))


if __name__ == "__main__":
    main()
