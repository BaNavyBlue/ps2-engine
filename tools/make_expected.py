#!/usr/bin/env python3
"""Write ps2/tools/assets.sha256: the files the game must make.

  python3 tools/make_expected.py PRIVATE_REPO

Run after the game's files are made under ps2/assets/ from the cartridge
(the game's first start, or `./ps2_extract assets IMAGE` in ps2/) and the
proofs passed on them, with ps2/ps2_extract built (`make ps2_extract`).
Section "us" is every file under ps2/assets/ as it is now; section "ja"
the private tree's assets/lang/ja/ and its preload list
(assets/manifest.txt, which names the Japanese files too), which the
JAPANESE language must make. Only hashes are written: nothing of the
files. A PNG's is its picture's, not its bytes': `ps2_extract digest`
gives each file's as the game checks it (ps2/src/extract/exdigest.c).
Nothing here needs more than Python's own library.

The list is the engine's own, not the private tree's: it is written to
overlay/ps2/tools/, which the sync lays over ps2/, and to ps2/tools/,
where the game reads it.
"""
import subprocess
import sys
from pathlib import Path

TOP = Path(__file__).resolve().parent.parent
WHERE = ["overlay/ps2/tools/assets.sha256", "ps2/tools/assets.sha256"]
BATCH = 200                     # files a command line, well inside Windows'


def tool():
    for name in ("ps2_extract", "ps2_extract.exe"):
        path = TOP / "ps2" / name
        if path.is_file():
            return path
    raise SystemExit("no ps2/ps2_extract: build it first (in ps2/, make "
                     "ps2_extract)")


def digests(exe, folder, files):
    """Each file's digest, by its path under FOLDER, in the list's order."""
    names = [p.relative_to(folder).as_posix() for p in files]
    lines = []
    for i in range(0, len(names), BATCH):
        part = names[i:i + BATCH]
        out = subprocess.run([str(exe), "digest", *part], cwd=folder,
                             check=True, capture_output=True,
                             encoding="utf-8").stdout.splitlines()
        if len(out) != len(part):
            raise SystemExit("ps2_extract digest gave %d lines for %d files"
                             % (len(out), len(part)))
        for line, name in zip(out, part):
            digest, _, said = line.partition("  ")
            if said != name or digest == "(unreadable)":
                raise SystemExit("%s: %s" % (name, line))
            lines.append("%s  %s" % (digest, name))
    return lines


def main():
    if len(sys.argv) != 2:
        print(__doc__)
        return 2
    exe = tool()
    private = Path(sys.argv[1]).resolve() / "ps2"
    game = TOP / "ps2"
    lines = ["# The files the game makes under ps2/ (ps2/src/extract/; "
             "ps2/tools/make_assets.py", "# is its Python twin), by "
             "SHA-256. Written by the engine's tools/make_expected.py;",
             "# checked by the game, ps2_extract and make_assets.py.",
             "# section us"]
    us = sorted(p for p in (game / "assets").rglob("*") if p.is_file()
                and "lang/ja/" not in p.relative_to(game).as_posix())
    lines += digests(exe, game, us)
    lines.append("# section ja")
    ja = sorted(p for p in (private / "assets" / "lang" / "ja").rglob("*")
                if p.is_file())
    lines += digests(exe, private, ja + [private / "assets" / "manifest.txt"])
    for where in WHERE:
        (TOP / where).parent.mkdir(parents=True, exist_ok=True)
        (TOP / where).write_text("\n".join(lines) + "\n", encoding="utf-8",
                                 newline="\n")
    print("%s: %d files (us), %d (ja)" % (", ".join(WHERE), len(us),
                                          len(ja) + 1))
    return 0


if __name__ == "__main__":
    sys.exit(main())
