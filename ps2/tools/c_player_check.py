#!/usr/bin/env python3
"""(build 171) The C player against the Python's: for each seed and
language, tools/playthrough.py and `route_probe --play` (src/player/,
docs/c_player.md) play from a new game to --to at the watch pace, and
their files must be the same -- the probe's log.txt line for line (every
command at the same tick), story.txt, the report less its seconds, the
probe's summary and every checkpoint, byte for byte. The C player's go
under route_out/cplay_s<seed>[_<lang>]/, which names itself in its files
where the Python's says playthrough_s<seed>[_<lang>].

    python3 tools/c_player_check.py --scratch DIR [--seeds 1-12]
        [--languages LIST] [--to arima] [--jobs 32] [--no-python]
        [--no-kain]

Run from ps2/, after `make route_probe`. Both play in DIR, a scratch tree
made afresh (assets/, tools/, routes/, src/game/script_ids.h and the
probe, as tools/measure/scratch_tree.sh makes one): playthrough.py writes
route_out/playthrough_s<seed>/, where the tree keeps the uploads' whole
games. --no-python keeps DIR and compares against the Python's games
already in it. (build 177) --no-kain plays both with playthrough's
--no-kain (Noah and the bosses with Rolf, Rudo, Amy and Anna). Exit 0
when every game matches."""
import argparse
import re
import shutil
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

PROBE = "./route_probe.exe" if Path("route_probe.exe").exists() \
    else "./route_probe"


def seeds_of(text):
    out = []
    for part in text.split(","):
        if "-" in part:
            a, b = part.split("-")
            out.extend(range(int(a), int(b) + 1))
        else:
            out.append(int(part))
    return out


def base_of(prefix, seed, lang):
    return f"{prefix}_s{seed}" + ("" if lang == "us" else f"_{lang}")


def scratch_tree(d):
    """tools/measure/scratch_tree.sh's tree, with the probe alone."""
    shutil.rmtree(d, ignore_errors=True)
    d.mkdir(parents=True)
    for sub in ("assets", "tools", "routes"):
        shutil.copytree(sub, d / sub)
    (d / "src/game").mkdir(parents=True)
    shutil.copy2("src/game/script_ids.h", d / "src/game/script_ids.h")
    shutil.copy2(PROBE, d / Path(PROBE).name)


def run_python(d, seed, lang, to, more=()):
    shutil.rmtree(d / "route_out" / base_of("playthrough", seed, lang),
                  ignore_errors=True)
    return subprocess.run(
        [sys.executable, "tools/playthrough.py", "--seed", str(seed),
         "--to", to, "--pace", "watch", "--language", lang, *more], cwd=d,
        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL).returncode


def run_c(d, seed, lang, to, more=()):
    base = base_of("cplay", seed, lang)
    shutil.rmtree(d / "route_out" / base, ignore_errors=True)
    # (build 180) The scratch tree's probe by its full path: Windows looks
    # for a relative program from the parent's folder, not `cwd`.
    probe = str((d / Path(PROBE).name).resolve())
    return subprocess.run(
        [probe, "--play", "--seed", str(seed), "--to", to, "--pace", "watch",
         "--language", lang, "--base", base, *more], cwd=d,
        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL).returncode


# The report's seconds: the first line's, the table's third column and a
# try that did not finish. (build 173) A stage's name may hold a digit
# (lab_b1).
SECONDS = [(re.compile(r", \d+ s$", re.M), ", T s"),
           (re.compile(r"^(\| [a-z0-9_]+ \| \d+ \| )[0-9.]+ \|", re.M),
            r"\1T |"),
           (re.compile(r"( try \d+ )\([0-9.]+ s\):"), r"\1(T s):")]


def text(path, rename=None, seconds=False):
    if not path.exists():
        return None
    t = path.read_text(encoding="utf-8", errors="replace")
    if rename:
        t = t.replace(*rename)
    if seconds:
        for rx, to in SECONDS:
            t = rx.sub(to, t)
    return t


def first_diff(a, b):
    la, lb = a.split("\n"), b.split("\n")
    for i, (x, y) in enumerate(zip(la, lb)):
        if x != y:
            return f"line {i + 1}: C {x.strip()!r} / Python {y.strip()!r}"
    return f"C {len(la)} lines, Python {len(lb)}"


def compare(d, seed, lang):
    py = d / "route_out" / base_of("playthrough", seed, lang)
    c = d / "route_out" / base_of("cplay", seed, lang)
    rename = ("cplay_s", "playthrough_s")
    bad = []
    for name, secs in (("probe/log.txt", False), ("story.txt", False),
                       ("probe/summary.txt", False), ("report.txt", True)):
        a = text(c / name, rename, secs)
        b = text(py / name, None, secs)
        if a is None or b is None:
            bad.append(f"{name}: {'C' if a is None else 'Python'} wrote none")
        elif a != b:
            bad.append(f"{name}: {first_diff(a, b)}")
    pc = sorted(p.relative_to(py) for p in (py / "checkpoints").glob("*")
                if p.is_file())
    cc = sorted(p.relative_to(c) for p in (c / "checkpoints").glob("*")
                if p.is_file())
    if pc != cc:
        bad.append(f"checkpoints: C {[str(p) for p in cc]}, Python "
                   f"{[str(p) for p in pc]}")
    for p in pc:
        if (c / p).exists() and (c / p).read_bytes() != (py / p).read_bytes():
            bad.append(f"{p}: differs")
    return bad


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--scratch", required=True, type=Path,
                    help="the scratch tree both play in (made afresh)")
    ap.add_argument("--seeds", default="1-12")
    ap.add_argument("--languages", default="us,retranslation,ja")
    ap.add_argument("--to", default="arima")
    ap.add_argument("--jobs", type=int, default=8)
    ap.add_argument("--no-python", action="store_true")
    ap.add_argument("--no-kain", action="store_true")
    args = ap.parse_args()
    more = ("--no-kain",) if args.no_kain else ()
    games = [(s, lang) for s in seeds_of(args.seeds)
             for lang in args.languages.split(",")]
    d = args.scratch.resolve()
    if d == Path.cwd().resolve() or Path.cwd().resolve() in d.parents:
        sys.exit("c_player_check: --scratch must be outside the tree")
    if args.no_python:
        shutil.copy2(PROBE, d / Path(PROBE).name)
    else:
        scratch_tree(d)
    with ThreadPoolExecutor(max_workers=args.jobs) as pool:
        jobs = []
        if not args.no_python:
            jobs += [pool.submit(run_python, d, s, lang, args.to, more)
                     for s, lang in games]
        jobs += [pool.submit(run_c, d, s, lang, args.to, more)
                 for s, lang in games]
        for j in jobs:
            j.result()
    same = 0
    for s, lang in games:
        bad = compare(d, s, lang)
        if bad:
            print(f"DIFF seed {s} {lang}")
            for b in bad:
                print(f"     {b}")
        else:
            same += 1
            print(f"same seed {s} {lang}")
    print(f"c_player_check: {same} of {len(games)} games the Python's, to "
          f"{args.to}" + (", no Kain" if args.no_kain else ""))
    return 0 if same == len(games) else 1


if __name__ == "__main__":
    sys.exit(main())
