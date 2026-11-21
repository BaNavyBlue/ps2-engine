#!/usr/bin/env python3
"""(build 121) `make dist`: the game to hand to someone.

    python3 tools/make_dist.py --exe build/dist/ps2.exe --name ps2-windows \\
        --q 1 --q-dir /path/to/Q

writes dist/NAME/ (emptied first) and dist/NAME.zip, from the tree's top:

  * the game (the Makefile links it: on Windows statically, one .exe);
    (build 125) with its pack in it (tools/make_pack.py): assets/, shaders/,
    and what COMPUTER PLAYS reads -- (build 179) the C player is in the
    game (src/player/): routes/saves/ and src/game/script_ids.h. Builds
    125-178 packed the Python driver (tools/computer_plays.py and the four
    modules it imports, routes/new_game.route), which the game wrote out
    under the computer's folder the first time it played, and builds
    122-178 carried python/, python.org's embeddable Python, on Windows;
  * README.txt for a player;
  * (build 164) licenses/retranslation-LICENSE.txt: Excalibur_Z's MIT
    notice for the retranslation's text and font, which are in the pack
    (assets/lang/retranslation; docs/languages.md, Credits);
  * licenses/: the notices of what is linked in -- Nuked-OPN2 (LGPL-2.1,
    from ym3438.c's head), Q with --q 1 (its clone's LICENSE: Boost before
    Q 1.5, MIT from 1.5), and on Windows SDL3 (zlib), iconv (LGPL-2.1),
    winpthreads and the GCC runtime (from MSYS2's share/licenses) (and
    Python's in builds 122-178);
  * source.zip: `git archive` of HEAD -- the port's source, which the LGPL
    asks to go with a program linked statically with an LGPL library -- less
    the working notes (NOT_SOURCE). A tree with changes not committed is
    said so: the archive is HEAD's. (build 138a) A tree that is not a git
    repository (one the tarballs are unpacked over) gives its own files
    instead, less what the build and the game write (NOT_TREE_DIRS,
    NOT_TREE_FILES, programs by their first bytes).

(build 138a) Off Windows, LGPL-2.1.txt is the system's copy of the licence
(LGPL_SYSTEM: Arch's licenses package, Debian's common-licenses); without
one it is said and the package is made anyway.

(build 122) Every text file the package carries, source.zip's included, has
the maintainer's name put as "the author" (scrub).

(build 161a) Every entry of both zips is dated HEAD's commit time in UTC
(zip_stamp), as herzog-zwei's tools/hz_dist.py dates its zip: a zip's
times are local, and beside the exe's link stamp (UTC) a file's own time
gave away the builder's UTC offset.

Nothing is written outside dist/.
"""
import argparse
import datetime
import fnmatch
import io
import os
import re
import shutil
import stat
import subprocess
import sys
import zipfile
from pathlib import Path

# MSYS2's licence folders for what a static Windows build takes in.
MSYS_LICENSES = ("SDL3", "libiconv", "winpthreads", "libgcc")

# (build 122) The working notes, which are not the port's source: left out
# of source.zip. (build 142b) The old handover and questions files are in
# docs/old/ now.
NOT_SOURCE = ("CLAUDE.md", "NOTES_next_context.md", "questions_*.txt",
              "handover_*.txt", "docs/old/*")

# (build 138a) A tree outside git: what source.zip leaves out of it -- the
# build's and the game's output, the tarballs' leftovers -- beside
# NOT_SOURCE and any program (ELF, PE, Mach-O by their first bytes).
NOT_TREE_DIRS = {".git", "build", "dist", "saves", "mods", "route_out",
                 "fuzz_out", "listen", "__pycache__"}
NOT_TREE_FILES = ("*.o", "*.a", "*.d", "*.obj", "*.exe", "*.dll", "*.pyc",
                  "*.zip", "*.tar.gz", "*.tgz", "*.wav", "*.flac")
PROGRAM_MAGIC = (b"\x7fELF", b"MZ", b"\xcf\xfa\xed\xfe",
                 b"\xce\xfa\xed\xfe", b"\xca\xfe\xba\xbe")

# (build 138a) The LGPL 2.1's text where a Linux system keeps it: Arch's
# licenses package (SPDX names, then the older common/), Debian's and
# Ubuntu's common-licenses.
LGPL_SYSTEM = ("/usr/share/licenses/spdx/LGPL-2.1-only.txt",
               "/usr/share/licenses/spdx/LGPL-2.1-or-later.txt",
               "/usr/share/licenses/common/LGPL2.1/license.txt",
               "/usr/share/common-licenses/LGPL-2.1")

# (build 122) The maintainer's name, in the notes and comments the package
# carries, and what it becomes.
NAME = "The owner"
NAME_AS = "the author"

README = """Phantasy Star II -- a port in C

Run {exe}. The game keeps its saves and settings in saves/ beside it, or,
where that folder cannot be written (Program Files), in
%LOCALAPPDATA%\\PhantasyStarII\\saves. It makes what it needs as it goes:
everything it reads is inside {exe}.

{unsigned}Keys (Escape, then CONTROLS, changes them):
    arrow keys or W A S D   the pad   Enter   Start
    Z X C or L ; '          A B C     Escape  the menu: controls, display,
                                              fullscreen, CRT, audio,
                                              COMPUTER PLAYS, gamepad,
                                              language, quit
A gamepad: the d-pad or the left stick, the left, bottom and right face
buttons for A B C, Start; Back opens the menu (Escape, then GAMEPAD, then
BUTTONS, changes them). gamecontrollerdb.txt beside {exe} adds mappings
for a gamepad SDL does not know.

COMPUTER PLAYS (in the Escape menu) plays the game by itself from a new game
or one of your saves, for watching; SPEED sets how fast (AUTO speeds through
its grinding). It needs nothing installed. Its own saves go to
saves/computer/, never yours.

LANGUAGE (in the Escape menu): ENGLISH 1989, the cartridge's, or
RETRANSLATED -- Excalibur_Z's English retranslation of Phantasy Star II,
made from the Japanese script, with his proportional font
(github.com/ExcalZ/ps2-retranslation; his MIT notice is
licenses/retranslation-LICENSE.txt) -- or JAPANESE, the Japanese
cartridge's text and font. The game's play is the cartridge's in all
three.

To change the game's music, words or pictures, see mods/README.txt, which
the game writes on its first start beside mods/originals/, a copy of every
file it carries: copy one up into mods/ and edit it.

Built from {commit}{dirty}, with the Q synthesizer {q}.
licenses/ has the licences of the libraries in the program; source.zip is
the port's source.
"""

# (build 167a) The README's paragraph on Windows's warnings, for the
# Windows package alone: the exe is unsigned (herzog-zwei's
# docs/windows_build.md, "Unsigned", says why).
UNSIGNED = """Windows may stop {exe} the first time: it isn't signed (a
signature costs money every year and names a person). "Windows protected
your PC": More info, then Run anyway. To be asked nothing, before
unpacking: right-click the zip, Properties, tick Unblock at the bottom of
General, OK; then unpack it. Windows 11's Smart App Control blocks it
with no Run anyway, even unblocked: Windows Security, App & browser
control, Smart App Control settings, Off (Windows 11 updated since April
2026 can turn it on again later; on older ones, off is for good).

"""

def run(*cmd):
    return subprocess.run(cmd, capture_output=True, text=True)


def msys_path(p):
    """A POSIX path from MSYS2's tools as this Python can open it."""
    if os.name != "nt" or not p.startswith("/"):
        return p
    r = run("cygpath", "-w", p)
    return r.stdout.strip() if r.returncode == 0 else p


def writable_again(func, path, exc):
    """rmtree's retry: copytree carries a folder's read-only attribute
    over (the tree's assets/data has it on Windows), and a read-only folder
    will not go until it is cleared."""
    os.chmod(path, stat.S_IWRITE | stat.S_IREAD | stat.S_IEXEC)
    func(path)


def nuked_notice():
    """ym3438.c's opening comment: Nuked-OPN2's copyright and licence."""
    text = Path("src/audio/ym3438.c").read_text(encoding="utf-8")
    end = text.find("*/")
    return text[:end + 2] + "\n" if text.startswith("/*") and end > 0 else ""


# A sentence's start before the name: a line's start (past a comment's
# marks), or after . ! ? : or an opening quote or bracket.
_SENTENCE = re.compile(r"(^[\s#*/;>|-]*|[.!?:][\s\"']*|[(\"'\[]\s*)$")


def scrub(text):
    """(build 122) The name as NAME_AS -- "The author" where it begins a
    sentence -- in any of its forms (NAME's, NAME:). Returns the text and
    how many were put."""
    count = 0

    def put(m):
        nonlocal count
        count += 1
        before = text[max(0, m.start() - 40):m.start()].split("\n")[-1]
        word = NAME_AS
        if _SENTENCE.search(before):
            word = word[0].upper() + word[1:]
        return word
    return re.sub(rf"\b{NAME}\b", put, text), count


def scrub_file(path):
    """scrub() on a text file in place; binaries (not UTF-8) left alone."""
    try:
        text = path.read_text(encoding="utf-8")
    except (UnicodeDecodeError, OSError):
        return 0
    new, n = scrub(text)
    if n:
        path.write_bytes(new.encode("utf-8"))
    return n


def in_git():
    """(build 138a) True when the tree is a git repository with a HEAD."""
    return run("git", "rev-parse", "--verify", "-q", "HEAD").returncode == 0


def tree_files():
    """(build 138a) The tree's own files, for a tree outside git: relative
    POSIX paths, sorted, less NOT_TREE_DIRS, NOT_TREE_FILES and programs."""
    files = []
    for root, dirs, names in os.walk("."):
        dirs[:] = sorted(d for d in dirs if d not in NOT_TREE_DIRS)
        for name in sorted(names):
            path = Path(root) / name
            rel = path.relative_to(".").as_posix()
            if any(fnmatch.fnmatch(name, p) for p in NOT_TREE_FILES):
                continue
            if path.is_symlink() or not path.is_file():
                continue
            with open(path, "rb") as f:
                head = f.read(4)
            if any(head.startswith(m) for m in PROGRAM_MAGIC):
                continue
            files.append(rel)
    return files


def zip_stamp():
    """(build 161a) Every zip entry's time: HEAD's commit time in UTC
    (1980's first second outside git, a zip's earliest)."""
    when = run("git", "log", "-1", "--format=%ct").stdout.strip()
    stamp = datetime.datetime.fromtimestamp(
        int(when) if when.isdigit() else 315532800, datetime.timezone.utc)
    return max(stamp.timetuple()[:6], (1980, 1, 1, 0, 0, 0))


def stamped(info, stamp):
    """(build 161a) A copy of a zip entry, a file's deflated, dated stamp:
    its name, its mode and the system that made it kept; its own time and
    extra fields (git archive's carry the time again) not."""
    new = zipfile.ZipInfo(info.filename, stamp)
    new.compress_type = (zipfile.ZIP_STORED if info.is_dir()
                         else zipfile.ZIP_DEFLATED)
    new.create_system = info.create_system
    new.external_attr = info.external_attr
    return new


def put_scrubbed(out, info, data):
    """One entry into source.zip, its text scrubbed; how many names put."""
    n = 0
    if not info.is_dir():
        try:
            text, n = scrub(data.decode("utf-8"))
            if n:
                data = text.encode("utf-8")
        except UnicodeDecodeError:
            n = 0
    out.writestr(info, data)
    return n


def source_zip(dest, stamp):
    """git archive of HEAD -- or, outside git (build 138a), the tree's own
    files -- less NOT_SOURCE, every text file scrubbed, every entry dated
    stamp (build 161a). Returns how many names were put."""
    put = 0
    if not in_git():
        with zipfile.ZipFile(dest, "w", zipfile.ZIP_DEFLATED) as out:
            for rel in tree_files():
                if any(fnmatch.fnmatch(rel, p) for p in NOT_SOURCE):
                    continue
                info = zipfile.ZipInfo.from_file(rel, "ps2/" + rel)
                put += put_scrubbed(out, stamped(info, stamp),
                                    Path(rel).read_bytes())
        return put
    r = subprocess.run(["git", "archive", "--format=zip", "--prefix=ps2/",
                        "HEAD"], capture_output=True)
    if r.returncode != 0:
        raise SystemExit("make_dist: git archive failed: "
                         + r.stderr.decode(errors="replace"))
    with zipfile.ZipFile(io.BytesIO(r.stdout)) as src, \
            zipfile.ZipFile(dest, "w", zipfile.ZIP_DEFLATED) as out:
        for info in src.infolist():
            rel = info.filename[len("ps2/"):]
            if any(fnmatch.fnmatch(rel, p) for p in NOT_SOURCE):
                continue
            put += put_scrubbed(out, stamped(info, stamp), src.read(info))
    return put


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--exe", required=True, help="the game, as linked")
    ap.add_argument("--name", required=True, help="dist/NAME/, dist/NAME.zip")
    ap.add_argument("--q", default="0", help="1: Q is linked in")
    ap.add_argument("--q-dir", default="", help="Q's clone (its LICENSE)")
    args = ap.parse_args()

    exe = Path(args.exe)
    if not exe.is_file():
        print(f"make_dist: no {exe}", file=sys.stderr)
        return 1
    out = Path("dist") / args.name
    if out.exists():
        shutil.rmtree(out, onexc=writable_again)
    out.mkdir(parents=True)

    shutil.copy2(exe, out / exe.name)
    # (build 125) assets/, shaders/ and what the C player reads are in the
    # game's pack.

    lic = out / "licenses"
    lic.mkdir()
    # (build 164) The retranslation's notice: its text and font are in the
    # pack, and his MIT licence asks that it go with them.
    shutil.copy2(Path("assets/lang/retranslation/LICENSE.txt"),
                 lic / "retranslation-LICENSE.txt")
    notice = nuked_notice()
    (lic / "Nuked-OPN2.txt").write_text(
        notice + "\nThe GNU LGPL 2.1's text: LGPL-2.1.txt beside this "
        "(from iconv's, the same licence).\n", encoding="utf-8",
        newline="\n")
    if args.q == "1":
        q_license = Path(msys_path(args.q_dir)) / "LICENSE"
        if q_license.is_file():
            shutil.copy2(q_license, lic / "Q-LICENSE.txt")
        else:
            print(f"make_dist: no {q_license}", file=sys.stderr)
    have_lgpl = False
    if os.name == "nt":
        prefix = run("pkg-config", "--variable=prefix", "sdl3").stdout.strip()
        share = Path(msys_path(prefix or "/ucrt64")) / "share" / "licenses"
        for name in MSYS_LICENSES:
            src = share / name
            if src.is_dir():
                shutil.copytree(src, lic / name)
            else:
                print(f"make_dist: no {src}", file=sys.stderr)
        lgpl = share / "libiconv" / "COPYING.LIB"
        if lgpl.is_file():
            shutil.copy2(lgpl, lic / "LGPL-2.1.txt")
            have_lgpl = True
    if not have_lgpl:
        # (build 138a) Off Windows, the system's copy.
        for cand in LGPL_SYSTEM:
            if Path(cand).is_file():
                shutil.copy2(cand, lic / "LGPL-2.1.txt")
                have_lgpl = True
                break
    if not have_lgpl:
        print("make_dist: no LGPL-2.1 text found for licenses/ (looked in "
              + ", ".join(LGPL_SYSTEM) + "); the package is made without it",
              file=sys.stderr)

    if in_git():
        commit = run("git", "rev-parse", "--short", "HEAD").stdout.strip()
        dirty = run("git", "status", "--porcelain",
                    "--untracked-files=no").stdout
        if dirty.strip():
            print("make_dist: the tree has changes not committed; "
                  f"source.zip is HEAD's ({commit})", file=sys.stderr)
    else:
        # (build 138a) A tree the tarballs are unpacked over.
        commit = "a tree outside git"
        dirty = ""
        print("make_dist: not a git repository; source.zip is the tree's own "
              "files", file=sys.stderr)
    stamp = zip_stamp()
    put = source_zip(out / "source.zip", stamp)

    (out / "README.txt").write_text(README.format(
        exe=exe.name, commit=commit,
        unsigned=UNSIGNED.format(exe=exe.name) if os.name == "nt" else "",
        dirty=" (and changes not committed)" if dirty.strip() else "",
        q="in" if args.q == "1" else "not in (the chip alone)"),
        encoding="utf-8", newline="\r\n" if os.name == "nt" else "\n")

    # (build 122) The name, in everything else the package carries as text.
    for p in sorted(out.rglob("*")):
        if p.is_file() and p.suffix != ".zip":
            put += scrub_file(p)

    archive = Path("dist") / f"{args.name}.zip"
    archive.unlink(missing_ok=True)
    with zipfile.ZipFile(archive, "w", zipfile.ZIP_DEFLATED) as z:
        for p in sorted(out.rglob("*")):
            if p.is_file():
                info = zipfile.ZipInfo.from_file(
                    p, Path(args.name) / p.relative_to(out))
                z.writestr(stamped(info, stamp), p.read_bytes())
    size = sum(p.stat().st_size for p in out.rglob("*") if p.is_file())
    print(f"make_dist: {out} ({size / 1e6:.1f} MB), {archive} "
          f"({archive.stat().st_size / 1e6:.1f} MB); Q "
          f"{'in' if args.q == '1' else 'not in'}; "
          f"the name put as \"{NAME_AS}\" {put} times")
    return 0


if __name__ == "__main__":
    sys.exit(main())
