#!/usr/bin/env python3
"""(build 191) The Japanese importer in C (src/extract/ex_japanese.c,
`./ps2_extract japanese`) against the Python it ports,
tools/import_japanese.py (docs/first_start.md, "In C").

    make ps2_extract
    python3 tools/japanese_c_check.py RETRANSLATION --ja JP_IMAGE
    python3 tools/japanese_c_check.py RETRANSLATION --stand-in US_IMAGE
                                      [--commit C] [--scratch DIR] [--keep]

RETRANSLATION is Excalibur_Z's ps2-retranslation as the game takes it: a
checkout, its GitHub "Download ZIP", or that zip unpacked. JP_IMAGE is the
Japanese cartridge (or a zip of it). Each importer writes the language
into a scratch folder of its own, from the same files, and then:

  * the two wrote the same files, each the same bytes (font.png and
    font_upper.png the same picture: tools/make_assets.py's file_digest),
    and said the same lines;
  * both take the image in a zip (deflated), and both refuse a zip
    holding two images, and the US cartridge in the Japanese one's place;
  * with the retranslation's proved commit (make_assets.py's
    RETRANSLATION_PROVED), every file is the one assets/lang/ja holds.

--stand-in, for a machine without the Japanese cartridge: an image made
for the check from the US cartridge -- its own font art where the Japanese
cartridge's is, a character map and a name board's art built back from
assets/lang/ja's lang.json and names.json, and four bytes at its end set
so that its CRC32 is the Japanese cartridge's, which both importers ask
for. Nothing in it is the Japanese cartridge's; it is made in the scratch
folder and goes with it. The face it gives is not the Japanese one, so
font.png and font_upper.png are left out of the last comparison; the
rest -- the character map's characters, the script, the names, the board
-- must still be assets/lang/ja's.

(build 192a) In a tree that has not made the JAPANESE language (no
assets/lang/ja), --stand-in has nothing to build its image from and stops
(exit 2), and --ja holds the two importers to each other alone.

Exit 1 on any difference. The scratch folder goes unless --keep.
"""
import argparse
import json
import os
import shutil
import subprocess
import sys
import tempfile
import zipfile
import zlib
from pathlib import Path

HERE = Path(__file__).resolve().parent
GAME = HERE.parent
sys.path.insert(0, str(HERE))
import import_japanese as ij  # noqa: E402
import make_assets  # noqa: E402
from ps2art import decompress  # noqa: E402
from rommap import RomMap  # noqa: E402

STOCK = GAME / "assets" / "lang" / "ja"
FACE = ("font.png", "font_upper.png")
STAND_IN_FROM = ("lang.json", "names.json")
NO_STAND_IN = ("--stand-in builds its image from assets/lang/ja's lang.json "
               "and names.json, and this tree has not made the JAPANESE "
               "language: give the Japanese cartridge with --ja")


def stand_in_ready():
    """Whether the stand-in's sources are here (the JAPANESE language made
    in this tree)."""
    return all((STOCK / f).is_file() for f in STAND_IN_FROM)


# ---- the stand-in ----

def crc_table():
    table = []
    for i in range(256):
        c = i
        for _ in range(8):
            c = (0xEDB88320 ^ (c >> 1)) if c & 1 else c >> 1
        table.append(c)
    return table


def forge_tail(data, target):
    """data's last four bytes set so that zlib.crc32(data) == target: the
    table entries those bytes must pick, found from the end back (each
    entry's top byte is its own), then the bytes that pick them."""
    table = crc_table()
    top = {t >> 24: i for i, t in enumerate(table)}
    want = target ^ 0xFFFFFFFF
    picks = []
    for _ in range(4):
        i = top[want >> 24]
        picks.append(i)
        want = ((want ^ table[i]) << 8) & 0xFFFFFFFF
    picks.reverse()
    state = zlib.crc32(bytes(data[:-4])) ^ 0xFFFFFFFF
    for k, i in enumerate(picks):
        data[len(data) - 4 + k] = (state ^ i) & 0xFF
        state = (state >> 8) ^ table[i]
    assert zlib.crc32(bytes(data)) == target


def tile_letters():
    tiles = {}
    for start, chars in ij.TILE_ROWS:
        for i, c in enumerate(chars):
            if c != "_":
                tiles.setdefault(c, start + i)
    return tiles


def stand_in(us_image, out):
    """The stand-in image (see the docstring), written to OUT."""
    lang = json.loads((STOCK / "lang.json").read_text(encoding="utf-8"))
    board = json.loads((STOCK / "names.json").read_text(
        encoding="utf-8"))["board"]
    us = RomMap(us_image)
    rom = bytearray(ij.JP_SIZE)

    # the US cartridge's FontsIconsArt stream, where the Japanese has its
    # own: 192 tiles all the same
    at = us.addr("FontsIconsArt")
    tiles, used = decompress(us.rom, at)
    assert len(tiles) == 192, len(tiles)
    rom[ij.JP_FONT:ij.JP_FONT + used] = us.rom[at:at + used]

    # the character map: each character at the code lang.json gives it,
    # made of the tiles that spell it; every other code (0, 0), no letter
    letters = tile_letters()
    base_of = {v: (k, ij.DAKUTEN) for k, v in ij.VOICED.items()}
    base_of.update({v: (k, ij.HANDAKUTEN) for k, v in ij.HALF_VOICED.items()})
    pair_of = {}
    for ch, code in lang["charset"].items():
        if ch in letters:
            pair = (ij.BLANK, letters[ch])
        else:
            base, mark = base_of[ch]
            pair = (mark, letters[base])
        pair_of[ch] = pair
        rom[ij.JP_CHARMAP + 2 * code] = pair[0]
        rom[ij.JP_CHARMAP + 2 * code + 1] = pair[1]

    # the name board's art: window $24's entry, its rows 5-15 two apart
    width = len(board[0])
    art_at = 0x40000
    for r, row in enumerate(board):
        k = 5 + 2 * r
        for c, ch in enumerate(row):
            if ch != " ":
                rom[art_at + (k - 1) * width + c] = pair_of[ch][0]
                rom[art_at + k * width + c] = pair_of[ch][1]
    entry = ij.JP_WINDOWS + 8 * (ij.WIN_NAME_INPUT - 1)
    rom[entry:entry + 8] = (bytes(2) + art_at.to_bytes(4, "big")
                            + bytes([width + 1, 16 - 1]))

    forge_tail(rom, ij.JP_CRC)
    out.write_bytes(rom)


# ---- the runs ----

def snapshot(root):
    out = {}
    for dirpath, _, files in os.walk(root):
        for f in files:
            p = Path(dirpath) / f
            out[p.relative_to(root).as_posix()] = p
    return out


def same_file(a, b):
    if a.read_bytes() == b.read_bytes():
        return "bytes"
    if a.suffix.lower() == ".png" \
            and make_assets.file_digest(a) == make_assets.file_digest(b):
        return "picture"
    return None


def run(argv, log):
    proc = subprocess.run(argv, cwd=GAME, stdout=subprocess.PIPE,
                          stderr=subprocess.PIPE, text=True,
                          encoding="utf-8", errors="replace")
    log.write("$ %s\n%s%s(exit %d)\n" % (" ".join(str(a) for a in argv),
                                         proc.stdout, proc.stderr,
                                         proc.returncode))
    return proc.returncode, proc.stdout.splitlines()


def both(image, rt, commit, scratch, tag, c_tool, log):
    """The Python and the C on IMAGE into scratch/TAG_py, scratch/TAG_c:
    (status, lines, files) of each."""
    out = {}
    for who in ("py", "c"):
        dest = scratch / ("%s_%s" % (tag, who))
        extra = ["--commit", commit] if commit else []
        if who == "py":
            argv = [sys.executable, "-X", "utf8",
                    str(HERE / "import_japanese.py"), str(image), str(rt),
                    "-o", str(dest), *extra]
        else:
            argv = [str(c_tool), "japanese", str(image), str(rt),
                    "-o", str(dest), "--assets", str(GAME / "assets"),
                    *extra]
        status, lines = run(argv, log)
        out[who] = (status, lines, snapshot(dest) if dest.exists() else {})
    return out


def compare(res, failures, what):
    """The two runs the same: files, bytes (pictures) and lines."""
    (ps, pl, pf), (cs, cl, cf) = res["py"], res["c"]
    if ps != 0 or cs != 0:
        failures.append("%s: exit %d (Python), %d (C)" % (what, ps, cs))
        return 0, 0
    if set(pf) != set(cf):
        failures.append("%s: other files (Python's alone %s, C's alone %s)"
                        % (what, sorted(set(pf) - set(cf))[:5],
                           sorted(set(cf) - set(pf))[:5]))
    files = pictures = 0
    for rel in sorted(set(pf) & set(cf)):
        files += 1
        how = same_file(pf[rel], cf[rel])
        if how is None:
            failures.append("%s: %s differs" % (what, rel))
        elif how == "picture":
            pictures += 1
    if pl != cl:
        failures.append("%s: other lines said (Python %d, C %d; first "
                        "Python's %r, C's %r)"
                        % (what, len(pl), len(cl), pl[:1], cl[:1]))
    return files, pictures


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("retranslation", type=Path)
    src = ap.add_mutually_exclusive_group(required=True)
    src.add_argument("--ja", type=Path, help="the Japanese cartridge")
    src.add_argument("--stand-in", dest="stand_in", type=Path,
                     help="the US cartridge (raw): a stand-in made from it")
    ap.add_argument("--commit", default=None,
                    help="the retranslation's commit, when it is not a "
                         "checkout or GitHub's zip")
    ap.add_argument("--scratch", type=Path, default=None)
    ap.add_argument("--keep", action="store_true")
    ap.add_argument("--c", dest="c_tool", type=Path, default=None)
    a = ap.parse_args()

    c_tool = a.c_tool
    if c_tool is None:
        exe = GAME / "ps2_extract.exe"
        c_tool = exe if exe.is_file() else GAME / "ps2_extract"
    c_tool = c_tool.resolve()
    if a.stand_in is not None and not stand_in_ready():
        print("japanese_c: " + NO_STAND_IN, file=sys.stderr)
        return 2

    failures = []
    scratch = Path(tempfile.mkdtemp(prefix="japanese_c_", dir=a.scratch))
    try:
        rt, commit = make_assets.unpack_retranslation(
            a.retranslation.resolve(), scratch)
        if a.commit is not None:
            commit = a.commit
        effective = commit or ij.commit_of(rt)

        if a.ja is not None:
            image = a.ja.resolve()
        else:
            image = scratch / "stand_in.bin"
            stand_in(a.stand_in.resolve(), image)

        with open(scratch / "log.txt", "w", encoding="utf-8") as log:
            res = both(image, rt, commit, scratch, "main", c_tool, log)
            files, pictures = compare(res, failures, "the import")

            # the image in a zip, deflated
            zipped = scratch / "image.zip"
            with zipfile.ZipFile(zipped, "w", zipfile.ZIP_DEFLATED) as z:
                z.writestr("Kaerazaru.md", image.read_bytes())
            compare(both(zipped, rt, commit, scratch, "zip", c_tool, log),
                    failures, "the image in a zip")

            # refusals: a zip of two images; the US cartridge
            two = scratch / "two.zip"
            with zipfile.ZipFile(two, "w", zipfile.ZIP_STORED) as z:
                z.writestr("a.bin", image.read_bytes())
                z.writestr("b.gen", image.read_bytes())
            wrong = [(two, "a zip of two images")]
            if a.stand_in is not None:
                wrong.append((a.stand_in.resolve(), "the US cartridge"))
            for path, what in wrong:
                r = both(path, rt, commit, scratch, "refused", c_tool, log)
                if r["py"][0] == 0 or r["c"][0] == 0:
                    failures.append("%s: taken (exit %d Python, %d C)"
                                    % (what, r["py"][0], r["c"][0]))
                shutil.rmtree(scratch / "refused_py", ignore_errors=True)
                shutil.rmtree(scratch / "refused_c", ignore_errors=True)

        # against the tree's own
        stock_checked = 0
        if not STOCK.is_dir():
            against = ("this tree has not made the JAPANESE language: not "
                       "held to assets/lang/ja")
        elif effective == make_assets.RETRANSLATION_PROVED:
            mine = res["c"][2]
            stock = snapshot(STOCK)
            left_out = FACE if a.stand_in is not None else ()
            for rel in sorted(set(stock) | set(mine)):
                if rel in left_out:
                    continue
                if rel not in stock or rel not in mine:
                    failures.append("assets/lang/ja: %s %s" % (
                        rel, "not made" if rel in stock else "not there"))
                    continue
                stock_checked += 1
                if same_file(stock[rel], mine[rel]) is None:
                    failures.append("assets/lang/ja/%s differs" % rel)
            against = ("%d files the same as assets/lang/ja's%s"
                       % (stock_checked, " (the face left out)"
                          if left_out else ""))
        else:
            against = ("the retranslation's commit %s is not the proved "
                       "one: not held to assets/lang/ja"
                       % ((effective or "(unknown)")[:12]))

        print("japanese_c: %s; %d files the Python's (%d PNGs the same "
              "picture, %d the same bytes), the same lines; %s"
              % ("a stand-in image" if a.stand_in else "the cartridge",
                 files, pictures, files - pictures, against))
    finally:
        if a.keep:
            print("kept: %s" % scratch)
        else:
            shutil.rmtree(scratch, ignore_errors=True)

    if failures:
        print("japanese_c: FAIL (%d)" % len(failures))
        for f in failures:
            print("  " + f)
        return 1
    print("japanese_c: PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
