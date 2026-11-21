#!/usr/bin/env python3
"""(build 160) The rumble's check (src/game/rumble.c).

  python3 tools/rumble_check.py [--probe ./route_probe]
                                [--game ./ps2_headless] [--jobs N]
                                [--keep DIR]

Three parts, every run in scratch folders (nothing under route_out/ or
saves/ is touched):

1. Every route in routes/, and fuzz runs from many fights (formations 1
   to 259 a stride apart, three parties) and from two damage floors
   (Gaira's, the Biosystems Lab's), each through route_probe with
   PS2_RUMBLE_CHECK: what the watcher felt held, tick by tick, against
   what the game itself did -- its sound ids and each member's HP -- both
   ways (the rules are at PS2_RUMBLE_CHECK in tools/route_probe.c). PASS
   when no run broke a rule, no blow was felt only by
   RUMBLE_PENDING_MAX ("late"), every route passed, and every kind --
   hurt, fell, lost, floor -- was felt somewhere.

2. The headless game (main.c, through the platform's PS2_RUMBLE log, a
   gamepad connected by PS2_GAMEPADS): the Escape menu's GAMEPAD page --
   RUMBLE stepped HIGH, MEDIUM, LOW, OFF, each buzzing the gamepad at the
   new level (the test's peaks, the big motor 80%, the small 60%, scaled)
   and OFF not at all, and settings.json's "rumble" written; a PAD row
   buzzing its own gamepad alone, and nothing with none connected.

3. A fight in the headless game against the same fight in route_probe:
   Mother Brain against a level-1 Rolf, the route's presses replayed as
   PS2_PAD's, the PS2_RUMBLE log the route's `rumble-log` line for line
   -- at MEDIUM, at LOW and from a word settings.json does not know
   (MEDIUM); none at OFF; and the Escape menu, opened while Rolf's fall
   shakes, stilling the motors as it opens and keeping them still while
   it is open.

Exit 1 on any failure."""

import argparse
import os
import re
import shutil
import subprocess
import sys
import tempfile
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

TREE = Path(__file__).resolve().parent.parent
KINDS = ["hurt", "fell", "lost", "floor",
         "blast", "shake", "flight", "landed", "hole"]     # (build 161)
FELT = re.compile(r"felt hurt (\d+) fell (\d+) lost (\d+) floor (\d+) "
                  r"blast (\d+) shake (\d+) flight (\d+) landed (\d+) "
                  r"hole (\d+) "
                  r"late (\d+); HP drops elsewhere (\d+); (\d+) ticks; "
                  r"(\d+) broken")

failures = []


def check(name, ok, detail=""):
    print("%-52s %s %s" % (name, "ok" if ok else "FAILED", detail))
    if not ok:
        failures.append(name)


# ---- 1. the routes and the fuzz, under the rules ----

def fuzz_routes():
    """(name, text): the fuzz's runs."""
    runs = []
    parties = [(1, 1), (2, 6), (4, 20)]
    for f in list(range(1, 260, 6)) + [0x100, 0x101, 0x102, 0x103]:
        for i, (members, level) in enumerate(parties):
            runs.append(("fight_%03d_%d" % (f, i),
                         "start battle %d\nparty %d %d\nlimit 30000\n"
                         "fuzz 5000 %d\n" % (f, members, level, f + i)))
    for seed in range(1, 9):
        runs.append(("gaira_%d" % seed,
                     "start level $41 3 5\nparty %d %d\nlimit 30000\n"
                     "fuzz 6000 %d\n" % (1 + seed % 3, 1 + seed, seed)))
        runs.append(("lab_%d" % seed,
                     "start slot 1 routes/saves/biolab_recorder.json\n"
                     "limit 30000\nfuzz 6000 %d\n" % seed))
    return runs


def run_rules(probe, scratch, name, route, text):
    out = scratch / "out" / name
    rules = scratch / "rules" / (name + ".txt")
    if text is not None:
        route = scratch / "fuzz" / (name + ".route")
        route.write_text(text)
    env = dict(os.environ, PS2_RUMBLE_CHECK=str(rules))
    p = subprocess.run([probe, "-o", str(out), str(route)], cwd=TREE,
                       env=env, capture_output=True, text=True, timeout=1800)
    summary = p.stdout.strip().splitlines()[-1] if p.stdout.strip() else ""
    report = rules.read_text() if rules.exists() else ""
    return name, text is None, summary, report


def part_rules(probe, scratch, jobs):
    for d in ("out", "rules", "fuzz"):
        (scratch / d).mkdir(parents=True, exist_ok=True)
    work = [(r.stem, r, None) for r in sorted((TREE / "routes").glob(
        "*.route"))]
    work += [(n, None, t) for n, t in fuzz_routes()]
    totals = dict.fromkeys(KINDS + ["late", "elsewhere", "ticks", "broken"],
                           0)
    route_fails, broke, elsewhere = [], [], []
    with ThreadPoolExecutor(jobs) as pool:
        results = list(pool.map(lambda w: run_rules(probe, scratch, *w),
                                work))
    routes = 0
    for name, is_route, summary, report in results:
        m = FELT.search(report)
        if is_route:
            routes += 1
            if not summary.startswith("PASS"):
                route_fails.append(name)
        if m is None:
            broke.append("%s: no report" % name)
            continue
        for k, v in zip(KINDS + ["late", "elsewhere", "ticks", "broken"],
                        m.groups()):
            totals[k] += int(v)
        for line in report.splitlines():
            if line.startswith("broke"):
                broke.append("%s: %s" % (name, line))
            elif line.startswith("elsewhere"):
                elsewhere.append("%s: %s" % (name, line))
    print("rules: %d routes and %d fuzz runs, %d ticks; felt %s; late %d; "
          "HP drops elsewhere %d" % (
              routes, len(results) - routes, totals["ticks"],
              ", ".join("%s %d" % (k, totals[k]) for k in KINDS),
              totals["late"], totals["elsewhere"]))
    for line in broke[:20] + elsewhere[:5]:
        print("   ", line)
    check("rules: every route passes", not route_fails,
          " ".join(route_fails[:10]))
    check("rules: no rule broken", not broke, "%d" % len(broke))
    check("rules: no blow felt late", totals["late"] == 0,
          "%d" % totals["late"])
    never = [k for k in KINDS if totals[k] == 0]
    check("rules: every kind felt", not never, " ".join(never))


# ---- 2 and 3. the headless game ----

def link_tree(dst):
    """assets/ into dst: hard links, or copies where links fail."""
    def link(src, to):
        try:
            os.link(src, to)
        except OSError:
            shutil.copy2(src, to)
    shutil.copytree(TREE / "assets", dst / "assets", copy_function=link)


class Headless:
    def __init__(self, game, scratch):
        self.game = game
        self.base = scratch / "headless"
        self.base.mkdir(parents=True, exist_ok=True)
        link_tree(self.base)
        self.n = 0

    def run(self, frames, pad="", pads=1, settings=None, battle=None):
        """The game in its own saves and mods folders: the rumble log's
        lines (frame, pad, low, high) and settings.json as left."""
        self.n += 1
        run = self.base / ("run%d" % self.n)
        (run / "saves").mkdir(parents=True)
        (run / "mods").mkdir()
        if settings is not None:
            (run / "saves" / "settings.json").write_text(settings)
        log = run / "rumble.log"
        env = dict(os.environ, PS2_FRAMES=str(frames), PS2_GAMEPADS=str(pads),
                   PS2_RUMBLE=str(log), PS2_OUT=str(run / "out.ppm"),
                   PS2_SAVES=str(run / "saves"), PS2_MODS=str(run / "mods"))
        for k in ("PS2_PAD", "PS2_BATTLE", "PS2_LEVEL", "PS2_BUILDING",
                  "PS2_START", "PS2_TOUCH", "PS2_WAV"):
            env.pop(k, None)
        if pad:
            env["PS2_PAD"] = pad
        if battle is not None:
            env["PS2_BATTLE"] = str(battle)
        subprocess.run([self.game], cwd=self.base, env=env,
                       capture_output=True, text=True, timeout=900)
        lines = []
        if log.exists():
            for line in log.read_text().splitlines():
                f, p, lo, hi = line.split()
                lines.append((int(f), int(p), int(lo, 16), int(hi, 16)))
        sfile = run / "saves" / "settings.json"
        text = sfile.read_text() if sfile.exists() else ""
        return lines, text


def presses(start, step, *tokens):
    """PS2_PAD: each token a one-frame press, `step` frames apart; 'wN'
    waits N frames more."""
    out, f = [], start
    for t in tokens:
        if t.startswith("w"):
            f += int(t[1:])
            continue
        out.append("%d:%s" % (f, t))
        f += step
    return ",".join(out), f


def part_menu(h):
    # The menu on the title (it runs on under the menu): Escape, down to
    # GAMEPAD (eight rows), into the page, down twice to RUMBLE; right to
    # HIGH, then left to MEDIUM, LOW and OFF, 40 frames apart.
    pad, end = presses(600, 6, "esc", "down", "down", "down", "down",
                       "down", "down", "down", "down", "c", "down", "down",
                       "right", "w34", "left", "w34", "left", "w34", "left",
                       "w34")
    lines, settings = h.run(end + 10, pad)
    starts = [int(p.split(":")[0]) + 1 for p in pad.split(",")[-4:]]
    peaks = []
    for at, stop in zip(starts, starts[1:] + [end + 10]):
        got = [(lo, hi) for f, p, lo, hi in lines if at <= f < stop]
        peaks.append(max(got) if got else (0, 0))
    want = [(0xCCCC, 0x9999), (0x9999, 0x7332), (0x6666, 0x4CCC), (0, 0)]
    check("menu: RUMBLE's steps buzz at each level",
          peaks == want and '"rumble": "off"' in settings
          and all(p == 1 for _, p, _, _ in lines),
          " ".join("%04X/%04X" % x for x in peaks)
          + (" off written" if '"rumble": "off"' in settings else
             " settings: " + settings[:60]))

    # PAD 2 with two gamepads: C buzzes it alone, at MEDIUM; PAD 1 with
    # none connected buzzes nothing.
    pad, end = presses(600, 6, "esc", "down", "down", "down", "down",
                       "down", "down", "down", "down", "c", "down", "c")
    two, _ = h.run(end + 40, pad, pads=2)
    pad, end = presses(600, 6, "esc", "down", "down", "down", "down",
                       "down", "down", "down", "down", "c", "c")
    none, _ = h.run(end + 40, pad, pads=0)
    check("menu: a PAD row buzzes its own gamepad",
          two and all(p == 2 for _, p, _, _ in two)
          and max(lo for _, _, lo, _ in two) == 0x9999 and none == [],
          "%d lines, pads %s; none connected: %d" % (
              len(two), sorted({x[1] for x in two}), len(none)))


FIGHT = """start battle $103
rumble-log rumble.txt {level}
limit 4000
wait idle
press c
wait idle
press c
wait screen gameover
wait ticks 200
"""


def route_fight(probe, scratch, level):
    """The route's rumble log at `level`, its presses as PS2_PAD, and
    its ticks."""
    d = scratch / ("fight_%d" % level)
    d.mkdir(parents=True, exist_ok=True)
    route = d / "fight.route"
    route.write_text(FIGHT.format(level=level))
    out = d / "out"
    subprocess.run([probe, "-o", str(out), str(route)], cwd=TREE,
                   capture_output=True, text=True, timeout=600)
    lines = []
    for line in (out / "rumble.txt").read_text().splitlines():
        f, p, lo, hi = line.split()
        lines.append((int(f), int(p), int(lo, 16), int(hi, 16)))
    log = (out / "log.txt").read_text()
    # A press logged at tick T runs as the next tick: the headless game's
    # frame T (frames count from 0, ticks from 1).
    pad = ",".join("%s:c" % t for t in
                   re.findall(r"^\s*(\d+)  > \d+\s+press c", log, re.M))
    ticks = int(re.search(r"(\d+) ticks", log.splitlines()[-2]).group(1))
    return lines, pad, ticks


def part_fight(probe, h, scratch):
    want, pad, ticks = route_fight(probe, scratch, 2)
    got, _ = h.run(ticks, pad, battle=0x103)
    check("fight: the game's rumble is the route's, MEDIUM",
          got == want and len(got) > 20,
          "%d lines, %d the route's" % (len(got), len(want)))
    low, _, _ = route_fight(probe, scratch, 1)
    got, _ = h.run(ticks, pad, battle=0x103, settings='{"rumble": "low"}')
    check("fight: \"low\" from settings.json, half",
          got == low and len(got) > 20,
          "%d lines, %d the route's" % (len(got), len(low)))
    got, _ = h.run(ticks, pad, battle=0x103, settings='{"rumble": "loud"}')
    check("fight: a word not known is MEDIUM", got == want,
          "%d lines" % len(got))
    got, _ = h.run(ticks, pad, battle=0x103, settings='{"rumble": "off"}')
    check("fight: \"off\" sends nothing", got == [], "%d lines" % len(got))

    # The Escape menu opened four frames into the fall's shake, shut 40 on.
    first = want[0][0]
    opened, shut = first + 3, first + 43
    menu_pad = pad + ",%d:esc,%d:esc" % (opened, shut)
    got, _ = h.run(ticks, menu_pad, battle=0x103)
    before = [x for x in got if x[0] <= opened + 1]
    during = [x for x in got if opened + 1 < x[0] <= shut + 1]
    check("fight: the Escape menu stills the rumble",
          len(before) >= 2 and before[-1][0] == opened + 1
          and before[-1][2:] == (0, 0) and during == [],
          "%s; %d while open" % (before[-2:], len(during)))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--probe", default="./route_probe")
    ap.add_argument("--game", default="./ps2_headless")
    ap.add_argument("--jobs", type=int, default=os.cpu_count() or 4)
    ap.add_argument("--keep", default=None,
                    help="keep the scratch folders under DIR")
    a = ap.parse_args()
    probe = str(Path(a.probe).resolve())
    game = str(Path(a.game).resolve())
    if a.keep:
        Path(a.keep).mkdir(parents=True, exist_ok=True)
    scratch = Path(tempfile.mkdtemp(prefix="rumble_", dir=a.keep))
    try:
        part_rules(probe, scratch, a.jobs)
        h = Headless(game, scratch)
        part_menu(h)
        part_fight(probe, h, scratch)
    finally:
        if not a.keep:
            shutil.rmtree(scratch, ignore_errors=True)
    if failures:
        print("rumble: FAIL (%d): %s" % (len(failures), ", ".join(failures)))
        return 1
    print("rumble: PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
