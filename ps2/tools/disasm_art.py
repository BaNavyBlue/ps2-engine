#!/usr/bin/env python3
"""(build 156) The art as the cartridge holds it, by tile sheet name.

Every compressed-art tile sheet is made by extract_art.py (an
art/<stem>.bin, named by asset_names.art_name) or extract_inline_art.py (an
inline block under a label, named by its output_name). This reads the same
art straight from the cartridge image: a sheet's tiles, padded to the grid
its PNG would have (ps2png.write_tile_sheet's), and that grid. So the
extractors that read sheets -- the pictures, the window maps' and screen
nametables' tilesets, the art dump -- need no PNG, and the sheets nothing
reads at run time (asset_names.PICTURE_ART: the art the pictures replaced)
are not kept under assets/tiles.

(build 185) From the cartridge image through tools/rom_map.json
(tools/rommap.py), by the disassembly's names for the art -- its art/*.bin
files and its labels -- where until now it read ps2.asm and art/ itself.

    python3 tools/disasm_art.py ROM [--tiles assets/tiles]

checks that every sheet under assets/tiles this reader can make has the
same tiles and grid it gives (exit 0 when all do), and lists the
PICTURE_ART it gives that assets/tiles no longer holds.
"""

import argparse
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
import asset_names  # noqa: E402
import extract_art  # noqa: E402
import extract_inline_art  # noqa: E402
import ps2png  # noqa: E402
from ps2art import decompress  # noqa: E402
from rommap import RomMap  # noqa: E402


class DisasmArt:
    """The compressed art of the cartridge image, by the disassembly's
    names. `rom` is the image's path or a RomMap of it."""

    def __init__(self, rom):
        self.rom = rom if isinstance(rom, RomMap) else RomMap(rom)
        self._index = None
        self._sheets = {}

    def index(self):
        """{sheet name: ("bin", stem) or ("inline", label)}, as the two
        extractors name what they write."""
        if self._index is None:
            index = {}
            sources = [(asset_names.art_name(stem), ("bin", stem))
                       for stem, _ in extract_art.art_files(self.rom)
                       if stem not in asset_names.ART_NOT_ART]
            sources += [(extract_inline_art.output_name(label),
                         ("inline", label))
                        for label in extract_inline_art.DEFAULT_LABELS]
            for name, src in sources:
                if name in index:
                    raise ValueError(f"{name}: both {index[name]} and {src}")
                index[name] = src
            self._index = index
        return self._index

    def names(self):
        return sorted(self.index())

    def sheet(self, name):
        """(tiles, across, rows): each tile its 64 indices, row by row, the
        last grid row padded with blank tiles, as the sheet's PNG holds
        them."""
        if name not in self._sheets:
            kind, key = self.index()[name]
            if kind == "bin":
                raw = self.rom.file(f"art/{key}.bin")
                tiles, _ = decompress(raw)
                across = extract_art.guess_width(key, len(tiles))
            else:
                raw = extract_inline_art.read_block(self.rom, key)
                tiles, _ = decompress(raw)
                across = ps2png.SHEET_TILES
            across = max(1, min(across, len(tiles) or 1))
            rows = (len(tiles) + across - 1) // across
            px = [ps2png.tile_indices(t) for t in tiles]
            px += [[0] * 64] * (across * rows - len(px))
            self._sheets[name] = (px, across, rows)
        return self._sheets[name]

    def tiles(self, name):
        """The sheet's tiles as 8 rows of 8 indices each."""
        return [[t[y * 8:y * 8 + 8] for y in range(8)]
                for t in self.sheet(name)[0]]

    def geometry(self, name):
        """(tile count, columns, image width, image height) of the sheet's
        PNG, padding included."""
        tiles, across, rows = self.sheet(name)
        return len(tiles), across, across * 8, rows * 8


def png_sheet(path):
    """(tiles, across, rows) of a sheet PNG, each tile its 64 indices."""
    from PIL import Image
    with Image.open(path) as im:
        w, h = im.size
        px = im.load()
        tiles = [[px[tx * 8 + i % 8, ty * 8 + i // 8] for i in range(64)]
                 for ty in range(h // 8) for tx in range(w // 8)]
    return tiles, w // 8, h // 8


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("rom", type=Path, help="the cartridge image")
    ap.add_argument("--tiles", type=Path, default=Path("assets/tiles"))
    args = ap.parse_args()

    da = DisasmArt(args.rom)
    same, differ, absent = 0, [], []
    for name in da.names():
        path = args.tiles / f"{name}.png"
        if not path.exists():
            absent.append(name)
            continue
        if png_sheet(path) == da.sheet(name):
            same += 1
        else:
            differ.append(name)
    print(f"{same} sheets the same as the cartridge image gives them, "
          f"{len(differ)} not")
    for name in differ:
        print(f"  DIFFERS {name}")
    stray = sorted(set(absent) - asset_names.PICTURE_ART)
    print(f"{len(absent) - len(stray)} not under {args.tiles}: the picture "
          f"art (asset_names.PICTURE_ART)")
    for name in stray:
        print(f"  MISSING {name} (not picture art)")
    return 1 if differ or stray else 0


if __name__ == "__main__":
    sys.exit(main())
