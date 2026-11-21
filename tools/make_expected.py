#!/usr/bin/env python3
"""Write tools/assets.sha256: the files tools/setup.py must make.

  python3 tools/make_expected.py PRIVATE_REPO

Run after tools/setup.py has made ps2/assets/ from the cartridge, and after
the proofs passed on them. Section "us" is every file under ps2/assets/ as
it is now; section "ja" the private tree's assets/lang/ja/ and its preload
list (assets/manifest.txt, which names the Japanese files too), which
`setup.py --ja` must make. Only hashes are written: nothing of the files.
A PNG's is its picture's (setup.file_digest), not its bytes'.
"""
import sys
from pathlib import Path

TOP = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(TOP / "tools"))
from setup import file_digest as digest  # noqa: E402


def main():
    if len(sys.argv) != 2:
        print(__doc__)
        return 2
    private = Path(sys.argv[1]).resolve() / "ps2"
    game = TOP / "ps2"
    lines = ["# The files tools/setup.py makes under ps2/, by SHA-256.",
             "# Written by tools/make_expected.py; checked by setup.py.",
             "# section us"]
    us = sorted(p for p in (game / "assets").rglob("*") if p.is_file()
                and "lang/ja/" not in p.relative_to(game).as_posix())
    for p in us:
        lines.append("%s  %s" % (digest(p), p.relative_to(game).as_posix()))
    lines.append("# section ja")
    ja = sorted(p for p in (private / "assets" / "lang" / "ja").rglob("*")
                if p.is_file())
    for p in ja + [private / "assets" / "manifest.txt"]:
        lines.append("%s  %s" % (digest(p),
                                 p.relative_to(private).as_posix()))
    (TOP / "tools" / "assets.sha256").write_text("\n".join(lines) + "\n",
                                                 encoding="utf-8",
                                                 newline="\n")
    print("tools/assets.sha256: %d files (us), %d (ja)" % (len(us),
                                                          len(ja) + 1))
    return 0


if __name__ == "__main__":
    sys.exit(main())
