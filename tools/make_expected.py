#!/usr/bin/env python3
"""Write ps2/tools/assets.sha256: the files the setup must make.

  python3 tools/make_expected.py PRIVATE_REPO

Run after the setup (the game's first start, or tools/setup.py) has made
ps2/assets/ from the cartridge, and after the proofs passed on them.
Section "us" is every file under ps2/assets/ as it is now; section "ja"
the private tree's assets/lang/ja/ and its preload list
(assets/manifest.txt, which names the Japanese files too), which the
JAPANESE language's setup must make. Only hashes are written: nothing of
the files. A PNG's is its picture's (make_assets.file_digest), not its
bytes'.

The list is the engine's own, not the private tree's: it is written to
overlay/ps2/tools/, which the sync lays over ps2/, and to ps2/tools/,
where ps2/tools/make_assets.py reads it.
"""
import sys
from pathlib import Path

TOP = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(TOP / "ps2" / "tools"))
from make_assets import file_digest as digest  # noqa: E402

WHERE = ["overlay/ps2/tools/assets.sha256", "ps2/tools/assets.sha256"]


def main():
    if len(sys.argv) != 2:
        print(__doc__)
        return 2
    private = Path(sys.argv[1]).resolve() / "ps2"
    game = TOP / "ps2"
    lines = ["# The files ps2/tools/make_assets.py makes under ps2/, by "
             "SHA-256.",
             "# Written by the engine's tools/make_expected.py; checked by "
             "make_assets.py.",
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
    for where in WHERE:
        (TOP / where).parent.mkdir(parents=True, exist_ok=True)
        (TOP / where).write_text("\n".join(lines) + "\n", encoding="utf-8",
                                 newline="\n")
    print("%s: %d files (us), %d (ja)" % (", ".join(WHERE), len(us),
                                          len(ja) + 1))
    return 0


if __name__ == "__main__":
    sys.exit(main())
