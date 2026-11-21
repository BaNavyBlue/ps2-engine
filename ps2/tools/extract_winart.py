#!/usr/bin/env python3
"""Extract the WinArt_* tilemaps from the cartridge image.

Every window and portrait in the game is a rectangle of tile indices: a
top border row, the rows inside, a bottom border row. Portraits are 10
wide with 10 interior rows; menus are narrower and hold text rather than
art. A window's text is stored as the very tile indices the VDP will draw
(the font's, FontsIconsArt), so a map is bytes, one a cell.

(build 185) From the cartridge image (tools/rommap.py), not ps2.asm's
border, cursorbox and dc.b lines and their charset: a map is its label's
bytes, and its size is the one the game draws it at, from the window
records (WindowArtLayoutPtrs) that point at it -- see window_sizes.

(build 134) Written as Tiled maps, assets/winart/<name>.tmx (see
write_winart_tmx); before that, "PS2M" containers (.map). Reads the tile
sheets and the portrait tables, so it runs after `art` and `buildings`.

    python3 tools/extract_winart.py ROM -o assets/winart.json
"""

import argparse
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from disasm_art import DisasmArt  # noqa: E402
from rommap import RomMap  # noqa: E402


class WinArtError(Exception):
    pass


# (build 185) A window record, WindowArtLayoutPtrs' (ps2.asm:28821), 8
# bytes: its Y and X, a long pointer to its map, then the map's width plus
# one and its height less one (ProcessWindows, ps2.asm:13502, reads window
# n's at WindowArtLayoutPtrs + (n - 1) * 8). Each record has a PtrWin_
# label; three records have two.
RECORD_MAP = 2
RECORD_WIDTH = 6
RECORD_HEIGHT = 7


def window_sizes(rom):
    """{map label: {(width, height), ...}}: the sizes the window records
    draw each map at. A pointer into RAM is into window_art_buffer, which
    LoadDynWindowsInRam (ps2.asm:26523) fills with the ROM from
    DynamicWindowsStart on, so it names the map at the same offset from
    there."""
    ram = rom.const("window_art_buffer") & 0xFFFFFF
    dynamic = rom.addr("DynamicWindowsStart")
    records = {at for _, at in rom.matching(r"^PtrWin_")}
    sizes = {}
    for at in sorted(records):
        ptr = rom.u32(at + RECORD_MAP) & 0xFFFFFF
        if ptr >= ram:
            ptr = ptr - ram + dynamic
        size = (rom.u8(at + RECORD_WIDTH) - 1, rom.u8(at + RECORD_HEIGHT) + 1)
        for name in rom.names_at(ptr):
            if name.startswith("WinArt_"):
                sizes.setdefault(name, set()).add(size)
    return sizes


def map_bytes(start, block, width, height):
    """The map's bytes out of its label's block, or None when the size does
    not fit it: the block is the map, or the map and the one pad byte
    `even` puts after a map that ends at an odd address."""
    n = width * height
    if len(block) == n or (len(block) == n + 1 and (start + n) % 2):
        return block[:n]
    return None


def collect(rom):
    """{map label: {"width", "height", "rows"}} for every WinArt_ label of
    the cartridge image (a RomMap), and the ones that failed. A map's size
    is the one of its records' sizes that fits its block (a map two
    windows draw at different sizes has one that fits: the battle's
    empty-spots box, which two tower portrait records draw 10x12)."""
    sizes = window_sizes(rom)
    maps, failures = {}, []
    for name, start in rom.matching(r"^WinArt_"):
        block = rom.blob(name)
        fits = [(w, h, data) for w, h in sorted(sizes.get(name, ()))
                if (data := map_bytes(start, block, w, h)) is not None]
        if len(fits) != 1:
            failures.append((name, f"{len(fits)} window record sizes of "
                             f"{sorted(sizes.get(name, ()))} fit its "
                             f"{len(block)} bytes"))
            continue
        width, height, data = fits[0]
        rows = [list(data[i:i + width]) for i in range(0, len(data), width)]
        maps[name] = {"width": width, "height": height, "rows": rows}
    return maps, failures


# ---------------------------------------------------------------------------
# (build 134) The Tiled form. A cell is the map's byte plus the gid of the
# tileset it is drawn from; each tileset's `byte_base` property turns a tile
# of that sheet back into the byte (byte = gid - firstgid + byte_base), so
# the map survives Tiled renumbering its tilesets on save. The sheets are
# the tile PNGs under assets/tiles/ themselves, 16 tiles across in tile
# order, as Tiled lays a tileset out.

# loc_980A: a window cell is $F72C ($8500) plus the map byte, so the byte is
# a tile of FontsIconsArt, which sits at $500.
FONT_SHEET = "FontsIconsArt"

# Portrait slots PortraitTable pairs with a window map not their own (build
# 127, art_export.MISMATCHED): not used to pick a map's art.
MISMATCHED_SLOTS = {25, 28, 29}

# Win_RolfPortrait's base is $A400 with RolfPortraitArt at $4A0, so its map
# bytes run from $A0; the library's graph is drawn the same way (art_export,
# portraits()). Every other portrait's art sits at $400: its bytes from 0.
ART_AT_4A0_SLOTS = {0, 26}


def portrait_art(assets):
    """{map name: (art sheet, byte_base)} for the maps named ...Portrait:
    the art of the first portrait slot that uses the map and is its own."""
    wins = json.loads((assets / "data" / "portrait_windows.json").read_text())
    ports = {p["index"]: p for p in
             json.loads((assets / "data" / "portraits.json").read_text())}
    out = {}
    for w in wins:
        name, slot = w["art"], w["portrait"]
        if (not name.endswith("Portrait") or name in out
                or slot in MISMATCHED_SLOTS):
            continue
        art = ports.get(slot, {}).get("art") or "rolf_portrait"
        out[name] = (art, 0xA0 if slot in ART_AT_4A0_SLOTS else 0)
    return out


def sheet(da, art, byte_base):
    """A tileset: (name, image path from assets/winart, tile count, columns,
    image width, image height, byte_base). (build 156) The size is the
    sheet's as the cartridge gives it (disasm_art.py): a portrait's sheet
    is not kept in the tree any more, and its tileset names an image Tiled
    will not find (the portrait is its picture, assets/art/portraits)."""
    count, cols, w, h = da.geometry(art)
    return (art, f"../tiles/{art}.png", count, cols, w, h, byte_base)


def write_winart_tmx(path, width, height, data, tilesets, row_sets):
    """data: the ROM's bytes, row by row. row_sets[r]: the index into
    tilesets that row r is drawn from. Every byte must fall in its row's
    sheet."""
    first = []
    gid = 1
    for ts in tilesets:
        first.append(gid)
        gid += ts[2]
    rows = []
    for r in range(height):
        k = row_sets[r]
        _, _, count, _, _, _, base = tilesets[k]
        cells = []
        for b in data[r * width:(r + 1) * width]:
            if not base <= b < base + count:
                raise WinArtError(f"{path.name}: byte ${b:02X} in row {r} is "
                                  f"outside {tilesets[k][0]} "
                                  f"(${base:02X}-${base + count - 1:02X})")
            cells.append(str(first[k] + b - base))
        rows.append(",".join(cells))
    out = ['<?xml version="1.0" encoding="UTF-8"?>',
           f'<map version="1.10" orientation="orthogonal" '
           f'renderorder="right-down" width="{width}" height="{height}" '
           f'tilewidth="8" tileheight="8" infinite="0" nextlayerid="2" '
           f'nextobjectid="1">']
    for ts, fg in zip(tilesets, first):
        name, src, count, cols, iw, ih, base = ts
        out += [f' <tileset firstgid="{fg}" name="{name}" tilewidth="8" '
                f'tileheight="8" tilecount="{count}" columns="{cols}">',
                '  <properties>',
                f'   <property name="byte_base" type="int" value="{base}"/>',
                '  </properties>',
                f'  <image source="{src}" width="{iw}" height="{ih}"/>',
                ' </tileset>']
    out += [f' <layer id="1" name="window" width="{width}" '
            f'height="{height}">',
            '  <data encoding="csv">',
            ",\n".join(rows),
            '</data>',
            ' </layer>',
            '</map>', '']
    path.write_text("\n".join(out), newline="\n")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("rom", type=Path, help="the cartridge image")
    ap.add_argument("-o", "--out", type=Path,
                    default=Path("assets/winart.json"))
    args = ap.parse_args()
    rom = RomMap(args.rom)
    da = DisasmArt(rom)

    maps, failures = collect(rom)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(maps, indent=1, sort_keys=True), newline="\n")

    # (build 134) Tiled maps for the runtime and for editing: one .tmx a
    # window, beside the tile sheets it is drawn from.
    bin_dir = args.out.parent / "winart"
    bin_dir.mkdir(parents=True, exist_ok=True)
    for stale in bin_dir.glob("*.map"):
        stale.unlink()
    portraits = portrait_art(args.out.parent)
    for name, m in maps.items():
        tilesets = [sheet(da, FONT_SHEET, 0)]
        row_sets = [0] * m["height"]
        if name in portraits:
            art, base = portraits[name]
            tilesets.append(sheet(da, art, base))
            row_sets = [0] + [1] * (m["height"] - 2) + [0]
        data = bytes(b for row in m["rows"] for b in row)
        write_winart_tmx(bin_dir / f"{name}.tmx", m["width"], m["height"],
                         data, tilesets, row_sets)

    print(f"extracted {len(maps)} window maps -> {args.out}")
    sizes = {}
    for name, m in maps.items():
        sizes.setdefault((m["width"], m["height"]), []).append(name)
    for (w, h), names in sorted(sizes.items()):
        print(f"  {w:2}x{h:<3} {len(names):3} maps")
    for name, why in failures:
        print(f"  FAILED {name}: {why}", file=sys.stderr)


if __name__ == "__main__":
    main()
