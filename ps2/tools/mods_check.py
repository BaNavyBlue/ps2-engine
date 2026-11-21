#!/usr/bin/env python3
"""(build 157d) A refused mod is set aside and the game's own read in its
place (docs/mods.md, "When a mod is refused").

  python3 tools/mods_check.py [--game ./ps2_headless] [--frames 3600]
                              [--jobs N] [--keep DIR]

The headless game (main.c, the only one that reads a mods folder) from
power-on through the SEGA screen and the title into the attract crawl on
Motavia, once with no mods and once a case: each case puts one broken
file in a scratch mods folder (PS2_MODS) -- a picture, a map, a JSON
file, a script, the credits, each the way a loader refuses it. A case
passes when the game runs to the end, says the mod was set aside, and
plays and draws exactly what it does with no mods (the WAV and the last
frame byte for byte). A last case, an edit the loaders take, must be
named in use, not set aside, and change the last frame: the mods are
still read.

Each run is in a scratch folder holding the tree's assets/ (hard links
where the file system has them), since main.c writes saves/ where it
runs. Until 157d only the music was guarded: most of these cases stopped
the game at its start ("game_init" fails; exit 1).
"""
import argparse
import hashlib
import io
import os
import re
import shutil
import subprocess
import sys
import tempfile
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

from PIL import Image

TREE = Path(__file__).resolve().parent.parent


def own(rel):
    return (TREE / "assets" / rel).read_bytes()


def png_bytes(img):
    out = io.BytesIO()
    img.save(out, format="PNG")
    return out.getvalue()


def indexed_png(w, h):
    img = Image.new("P", (w, h), 0)
    img.putpalette([0, 0, 0, 255, 255, 255])
    return png_bytes(img)


def ramp_past_palette():
    """sega_logo_sweep.png with a pixel past its PLTE: three colours, so
    two bits a pixel (Pillow's choice), and a pixel of colour 3."""
    src = Image.open(TREE / "assets/palettes/sega_logo_sweep.png")
    img = Image.new("P", src.size, 0)
    img.putpalette([0, 0, 0, 255, 255, 255, 128, 128, 128])
    img.putpixel((0, 0), 3)
    return png_bytes(img)


def dissolve_short():
    text = own("tables/dissolve_order.json").decode()
    i = text.index('"order"')
    j = text.index("[", text.index("[", i) + 1)        # the first position
    k = text.index("]", j) + 1
    rest = text[k:].lstrip()
    if rest.startswith(","):
        rest = rest[1:]
    return (text[:j] + rest.lstrip()).encode()


def json_unclosed():
    text = own("data/items.json").decode().rstrip()
    return text[:-1].encode()                            # no closing bracket


def tmx_cut():
    data = own("nametables/TitleScrBGTileInd.tmx")
    return data[: len(data) // 2]


def gid_flagged(rel, flag):
    """The map's first non-empty cell with a Tiled flip flag on it."""
    text = own(rel).decode()
    i = text.index('encoding="csv">') + len('encoding="csv">')
    m = re.compile(r"[1-9][0-9]*").search(text, i)
    gid = int(m.group(0)) | flag
    return (text[: m.start()] + str(gid) + text[m.end():]).encode()


def picture_size_wrong():
    text = own("art/scenes/sega_logo/sega_logo.json").decode()
    return re.sub(r'"size":\s*\[[^\]]*\]', '"size": [1, 1]', text,
                  count=1).encode()


def rgba_png(w, h):
    return png_bytes(Image.new("RGBA", (w, h), (255, 0, 255, 255)))


def credits_no_end():
    lines = own("script/ending_credits.txt").decode().split("\n")
    return "\n".join(line for line in lines
                     if line.strip() != "{end}").encode()


def level_no_plane_a():
    return own("levels/00_MotaviaOutside.tmx").decode().replace(
        'name="planeA"', 'name="planeX"').encode()


def palette_inverted():
    """The control: a palette the loaders take, its colours inverted."""
    src = Image.open(TREE / "assets/palettes/level_motavia_outside.png")
    pal = src.getpalette()
    src.putpalette([255 - v for v in pal])
    return png_bytes(src)


# name, {path under assets/: bytes}, the loader that refuses it
CASES = [
    ("png_not_a_png", {"tiles/title_copyright_art.png": b"not a png"},
     "png"),
    ("tile_sheet_grid", {"tiles/title_copyright_art.png": None}, "tileset"),
    ("palette_png_cut", {"palettes/level_motavia_outside.png": None},
     "png"),
    ("ramp_past_palette", {"palettes/sega_logo_sweep.png": None}, "table"),
    ("dissolve_63", {"tables/dissolve_order.json": None}, "table"),
    ("json_unclosed", {"data/items.json": None}, "json"),
    ("tmx_cut", {"nametables/TitleScrBGTileInd.tmx": None}, "tmx"),
    ("nametable_diagonal", {"nametables/SegaLogoTileInd.tmx": None},
     "nametable"),
    ("winmap_flipped", {"winart/WinArt_AmyPortrait.tmx": None}, "winmap"),
    ("picture_size", {"art/scenes/sega_logo/sega_logo.json": None}, "art"),
    ("picture_png_size", {"art/portraits/rolf/rolf.png": None}, "art"),
    ("script_empty", {"script/Script_Opening.txt": b"# nothing\n"},
     "script"),
    ("credits_no_end", {"script/ending_credits.txt": None}, "credits"),
    ("level_no_plane_a", {"levels/00_MotaviaOutside.tmx": None}, "level"),
]
MAKE = {
    "tile_sheet_grid": lambda: indexed_png(7, 7),
    "palette_png_cut": lambda: own("palettes/level_motavia_outside.png")[:40],
    "ramp_past_palette": ramp_past_palette,
    "dissolve_63": dissolve_short,
    "json_unclosed": json_unclosed,
    "tmx_cut": tmx_cut,
    "nametable_diagonal": lambda: gid_flagged(
        "nametables/SegaLogoTileInd.tmx", 0x20000000),
    "winmap_flipped": lambda: gid_flagged("winart/WinArt_AmyPortrait.tmx",
                                          0x80000000),
    "picture_size": picture_size_wrong,
    "picture_png_size": lambda: rgba_png(8, 8),
    "credits_no_end": credits_no_end,
    "level_no_plane_a": level_no_plane_a,
}
CONTROL = ("palette_edited", {"palettes/level_motavia_outside.png": None})


def link_tree(dst):
    """assets/ into dst: hard links, or copies where links fail."""
    def link(src, to):
        try:
            os.link(src, to)
        except OSError:
            shutil.copy2(src, to)
    shutil.copytree(TREE / "assets", dst / "assets", copy_function=link)


def run(game, frames, name, mods, keep):
    scratch = Path(tempfile.mkdtemp(prefix=f"mods_{name}_",
                                    dir=keep if keep else None))
    try:
        link_tree(scratch)
        modsdir = scratch / "mods"
        modsdir.mkdir()
        for rel, data in mods.items():
            f = modsdir / rel
            f.parent.mkdir(parents=True, exist_ok=True)
            f.write_bytes(data)
        env = dict(os.environ, PS2_FRAMES=str(frames),
                   PS2_WAV=str(scratch / "out.wav"),
                   PS2_OUT=str(scratch / "out.ppm"),
                   PS2_MODS=str(modsdir))
        p = subprocess.run([game], cwd=scratch, env=env, capture_output=True,
                           text=True, timeout=900)
        if keep:
            (scratch / "stderr.txt").write_text(p.stderr)
        digest = {}
        for out in ("out.wav", "out.ppm"):
            f = scratch / out
            digest[out] = (hashlib.sha256(f.read_bytes()).hexdigest()
                           if f.exists() else None)
        return p.returncode, p.stderr, digest
    finally:
        if not keep:
            shutil.rmtree(scratch, ignore_errors=True)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--game", default="./ps2_headless")
    ap.add_argument("--frames", type=int, default=3600)
    ap.add_argument("--jobs", type=int, default=os.cpu_count() or 4)
    ap.add_argument("--keep", default=None,
                    help="keep each run's scratch folder under DIR")
    a = ap.parse_args()
    game = str(Path(a.game).resolve())
    if a.keep:
        Path(a.keep).mkdir(parents=True, exist_ok=True)

    cases = []
    for name, files, kind in CASES:
        mods = {rel: (data if data is not None else MAKE[name]())
                for rel, data in files.items()}
        cases.append((name, mods, kind))
    control_mods = {rel: palette_inverted() for rel in CONTROL[1]}

    jobs = [("none", {})] + [(n, m) for n, m, _ in cases] \
        + [(CONTROL[0], control_mods)]
    with ThreadPoolExecutor(a.jobs) as ex:
        results = dict(zip([n for n, _ in jobs],
                           ex.map(lambda j: run(game, a.frames, j[0], j[1],
                                                a.keep), jobs)))

    rc0, err0, base = results["none"]
    good = rc0 == 0 and base["out.wav"] is not None
    print(f"{'ok  ' if good else 'FAIL'} no mods: exit {rc0}, "
          f"{a.frames} frames")
    for name, mods, kind in cases:
        rc, err, dig = results[name]
        rel = next(iter(mods))
        aside = f"{rel} set aside" in err.replace("\\", "/")
        said = "refused (above)" in err
        same = dig == base
        ok = rc == 0 and aside and said and same
        good = good and ok
        why = [] if ok else [f"exit {rc}"] + \
            (["not set aside"] if not aside else []) + \
            (["no refusal said"] if not said else []) + \
            (["output differs"] if not same else [])
        print(f"{'ok  ' if ok else 'FAIL'} {name} ({kind}): {rel}"
              + ("" if ok else " -- " + ", ".join(why)))
    rc, err, dig = results[CONTROL[0]]
    rel = next(iter(CONTROL[1]))
    in_use = f"{rel} in place of the game's own" in err.replace("\\", "/")
    aside = "set aside" in err
    ok = rc == 0 and in_use and not aside and dig["out.ppm"] != base["out.ppm"]
    good = good and ok
    print(f"{'ok  ' if ok else 'FAIL'} {CONTROL[0]} (taken): {rel} in use, "
          f"the last frame {'changed' if dig['out.ppm'] != base['out.ppm'] else 'the same'}")
    print("PASS" if good else "FAIL")
    return 0 if good else 1


if __name__ == "__main__":
    sys.exit(main())
