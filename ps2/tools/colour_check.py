#!/usr/bin/env python3
"""(build 140) Every route's frames drawn both ways (docs/palette_effects.md,
step 2; docs/history.md, Build 140).

    python3 tools/colour_check.py [--jobs N] [--out DIR] [ROUTE ...]

Runs each route (all of routes/*.route by default) through route_probe with
PS2_COLOUR_CHECK=DIR, which composes every tick's frame as main.c does and
holds its colour plane to the palette path pixel for pixel, and writes each
shot a second time from the colour plane into DIR/<route>/. Then each of
those is read back with Pillow beside the route's own shot -- an indexed
PNG, read through its palette -- and compared pixel for pixel: the two
drawings of the frame by a decoder that is not the port's.

Prints one line a route and a total; exit 0 when every frame and every shot
is the same both ways and every route passes. (build 142) Each route's line
and the total also say how many pixels were drawn from pictures (full
colour's art, the portraits first) and which: a portrait route whose count
is 0 drew its portraits from their tiles. The routes' own files are
written as `make routes` writes them (the check adds nothing to them; take
tools/proofs/proof.sh after to see so). Build route_probe first.
"""
import argparse
import os
import re
import subprocess
import sys
from pathlib import Path

from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parent))
import jobs  # noqa: E402

TREE = Path(__file__).resolve().parent.parent


def probe_binary():
    exe = TREE / "route_probe.exe"
    return str(exe if exe.is_file() else TREE / "route_probe")


def run_route(task):
    """One route with the check on: (name, PASS line, report, shot results)."""
    name, out = task
    env = dict(os.environ, PS2_COLOUR_CHECK=out)
    env.pop("PS2_DRAW", None)
    summary = TREE / "route_out" / name / "summary.txt"
    if summary.exists():
        summary.unlink()
    proc = subprocess.run([probe_binary(), f"routes/{name}.route"], cwd=TREE,
                          env=env, capture_output=True, text=True)
    passed = summary.is_file() and summary.read_text().startswith("PASS")
    report_path = Path(out) / f"{name}.txt"
    report = (report_path.read_text().strip()
              if report_path.is_file() else f"MISSING {name}: no report")
    shots = []
    again_dir = Path(out) / name
    for own in sorted((TREE / "route_out" / name).glob("*.png")):
        again = again_dir / own.name
        if not again.is_file():
            shots.append((own.name, "no colour-plane shot"))
            continue
        a = Image.open(own).convert("RGB")
        b = Image.open(again).convert("RGB")
        if a.size != b.size:
            shots.append((own.name, f"sizes {a.size} {b.size}"))
            continue
        pa = a.tobytes()
        pb = b.tobytes()
        if pa != pb:
            n = sum(1 for i in range(0, len(pa), 3) if pa[i:i + 3] != pb[i:i + 3])
            shots.append((own.name, f"{n} pixels differ"))
        else:
            shots.append((own.name, None))
    return name, passed, report, shots, proc.returncode


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("routes", nargs="*", help="route names (default: all)")
    ap.add_argument("--jobs", type=int, default=1)
    ap.add_argument("--out", default="/tmp/colour_check",
                    help="where the colour-plane shots and reports go")
    args = ap.parse_args()

    names = args.routes or sorted(p.stem for p in (TREE / "routes").glob("*.route"))
    out = str(Path(args.out).resolve())
    os.makedirs(out, exist_ok=True)

    frames = differ = shots = shots_bad = failed = 0
    picture_pixels = picture_frames = 0
    things = set()
    for name, passed, report, shot_results, _code in jobs.ordered(
            run_route, [(n, out) for n in names], args.jobs):
        words = report.split()
        try:
            frames += int(words[2])
            differ += int(words[4].rstrip(";"))
        except (IndexError, ValueError):
            differ += 1
        m = re.search(r"(\d+) pixels from pictures in (\d+) frames"
                      r"(?: \(([^)]*)\))?", report.splitlines()[0])
        if m:
            picture_pixels += int(m.group(1))
            picture_frames += int(m.group(2))
            if m.group(3):
                things.update(t.strip() for t in m.group(3).split(","))
        bad = [f"{s}: {why}" for s, why in shot_results if why is not None]
        shots += len(shot_results)
        shots_bad += len(bad)
        if not passed:
            failed += 1
        state = "PASS" if passed else "FAIL"
        print(f"{state} {report.splitlines()[0]}; {len(shot_results)} shots "
              f"read back, {len(bad)} differ")
        for line in report.splitlines()[1:]:
            print(f"    {line}")
        for line in bad:
            print(f"    {line}")

    ok = differ == 0 and shots_bad == 0 and failed == 0
    print(f"{'SAME' if ok else 'DIFF'}: {len(names)} routes ({failed} failed), "
          f"{frames} frames composed both ways, {differ} differ; "
          f"{shots} shots read back both ways, {shots_bad} differ; "
          f"{picture_pixels} pixels from pictures in {picture_frames} frames"
          f" ({len(things)} pictures: {', '.join(sorted(things)) or 'none'})")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
