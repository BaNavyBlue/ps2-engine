#!/usr/bin/env python3
"""(build 128; /tmp/cp115/summ_end2.py of build 119) The endgames of an
endgame_run.sh tree summarised: ticks from the checkpoint to the ending,
Dark Force's losses inside its stage tries, failed stage tries, the boss
party's levels.

    python3 tools/measure/summarize_endgames.py DIR [--tag=STAGE] [-v]

--tag is the stage the runs started after (endgame_run.sh's FROM,
nei_sword by default): their files are report_from_STAGE.txt and
story_from_STAGE.txt. -v lists every seed."""
import re
import statistics
import sys
from pathlib import Path

SEEDS = list(range(1, 13)) + list(range(101, 113)) + list(range(201, 213))


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("-")]
    if len(args) != 1:
        print(__doc__)
        return 2
    root = Path(args[0])
    tag = "nei_sword"
    for a in sys.argv[1:]:
        if a.startswith("--tag="):
            tag = a[len("--tag="):]
    rows = []
    for s in SEEDS:
        d = root / "route_out" / f"playthrough_s{s}"
        rep = d / f"report_from_{tag}.txt"
        if not rep.exists():
            rows.append((s, None, 0, 0, False, ""))
            continue
        t = rep.read_text()
        story = (d / f"story_from_{tag}.txt").read_text()
        ticks = int(re.search(r"^ticks (\d+);", t, re.M).group(1))
        failed = int(re.search(r"^tries that did not finish: (\d+)",
                               t, re.M).group(1))
        losses = len(re.findall(r"dark_force: try \d+ lost", story))
        ended = bool(re.search(r"^\| mother_brain ", t, re.M))
        bp = re.search(r"^\| boss_party \|[^\n]*\| ([^|]*) \|$", t, re.M)
        rows.append((s, ticks, failed, losses, ended,
                     bp.group(1).strip() if bp else ""))
    done = [r for r in rows if r[1] is not None and r[4]]
    present = [r for r in rows if r[1] is not None]
    if not done:
        print(f"{root.name}: no endgame reached the ending "
              f"({len(present)} reports, tag {tag})")
        return 1
    ticks = [r[1] for r in done]
    print(f"{root.name}: endings {len(done)}/{len(present)}; endgame ticks "
          f"mean {statistics.mean(ticks) / 1e6:.2f} M, median "
          f"{statistics.median(ticks) / 1e6:.2f} M (min {min(ticks) / 1e6:.2f}, "
          f"max {max(ticks) / 1e6:.2f}); DF losses {sum(r[3] for r in rows)}; "
          f"failed stage tries {sum(r[2] for r in rows)}")
    if "-v" in sys.argv:
        for r in rows:
            print(f"  s{r[0]}: {(r[1] or 0) / 1e6:.2f} M, failed {r[2]}, "
                  f"DF losses {r[3]}, ended {r[4]} -- {r[5]}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
