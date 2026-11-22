#!/usr/bin/env python3
"""Package a release: the engine, a built game, and what the setup needs.

  python3 tools/make_release.py --platform windows --exe ps2/ps2.exe \\
      --version v0.1.0 [--python-zip F --python-sha256 X --pillow-wheel W] \\
      [--license NAME PATH ...] [--out dist]

Writes OUT/ps2-engine-VERSION-PLATFORM/ (emptied first) and beside it a
.zip (Windows) or .tar.gz (Linux, macOS, which keeps the programs'
permissions):

  * every file of this repository that git tracks or would track (so the
    release is its own complete source -- which the LGPL asks for, the game
    linking Nuked-OPN2 -- and the setup and the ROM map are there);
  * the game, built from that source, as ps2/ps2[.exe];
  * on Windows, python/: python.org's embeddable Python (its SHA-256
    checked) with Pillow unpacked into it, so the setup needs nothing
    installed -- the game finds it at ../python/ on its first start
    (ps2/docs/first_start.md); elsewhere the setup uses the system's
    Python 3 and Pillow;
  * licenses/: each --license NAME PATH (a file, or a folder's files) --
    what the game links in -- with Python's and Pillow's on Windows;
  * README.txt for players, and setup.bat or setup.sh.

No game data goes in: ps2/assets/ holds only the files the repository
tracks (the setup's are ignored by git, so a tree where it ran packs the
same), and it refuses a package with a Mega Drive cartridge image in it.
"""
import argparse
import hashlib
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
   thousand files under ps2/assets/ from it and checks every one -- a few
   seconds -- and the game starts.{python_note}

   The same from a terminal: {setup_step}

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

SETUP_BAT = r"""@echo off
rem Makes the game's files from your own cartridge image.
rem Drag the image onto this file, or run:  setup.bat path\to\your-image.bin
rem (The game does the same the first time it starts: ps2\ps2.exe.)
setlocal
if "%~1"=="" goto usage
"%~dp0python\python.exe" "%~dp0tools\setup.py" %*
set "STATUS=%ERRORLEVEL%"
echo.
pause
exit /b %STATUS%
:usage
echo Drag your Phantasy Star II cartridge image (USA/Europe, revision 02)
echo onto setup.bat, or run:  setup.bat path\to\your-image.bin
echo.
pause
exit /b 2
"""

SETUP_SH = """#!/bin/sh
# Makes the game's files from your own cartridge image:
#   ./setup.sh /path/to/your-image.bin
here=$(cd "$(dirname "$0")" && pwd)
py=${PYTHON:-python3}
if ! "$py" -c "import PIL" 2>/dev/null; then
    echo "The setup needs Python 3 with Pillow:"
    echo "  Debian/Ubuntu: sudo apt install python3 python3-pil"
    echo "  Fedora:        sudo dnf install python3 python3-pillow"
    echo "  Arch:          sudo pacman -S python python-pillow"
    echo "  macOS:         brew install python pillow"
    exit 1
fi
exec "$py" "$here/tools/setup.py" "$@"
"""

PLATFORMS = {
    "windows": dict(
        platform_name="Windows (64-bit)",
        setup_step="drag your image onto setup.bat\n   (or run: setup.bat "
                   "path\\to\\image.bin).",
        play_step="run ps2\\ps2.exe.",
        python_note="",
        platform_notes="Windows may warn that the program is from an unknown "
                       "publisher (it is not\nsigned): \"More info\", then "
                       "\"Run anyway\".\n\n"),
    "linux": dict(
        platform_name="Linux (x86-64)",
        setup_step="./setup.sh /path/to/image.bin",
        play_step="./ps2/ps2",
        python_note="\n   It needs Python 3 with Pillow (Debian/Ubuntu: "
                    "sudo apt install python3-pil;\n   Fedora: "
                    "python3-pillow; Arch: python-pillow).",
        platform_notes=""),
    "macos": dict(
        platform_name="macOS (Apple silicon and Intel)",
        setup_step="./setup.sh /path/to/image.bin",
        play_step="./ps2/ps2",
        python_note="\n   It needs Python 3 with Pillow: brew install "
                    "python pillow.",
        platform_notes="macOS blocks programs from the internet that are "
                       "not signed. Once, in this\nfolder: xattr -dr "
                       "com.apple.quarantine .\n\n"),
}


def git_files():
    out = subprocess.run(["git", "-C", str(TOP), "ls-files", "-z",
                          "--cached", "--others", "--exclude-standard"],
                         check=True, capture_output=True).stdout
    return sorted({p for p in out.decode("utf-8").split("\0") if p})


def sha256(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def add_python(dest, pyzip, want, wheel, licenses):
    """python.org's embeddable Python into dest/python/, Pillow into its
    site-packages, and its ._pth told to read them."""
    got = sha256(pyzip)
    if got != want:
        raise SystemExit("%s: SHA-256 %s, not %s" % (pyzip, got, want))
    py = dest / "python"
    with zipfile.ZipFile(pyzip) as z:
        z.extractall(py)
    pth = list(py.glob("python3*._pth"))
    if len(pth) != 1:
        raise SystemExit("no python3*._pth in %s" % pyzip)
    lines = pth[0].read_text().splitlines()
    lines = [("import site" if l.strip() == "#import site" else l)
             for l in lines]
    if "Lib\\site-packages" not in lines:
        lines.insert(len(lines) - 1 if lines[-1] == "import site"
                     else len(lines), "Lib\\site-packages")
    pth[0].write_text("\n".join(lines) + "\n")
    site = py / "Lib" / "site-packages"
    with zipfile.ZipFile(wheel) as z:
        z.extractall(site)
    shutil.copy2(py / "LICENSE.txt", licenses / "python-LICENSE.txt")
    found = [p for p in site.glob("pillow-*.dist-info/**/LICENSE*")
             if p.is_file()]
    if not found:
        raise SystemExit("no licence in %s" % wheel)
    shutil.copy2(found[0], licenses / "pillow-LICENSE.txt")


def archive(folder, platform):
    if platform == "windows":
        path = folder.with_suffix(".zip")
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
    ap.add_argument("--python-zip")
    ap.add_argument("--python-sha256")
    ap.add_argument("--pillow-wheel")
    ap.add_argument("--license", nargs=2, action="append", default=[],
                    metavar=("NAME", "PATH"))
    ap.add_argument("--out", default="dist")
    a = ap.parse_args()
    if a.platform == "windows" and not (a.python_zip and a.python_sha256
                                        and a.pillow_wheel):
        ap.error("windows needs --python-zip, --python-sha256 and "
                 "--pillow-wheel")

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

    if a.platform == "windows":
        add_python(folder, a.python_zip, a.python_sha256, a.pillow_wheel,
                   licenses)
        (folder / "setup.bat").write_text(SETUP_BAT.replace("\n", "\r\n"),
                                          newline="")
    else:
        sh = folder / "setup.sh"
        sh.write_text(SETUP_SH, newline="\n")
        os.chmod(sh, 0o755)
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
