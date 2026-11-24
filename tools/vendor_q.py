#!/usr/bin/env python3
"""Copy Cycfi's Q headers, the part the game builds with, into Q/.

  python3 tools/vendor_q.py PATH/TO/Q-CLONE

The game's Q synth (ps2/src/audio/q/) reads only Q's headers: q_lib/include
and its infra submodule's infra/include. Q/ holds those, unchanged, with
Q's LICENSE, so a clone of this repository (or its "Download ZIP") builds
the game with Q from plain `make` -- the port's Makefile looks for Q in the
repository's top folder first (ps2/../Q). The clone must be checked out at
the version wanted, with its submodule (git clone --recurse-submodules
--branch v1.5.4 https://github.com/cycfi/Q.git). Left out: infra's two test
frameworks (catch.hpp, doctest.hpp), which nothing includes. Q/VERSION
records what was copied.
"""
import shutil
import subprocess
import sys
from pathlib import Path

TOP = Path(__file__).resolve().parent.parent
DEST = TOP / "Q"
TREES = ["q_lib/include", "infra/include"]
LEFT_OUT = {"infra/include/infra/catch.hpp", "infra/include/infra/doctest.hpp"}


def git(repo, *args):
    return subprocess.run(["git", "-C", str(repo), *args], check=True,
                          capture_output=True, text=True).stdout.strip()


def main():
    if len(sys.argv) != 2:
        raise SystemExit(__doc__)
    src = Path(sys.argv[1]).resolve()
    for t in TREES:
        if not (src / t).is_dir():
            raise SystemExit("%s: no %s (cloned with --recurse-submodules?)"
                             % (src, t))
    if DEST.exists():
        shutil.rmtree(DEST)
    count = 0
    for t in TREES:
        for p in sorted((src / t).rglob("*")):
            rel = p.relative_to(src).as_posix()
            if not p.is_file() or rel in LEFT_OUT:
                continue
            out = DEST / rel
            out.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(p, out)
            count += 1
    shutil.copyfile(src / "LICENSE", DEST / "LICENSE")
    version = git(src, "describe", "--tags", "--always")
    commit = git(src, "rev-parse", "HEAD")
    infra = git(src / "infra", "rev-parse", "HEAD")
    (DEST / "VERSION").write_text(
        "Cycfi Q %s (%s), https://github.com/cycfi/Q\n"
        "infra %s, https://github.com/cycfi/infra\n"
        "%d headers: q_lib/include and infra/include, unchanged, less\n"
        "infra's catch.hpp and doctest.hpp. Copied by tools/vendor_q.py.\n"
        % (version, commit, infra, count), newline="\n")
    print("Q/: %d headers from Q %s" % (count, version))
    return 0


if __name__ == "__main__":
    sys.exit(main())
