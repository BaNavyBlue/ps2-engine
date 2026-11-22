#!/usr/bin/env python3
"""Make the game's files from your own Phantasy Star II cartridge image.

  python3 tools/setup.py IMAGE [JP_IMAGE RETRANSLATION]
  python3 tools/setup.py IMAGE [--ja JP_IMAGE --retranslation R]

The game does this itself the first time it starts: a window asks for the
image (drop it there, or choose it) and shows the steps. This script is the
same thing from a terminal. Both run ps2/tools/make_assets.py, the game's
own script; everything here is passed on to it.

IMAGE is the US/European cartridge, revision 02 -- the image the engine's
extractors are made for (ps2/tools/rom_map.json gives its size and SHA-1).
A .bin, .md or .gen file, a .smd (interleaved; it is put in order), one
with a copier's 512-byte header (it is dropped), or a .zip holding any of
those. Nothing is changed in it and nothing is sent anywhere.

It checks the image is that cartridge, runs every extractor the game's
Makefile names (ps2/Makefile, EXTRACTORS, in its order) on it into
ps2/assets/, writes the preload list, and checks every file made against
ps2/tools/assets.sha256, the files the engine was proved with.

The JAPANESE language is made from the Japanese cartridge (Kaerazaru Toki
no Owari ni) and Excalibur_Z's retranslation, which holds the Japanese
script (github.com/ExcalZ/ps2-retranslation: a clone, its "Download ZIP",
or that zip unpacked). Give them with the US image, or later on their own;
or choose JAPANESE on the game's LANGUAGE menu, which asks for them.

Run it again whenever you like: it makes the same files.
"""
import subprocess
import sys
from pathlib import Path

SCRIPT = Path(__file__).resolve().parent.parent / "ps2" / "tools" \
    / "make_assets.py"


def main():
    if len(sys.argv) < 2 or sys.argv[1] in ("-h", "--help"):
        print(__doc__)
        if len(sys.argv) >= 2:
            subprocess.run([sys.executable, str(SCRIPT), "--help"])
        return 0 if len(sys.argv) >= 2 else 2
    return subprocess.run([sys.executable, "-X", "utf8", str(SCRIPT),
                           *sys.argv[1:]]).returncode


if __name__ == "__main__":
    sys.exit(main())
