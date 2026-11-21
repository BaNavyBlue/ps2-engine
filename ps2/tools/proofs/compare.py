#!/usr/bin/env python3
"""(build 128) Two proof runs side by side -- what CLAUDE.md's "Proofs"
asks to match, without cmp or diff (MSYS2 has neither):

    python3 tools/proofs/compare.py /tmp/proofs/b127 /tmp/proofs/b128
    python3 tools/proofs/compare.py --all /tmp/qproofs/q_127 /tmp/qproofs/q_128

The first form: each single-result file (the crt, patches, audio and
probe_check lines, the attract WAV, the Dark Force trial, the build's
warnings, and (142a) colour_check's verdict when either run took it)
same or DIFF; the routes' PASS lines that differ; and the route
files whose checksums differ, or that one side has and the other not,
counted by path. (build 146) When both runs' colour_check_full.txt carry
each route's hash of every frame's colours, the routes whose hashes
differ. --all: every file of the two directories, same or DIFF.
Exit status 0 when everything matches.
"""
import hashlib
import re
import sys
from collections import Counter
from pathlib import Path

SINGLE = ("routes_pass.txt", "crt_check.txt", "crt_sim_check.txt",
          "patches_check.txt", "audio_check.txt", "attract.wav",
          "boss_trials_dark_force.json", "boss_trials_dark_force.md",
          "probe_check.txt", "build_warnings.txt",
          "colour_check.txt",   # (142a) PROOF_COLOUR=1's verdict
          "mods_check.txt")     # (157d) refused mods set aside


def digest(p):
    return hashlib.sha256(p.read_bytes()).hexdigest() if p.exists() else None


def sha_lines(p):
    """route path -> checksum, from a routes_files.sha."""
    out = {}
    for line in p.read_text(encoding="utf-8", errors="replace").splitlines():
        h, _, name = line.partition(" ")
        out[name.lstrip("*").strip()] = h
    return out


def main():
    args = sys.argv[1:]
    every = "--all" in args
    args = [a for a in args if a != "--all"]
    if len(args) != 2:
        print(__doc__)
        return 2
    a, b = Path(args[0]), Path(args[1])
    differ = 0
    if every:
        names = sorted({p.name for p in a.iterdir()} | {p.name for p in b.iterdir()})
        for n in names:
            same = digest(a / n) == digest(b / n)
            differ += not same
            print(f"{'same' if same else 'DIFF'} {n}")
        return 1 if differ else 0

    for n in SINGLE:
        same = digest(a / n) == digest(b / n)
        differ += not same
        print(f"{'same' if same else 'DIFF'} {n}")
    pa = (a / "routes_pass.txt").read_text().splitlines()
    pb = (b / "routes_pass.txt").read_text().splitlines()
    for line in sorted(set(pa) - set(pb)):
        print(f"  < {line}")
    for line in sorted(set(pb) - set(pa)):
        print(f"  > {line}")
    fa, fb = sha_lines(a / "routes_files.sha"), sha_lines(b / "routes_files.sha")
    changed = Counter()
    for name in sorted(set(fa) | set(fb)):
        if fa.get(name) != fb.get(name):
            how = ("changed" if name in fa and name in fb
                   else "only in " + (a.name if name in fa else b.name))
            changed[(name, how)] += 1
    print(f"route files: {len(fa)} and {len(fb)}; "
          f"{len(changed)} differ")
    for (name, how), _ in sorted(changed.items()):
        print(f"  {how}: {name}")
    differ += len(changed)
    differ += colour_hashes(a, b)
    return 1 if differ else 0


def hashes(p):
    """(build 146) route -> the frames' colours, from a colour_check_full.txt
    whose route lines carry them; {} when it has none."""
    out = {}
    if not p.exists():
        return out
    for line in p.read_text(encoding="utf-8", errors="replace").splitlines():
        m = re.match(r"(?:PASS|FAIL) (?:SAME|DIFF) (\S+): .*the frames' "
                     r"colours (\$[0-9A-F]{16})", line)
        if m:
            out[m.group(1)] = m.group(2)
    return out


def colour_hashes(a, b):
    """(build 146) Each route's hash of every frame's colour plane, when
    both runs took colour_check with them: the routes whose frames showed
    other colours. The number that differ (a route on one side only
    counts)."""
    ha = hashes(a / "colour_check_full.txt")
    hb = hashes(b / "colour_check_full.txt")
    if not ha or not hb:
        print("colour hashes: not in both runs")
        return 0
    bad = [n for n in sorted(set(ha) | set(hb)) if ha.get(n) != hb.get(n)]
    print(f"colour hashes: {len(ha)} and {len(hb)} routes; {len(bad)} differ")
    for n in bad:
        how = ("changed" if n in ha and n in hb
               else "only in " + (a.name if n in ha else b.name))
        print(f"  {how}: {n}")
    return len(bad)


if __name__ == "__main__":
    sys.exit(main())
