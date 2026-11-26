#!/usr/bin/env python3
"""Package a release: the engine and a built game.

  python3 tools/make_release.py --platform windows --exe ps2/ps2.exe \\
      --version v0.1.0 [--license NAME PATH ...] [--out dist]

Writes OUT/ps2-engine-VERSION-PLATFORM/ (emptied first) and beside it a
.zip (Windows) or .tar.gz (Linux, macOS, which keeps the programs'
permissions):

  * every file of this repository that git tracks or would track (so the
    release is its own complete source -- which the LGPL asks for, the game
    linking Nuked-OPN2 -- and the ROM map is there);
  * the game, built from that source, as ps2/ps2[.exe]. It makes its own
    files the first time it starts, in C, from the player's cartridge
    image (ps2/docs/first_start.md): nothing else is needed, no Python
    (until 2026-11-25 the Windows package carried python.org's
    embeddable Python and Pillow, and setup.bat or setup.sh);
  * licenses/: each --license NAME PATH (a file, or a folder's files) --
    what the game links in;
  * README.txt for players.

No game data goes in: ps2/assets/ holds only the files the repository
tracks (the ones the game makes are ignored by git, so a tree where it ran
packs the same), and it refuses a package with a Mega Drive cartridge image
in it.
"""
import argparse
import os
import shutil
import subprocess
import sys
import tarfile
import zipfile
from pathlib import Path

TOP = Path(__file__).resolve().parent.parent
NAME = "ps2-engine"

README = """Phantasy Star II -- a ROM-free engine, {version} for {platform_name}

This is a port of Phantasy Star II (SEGA, 1989) to modern computers, in C.
It contains none of the game: the game makes its files once, the first time
it starts, from your own cartridge image. Nothing is downloaded or sent.

1. You need your own image of Phantasy Star II for the USA/Europe,
   revision 02 -- the one commonly named "Phantasy Star II (UE) (REV02)".
   A .bin, .md, .gen or .smd file, or a .zip holding one, all work.

2. Start the game: {play_step}
   The first time, it asks for the image: drop the file onto its window,
   or press Enter and choose it. It checks the image, makes about a
   thousand files under ps2/assets/ from it and checks every one -- about
   a second -- and the game starts. Nothing else needs installing.

   From a terminal, give the image on the command line: {cli_step}

{platform_notes}Keys (Escape, then CONTROLS, changes them): the arrow keys or W A S D for
the pad, Enter for Start, Z X C for A B C, Escape for the menu (controls,
display, fullscreen, the CRT look, audio, language, COMPUTER PLAYS).
Gamepads work too. Saves and settings are kept beside the game, in
ps2/saves/, or in your user folder where that cannot be written.

The JAPANESE language needs the Japanese cartridge as well, and Excalibur_Z's
retranslation (github.com/ExcalZ/ps2-retranslation: Code, Download ZIP).
Choose JAPANESE on the LANGUAGE menu and the game asks for both.

This project is not affiliated with SEGA. The engine is MIT-licensed (LICENSE);
NOTICE.md and licenses/ list the parts that are other people's. This
folder is the complete source the game was built from: ps2/ is the game,
README.md says how to build it.
"""

PLATFORMS = {
    "windows": dict(
        platform_name="Windows (64-bit)",
        cli_step="ps2\\ps2.exe path\\to\\image.bin\n   (or drag the image "
                 "onto ps2\\ps2.exe).",
        play_step="run ps2\\ps2.exe.",
        platform_notes="Windows may warn that the program is from an unknown "
                       "publisher (it is not\nsigned): \"More info\", then "
                       "\"Run anyway\".\n\n"),
    "linux": dict(
        platform_name="Linux (x86-64)",
        cli_step="./ps2/ps2 /path/to/image.bin",
        play_step="./ps2/ps2",
        platform_notes="Choosing the image with Enter opens the desktop's "
                       "file dialog, which needs\nxdg-desktop-portal or "
                       "zenity; dropping the file onto the window, or the\n"
                       "command line, needs neither.\n\n"),
    "macos": dict(
        platform_name="macOS (Apple silicon and Intel)",
        cli_step="./ps2/ps2 /path/to/image.bin",
        play_step="./ps2/ps2",
        platform_notes="macOS blocks programs from the internet that are "
                       "not signed. Once, in this\nfolder: xattr -dr "
                       "com.apple.quarantine .\n\n"),
}


def git_files():
    out = subprocess.run(["git", "-C", str(TOP), "ls-files", "-z",
                          "--cached", "--others", "--exclude-standard"],
                         check=True, capture_output=True).stdout
    return sorted({p for p in out.decode("utf-8").split("\0") if p})


def archive(folder, platform):
    if platform == "windows":
        # Not with_suffix: a version's dots (v0.1.0) are not a suffix.
        path = folder.parent / (folder.name + ".zip")
        with zipfile.ZipFile(path, "w", zipfile.ZIP_DEFLATED,
                             compresslevel=9) as z:
            for p in sorted(folder.rglob("*")):
                if p.is_file():
                    z.write(p, p.relative_to(folder.parent).as_posix())
    else:
        path = folder.parent / (folder.name + ".tar.gz")
        with tarfile.open(path, "w:gz") as t:
            t.add(folder, arcname=folder.name)
    return path


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--platform", choices=sorted(PLATFORMS), required=True)
    ap.add_argument("--exe", required=True, help="the built game")
    ap.add_argument("--version", required=True)
    ap.add_argument("--license", nargs=2, action="append", default=[],
                    metavar=("NAME", "PATH"))
    ap.add_argument("--out", default="dist")
    a = ap.parse_args()

    files = git_files()
    folder = Path(a.out).resolve() / ("%s-%s-%s" % (NAME, a.version,
                                                    a.platform))
    if folder.exists():
        shutil.rmtree(folder)
    folder.mkdir(parents=True)
    for rel in files:
        src = TOP / rel
        if not src.is_file():
            continue                        # deleted, not yet committed
        dest = folder / rel
        dest.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(src, dest)

    exe = Path(a.exe)
    dest = folder / "ps2" / ("ps2.exe" if a.platform == "windows" else "ps2")
    shutil.copy2(exe, dest)
    os.chmod(dest, 0o755)

    licenses = folder / "licenses"
    licenses.mkdir()
    for name, path in a.license:
        path = Path(path)
        if path.is_dir():
            for p in sorted(path.rglob("*")):
                if p.is_file():
                    out = licenses / name / p.relative_to(path)
                    out.parent.mkdir(parents=True, exist_ok=True)
                    shutil.copy2(p, out)
        elif path.is_file():
            shutil.copy2(path, licenses / ("%s-%s" % (name, path.name)))
        else:
            raise SystemExit("--license %s: %s not found" % (name, path))
    shutil.copy2(TOP / "ps2" / "src" / "audio" / "LICENSE.ym3438",
                 licenses / "Nuked-OPN2-LGPL-2.1.txt")
    shutil.copy2(TOP / "ps2" / "assets" / "lang" / "retranslation" /
                 "LICENSE.txt", licenses / "retranslation-LICENSE.txt")

    text = README.format(version=a.version, **PLATFORMS[a.platform])
    if a.platform == "windows":
        text = text.replace("\n", "\r\n")
    (folder / "README.txt").write_text(text, newline="")

    # Nothing of the game: no cartridge image ("SEGA" at $100, the Mega
    # Drive header) in any file the package holds.
    for p in folder.rglob("*"):
        if p.is_file() and p.stat().st_size >= 0x10000:
            with open(p, "rb") as fh:
                fh.seek(0x100)
                if fh.read(4) == b"SEGA":
                    raise SystemExit("a cartridge image in the package: %s"
                                     % p)
    path = archive(folder, a.platform)
    print("%s (%.1f MB)" % (path, path.stat().st_size / 1e6))
    return 0


if __name__ == "__main__":
    sys.exit(main())
