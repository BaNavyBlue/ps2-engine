#!/usr/bin/env python3
"""Extract VDP nametables (the data DrawArtTiles feeds to the data port).

An entry is a 16-bit VDP nametable word:  P CC V H TTTTTTTTTTT
    priority(1) palette(2) vflip(1) hflip(1) tile(11)
The tile number is an absolute VRAM tile index, so the runtime mirrors
VRAM tile space and places each art blob at the index its load address
implies.

Sources are not uniform. SegaLogoTileInd is inline dc.w. TitleScrBGTileInd
is a 160-byte binclude immediately followed by 2240 bytes of inline dc.b
under a loc_ label -- contiguous in ROM, split only by the disassembler --
and DrawArtTiles reads straight through both.

(build 135) Written as Tiled maps, assets/nametables/<name>.tmx (see
write_nametable_tmx); before that, "PS2N" containers (.nam), which this
deletes. A tileset is one of the art sheets under assets/tiles/ itself --
the sheet the table's tiles come from, as video memory holds it when the
table is shown (SHEETS) -- so this runs after `make extract-art` and
`extract-inline_art`, which write them. (build 156) A tileset's size is
the sheet's as the cartridge gives it (disasm_art.py), not read from its
PNG: the sheets of the art the pictures replaced are not kept, and a
tileset of one names an image the tree does not hold (Tiled shows those
cells empty; the game reads only the counts and properties).

(build 185) The tables come out of the cartridge image (tools/rommap.py),
not ps2.asm's dc lines and the art/*.bin its bincludes name: a label's
bytes run to the next label, binclude'd or inline alike, as DrawArtTiles
sees them.

    python3 tools/extract_nametables.py ROM -o assets/nametables
"""

import argparse
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from disasm_art import DisasmArt  # noqa: E402
from rommap import RomMap  # noqa: E402

# (build 156) The cartridge's art, for the sheets' sizes: main() sets it
# (write_nametable_tmx keeps the arguments tools/roundtrip.py wraps).
ART = None

# label -> (width, height, extra labels whose bytes continue the run)
NAMETABLES = {
    "SegaLogoTileInd":       (12, 4, []),
    "TitleScrBGTileInd":     (40, 28, ["loc_6C5DA"]),
    # (build 153) GameOverScreen (ps2.asm:12255) draws its plane B from
    # loc_6C5DA, 160 bytes into the title's: the title's background two
    # rows up, and below it loc_6C5DA's last two rows, which the title
    # never draws.
    "loc_6C5DA":             (40, 28, []),
    "TitleScrPlaneATileInd": (40, 28, []),
    # IntroScr_EventStartGame draws both 28x14 at $410C0003.
    "loc_70A32":             (28, 14, []),
    "loc_71052":             (28, 14, []),
    # BattleScreen draws 40x16 at $66000003 -> plane B, row 12.
    "loc_75BD8":             (40, 16, []),
    # loc_7E02 cases 1 and 2 (ps2.asm:11498, 11521), after the Neifirst
    # battle: 28x14 at $410C0003 -- plane A, row 2, col 6, the same place
    # and size as the dream pictures -- and 6x4 at $44A20003, row 9, col
    # 17: Nei's eyes closed, drawn over it. Both index loc_AED00's art,
    # decompressed to tile 1. (build 126) The calls give d1 = $1B, d2 = $D
    # and 5, 3, and DrawArtTiles counts both with dbf, so a count is the
    # size less one: this table had the counts, 27x13 and 5x3, and drew
    # both sheared and wrapped. The data agree: loc_AF4EA to loc_AF7FA is
    # $310 bytes, 28x14 words, and loc_AF7FA to loc_AF82A $30, 6x4.
    "loc_AF4EA":             (28, 14, []),
    "loc_AF7FA":             (6, 4, []),
    # Building_TylerSpaceship (ps2.asm:18715). loc_CCE6 draws the second
    # picture 28x14 at $410C0003 (the first is loc_70A32 again). loc_CD50
    # draws Tyler 13x12 at $418C0003 (row 3, col 6) and loc_CDDE the
    # monitor 15x12 at $41A60003 (row 3, col 19), frame d0 at d0 * $168:
    # five frames of twelve rows back to back, kept here as one 15x60
    # table. The last two are bincludes of art/*.bin.
    "loc_70D42":                  (28, 14, []),
    "TylerPortraitTileInd":       (13, 12, []),
    "TylerSpcshp_MonitorTileInd": (15, 60, []),
    # Building_Roof's flight (loc_C58A, ps2.asm:18105): the planet 6x6 at
    # $63AC0003 -- plane B, row 7, col 22 -- over loc_71362 at tile 1.
    "loc_716BA":                  (6, 6, []),
    # BattleScreen against Mother Brain (ps2.asm:12092): her machine,
    # 40x14 at $60000003 -- plane B, row 0 -- after the floor, so it
    # covers the floor's first two rows. Its tiles are her own art (tile
    # 1 on), palette line 3.
    "loc_AE8A0":                  (40, 14, []),
    # loc_8364 (ps2.asm:11915): Lutz, 30x14, a binclude
    # (art/lutz_port_tile_ind.bin).
    "LutzPortraitTileInd":        (30, 14, []),
    # EndingScreen (ps2.asm:10795), build 93a. The starfield, 40x28 at
    # $60000003 -- plane B, row 0 -- for the whole screen.
    "loc_B7572":                  (40, 28, []),
    # The six epilogue portraits, loc_79F2's entries 0-5: 16x24 each, at
    # plane A row 2, column 4 ($4108) or 20 ($4128). Stored in ROM in the
    # order pages 1, 2, 5, 6, 3, 4; named here by page.
    "loc_B5A2C":                  (16, 24, []),
    "loc_B5D2C":                  (16, 24, []),
    "loc_B662C":                  (16, 24, []),
    "loc_B692C":                  (16, 24, []),
    "loc_B602C":                  (16, 24, []),
    "loc_B632C":                  (16, 24, []),
    # The last picture, 40x14. loc_79F2's entries 6-12 are seven windows
    # into this one table, a row ($50 bytes) apart -- the strip wipe opens
    # it from its middle two rows outward -- so the seven labels are one
    # run, read straight through.
    "loc_B55CC":                  (40, 14, ["loc_B561C", "loc_B566C",
                                            "loc_B56BC", "loc_B570C",
                                            "loc_B575C", "loc_B57AC"]),
}


# (build 135) Where each table's tiles come from: the art sheets in video
# memory when the game shows it, (sheet under assets/tiles/, first tile),
# in the order they are loaded -- where two overlap, the later is what
# video memory holds. The places are the game's (game.c, ending.c) and the
# art dump's (art_export.SCENES). The title's figures dissolve stops at 287
# tiles (FadeArtTiles with $11E, counted by dbf, ps2.asm:10733), so tile
# $200 is the logo's first; the figures sheet's 288th tile is the sheet's
# padding, and the logo, loaded after, wins it.
TITLE_SHEETS = [("title_background_art", 0x001), ("title_copyright_art", 0x300),
                ("FontsIconsArt", 0x500), ("title_figures_art", 0x0E1),
                ("title_logo_art", 0x200)]
SHEETS = {
    "SegaLogoTileInd":             [("SegaLogoArt", 0x000)],
    "TitleScrBGTileInd":           TITLE_SHEETS,
    "TitleScrPlaneATileInd":       TITLE_SHEETS,
    "GameOverBGTileInd":           [("title_background_art", 0x001)],
    "DreamBattleTileInd":          [("DreamBattleArt", 1)],
    "DreamWakingTileInd":          [("DreamWakingArt", 1)],
    "DreamHelpTileInd":            [("DreamHelpArt", 1)],
    "NeiDeathTileInd":             [("NeiDeathArt", 1)],
    "NeiBodyTileInd":              [("NeiDeathArt", 1)],
    "TylerPortraitTileInd":        [("tyler_spaceship_art", 1)],
    "TylerSpcshp_MonitorTileInd":  [("tyler_spaceship_art", 1)],
    "SpaceshipPlanetTileInd":      [("SpaceshipPlanetArt", 1)],
    "BattleFloorTileInd":          [("FontsIconsArt", 0x500),
                                    ("battle_floor_art", 0x781)],
    "MotherBrainMachineTileInd":   [("battle_mombrain_art", 1)],
    "LutzPortraitTileInd":         [("LutzPortraitArt", 1)],
    "EndingStarfieldTileInd":      [("EndingStarfieldUfoArt", 0x480)],
    "EndingLastPictureTileInd":    [("EndingPicturesArt", 1)],
}
for _i in range(1, 7):
    SHEETS[f"EndingPage{_i}PortraitTileInd"] = [("EndingPicturesArt", 1)]

# Tiled's flip flags on a gid.
FLIP_H = 0x80000000
FLIP_V = 0x40000000


class NametableError(Exception):
    pass


def sheet_size(assets, art):
    """(tile count, columns, image width, image height) of a tile sheet.
    (build 156) From the cartridge's art (ART), padding included."""
    return ART.geometry(art)


def write_nametable_tmx(path, width, height, words, sheets, assets):
    """words: the ROM's entries, row by row. sheets: SHEETS[name]. Each
    entry becomes a cell of the tileset for its sheet, palette line and
    priority (one per combination the table uses), flipped by Tiled's flags
    for the entry's H and V; a tile in no sheet must be the empty word,
    tile 0 in one palette line with nothing else set, the same for every
    such cell -- the map's empty_palette_line. Refuses anything else."""
    size = {art: sheet_size(assets, art) for art, _ in sheets}
    empty = None
    resolved = []
    for k, w in enumerate(words):
        tile = w & 0x7FF
        art = None
        for a, base in sheets:              # the last loaded wins
            if base <= tile < base + size[a][0]:
                art, local = a, tile - base
        if art is None:
            if w & ~0x6000:
                raise NametableError(
                    f"{path.name}: cell {k % width},{k // width} is "
                    f"${w:04X}, tile ${tile:03X} in no sheet and not an "
                    f"empty cell (tile 0, a palette line, nothing else)")
            if empty is not None and empty != w:
                raise NametableError(
                    f"{path.name}: cell {k % width},{k // width} is "
                    f"${w:04X}, another empty cell ${empty:04X}")
            empty = w
            resolved.append(None)
            continue
        resolved.append((art, (w >> 13) & 3, w >> 15, local,
                         (w >> 11) & 1, (w >> 12) & 1))

    # One tileset a sheet, palette line and priority the table uses, in
    # SHEETS' order.
    combos = []
    for a, _ in sheets:
        used = sorted({(r[1], r[2]) for r in resolved
                       if r is not None and r[0] == a})
        combos += [(a, line, prio) for line, prio in used]
    base_of = dict((a, b) for a, b in sheets)
    first, gid = {}, 1
    for c in combos:
        first[c] = gid
        gid += size[c[0]][0]

    cells = []
    for r in resolved:
        if r is None:
            cells.append("0")
            continue
        art, line, prio, local, hf, vf = r
        g = first[(art, line, prio)] + local
        g |= FLIP_H if hf else 0
        g |= FLIP_V if vf else 0
        cells.append(str(g))

    out = ['<?xml version="1.0" encoding="UTF-8"?>',
           f'<map version="1.10" orientation="orthogonal" '
           f'renderorder="right-down" width="{width}" height="{height}" '
           f'tilewidth="8" tileheight="8" infinite="0" nextlayerid="2" '
           f'nextobjectid="1">',
           ' <properties>',
           f'  <property name="empty_palette_line" type="int" '
           f'value="{(empty or 0) >> 13}"/>',
           ' </properties>']
    for c in combos:
        art, line, prio = c
        count, cols, iw, ih = size[art]
        multi = sum(1 for d in combos if d[0] == art) > 1
        name = art + (f"_line{line}" + ("_priority" if prio else "")
                      if multi else "")
        out += [f' <tileset firstgid="{first[c]}" name="{name}" '
                f'tilewidth="8" tileheight="8" tilecount="{count}" '
                f'columns="{cols}">',
                '  <properties>',
                f'   <property name="palette_line" type="int" '
                f'value="{line}"/>',
                f'   <property name="priority" type="int" value="{prio}"/>',
                f'   <property name="vram_base" type="int" '
                f'value="{base_of[art]}"/>',
                '  </properties>',
                f'  <image source="../tiles/{art}.png" width="{iw}" '
                f'height="{ih}"/>',
                ' </tileset>']
    rows = [",".join(cells[r * width:(r + 1) * width]) for r in range(height)]
    out += [f' <layer id="1" name="nametable" width="{width}" '
            f'height="{height}">',
            '  <data encoding="csv">',
            ",\n".join(rows),
            '</data>',
            ' </layer>',
            '</map>', '']
    path.write_text("\n".join(out), newline="\n")


def label_bytes(rom, label):
    """(build 185) A label's bytes in the cartridge image (a RomMap), up
    to the next label: a binclude's file and inline words alike."""
    return rom.blob(label)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("rom", type=Path, help="the cartridge image")
    ap.add_argument("-o", "--outdir", type=Path,
                    default=Path("assets/nametables"))
    args = ap.parse_args()

    global ART
    rom = RomMap(args.rom)
    ART = DisasmArt(rom)
    args.outdir.mkdir(parents=True, exist_ok=True)
    assets = args.outdir.parent
    # (build 135) the .nam containers are Tiled maps now
    for stale in args.outdir.glob("*.nam"):
        stale.unlink()

    # Merged into what is there: a table the image is too short for is
    # skipped and keeps its old .tmx and entry, rather than stopping the
    # run (build 82, when the art/ a session had might lack a file).
    manifest_path = args.outdir / "nametables.json"
    manifest = (json.loads(manifest_path.read_text())
                if manifest_path.exists() else {})
    output_names = {"loc_6C5DA": "GameOverBGTileInd",
                    "loc_70A32": "DreamBattleTileInd",
                    "loc_71052": "DreamWakingTileInd",
                    "loc_75BD8": "BattleFloorTileInd",
                    "loc_AF4EA": "NeiDeathTileInd",
                    "loc_AF7FA": "NeiBodyTileInd",
                    "loc_70D42": "DreamHelpTileInd",
                    "loc_716BA": "SpaceshipPlanetTileInd",
                    "loc_AE8A0": "MotherBrainMachineTileInd",
                    "loc_B7572": "EndingStarfieldTileInd",
                    "loc_B5A2C": "EndingPage1PortraitTileInd",
                    "loc_B5D2C": "EndingPage2PortraitTileInd",
                    "loc_B662C": "EndingPage3PortraitTileInd",
                    "loc_B692C": "EndingPage4PortraitTileInd",
                    "loc_B602C": "EndingPage5PortraitTileInd",
                    "loc_B632C": "EndingPage6PortraitTileInd",
                    "loc_B55CC": "EndingLastPictureTileInd"}
    for label, (w, h, extra) in NAMETABLES.items():
        data = label_bytes(rom, label)
        for cont in extra:
            data += label_bytes(rom, cont)

        need = w * h * 2
        if len(data) < need:
            print(f"  {label}: only {len(data)} of {need} bytes",
                  file=sys.stderr)
            continue
        data = data[:need]

        name = output_names.get(label, label)
        out = args.outdir / f"{name}.tmx"
        words = [data[i] << 8 | data[i + 1] for i in range(0, need, 2)]
        write_nametable_tmx(out, w, h, words, SHEETS[name], assets)

        manifest[name] = {"width": w, "height": h}
        print(f"{label}: {w}x{h}, {need} bytes -> {out}")

    manifest_path.write_text(
        json.dumps(manifest, indent=1, sort_keys=True), newline="\n")


if __name__ == "__main__":
    main()
