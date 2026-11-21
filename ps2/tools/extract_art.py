#!/usr/bin/env python3
"""Decompress the art/*.bin blobs into indexed PNG tile sheets.

PNGs are written in palette mode so the index of every pixel survives the
round trip. That matters: the original implements fades, flashes and colour
cycling as arithmetic on palette entries, so the indices are the data and
the RGB values are only a preview.

Tile grid widths are a guess (see WIDTH_HINTS). Nothing downstream depends
on them -- the .ps2t stores a flat tile list and the tilemap decides
placement -- but a wrong guess makes the preview PNG look shuffled.

(build 185) The blobs come out of the cartridge image, not the
disassembly's art/: tools/rom_map.json has every binclude'd file's place
and length (tools/rommap.py), so a file is the ROM's bytes there.

    python3 tools/extract_art.py ROM -o assets
"""

import argparse
import json
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
import asset_names  # noqa: E402
import ps2png  # noqa: E402
from ps2art import ArtError, decompress  # noqa: E402
from rommap import RomMap  # noqa: E402



# Verified against rolf_portrait: portraits are 8 tiles wide.
WIDTH_HINTS = {
    "_portrait": 8,
    "_port": 8,
}
DEFAULT_WIDTH = 16


def guess_width(stem, tile_count):
    """Preview grid width only.

    Portraits are true rectangles and their width is known. Battle art is
    assembled from sprite mappings rather than laid out as a grid -- several
    have prime tile counts -- so those just get a fixed-width contact strip
    with the last row padded. The real placement comes from the sprite
    mapping tables, not from here.
    """
    for suffix, width in WIDTH_HINTS.items():
        if stem.endswith(suffix) and tile_count % width == 0:
            return width
    return min(DEFAULT_WIDTH, tile_count) if tile_count else 1


def palette_for(stem, palettes):
    """Match rolf_portrait -> Pal_RolfPortrait, loosely."""
    key = re.sub(r"[^a-z0-9]", "", stem.lower())
    for name, data in palettes.items():
        cand = re.sub(r"[^a-z0-9]", "", name.lower()).removeprefix("pal")
        if cand and (cand in key or key in cand):
            return name, data["rgb"]
    return None, None


def art_files(rom):
    """(build 185) [(stem, file name)] of the disassembly's art/*.bin, in
    name order: the binclude'd files tools/rom_map.json places in the
    cartridge image (a RomMap); rom.file(name) is a file's bytes."""
    found = []
    for key in rom.files:
        folder, _, base = key.rpartition("/")
        if folder == "art" and base.endswith(".bin"):
            found.append((base[:-len(".bin")], key))
    return sorted(found)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("rom", type=Path, help="the cartridge image")
    ap.add_argument("-o", "--outdir", type=Path, default=Path("assets"))
    ap.add_argument("-p", "--palettes", type=Path,
                    default=Path("assets/palettes/palettes.json"))
    args = ap.parse_args()

    rom = RomMap(args.rom)
    palettes = {}
    if args.palettes.exists():
        palettes = json.loads(args.palettes.read_text())

    tile_dir = args.outdir / "tiles"
    tile_dir.mkdir(parents=True, exist_ok=True)

    manifest, failures = {}, []
    for stem, key in art_files(rom):
        # (build 97b) Tile-index maps are extract_nametables.py's, and the
        # abbreviated file names get a readable one (tools/asset_names.py).
        if stem in asset_names.ART_NOT_ART:
            continue
        raw = rom.file(key)
        try:
            tiles, used = decompress(raw)
        except ArtError as exc:
            failures.append((key, str(exc)))
            continue

        name = asset_names.art_name(stem)
        # (build 156) The art the pictures replaced is read from the
        # cartridge image where it is needed (disasm_art.py), not kept as
        # a sheet: an old one goes.
        if name in asset_names.PICTURE_ART:
            (tile_dir / f"{name}.png").unlink(missing_ok=True)
            continue
        width = guess_width(stem, len(tiles))
        pal_name, rgb = palette_for(stem, palettes)
        if rgb is None:
            rgb = ps2png.grey_ramp()
        else:
            rgb = [(int(c[1:3], 16), int(c[3:5], 16), int(c[5:7], 16))
                   for c in rgb[:16]]

        ps2png.write_tile_sheet(tile_dir / f"{name}.png", tiles, rgb,
                                across=width)

        manifest[name] = {
            "source": key,
            "tiles": len(tiles),
            "grid_width": width,
            "palette": pal_name,
            "compressed_bytes": len(raw),
            "bytes_consumed": used,
            "trailing_bytes": len(raw) - used,
            "file": f"tiles/{name}.png",
        }

    # (build 97b) No art_manifest.json: nothing read it. The counts are
    # printed instead.

    total = sum(m["tiles"] for m in manifest.values())
    print(f"decoded {len(manifest)} files, {total} tiles")
    unmatched = [k for k, m in manifest.items() if m["palette"] is None]
    if unmatched:
        print(f"  no palette matched for {len(unmatched)}: "
              f"{', '.join(unmatched[:6])}...")
    for name, why in failures:
        print(f"  FAILED {name}: {why}", file=sys.stderr)


if __name__ == "__main__":
    main()
