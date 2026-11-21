#!/usr/bin/env python3
"""(build 128; /tmp/cp115/summ.py of builds 118-127) Whole-game reports
summarised: per seed the total ticks, the ticks in fights and on the
victory's pages, fights, tries that did not finish, lean go-ons and
whether it reached the ending; then the mean and the stages by their mean
ticks.

    python3 tools/measure/summarize_games.py DIR

DIR is a scratch tree whole_games.sh ran in (route_out/playthrough_s*/)."""
import re
import sys
from collections import defaultdict
from pathlib import Path


def main():
    if len(sys.argv) != 2:
        print(__doc__)
        return 2
    root = Path(sys.argv[1])
    per_stage = defaultdict(list)
    rows = []
    reports = sorted(root.glob("route_out/playthrough_s*/report.txt"),
                     key=lambda p: int(p.parent.name[len("playthrough_s"):]))
    for rep in reports:
        t = rep.read_text()
        seed = int(rep.parent.name[len("playthrough_s"):])
        m = re.search(r"^ticks (\d+); in fights (\d+) .*?victory pages (\d+)",
                      t, re.M)
        f = re.search(r"^fights (\d+);", t, re.M)
        fail = re.search(r"^tries that did not finish: (\d+)", t, re.M)
        lean = re.search(r"^lean go-ons: (\d+)", t, re.M)
        last = [ln for ln in t.splitlines() if ln.startswith("| ")][-1]
        done = "mother_brain" in last
        rows.append((seed, int(m.group(1)), int(m.group(2)), int(m.group(3)),
                     int(f.group(1)), int(fail.group(1)),
                     int(lean.group(1)) if lean else 0, done))
        for ln in t.splitlines():
            c = [x.strip() for x in ln.strip("|").split("|")]
            if len(c) >= 5 and c[0] and c[0] != "stage" \
                    and not c[0].startswith("-") and c[4].isdigit():
                per_stage[c[0]].append(int(c[4]))
    if not rows:
        print(f"no route_out/playthrough_s*/report.txt under {root}")
        return 1
    print("| seed | ticks | in fights | victory pages | fights | failed tries "
          "| lean | ending |")
    print("|---|---|---|---|---|---|---|---|")
    for r in rows:
        print("| " + " | ".join(str(x) for x in r) + " |")
    n = len(rows)
    print(f"mean ticks {sum(r[1] for r in rows) / n / 1e6:.2f} M "
          f"(min {min(r[1] for r in rows) / 1e6:.2f}, "
          f"max {max(r[1] for r in rows) / 1e6:.2f}); "
          f"fights {sum(r[4] for r in rows) / n:.0f}; "
          f"failed tries {sum(r[5] for r in rows)}; "
          f"endings {sum(r[7] for r in rows)}/{n}")
    top = sorted(per_stage.items(), key=lambda kv: -sum(kv[1]) / len(kv[1]))
    print("stages by mean ticks: " + ", ".join(
        f"{k} {sum(v) / len(v) / 1e6:.2f}" for k, v in top[:14]))
    return 0


if __name__ == "__main__":
    sys.exit(main())
