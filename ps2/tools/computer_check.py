#!/usr/bin/env python3
"""(build 179) COMPUTER PLAYS's check, as the PS1 port's: a seed through the
window's own path -- the headless game, the Escape menu, COMPUTER PLAYS from
NEW GAME at each SPEED asked for -- plays the stand-alone tool's game,
`route_probe --computer` (src/player/computer.c on the window's host): the
same lines from the interpreter (every command at the same tick: the game's
PS2_COMPUTER_LOG against the probe's log.txt), story.txt, the report less
its seconds, every checkpoint, the slots and last.json, byte for byte
(the window's shots read as the probe's: it draws its own frames).
--python plays it in the window with the Python driver as well
(PS2_COMPUTER_DRIVER=python, tools/computer_plays.py), the reference the C
player is held to while it is kept, its paths taken with slashes (a
Python on Windows writes backslashes).

    python3 tools/computer_check.py --scratch DIR [--seeds 1]
        [--languages us] [--speeds fast,auto,auto_2x] [--to STAGE]
        [--python] [--jobs 16] [--keep] [--more "ARGS"]

Run from ps2/, after `make headless route_probe` (`make computer_check`
runs seed 1 in us to the ending at the three speeds). Every game plays in a
scratch tree of its own under DIR (assets/, tools/, routes/,
src/game/script_ids.h and the two programs), the window's settings.json
saying the language and the speed; a game whose files all match is
removed unless --keep (a whole game's log is some 300 MB). --more passes
COMPUTER PLAYS more arguments (PS2_COMPUTER_ARGS). Exit 0 when every game
matches."""
import argparse
import json
import os
import re
import shutil
import subprocess
import sys
import time
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

EXE = ".exe" if Path("ps2_headless.exe").exists() else ""
GAME = f"ps2_headless{EXE}"
PROBE = f"route_probe{EXE}"
SPEEDS = ("normal", "auto", "2x", "auto_2x", "fast")

# The Escape menu at frame 400, six downs to COMPUTER PLAYS (from NEW
# GAME), C for its lamp, Escape: the computer starts at settings.json's
# SPEED (tools/measure/computer_plays_speed.sh's way).
PAD = "400:esc,410:down,420:down,430:down,440:down,450:down,460:down," \
      "470:c,480:esc"


def seeds_of(text):
    out = []
    for part in text.split(","):
        if "-" in part:
            a, b = part.split("-")
            out.extend(range(int(a), int(b) + 1))
        else:
            out.append(int(part))
    return out


def tree(d):
    """tools/measure/scratch_tree.sh's tree. (build 180) Its own two
    programs are the ones run, by their full paths: Windows looks for a
    relative program from the parent's folder, not `cwd`, so until 180 the
    tree's were run (the same files), and a build there could not write
    them while a check ran."""
    shutil.rmtree(d, ignore_errors=True)
    d.mkdir(parents=True)
    for sub in ("assets", "tools", "routes"):
        shutil.copytree(sub, d / sub)
    (d / "src/game").mkdir(parents=True)
    shutil.copy2("src/game/script_ids.h", d / "src/game/script_ids.h")
    shutil.copy2(GAME, d / GAME)
    shutil.copy2(PROBE, d / PROBE)


def run_window(d, seed, lang, speed, to, more, python):
    tree(d)
    (d / "saves").mkdir()
    (d / "saves/settings.json").write_text(
        json.dumps({"language": lang, "computer_speed": speed}) + "\n")
    env = dict(os.environ)
    env.pop("PS2_COMPUTER_DRIVER", None)
    if python:
        env["PS2_COMPUTER_DRIVER"] = "python"
    env.update(PS2_PAD=PAD, PS2_COMPUTER_QUIT="1",
               PS2_COMPUTER_ARGS=f"--seed {seed} --to {to} {more}".strip(),
               PS2_COMPUTER_LOG=str((d / "computer_log.txt").resolve()),
               PS2_FRAMES="2000000000")
    t0 = time.time()
    with open(d / "game.log", "wb") as f:
        rc = subprocess.run([str((d / GAME).resolve())], cwd=d, env=env,
                            stdout=f, stderr=subprocess.STDOUT).returncode
    return rc, time.time() - t0


def run_tool(d, seed, lang, to, more):
    tree(d)
    t0 = time.time()
    with open(d / "out.txt", "wb") as f:
        rc = subprocess.run([str((d / PROBE).resolve()), "--computer",
                             "--language", lang,
                             "--seed", str(seed), "--to", to, *more.split()],
                            cwd=d, stdout=f, stderr=subprocess.STDOUT
                            ).returncode
    return rc, time.time() - t0


def probe_lines(path):
    """The probe's log.txt as the game's PS2_COMPUTER_LOG has it: less its
    two header lines, the port's stderr ("    |     ") and the summary."""
    with open(path, encoding="utf-8", errors="replace", newline="") as f:
        f.readline()
        f.readline()
        for ln in f:
            if ln == "\n":
                return
            if "    |     " not in ln:
                yield ln


# The window draws its own frames: its shot lines say "(not drawn here)"
# where the probe's name the PNG (probe.c, cmd_shot).
NOT_DRAWN = re.compile(r"^(.*     shot \S+) \(not drawn here\)$")


def window_lines(path):
    """The game's PS2_COMPUTER_LOG, its shots as the probe's."""
    with open(path, encoding="utf-8", errors="replace", newline="") as f:
        for ln in f:
            m = NOT_DRAWN.match(ln.rstrip("\n"))
            yield m.group(1) + ".png\n" if m else ln


def first_difference(a, b):
    """None when the two line streams are the same, else where they part."""
    n = 0
    for x, y in zip(a, b):
        n += 1
        if x != y:
            return f"line {n}: {x.rstrip()!r} / {y.rstrip()!r}"
    xa = next(a, None)
    yb = next(b, None)
    if xa is not None or yb is not None:
        return f"line {n + 1}: one log ends ({xa!r} / {yb!r})"
    return None


SECS = (re.compile(r", \d+ s$", re.M),
        re.compile(r"^(\| [^|]+ \| \d+ \| )[0-9.]+( \|)", re.M))


def report_text(path):
    text = path.read_text(encoding="utf-8")
    text = SECS[0].sub(", N s", text)
    return SECS[1].sub(r"\1S\2", text)


def files_of(d):
    """The run's own files, by name under saves/computer/."""
    base = d / "saves/computer"
    out = {}
    for sub in ("", "checkpoints", "slots"):
        p = base / sub if sub else base
        if not p.is_dir():
            continue
        for f in sorted(p.iterdir()):
            if f.is_file() and f.name in ("story.txt", "last.json") or (
                    sub and f.is_file()):
                out[(sub + "/" if sub else "") + f.name] = f.read_bytes()
    return out


def slashes(text):
    """A Python on Windows writes its paths with backslashes (last.json's
    doubled): the C player, and MSYS2's Python, with slashes."""
    if isinstance(text, bytes):
        return text.replace(b"\\\\", b"/").replace(b"\\", b"/")
    return text.replace("\\\\", "/").replace("\\", "/")


def compare(tool, other, python=False):
    """What differs between the tool's run and one in the window (the
    Python's paths taken with slashes)."""
    bad = []
    mine = window_lines(other / "computer_log.txt")
    if python:
        mine = (slashes(ln) for ln in mine)
    diff = first_difference(mine,
                            probe_lines(tool / "saves/computer/probe/log.txt"))
    if diff:
        bad.append("log " + diff)
    a, b = files_of(tool), files_of(other)
    if python:
        b = {k: slashes(v) for k, v in b.items()}
    for name in sorted(set(a) | set(b)):
        if a.get(name) != b.get(name):
            bad.append(f"{name} differs" if name in a and name in b
                       else f"{name} only in one")
    if report_text(tool / "saves/computer/report.txt") \
            != report_text(other / "saves/computer/report.txt"):
        bad.append("report.txt differs")
    return bad


def check_game(scratch, seed, lang, plays, keep):
    """The game's runs (their futures: the tool's first) compared, the
    lines to print, and its matching runs removed unless --keep."""
    name = f"s{seed}_{lang}"
    tool = scratch / f"tool_{name}"
    rc, secs = plays[0][2].result()
    lines = [f"{name}: tool exit {rc}, {secs:.0f} s"]
    ok = rc in (0, 1)
    for who, d, fut in plays[1:]:
        wrc, wsecs = fut.result()
        bad = compare(tool, d, who.startswith("python"))
        if wrc != 0:
            bad.append(f"game exit {wrc}")
        lines.append(f"   {who}: {wsecs:.0f} s, "
                     + ("same" if not bad else "; ".join(bad)))
        ok = ok and not bad
        if not bad and not keep:
            shutil.rmtree(d, ignore_errors=True)
    rep = tool / "saves/computer/report.txt"
    fin = [ln for ln in rep.read_text().splitlines()
           if ln.startswith("finished:")] if rep.exists() else []
    lines[0] += f", {fin[0] if fin else 'no report'}"
    if ok and not keep:
        shutil.rmtree(tool, ignore_errors=True)
    return ok, "\n".join(lines)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--scratch", required=True, type=Path)
    ap.add_argument("--seeds", default="1")
    ap.add_argument("--languages", default="us")
    ap.add_argument("--speeds", default="fast,auto,auto_2x")
    ap.add_argument("--to", default="mother_brain")
    ap.add_argument("--python", action="store_true")
    ap.add_argument("--jobs", type=int, default=16)
    ap.add_argument("--keep", action="store_true")
    ap.add_argument("--more", default="")
    args = ap.parse_args()
    speeds = [s for s in args.speeds.split(",") if s]
    for sp in speeds:
        if sp not in SPEEDS:
            sys.exit(f"computer_check: no speed {sp} ({', '.join(SPEEDS)})")
    for prog in (GAME, PROBE):
        if not Path(prog).exists():
            sys.exit(f"computer_check: no {prog} (make headless "
                     f"route_probe)")
    args.scratch.mkdir(parents=True, exist_ok=True)
    games = [(seed, lang) for lang in args.languages.split(",")
             for seed in seeds_of(args.seeds)]
    t0 = time.time()
    results = []
    with ThreadPoolExecutor(args.jobs) as ex:
        planned = []
        for seed, lang in games:
            name = f"s{seed}_{lang}"
            plays = [("tool", args.scratch / f"tool_{name}",
                      ex.submit(run_tool, args.scratch / f"tool_{name}",
                                seed, lang, args.to, args.more))]
            runs = [(sp, False) for sp in speeds] \
                + ([("fast", True)] if args.python else [])
            for speed, py in runs:
                who = f"{'python' if py else 'window'} {speed}"
                d = args.scratch / f"{who.replace(' ', '_')}_{name}"
                plays.append((who, d, ex.submit(run_window, d, seed, lang,
                                                speed, args.to, args.more,
                                                py)))
            planned.append((seed, lang, plays))
        for seed, lang, plays in planned:
            ok, text = check_game(args.scratch, seed, lang, plays, args.keep)
            print(text, flush=True)
            results.append((ok, text))
    good = sum(1 for ok, _ in results if ok)
    print(f"computer_check: {good} of {len(results)} games the tool's "
          f"through the window ({', '.join(speeds)}"
          + (", and the Python's" if args.python else "")
          + f"), {time.time() - t0:.0f} s")
    return 0 if good == len(results) else 1


if __name__ == "__main__":
    sys.exit(main())
