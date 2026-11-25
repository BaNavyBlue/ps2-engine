#!/usr/bin/env python3
"""(build 192) tools/make_assets.py's part in C (src/extract/exmake.c and
exdigest.c, `./ps2_extract assets` and `./ps2_extract digest`) against the
Python (docs/first_start.md, "In C").

    make ps2_extract
    python3 tools/make_assets_c_check.py IMAGE [RETRANSLATION
                                         (--ja JP_IMAGE | --stand-in)]
                                         [--scratch DIR] [--keep]

IMAGE is the US/European cartridge, raw. Each run is the Python's
make_assets.py in a scratch copy of the game (its Makefile, tools/, and
assets/ holding only the files no extractor makes) beside `ps2_extract
assets` in one of its own (assets/ the same, tools/ only the map and,
where a run wants it, the list), the same arguments to both; the two must
end with the same exit status, say the same lines (the log's path aside)
and leave the same files (each the same bytes, a PNG the same picture):

  * refusals, nothing made: nothing given, a file that is not there, a
    file that is no cartridge, the image with one byte changed (its
    header named), a header with quotes, controls and bytes past ASCII in
    it (Python's repr), files too short for a header, a zip of two
    images, a folder that is not the retranslation, the same cartridge
    twice, the language's two halves alone;
  * the image as players keep it, each a whole extraction: raw, .smd
    (named so, and named .bin: told by its header), a copier's header, a
    zip of the raw image, a zip of the .smd;
  * the check against tools/assets.sha256: the list as the Python's files
    give it (every file the same), then a list with a PNG's and a JSON's
    digest changed, a file that is not there and a file listed twice;
  * a step that fails (assets/data a file, not a folder), with and
    without --keep-going: the same steps tried, the same failed;
  * with RETRANSLATION and the Japanese cartridge (--ja) or a stand-in
    made from IMAGE (--stand-in, tools/japanese_c_check.py's, which needs
    the JAPANESE language made in this tree): the
    JAPANESE language after the game's files, from the retranslation's
    folder, its GitHub zip, a zip with another commit, a zip with its
    files at the top; the list with a "ja" section; and all three files
    at once into an empty game;

and `ps2_extract digest` gives make_assets.py's file_digest for every
file under assets/ and for PNGs of every kind Pillow writes (the kinds
the C does not read as Pillow does must come out EX_DIGEST_UNREAD).

Exit 1 on any difference. The scratch folder goes unless --keep.
"""
import argparse
import os
import random
import re
import shutil
import subprocess
import sys
import tempfile
import zipfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
GAME = HERE.parent
sys.path.insert(0, str(HERE))
import make_assets  # noqa: E402

UNREAD = "png:unreadable"       # extract.h's EX_DIGEST_UNREAD


# ---- the scratch games ----

def files_under(root):
    """{path under root: path} of every file."""
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


class Games:
    """The two scratch games a run is made in, from a template of the
    files no extractor makes (own)."""

    def __init__(self, scratch, own):
        self.scratch = scratch
        self.own = own
        self.count = 0

    def new(self, tag):
        base = self.scratch / ("%02d_%s" % (self.count, tag))
        self.count += 1
        py, c = base / "py", base / "c"
        # (build 192a) a list of proved files the tree has of its own
        # (tools/assets.sha256) stays behind, as it does from the C's
        # tools/: the probe makes the files with no list, and a run that
        # wants one writes its own (write_list)
        shutil.copytree(GAME / "tools", py / "tools",
                        ignore=shutil.ignore_patterns("__pycache__",
                                                      "assets.sha256"))
        shutil.copy2(GAME / "Makefile", py / "Makefile")
        (c / "tools").mkdir(parents=True)
        shutil.copy2(GAME / "tools" / "rom_map.json", c / "tools")
        for root in (py, c):
            for rel in self.own:
                dest = root / "assets" / rel
                dest.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(GAME / "assets" / rel, dest)
            (root / "assets").mkdir(exist_ok=True)
        return py, c


def run_py(tree, args, log):
    proc = subprocess.run([sys.executable, "-X", "utf8",
                           str(tree / "tools" / "make_assets.py"), *args],
                          cwd=tree, stdout=subprocess.PIPE,
                          stderr=subprocess.PIPE, text=True,
                          encoding="utf-8", errors="replace")
    log.write("$ (py) %s\n%s%s(exit %d)\n" % (" ".join(args), proc.stdout,
                                             proc.stderr, proc.returncode))
    logged = str(tree.resolve() / "make_assets.log")
    lines = [l.replace(logged, "make_assets.log")
             for l in proc.stdout.splitlines()]
    return proc.returncode, lines


def run_c(c_tool, tree, args, log):
    proc = subprocess.run([str(c_tool), "assets", *args], cwd=tree,
                          stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                          text=True, encoding="utf-8", errors="replace")
    log.write("$ (c) %s\n%s%s(exit %d)\n" % (" ".join(args), proc.stdout,
                                            proc.stderr, proc.returncode))
    lines = proc.stdout.splitlines()
    if proc.stderr.strip():
        lines.append("(stderr) " + proc.stderr.strip())
    return proc.returncode, lines


STEP_LINE = re.compile(r"^\[\d+/\d+\] |^FAILED: ")


class Check:
    def __init__(self, c_tool, games, log):
        self.c_tool = c_tool
        self.games = games
        self.log = log
        self.failures = []
        self.runs = 0
        self.files = 0

    def both(self, what, trees, args, status=None, steps_only=False,
             compare_files=True):
        """Both on ARGS in TREES: the same status (STATUS where given), the
        same lines (only the steps' and FAILED's with steps_only), the same
        files under assets/."""
        self.runs += 1
        py, c = trees
        ps, pl = run_py(py, args, self.log)
        cs, cl = run_c(self.c_tool, c, args, self.log)
        if ps != cs or (status is not None and ps != status):
            self.failures.append("%s: exit %d (Python), %d (C)%s"
                                 % (what, ps, cs, "" if status is None
                                    else ", %d wanted" % status))
        if steps_only:
            pl = [l for l in pl if STEP_LINE.match(l)]
            cl = [l for l in cl if STEP_LINE.match(l)]
        if pl != cl:
            first = next((i for i, (a, b) in enumerate(zip(pl, cl)) if a != b),
                         min(len(pl), len(cl)))
            self.failures.append(
                "%s: other lines said (Python %d, C %d; at line %d Python's "
                "%r, C's %r)" % (what, len(pl), len(cl), first + 1,
                                 pl[first] if first < len(pl) else None,
                                 cl[first] if first < len(cl) else None))
        if compare_files:
            self.same_trees(what, py, c)
        return ps, pl

    def same_trees(self, what, py, c):
        pf = files_under(py / "assets")
        cf = files_under(c / "assets")
        if set(pf) != set(cf):
            self.failures.append("%s: other files (Python's alone %s, C's "
                                 "alone %s)" % (what,
                                                sorted(set(pf) - set(cf))[:5],
                                                sorted(set(cf) - set(pf))[:5]))
        for rel in sorted(set(pf) & set(cf)):
            self.files += 1
            if same_file(pf[rel], cf[rel]) is None:
                self.failures.append("%s: assets/%s differs" % (what, rel))


# ---- the inputs ----

def interleave(image):
    """A .smd of IMAGE: a 512-byte header (blocks, 3, AA BB at 8), then
    each 16 KB block's odd bytes, then its even ones."""
    head = bytearray(512)
    head[0] = len(image) // 16384
    head[1] = 3
    head[8], head[9] = 0xAA, 0xBB
    body = bytearray()
    for b in range(0, len(image), 16384):
        block = image[b:b + 16384]
        body += block[1::2] + block[0::2]
    return bytes(head) + bytes(body)


def write_zip(path, entries, method=zipfile.ZIP_DEFLATED, comment=b""):
    with zipfile.ZipFile(path, "w", method) as z:
        for name, data in entries:
            z.writestr(name, data)
        z.comment = comment


def inputs(dest, image):
    dest.mkdir(parents=True)
    rnd = random.Random(192)
    smd = interleave(image)
    made = {
        "us.bin": image,
        "Phantasy Star II (copy).smd": smd,
        "smd_named.bin": smd,
        "copier.bin": bytes(512) + image,
        "junk.bin": bytes(rnd.randrange(256) for _ in range(1000)),
        "short.bin": image[:0x18E],
        "short2.bin": image[:0x18F],
    }
    changed = bytearray(image)
    changed[0x200] ^= 0xFF
    made["changed.bin"] = bytes(changed)
    odd = bytearray(image)
    odd[0x150:0x180] = (b"  It's \x01 \x7f \xe9 back\\slash \" q\t end \x1c"
                        + b" " * 48)[:48]
    odd[0x18C:0x18E] = b"\x00'"
    made["odd_header.bin"] = bytes(odd)
    for name, data in made.items():
        (dest / name).write_bytes(data)
    write_zip(dest / "us.zip", [("Phantasy Star II (UE) (REV02) [!].bin",
                                 image)])
    write_zip(dest / "smd.zip", [("ps2.SMD", smd)], zipfile.ZIP_STORED)
    write_zip(dest / "two.zip", [("a.bin", image), ("b.gen", image)],
              zipfile.ZIP_STORED)
    (dest / "not_rt").mkdir()
    (dest / "not_rt" / "readme.txt").write_text("nothing here\n")


def japanese_inputs(dest, retranslation, jp_image, us_image):
    """The JAPANESE language's files in DEST: jp.bin, the retranslation as
    a folder (rt/, its files one folder down, as GitHub's zip unpacks),
    and as zips. The retranslation's own root as well (rt_root)."""
    if jp_image is not None:
        shutil.copy2(jp_image, dest / "jp.bin")
    else:
        import japanese_c_check
        japanese_c_check.stand_in(us_image, dest / "jp.bin")
    root, _ = make_assets.unpack_retranslation(retranslation.resolve(),
                                               dest / "unpacked")
    root = make_assets.retranslation_root(root) or root
    files = {f: (root / f).read_bytes()
             for f in make_assets.RETRANSLATION_FILES}
    proved = make_assets.RETRANSLATION_PROVED
    top = "ps2-retranslation-%s/" % proved
    for f, data in files.items():
        out = dest / "rt" / top / f
        out.parent.mkdir(parents=True, exist_ok=True)
        out.write_bytes(data)
    (dest / "rt" / top / "README.md").write_text("the retranslation\n")
    write_zip(dest / "rt.zip",
              [(top + f, d) for f, d in files.items()]
              + [(top + "README.md", b"the retranslation\n")],
              comment=proved.encode())
    write_zip(dest / "rt_other.zip",
              [(top + f, d) for f, d in files.items()],
              comment=("0123456789abcdef" * 3)[:40].encode())
    write_zip(dest / "rt_top.zip", list(files.items()))
    return dest / "rt" / top


# ---- the list ----

def write_list(tree, sections, extra=()):
    """tools/assets.sha256 in TREE from its own files: section "us" every
    file under assets/ but lang/ja, section "ja" lang/ja's and the preload
    list (the sections make_assets.py reads), then EXTRA lines."""
    files = files_under(tree / "assets")
    lines = ["# The files tools/make_assets.py makes, by SHA-256."]
    for s in sections:
        lines.append("# section %s" % s)
        for rel in sorted(files):
            ja = rel.startswith("lang/ja/") or rel == "manifest.txt"
            if (s == "us" and not rel.startswith("lang/ja/")) or \
                    (s == "ja" and ja):
                lines.append("%s  assets/%s"
                             % (make_assets.file_digest(files[rel]), rel))
    lines += list(extra)
    (tree / "tools" / "assets.sha256").write_text("\n".join(lines) + "\n",
                                                  encoding="utf-8",
                                                  newline="\n")


def tampered(tree):
    """The list of TREE changed: a PNG's and a JSON's digest, a file that
    is not there, and a file listed again (the later line kept)."""
    path = tree / "tools" / "assets.sha256"
    lines = path.read_text(encoding="utf-8").splitlines()
    png = next(i for i, l in enumerate(lines) if l.endswith(".png"))
    js = next(i for i, l in enumerate(lines) if l.endswith(".json"))
    lines[png] = "png:" + "0" * 64 + "  " + lines[png].split(None, 1)[1]
    lines[js] = "1" * 64 + "  " + lines[js].split(None, 1)[1]
    good = next(i for i, l in enumerate(lines)
                if l.endswith(".txt") and not l.startswith("#"))
    rel = lines[good].split(None, 1)[1]
    lines.insert(good, "2" * 64 + "  " + rel)       # wrong, then right
    lines.append("3" * 64 + "  assets/not_made.json")
    path.write_text("\n".join(lines) + "\n", encoding="utf-8", newline="\n")


# ---- the digests ----

def digest_kinds(dest, c_tool, failures, log):
    """PNGs of every kind Pillow writes, and the files under assets/: the
    C's digest the Python's, or UNREAD for a kind it does not read."""
    from PIL import Image
    dest.mkdir(parents=True)
    rnd = random.Random(1920)
    w, h = 13, 7
    made = {}

    def px(n):
        return bytes(rnd.randrange(256) for _ in range(n))

    for mode, size in (("L", 1), ("LA", 2), ("RGB", 3), ("RGBA", 4),
                       ("1", 1), ("I;16", 2)):
        im = Image.frombytes(mode, (w, h), px(w * h * size)
                             if mode != "1" else px((w + 7) // 8 * h))
        made["mode_%s.png" % mode.replace(";", "_")] = (im, {})
    for bits, colours in ((1, 2), (2, 4), (4, 16), (8, 200), (8, 256)):
        im = Image.frombytes("P", (w, h),
                             bytes(rnd.randrange(colours) for _ in range(w * h)))
        im.putpalette(px(colours * 3))
        made["p%d_%d.png" % (bits, colours)] = (im, {"bits": bits})
    im = made["p8_200.png"][0]
    made["p_trns_index.png"] = (im, {"transparency": 5})
    made["p_trns_bytes.png"] = (im, {"transparency": bytes(range(0, 250, 7))})
    made["p_trns_quote.png"] = (im, {"transparency": b"\xff'\x00\x10\\\n"})
    made["l_trns.png"] = (made["mode_L.png"][0], {"transparency": 77})
    made["rgb_trns.png"] = (made["mode_RGB.png"][0],
                            {"transparency": (1, 2, 3)})
    for name, (im, opts) in made.items():
        im.save(dest / name, **opts)
    # a PNG with a chunk's CRC broken, a file named .png that is none, and
    # one that is no PNG by its name
    good = (dest / "mode_RGB.png").read_bytes()
    (dest / "broken_crc.png").write_bytes(good[:29] + bytes([good[29] ^ 1])
                                          + good[30:])
    (dest / "not_a.png").write_bytes(b"hello")
    (dest / "plain.txt").write_bytes(b"hello\n")

    paths = sorted(str(p) for p in dest.iterdir())
    paths += sorted(str(p) for p in (GAME / "assets").rglob("*")
                    if p.is_file())
    c = {}
    for i in range(0, len(paths), 200):
        out = subprocess.run([str(c_tool), "digest", *paths[i:i + 200]],
                             stdout=subprocess.PIPE, text=True,
                             encoding="utf-8", check=True).stdout
        for line in out.splitlines():
            d, p = line.split(None, 1)
            c[p] = d
    unread = []
    same = 0
    for p in paths:
        try:
            want = make_assets.file_digest(p)
        except Exception as e:      # Pillow refused it
            want = "(Pillow: %s)" % type(e).__name__
        got = c.get(p)
        if got == want:
            same += 1
        elif got == UNREAD:
            unread.append(Path(p).name)
        else:
            failures.append("digest %s: Python %s, C %s" % (p, want, got))
    # the kinds the C leaves to "differs": none of them is in assets/
    expected_unread = {"mode_1.png", "mode_I_16.png", "broken_crc.png",
                       "not_a.png"}
    if set(unread) != expected_unread:
        failures.append("digests unread: %s, wanted %s"
                        % (sorted(unread), sorted(expected_unread)))
    log.write("digests: %d the same, unread %s\n" % (same, unread))
    return same, len(paths), unread


# ---- the runs ----

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("image", type=Path, help="the US/European cartridge, "
                                             "raw")
    ap.add_argument("retranslation", type=Path, nargs="?", default=None)
    src = ap.add_mutually_exclusive_group()
    src.add_argument("--ja", type=Path, default=None)
    src.add_argument("--stand-in", dest="stand_in", action="store_true")
    ap.add_argument("--scratch", type=Path, default=None)
    ap.add_argument("--keep", action="store_true")
    ap.add_argument("--c", dest="c_tool", type=Path, default=None)
    a = ap.parse_args()
    if a.retranslation is not None and a.ja is None and not a.stand_in:
        ap.error("with RETRANSLATION, --ja JP_IMAGE or --stand-in")
    if a.retranslation is not None and a.stand_in:
        import japanese_c_check
        if not japanese_c_check.stand_in_ready():
            print("make_assets_c: " + japanese_c_check.NO_STAND_IN,
                  file=sys.stderr)
            return 2

    c_tool = a.c_tool
    if c_tool is None:
        exe = GAME / "ps2_extract.exe"
        c_tool = exe if exe.is_file() else GAME / "ps2_extract"
    c_tool = c_tool.resolve()
    image = a.image.read_bytes()

    scratch = Path(tempfile.mkdtemp(prefix="make_assets_c_", dir=a.scratch))
    failures = []
    try:
        log = open(scratch / "log.txt", "w", encoding="utf-8")
        ins = scratch / "in"
        inputs(ins, image)

        def arg(name):
            return os.path.relpath(ins / name, scratch / "00_x" / "py")

        # the files no extractor makes: the game's files less what the
        # Python makes in an empty game
        probe = scratch / "probe"
        Games(scratch, []).new("probe")
        (scratch / "00_probe").rename(probe)
        status, _ = run_py(probe / "py", [str(ins / "us.bin")], log)
        if status != 0:
            raise SystemExit("make_assets_c: the Python failed in an empty "
                             "game: see %s" % (scratch / "log.txt"))
        made = set(files_under(probe / "py" / "assets"))
        own = sorted(rel for rel in files_under(GAME / "assets")
                     if rel not in made and not rel.startswith("lang/ja/"))
        shutil.rmtree(probe)

        games = Games(scratch, own)
        check = Check(c_tool, games, log)
        check.failures = failures

        # refusals: nothing made, nothing run
        trees = games.new("refusals")
        for what, args in (
                ("nothing given", []),
                ("no such file", ["nope.bin"]),
                ("not a cartridge", [arg("junk.bin")]),
                ("one byte changed", [arg("changed.bin")]),
                ("a header to quote", [arg("odd_header.bin")]),
                ("too short for a header", [arg("short.bin")]),
                ("just long enough", [arg("short2.bin")]),
                ("a zip of two", [arg("two.zip")]),
                ("not the retranslation", [arg("not_rt")]),
                ("the same twice", [arg("us.bin"), arg("us.zip")])):
            check.both(what, trees, args, status=2)
        shutil.rmtree(trees[0].parent)

        # the image as players keep it: each a whole extraction
        first = None
        for name in ("us.bin", "Phantasy Star II (copy).smd",
                     "smd_named.bin", "copier.bin", "us.zip", "smd.zip"):
            trees = games.new("us")
            check.both(name, trees, [arg(name)], status=0)
            if first is None:
                first = trees
            else:
                check.same_trees("%s against us.bin" % name, first[1],
                                 trees[1])
                shutil.rmtree(trees[0].parent)

        # the list: as made, then changed
        for tree in first:
            write_list(tree, ["us"])
        check.both("the list", first, [arg("us.bin")], status=0)
        for tree in first:
            tampered(tree)
        check.both("the list changed", first, [arg("us.bin")], status=3)
        for tree in first:
            (tree / "tools" / "assets.sha256").unlink()

        # a step that fails
        for keep in ([], ["--keep-going"]):
            trees = games.new("fails")
            for tree in trees:
                (tree / "assets" / "data").write_text("not a folder\n")
            check.both("a step fails%s" % (" (keep going)" if keep else ""),
                       trees, [arg("us.bin"), *keep], status=1,
                       steps_only=True, compare_files=False)
            shutil.rmtree(trees[0].parent)

        japanese = "not run (no RETRANSLATION given)"
        if a.retranslation is not None:
            rt = japanese_inputs(ins, a.retranslation, a.ja, a.image)
            trees = games.new("ja_alone")
            check.both("the retranslation alone, no game", trees,
                       [arg("rt")], status=2)
            check.both("the Japanese cartridge alone, no game", trees,
                       [arg("jp.bin")], status=2)
            shutil.rmtree(trees[0].parent)
            check.both("the cartridge without the retranslation", first,
                       [arg("us.bin"), arg("jp.bin")], status=2,
                       compare_files=False)
            check.both("the retranslation without the cartridge", first,
                       [arg("rt.zip")], status=2, compare_files=False)
            check.both("the Japanese cartridge twice", first,
                       [arg("jp.bin"), arg("jp.bin")], status=2,
                       compare_files=False)
            for what, args in (
                    ("JAPANESE from the folder", ["--ja", arg("jp.bin"),
                                                  "--retranslation",
                                                  arg("rt")]),
                    ("JAPANESE from its root", [arg("jp.bin"),
                                                os.path.relpath(
                                                    rt, scratch / "00_x"
                                                    / "py")]),
                    ("JAPANESE from GitHub's zip", [arg("rt.zip"),
                                                    arg("jp.bin")]),
                    ("JAPANESE, another commit", [arg("jp.bin"),
                                                  arg("rt_other.zip")]),
                    ("JAPANESE, the files at the top", [arg("jp.bin"),
                                                        arg("rt_top.zip")])):
                check.both(what, first, args, status=0)
            check.both("JAPANESE from GitHub's zip again", first,
                       [arg("rt.zip"), arg("jp.bin")], status=0)
            for tree in first:
                write_list(tree, ["us", "ja"])
            check.both("the list with JAPANESE", first,
                       [arg("jp.bin"), arg("rt.zip")], status=0)
            trees = games.new("all")
            check.both("all three at once", trees,
                       [arg("jp.bin"), arg("us.zip"), arg("rt.zip")],
                       status=0)
            shutil.rmtree(trees[0].parent)
            japanese = ("the %s" % ("cartridge" if a.ja else "stand-in"))
        shutil.rmtree(first[0].parent)

        same, total, unread = digest_kinds(scratch / "digests", c_tool,
                                           failures, log)
        log.close()
        print("make_assets_c: %d runs, both the same (%d files compared); "
              "JAPANESE: %s; digests %d of %d the Python's, %d left unread "
              "(%s)" % (check.runs, check.files, japanese, same, total,
                        len(unread), ", ".join(sorted(unread))))
    finally:
        if a.keep:
            print("kept: %s" % scratch)
        else:
            shutil.rmtree(scratch, ignore_errors=True)

    if failures:
        print("make_assets_c: FAIL (%d)" % len(failures))
        for f in failures:
            print("  " + f)
        return 1
    print("make_assets_c: PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
