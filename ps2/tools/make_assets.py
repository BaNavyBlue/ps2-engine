#!/usr/bin/env python3
"""(build 186) Make the game's files from the player's own cartridge image.

    python3 tools/make_assets.py FILE... [--keep-going]
    python3 tools/make_assets.py IMAGE [--ja JP_IMAGE --retranslation R]

What `make extract ROM=IMAGE` does, with no make, and what the game's first
start runs (docs/first_start.md): the window there hands it the files the
player dropped or chose. Each FILE is told apart by what it is, not by its
name or its place on the line:

  * the US/European cartridge, revision 02 (tools/rom_map.json gives its
    size and SHA-1): a .bin, .md or .gen image, a .smd (interleaved; put in
    order), one with a copier's 512-byte header (dropped), or a .zip
    holding one. Every extractor the Makefile names (EXTRACTORS, in its
    order) is run on it into assets/, then the preload list;
  * the Japanese cartridge (Kaerazaru Toki no Owari ni; its first 768 KB
    CRC32 BEC8EB5A) and Excalibur_Z's retranslation (a checkout of
    github.com/ExcalZ/ps2-retranslation, its "Download ZIP", or that zip
    unpacked): together they make the JAPANESE language
    (tools/import_japanese.py) into assets/lang/ja/. With the US files
    already made, they can come alone.

Nothing is sent anywhere and the images are not changed: the extractors
read a copy of the US image, in order, in a temporary folder deleted after.

Then, where tools/assets.sha256 is (the files a proved extraction made,
section "us" and section "ja"; a tree whose assets are checked in has none),
every file is checked against it: the same bytes, a PNG the same picture.

What it prints, for the game's window to follow: a line `[K/N] WHAT` as
each step starts, and plain sentences for the player. The whole output of
every step goes to make_assets.log beside the game. Its exit status:

  0  done (and checked, where there is a list)
  1  a step failed (the log says how)
  2  nothing it can make the files from: an image it does not know, or the
     Japanese cartridge without the retranslation (or the other way round)
  3  the files are made, but some differ from the list's
  4  this Python has no Pillow, which the extractors need

(build 192) Its twin in C is `./ps2_extract assets FILE...`
(src/extract/exmake.c; docs/first_start.md, "In C"): the same arguments,
lines, files and exit statuses, held to this by
tools/make_assets_c_check.py.
"""
import argparse
import hashlib
import io
import json
import os
import re
import shlex
import shutil
import subprocess
import sys
import tempfile
import zipfile
import zlib
from pathlib import Path

HERE = Path(__file__).resolve().parent
GAME = HERE.parent
ROM_MAP = HERE / "rom_map.json"
EXPECTED = HERE / "assets.sha256"
LOG = GAME / "make_assets.log"

IMAGE_SUFFIXES = (".bin", ".md", ".gen", ".smd")

# The Japanese cartridge as tools/import_japanese.py checks it: a 1 MB
# overdump is fine, its first 768 KB are the image.
JP_SIZE = 0xC0000
JP_CRC = 0xBEC8EB5A

# The retranslation's commit assets/lang/ja was made from (its lang.json's
# "source"): another one may carry other text, and the check says so.
RETRANSLATION_PROVED = "061e1f5799d72ee88ac1b1a872215b3a96cc850e"
RETRANSLATION_FILES = ("work/dialogue.json", "work/script.json")

# Windows: the steps start no console window of their own when the game,
# a program with none, runs this.
NO_WINDOW = getattr(subprocess, "CREATE_NO_WINDOW", 0)


def say(*a):
    print(*a, flush=True)


class Unusable(Exception):
    """A file that is none of the things the game's files are made from."""


def deinterleave_smd(data):
    """A .smd image: a 512-byte header, then 16 KB blocks, each the odd
    bytes' 8 KB then the even bytes'."""
    body = data[512:]
    out = bytearray(len(body))
    for b in range(0, len(body), 16384):
        block = body[b:b + 16384]
        half = len(block) // 2
        out[b + 1:b + len(block):2] = block[:half]
        out[b:b + len(block):2] = block[half:]
    return bytes(out)


def read_image(path, data):
    """The cartridge's bytes, in order, from a file's DATA (or the zip it
    is)."""
    name = path.name.lower()
    if zipfile.is_zipfile(io.BytesIO(data)):
        with zipfile.ZipFile(io.BytesIO(data)) as z:
            inner = [n for n in z.namelist()
                     if n.lower().endswith(IMAGE_SUFFIXES)]
            if len(inner) != 1:
                raise Unusable("%s: a zip with %d cartridge images in it, "
                               "not one" % (path.name, len(inner)))
            name = inner[0].lower()
            data = z.read(inner[0])
    if name.endswith(".smd") or (len(data) % 16384 == 512
                                 and data[8:10] == b"\xaa\xbb"):
        return deinterleave_smd(data)
    if len(data) % 16384 == 512:
        return data[512:]                  # a copier's header
    return data


def retranslation_root(path):
    """The folder holding work/ in a retranslation checkout or an unpacked
    zip (GitHub's has one folder at its top), or None."""
    if all((path / f).is_file() for f in RETRANSLATION_FILES):
        return path
    subs = [p for p in path.iterdir() if p.is_dir()]
    if len(subs) == 1 and all((subs[0] / f).is_file()
                              for f in RETRANSLATION_FILES):
        return subs[0]
    return None


def retranslation_zip(z):
    """{file: its name in the zip} when Z is the retranslation's zip."""
    found = {}
    for n in z.namelist():
        for f in RETRANSLATION_FILES:
            if n == f or (n.endswith("/" + f) and n.count("/") == 2):
                found[f] = n
    return found if len(found) == len(RETRANSLATION_FILES) else None


def identify(path, rom):
    """("us", bytes) | ("ja", path) | ("retranslation", folder or zip path),
    or Unusable saying what it is instead."""
    if not path.exists():
        raise Unusable("%s: there is no such file" % path)
    if path.is_dir():
        root = retranslation_root(path)
        if root is None:
            raise Unusable("%s: a folder, but not the retranslation's (no "
                           "work/dialogue.json in it)" % path.name)
        return "retranslation", root
    data = path.read_bytes()
    if zipfile.is_zipfile(io.BytesIO(data)):
        with zipfile.ZipFile(io.BytesIO(data)) as z:
            if retranslation_zip(z) is not None:
                return "retranslation", path
    image = read_image(path, data)
    if len(image) == rom["size"] \
            and hashlib.sha1(image).hexdigest() == rom["sha1"]:
        return "us", image
    if len(image) >= JP_SIZE and zlib.crc32(image[:JP_SIZE]) == JP_CRC:
        return "ja", path
    raise Unusable(not_the_cartridge(path, image, rom))


def not_the_cartridge(path, data, rom):
    sha1 = hashlib.sha1(data).hexdigest()
    lines = ["%s is not a cartridge the game knows." % path.name,
             "  its SHA-1: %s (%d bytes)" % (sha1, len(data))]
    header = data[0x150:0x180].decode("ascii", "replace").strip()
    if header and len(data) > 0x18E and data[0x100:0x104] == b"SEGA":
        lines.append("  its header names: %r, version %r"
                     % (header, data[0x18C:0x18E].decode("ascii", "replace")))
    lines += ["The game's own files are made from Phantasy Star II for the "
              "USA/Europe, revision 02, the image commonly named 'Phantasy "
              "Star II (UE) (REV02)' (SHA-1 %s, %d bytes); earlier revisions "
              "differ in code and cannot be used." % (rom["sha1"],
                                                       rom["size"]),
              "The JAPANESE language is made from the Japanese cartridge, "
              "Kaerazaru Toki no Owari ni (its first 768 KB CRC32 %08X)."
              % JP_CRC]
    return "\n".join(lines)


def makefile_runs(makefile, paths):
    """[(name, script, argv)] for each of the Makefile's EXTRACTORS, in its
    order: each rule's `python3 tools/X.py ...`, its variables replaced,
    then the preload list (`extract`'s own line)."""
    text = makefile.read_text(encoding="utf-8").replace("\\\n", " ")
    names = re.search(r"^EXTRACTORS\s*:=\s*(.*)$", text, re.M).group(1)
    runs = []
    for name in names.split():
        rule = re.search(r"^extract-%s:.*\n((?:\t.*\n)+)" % re.escape(name),
                         text, re.M)
        if rule is None:
            raise SystemExit("no extract-%s rule in %s" % (name, makefile))
        cmd = re.search(r"python3 (tools/\S+\.py)([^;\n]*)", rule.group(1))
        args = cmd.group(2)
        for var, value in paths.items():
            args = args.replace("$(%s)" % var, value)
        if "$(" in args:
            raise SystemExit("extract-%s uses a variable make_assets.py does "
                             "not know: %s" % (name, args.strip()))
        runs.append((name, cmd.group(1), shlex.split(args)))
    return runs, manifest_run(text)


def manifest_run(text):
    final = re.search(r"^extract:.*\n\tpython3 (tools/\S+\.py)(.*)$",
                      text, re.M)
    return ("the preload list", final.group(1), shlex.split(final.group(2)))


# Each tool runs in its own Python, from the game's folder, as `make
# extract` runs it. The bootstrap puts tools/ on the path first: an embedded
# Python (a Windows package's) does not, and the tools import their
# neighbours.
BOOT = ("import runpy, sys; sys.argv = sys.argv[1:]; "
        "sys.path.insert(0, 'tools'); "
        "runpy.run_path(sys.argv[0], run_name='__main__')")


def run_tool(script, argv, log):
    # -X utf8: the tools' text files are UTF-8 whatever the machine's
    # locale (the proofs' Python ran in a UTF-8 locale).
    proc = subprocess.run([sys.executable, "-X", "utf8", "-c", BOOT, script,
                           *argv],
                          cwd=GAME, stdout=subprocess.PIPE,
                          stderr=subprocess.STDOUT, text=True,
                          encoding="utf-8", errors="replace",
                          creationflags=NO_WINDOW)
    log.write("$ %s %s\n%s\n" % (script, " ".join(argv), proc.stdout))
    log.flush()
    return proc


def file_digest(path):
    """A file's SHA-256; a PNG's of its picture -- mode, size, palette,
    transparency and pixels -- since its bytes depend on the zlib that
    Pillow was built with (the game decodes them, so only the picture
    matters)."""
    path = Path(path)
    if path.suffix.lower() != ".png":
        return hashlib.sha256(path.read_bytes()).hexdigest()
    from PIL import Image
    with Image.open(path) as im:
        h = hashlib.sha256()
        h.update(("%s %dx%d;" % (im.mode, im.width, im.height)).encode())
        pal = im.getpalette()
        h.update(bytes(pal) if pal is not None else b"-")
        h.update(repr(im.info.get("transparency")).encode())
        h.update(im.tobytes())
        return "png:" + h.hexdigest()


def expected_hashes(section):
    """{path under the game's folder: digest} for SECTION ("us", "ja")."""
    out = {}
    current = None
    for line in EXPECTED.read_text(encoding="utf-8").splitlines():
        if line.startswith("# section "):
            current = line.split()[2]
        elif line and not line.startswith("#") and current == section:
            digest, path = line.split(None, 1)
            out[path] = digest
    return out


def verify(sections):
    """Every file of SECTIONS against assets.sha256 (a later section's
    preload list replaces an earlier one's: the Japanese one names its
    files too). True when all are the same."""
    want = {}
    for s in sections:
        want.update(expected_hashes(s))
    bad, missing = [], []
    for rel, digest in sorted(want.items()):
        p = GAME / rel
        if not p.is_file():
            missing.append(rel)
        elif file_digest(p) != digest:
            bad.append(rel)
    ok = len(want) - len(bad) - len(missing)
    say("checked: %d of %d files as the game was proved with"
        % (ok, len(want)))
    for rel in missing[:20]:
        say("  MISSING  %s" % rel)
    for rel in bad[:20]:
        say("  DIFFERS  %s" % rel)
    return len(bad) + len(missing)


def zip_commit(path):
    """The commit a GitHub "Download ZIP" was made from: its comment."""
    with zipfile.ZipFile(path) as z:
        comment = z.comment.decode("ascii", "replace").strip()
    return comment if re.fullmatch(r"[0-9a-f]{40}", comment) else ""


def unpack_retranslation(path, tmp):
    """The retranslation as a folder: PATH itself, or the two files its
    zip carries unpacked into TMP. With the commit, where known ("" for a
    copy that is neither a checkout nor GitHub's zip)."""
    if path.is_dir():
        return path, ""
    dest = tmp / "retranslation"
    with zipfile.ZipFile(path) as z:
        for f, name in retranslation_zip(z).items():
            out = dest / f
            out.parent.mkdir(parents=True, exist_ok=True)
            out.write_bytes(z.read(name))
    return dest, zip_commit(path)


def plan_inputs(files, rom):
    """{"us": image bytes, "ja": path, "retranslation": path} from FILES,
    each told apart by what it is; exit 2 with the reason when they make
    nothing."""
    got = {}
    for f in files:
        try:
            kind, value = identify(Path(f), rom)
        except Unusable as e:
            say(str(e))
            raise SystemExit(2)
        if kind in got:
            say("Two of the same given: %s twice (%s)." % (
                {"us": "the US cartridge", "ja": "the Japanese cartridge",
                 "retranslation": "the retranslation"}[kind], f))
            raise SystemExit(2)
        got[kind] = value
        say("%s: %s" % (Path(f).name, {
            "us": "the US/European cartridge, revision 02",
            "ja": "the Japanese cartridge",
            "retranslation": "Excalibur_Z's retranslation"}[kind]))
    if got and "us" not in got \
            and not (GAME / "assets" / "manifest.txt").is_file():
        say("The game's own files come first, made from the US/European "
            "cartridge (revision 02); the JAPANESE language is added to "
            "them afterwards.")
        raise SystemExit(2)
    if ("ja" in got) != ("retranslation" in got):
        say("The JAPANESE language is made from two things: the Japanese "
            "cartridge's image and Excalibur_Z's retranslation "
            "(github.com/ExcalZ/ps2-retranslation, its \"Download ZIP\"). "
            "Only %s was given."
            % ("the cartridge" if "ja" in got else "the retranslation"))
        raise SystemExit(2)
    if not got:
        say("Nothing given: the US/European cartridge's image (revision "
            "02), or the Japanese cartridge and the retranslation.")
        raise SystemExit(2)
    return got


def main():
    ap = argparse.ArgumentParser(
        description=__doc__.split("\n\n")[0],
        formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("files", nargs="*",
                    help="the cartridge images and the retranslation, in "
                         "any order")
    ap.add_argument("--ja", metavar="JP_IMAGE",
                    help="the Japanese cartridge (or give it among FILES)")
    ap.add_argument("--retranslation", metavar="R",
                    help="Excalibur_Z's ps2-retranslation: a folder or its "
                         "zip (or give it among FILES)")
    ap.add_argument("--keep-going", action="store_true",
                    help="run every step even after one fails")
    a = ap.parse_args()

    try:
        import PIL  # noqa: F401  (the extractors and the check need it)
    except ImportError:
        say("This Python has no Pillow, which making the game's files needs:")
        say("  Windows:       py -m pip install pillow")
        say("  Debian/Ubuntu: sudo apt install python3-pil")
        say("  Fedora:        sudo dnf install python3-pillow")
        say("  Arch:          sudo pacman -S python-pillow")
        say("  macOS:         brew install pillow")
        return 4

    rom = json.loads(ROM_MAP.read_text(encoding="utf-8"))["rom"]
    files = list(a.files) + [f for f in (a.ja, a.retranslation) if f]
    got = plan_inputs(files, rom)

    tmp = Path(tempfile.mkdtemp(prefix="ps2-assets-"))
    try:
        return make(a, got, tmp)
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


def make(a, got, tmp):
    steps = []
    image = tmp / "ps2.bin"
    if "us" in got:
        image.write_bytes(got["us"])
        runs, final = makefile_runs(GAME / "Makefile", {"ROM": str(image)})
        steps += runs
    else:
        final = manifest_run(
            (GAME / "Makefile").read_text(encoding="utf-8"))
    if "ja" in got:
        rt, commit = unpack_retranslation(Path(got["retranslation"]), tmp)
        argv = [str(Path(got["ja"]).resolve()), str(rt.resolve()),
                "-o", "assets/lang/ja"]
        if commit:
            argv += ["--commit", commit]
        steps.append(("the Japanese language", "tools/import_japanese.py",
                      argv))
        if commit and commit != RETRANSLATION_PROVED:
            say("(the retranslation's commit %s; the game was proved with "
                "%s: its text may differ)" % (commit[:12],
                                              RETRANSLATION_PROVED[:12]))
    steps.append(final)

    failed = []
    with open(LOG, "w", encoding="utf-8") as log:
        for i, (name, script, argv) in enumerate(steps, 1):
            say("[%d/%d] %s" % (i, len(steps), name))
            proc = run_tool(script, argv, log)
            if proc.returncode != 0:
                failed.append(name)
                say(proc.stdout.rstrip()[-2000:])
                if not a.keep_going:
                    break
    if failed:
        say("FAILED: %s (the whole output is in %s)"
            % (", ".join(failed), LOG))
        return 1
    if EXPECTED.is_file():
        sections = ["us"]
        if (GAME / "assets" / "lang" / "ja" / "lang.json").is_file():
            sections.append("ja")
        differ = verify(sections)
        if differ:
            say("%d files differ from the ones the game was proved with. It "
                "may still play; %s has the whole output." % (differ, LOG))
            return 3
    else:
        say("(no tools/assets.sha256 here: the files are not checked)")
    say("Done.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
