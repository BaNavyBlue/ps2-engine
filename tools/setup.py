#!/usr/bin/env python3
"""Make the game's files from your own Phantasy Star II cartridge image.

  python3 tools/setup.py ROM [--ja JP_ROM --retranslation DIR]

ROM is the US/European cartridge, revision 02 -- the image the engine's
extractors are made for (ps2/tools/rom_map.json gives its size and SHA-1).
A .bin, .md or .gen file, a .smd (interleaved; it is put in order), one
with a copier's 512-byte header (it is dropped), or a .zip holding any of
those. Nothing is changed in it and nothing is sent anywhere.

What it does, from this folder's top:

  1. checks the image is that cartridge;
  2. runs every extractor the game's Makefile names (ps2/Makefile,
     EXTRACTORS, in its order) on it into ps2/assets/, then the preload
     list. The extractors read the image itself, finding what they need by
     ps2/tools/rom_map.json (the disassembly's names and addresses); a
     copy of the image in order is written to a temporary folder for them
     and deleted after;
  3. checks every file made against tools/assets.sha256, the files the
     engine was proved with: the same bytes, or it says which differ.

--ja adds the JAPANESE language from the Japanese cartridge (Kaerazaru
Toki no Owari ni) with Excalibur_Z's retranslation repository, which holds
the Japanese script (github.com/ExcalZ/ps2-retranslation: a clone, or its
"Download ZIP" unpacked; give the folder with work/ in it).

Run it again whenever you like: it makes the same files.
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
from pathlib import Path

HERE = Path(__file__).resolve().parent
TOP = HERE.parent
GAME = TOP / "ps2"
ROM_MAP = GAME / "tools" / "rom_map.json"
EXPECTED = HERE / "assets.sha256"

IMAGE_SUFFIXES = (".bin", ".md", ".gen", ".smd")


def say(*a):
    print(*a, flush=True)


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


def read_image(path):
    """The cartridge's bytes, in order, from PATH (or the zip it is)."""
    path = Path(path)
    name = path.name.lower()
    data = path.read_bytes()
    if zipfile.is_zipfile(io.BytesIO(data)):
        with zipfile.ZipFile(io.BytesIO(data)) as z:
            inner = [n for n in z.namelist()
                     if n.lower().endswith(IMAGE_SUFFIXES)]
            if len(inner) != 1:
                raise SystemExit("%s: expected one cartridge image inside, "
                                 "found %d" % (path, len(inner)))
            name = inner[0].lower()
            data = z.read(inner[0])
    if name.endswith(".smd") or (len(data) % 16384 == 512
                                 and data[8:10] == b"\xaa\xbb"):
        return deinterleave_smd(data)
    if len(data) % 16384 == 512:
        return data[512:]                  # a copier's header
    return data


def check_image(data, rom, what):
    """ROM: rom_map.json's {"size", "sha1"} of the cartridge."""
    sha1 = hashlib.sha1(data).hexdigest()
    want = rom["sha1"]
    if sha1 == want and len(data) == rom["size"]:
        say("%s: the right cartridge (SHA-1 %s)" % (what, sha1))
        return
    header = data[0x150:0x180].decode("ascii", "replace").strip()
    revision = data[0x18C:0x18E] if len(data) > 0x18E else b""
    say("%s is not the cartridge the engine is made for." % what)
    say("  its SHA-1: %s (%d bytes)" % (sha1, len(data)))
    say("  wanted:    %s (%d bytes)" % (want, rom["size"]))
    if header:
        say("  its header names: %r, version %r" % (header, revision))
    say("The engine needs Phantasy Star II for the USA/Europe, revision 02:")
    say("the image commonly named 'Phantasy Star II (UE) (REV02)'. Earlier")
    say("revisions and the Japanese cartridge differ in code and cannot be")
    say("used for it (the Japanese one is for --ja).")
    raise SystemExit(1)


def makefile_runs(makefile, paths):
    """[(name, script, argv)] for each of the Makefile's EXTRACTORS, in its
    order: each rule's `python3 tools/X.py ...`, its variables replaced."""
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
            raise SystemExit("extract-%s uses a variable setup.py does not "
                             "know: %s" % (name, args.strip()))
        runs.append((name, cmd.group(1), shlex.split(args)))
    final = re.search(r"^extract:.*\n\tpython3 (tools/\S+\.py)(.*)$",
                      text, re.M)
    runs.append(("manifest", final.group(1), shlex.split(final.group(2))))
    return runs


# Each tool runs in its own Python, from ps2/, as `make extract` runs it.
# The bootstrap puts its folder on the path first: an embedded Python (the
# Windows release's) does not, and the tools import their neighbours.
BOOT = ("import runpy, sys; sys.argv = sys.argv[1:]; "
        "sys.path.insert(0, 'tools'); "
        "runpy.run_path(sys.argv[0], run_name='__main__')")


def run_tool(script, argv, log):
    # -X utf8: the tools' text files are UTF-8 whatever the machine's
    # locale (the proof's Python ran in a UTF-8 locale).
    proc = subprocess.run([sys.executable, "-X", "utf8", "-c", BOOT, script,
                           *argv],
                          cwd=GAME, stdout=subprocess.PIPE,
                          stderr=subprocess.STDOUT, text=True,
                          encoding="utf-8", errors="replace")
    log.write("$ %s %s\n%s\n" % (script, " ".join(argv), proc.stdout))
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
    """{path under ps2/: sha256} for SECTION ("us" or "ja")."""
    out = {}
    current = None
    for line in EXPECTED.read_text(encoding="utf-8").splitlines():
        if line.startswith("# section "):
            current = line.split()[2]
        elif line and not line.startswith("#") and current == section:
            digest, path = line.split(None, 1)
            out[path] = digest
    return out


def verify(ja):
    """Every file the setup makes against assets.sha256: section "us", and
    with the Japanese also section "ja" (whose preload list, which names
    the Japanese files, replaces the one without)."""
    want = expected_hashes("us")
    if ja:
        want.update(expected_hashes("ja"))
    bad, missing = [], []
    for rel, digest in sorted(want.items()):
        p = GAME / rel
        if not p.is_file():
            missing.append(rel)
        elif file_digest(p) != digest:
            bad.append(rel)
    ok = len(want) - len(bad) - len(missing)
    say("checked: %d of %d files as the engine was proved with"
        % (ok, len(want)))
    for rel in missing[:20]:
        say("  MISSING  %s" % rel)
    for rel in bad[:20]:
        say("  DIFFERS  %s" % rel)
    return not bad and not missing


def main():
    ap = argparse.ArgumentParser(
        description=__doc__.split("\n\n")[0],
        formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("rom", help="your Phantasy Star II (USA/Europe, rev 02) "
                    "cartridge image, or a zip holding it")
    ap.add_argument("--ja", metavar="JP_ROM",
                    help="the Japanese cartridge, for the JAPANESE language")
    ap.add_argument("--retranslation", metavar="DIR",
                    help="Excalibur_Z's ps2-retranslation folder (with --ja)")
    ap.add_argument("--keep-going", action="store_true",
                    help="run every extractor even after one fails")
    a = ap.parse_args()
    if a.ja and not a.retranslation:
        ap.error("--ja needs --retranslation DIR (see --help)")

    rom = read_image(a.rom)
    check_image(rom, json.loads(ROM_MAP.read_text(encoding="utf-8"))["rom"],
                a.rom)

    # The extractors take the image as a raw file: the bytes in order, in
    # a folder of our own, removed however the run ends.
    tmp = Path(tempfile.mkdtemp(prefix="ps2-setup-"))
    try:
        image = tmp / "ps2.bin"
        image.write_bytes(rom)
        return extract(a, image)
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


def extract(a, image):
    runs = makefile_runs(GAME / "Makefile", {"ROM": str(image)})
    failed = []
    log_path = TOP / "setup.log"
    with open(log_path, "w", encoding="utf-8") as log:
        for i, (name, script, argv) in enumerate(runs, 1):
            say("[%2d/%d] %s" % (i, len(runs), name))
            proc = run_tool(script, argv, log)
            if proc.returncode != 0:
                failed.append(name)
                say(proc.stdout.rstrip()[-2000:])
                if not a.keep_going:
                    break
        if a.ja and not failed:
            say("[ja] the Japanese language")
            jp = Path(a.ja).resolve()
            rt = Path(a.retranslation).resolve()
            proc = run_tool("tools/import_japanese.py",
                            [str(jp), str(rt), "-o", "assets/lang/ja"], log)
            if proc.returncode != 0:
                failed.append("ja")
                say(proc.stdout.rstrip()[-2000:])
            else:
                run_tool(runs[-1][1], runs[-1][2], log)
    if failed:
        say("FAILED: %s (the whole output is in %s)"
            % (", ".join(failed), log_path))
        return 1
    if not verify(bool(a.ja)):
        say("Some files differ from the ones the engine was proved with. The "
            "game may still run; please report it with %s." % log_path)
        return 1
    exe = "ps2.exe" if os.name == "nt" else "ps2"
    say("Done. The game: %s" % (GAME / exe))
    return 0


if __name__ == "__main__":
    sys.exit(main())
