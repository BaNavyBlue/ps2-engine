#!/usr/bin/env python3
"""Extract the level maps: tilesets, block definitions and block maps.

`LoadLevelData` (ps2.asm:13229) indexes `LevelData` (46217) by
`level_index`, 20 bytes per level:

    +0   u8   planet in bits 7+, tileset in bits 0-3
    +1   u8   high nibble * $100 = height in pixels
              low  nibble * $100 = width  in pixels
    +2   u32  flags<<24 | pointer to the plane B block map  -> $FF9000
    +6   u32  flags<<24 | pointer to the plane A block map  -> $FFA800
    +10  u32  flags<<24 | pointer to the per-block collision table
    +14  u32  palette<<24 | pointer to the interaction list
    +18  u8   index into the per-level extra-art table at loc_283EE
    +19  u8   music id

The tileset index picks a pair of pointers out of `loc_9342`: tile
graphics, Nemesis-compressed, and block definitions in the DecompressArt2
format. Seven tilesets serve all 101 levels.

Three nested things, which is what makes a level small:

    block map     one byte per 32x32 block, row-major,
                  width/32 by height/32
    block         32 bytes = 4x4 VDP nametable entries
    nametable     priority(1) palette(2) vflip(1) hflip(1) tile(11),
                  the tile field indexing the level's own tileset from 0

Plane A is the one the party walks on and the one `Sprites_ChkCanMove`
reads for collision, unless bits 4-6 of the +10 flag byte are set, in
which case plane B is. Both planes share the tileset, the block
definitions and the map dimensions.

Output, all of it either a picture or text:

    assets/tiles/tileset_<name>.png   the 8x8 tile graphics, indexed PNG (names:
                                      tools/asset_names.py, build 97b)
    assets/levels/blocks_<name>.txt   the block definitions, one block per
                                      line, 16 hex nametable entries
    assets/levels/blocks_<name>.png   the same blocks rendered 32x32, which
                                      is the tileset Tiled draws with
    assets/levels/<id>_<name>.tmx     the block maps, as a Tiled map
    assets/levels/<id>_<name>_A.tmx   a backdrop plane, at its own 16x8
    assets/levels/<id>_<name>.json    dimensions, tileset, palette, music
    assets/levels/levels.json         everything above, described
    assets/data/level_anims.json      the two tile animations (loc_118F4)
    assets/data/map_patches.json      the block-map writes LoadLevelData
                                      makes after decompressing, by flag
                                      or item (loc_9258)

A backdrop plane gets its own file because Tiled maps have one size for
all their layers and a backdrop is 16x8 whatever the level is. Expanding
it to the level's size would change where it wraps.

(build 185) From the cartridge image (tools/rommap.py), not ps2.asm: the
records, the tileset table and the tile animations' frame order are their
own bytes, each pointer is named by the label at its address, the music by
the MusicID_ constant of its value, and a compressed stream is read from
its label to the end of the ROM, the decompressor finding where it stops.
"""

import argparse
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
import nemesis                                              # noqa: E402
import ps2art                                               # noqa: E402
import asset_names                                          # noqa: E402
import ps2png                                               # noqa: E402
from extract_paletteptrs import label_at, parse_table       # noqa: E402
from rommap import RomMap                                   # noqa: E402

BLOCK_PX = 32
SHEET_BLOCKS = 16          # blocks across the rendered block sheet

# A plane flagged as a backdrop is drawn from a fixed 16 x 8 block map
# that wraps, whatever the level's own dimensions are (loc_8F70).
BACKDROP_COLS, BACKDROP_ROWS = 16, 8

TILESET_TABLE = "loc_9342"

# A LevelData record's four longs, by offset: plane B's map, plane A's,
# the collision table, the interaction list (see above).
RECORD_LONGS = (2, 6, 10, 14)


def stream(rom, label):
    """Every byte from a label to the end of the ROM.

    A compressed stream is one run of bytes, but the disassembler puts a
    label wherever something points into the middle of it -- several
    dungeon maps are pointed at from two levels at different offsets -- so
    stopping at the next label would truncate it. The decompressors report
    how much they consumed, so reading on costs nothing.
    """
    at = rom.addr(label)
    return rom.read(at, len(rom.rom) - at)


def pointer(rom, value, where):
    """A long's flag byte and the label its low 24 bits point at."""
    name = label_at(rom, value & 0xFFFFFF)
    if name is None:
        raise SystemExit(f"{where}: ${value:08X} points where no label is")
    return value >> 24, name


def music_names(rom):
    """Each MusicID_ constant's name, by its value."""
    names = {}
    for name in rom.consts:
        if name.startswith("MusicID_"):
            names.setdefault(rom.const(name), name)
    return names


def tileset_pointers(rom):
    """loc_9342: seven pairs of (graphics, block definitions)."""
    raw = rom.blob(TILESET_TABLE)
    syms = [pointer(rom, int.from_bytes(raw[i:i + 4], "big"),
                    TILESET_TABLE)[1]
            for i in range(0, len(raw) - 3, 4)]
    return list(zip(syms[0::2], syms[1::2]))


def parse_level(rom, label, music_ids):
    """A LevelData record, from its 20 bytes at the label; `music_ids`
    is music_names()."""
    at = rom.addr(label)
    kind, size = rom.u8(at), rom.u8(at + 1)
    longs = [pointer(rom, rom.u32(at + off), f"{label}+{off}")
             for off in RECORD_LONGS]

    # As ps2.asm writes it: by its MusicID_ name, or in hex where no
    # constant has the value.
    music_id = rom.u8(at + 19)
    music = music_ids.get(music_id, f"${music_id:02X}")

    return {
        "label": label,
        "name": label.removeprefix("Level_"),
        "planet": kind >> 7,
        "tileset": kind & 0x0F,
        "width_px": (size & 0x0F) * 0x100,
        "height_px": (size >> 4) * 0x100,
        "map_b": longs[0][1],
        "map_a": longs[1][1],
        "collision": longs[2][1],
        "collision_flags": longs[2][0],
        # The flag bytes on the two map pointers are the level's enemy
        # groups: ProcessRandomBattle (26535) indexes EnemyFormationTable
        # with enemy_data_buffer+6 (plane B's) or, after Climatrol
        # ($C737), +8 (plane A's). Zero means no encounters.
        "enemy_group": longs[0][0],
        "enemy_group_late": longs[1][0],
        "interactions": longs[3][1],
        "palette": longs[3][0],
        "extra_art": rom.u8(at + 18),
        "music": music,
        "music_id": music_id,
    }


def write_blocks_text(path, defs, tileset):
    """The block definitions as text, one block per line.

    These are not pixels, so they do not want to be an image: a block is
    4x4 VDP nametable entries and the fields that matter -- priority,
    palette line, the two flips -- are not things an image can carry. Hex
    words in a text file are editable in anything and parse with sscanf.
    """
    count = len(defs) // 32
    with path.open("w", newline="\n") as fh:
        fh.write(f"# tileset {tileset}: {count} blocks of 4x4 nametable"
                 " entries\n")
        fh.write("# entry: P CC V H TTTTTTTTTTT"
                 "  (priority, palette line, vflip, hflip, tile)\n")
        fh.write("# one block per line, 16 words, row-major\n")
        for b in range(count):
            words = [int.from_bytes(defs[b * 32 + i * 2:b * 32 + i * 2 + 2],
                                    "big") for i in range(16)]
            fh.write(" ".join(f"{w:04X}" for w in words) + "\n")


def render_blocks(path, defs, tiles, palette_words):
    """The block definitions drawn at 32x32, for Tiled to lay out with.

    Flips and palette lines are resolved here, so the indices in the
    image span all four lines: index = line * 16 + entry, with 0 kept
    transparent whatever line it came from, the way the hardware treats
    entry 0 of every line as the same backdrop.
    """
    count = len(defs) // 32
    across = min(SHEET_BLOCKS, count) or 1
    down = (count + across - 1) // across

    px = [0] * (across * BLOCK_PX * down * BLOCK_PX)
    sheet_w = across * BLOCK_PX

    for b in range(count):
        bx = (b % across) * BLOCK_PX
        by = (b // across) * BLOCK_PX
        for r in range(4):
            for c in range(4):
                o = b * 32 + (r * 4 + c) * 2
                w = int.from_bytes(defs[o:o + 2], "big")
                tile = tiles[w & 0x7FF] if (w & 0x7FF) < len(tiles) else \
                    bytes(32)
                hf, vf = (w >> 11) & 1, (w >> 12) & 1
                line = (w >> 13) & 3
                flat = ps2png.tile_indices(tile)
                for y in range(8):
                    sy = 7 - y if vf else y
                    for x in range(8):
                        sx = 7 - x if hf else x
                        v = flat[sy * 8 + sx]
                        px[(by + r * 8 + y) * sheet_w + bx + c * 8 + x] = \
                            (line * 16 + v) if v else 0

    rgb = [ps2png.cram_to_rgb(w) for w in palette_words]
    rgb += [(0, 0, 0)] * (64 - len(rgb))
    ps2png.write_flat(path, sheet_w, down * BLOCK_PX, px, rgb)
    return across, down


TMX = """<?xml version="1.0" encoding="UTF-8"?>
<map version="1.10" orientation="orthogonal" renderorder="right-down"\
 width="{w}" height="{h}" tilewidth="32" tileheight="32" infinite="0">
 <properties>
{props} </properties>
 <tileset firstgid="1" name="blocks_{ts}" tilewidth="32" tileheight="32"\
 tilecount="{count}" columns="{across}">
  <image source="blocks_{ts}.png" width="{iw}" height="{ih}"/>
 </tileset>
{layers}</map>
"""

LAYER = """ <layer id="{id}" name="{name}" width="{w}" height="{h}">
  <data encoding="csv">
{csv}
  </data>
 </layer>
"""


def write_tmx(path, tileset, count, across, down, layers, props=None):
    """A Tiled map. Layer data is CSV and gids are block id + 1.

    The level's own fields -- which tileset, which palette, the music id,
    the flag byte -- ride along as map properties, so a level is one file
    and all of it is editable in Tiled's own dialogs.
    """
    prop_xml = ""
    for k, v in sorted((props or {}).items()):
        prop_xml += (f'  <property name="{k}" type="int"'
                     f' value="{v}"/>\n')

    body = ""
    for n, (name, w, h, data) in enumerate(layers, start=1):
        rows = []
        for y in range(h):
            rows.append(",".join(str(data[y * w + x] + 1) for x in range(w)))
        body += LAYER.format(id=n, name=name, w=w, h=h,
                             csv=",\n".join(rows))

    path.write_text(TMX.format(
        w=layers[0][1], h=layers[0][2],
        ts=asset_names.TILESET_NAMES[tileset], count=count,
        across=across, iw=across * BLOCK_PX, ih=down * BLOCK_PX,
        layers=body, props=prop_xml), newline="\n")


# loc_118F4 (VBlankSub_10, the level screen's VBlank), which is code, not a
# table, so its handful of constants are written out here. Every 32 fields
# ($F63E counts $1F down, first on the first field after LevelScreen zeroes
# it) it copies one frame of raw, uncompressed 4bpp art over a run of VRAM
# tiles, the frame chosen by $F63C:
#   level $00 (Motavia)       loc_760D8, 8 tiles a frame, $F63C & 1, to
#                             VRAM $0020 (tile 1): the sea
#   level $13 (UnderwaterPassage)  loc_762D8, 16 tiles a frame, $F63C & 3
#                             looked up in the byte table loc_11978, to
#                             VRAM $21C0 (tile $10E)
# Either way the step (the counter, or the table's byte) is shifted left 8
# into a byte offset into the art; "order" names the table where there is
# one, and its bytes are read from the ROM.
TILE_ANIMS = [
    {"level": 0x00, "art": "loc_760D8", "tiles": 8,  "vram": 0x0020,
     "steps": 2},
    {"level": 0x13, "art": "loc_762D8", "tiles": 16, "vram": 0x21C0,
     "steps": 4, "order": "loc_11978"},
]
TILE_ANIM_PERIOD = 0x20
TILE_ANIM_SHIFT = 8


# loc_9258-loc_9300, the tail of LoadLevelData (ps2.asm:13389): block
# maps written in RAM after they are decompressed, by flag. Code, not a
# table, apart from the four run lists loc_9302 reads, so the constants
# are written out here in the routine's order:
#   level $00, $C735 set: every $8B in plane B ($FF9000, $1800 bytes)
#       becomes $89, then $89 at four fixed bytes. Only under $C735 do
#       the next four run -- loc_92CA is its rts -- each a loc_9302 list
#       into plane B, rows $60 blocks apart.
#   level $03, a party member holding ItemID_Prism (CheckItemExistInventory,
#       all sixteen slots, masked $7F): block $63 at four bytes of plane
#       B, Dezolis's collision plane. They are the four dungeon doors.
#   level $64, $C747 set: block $28 at plane A + $1B0.
# A run entry is row, column, n (loc_9302): n with bit 7 set is itself
# the block to write at that one place; otherwise n & $3F blocks of $87,
# across, or down when bit 6 is set. The run lists are read from the ROM.
MAP_PATCH_RUNS = [(0x2F, "loc_17C94"), (0x33, "loc_17CDA"),
                  (0x31, "loc_17D4A"), (0x2D, "loc_17DA2")]
MAP_PATCH_ITEM = "ItemID_Prism"


def write_map_patches(rom, outdir):
    """assets/data/map_patches.json: loc_9258's writes, in its order."""
    data_dir = outdir / "data"
    data_dir.mkdir(parents=True, exist_ok=True)

    def runs(label):
        raw = rom.blob(label)
        out = []
        for i in range(0, len(raw), 3):
            if raw[i] == 0xFF:
                return out
            out.append(list(raw[i:i + 3]))
        raise SystemExit(f"{label}: no $FF before the end of the list")

    patches = [{
        "level": 0x00, "flags": ["$C735"], "plane": "B",
        "replace": {"from": 0x8B, "to": 0x89, "bytes": 0x1800},
        "set": {"block": 0x89, "at": [0x8C0, 0xC2C, 0xEC8, 0xC17]},
    }]
    for flag, label in MAP_PATCH_RUNS:
        patches.append({
            "level": 0x00, "flags": ["$C735", f"$C7{flag:02X}"],
            "plane": "B", "source": label,
            "runs": {"stride": 0x60, "block": 0x87,
                     "entries": runs(label)},
        })
    patches.append({
        "level": 0x03, "item": rom.const(MAP_PATCH_ITEM),
        "item_name": MAP_PATCH_ITEM,
        "plane": "B", "set": {"block": 0x63,
                              "at": [0x096, 0x0EC, 0x7B9, 0xED0]},
    })
    patches.append({
        "level": 0x64, "flags": ["$C747"], "plane": "A",
        "set": {"block": 0x28, "at": [0x1B0]},
    })
    (data_dir / "map_patches.json").write_text(json.dumps(
        {"source": "loc_9258", "patches": patches}, indent=1), newline="\n")
    print(f"{len(patches)} map patches -> {data_dir / 'map_patches.json'}")


def write_tile_anims(rom, outdir):
    """assets/tiles/anim_<art>_<n>.png, one sheet a frame, and
    assets/data/level_anims.json saying where and when each goes."""
    tile_dir = outdir / "tiles"
    data_dir = outdir / "data"
    data_dir.mkdir(parents=True, exist_ok=True)
    out = []
    for a in TILE_ANIMS:
        raw = rom.blob(a["art"])
        frame_bytes = a["tiles"] * 32
        count = len(raw) // frame_bytes
        steps = (rom.read(rom.addr(a["order"]), a["steps"]) if "order" in a
                 else range(a["steps"]))
        sequence = [(s << TILE_ANIM_SHIFT) // frame_bytes for s in steps]
        files = []
        for f in range(count):
            chunk = raw[f * frame_bytes:(f + 1) * frame_bytes]
            tiles = [chunk[t * 32:(t + 1) * 32] for t in range(a["tiles"])]
            name = f"anim_{asset_names.LEVEL_ANIM_NAMES[a['art']]}_{f}.png"
            ps2png.write_tile_sheet(tile_dir / name, tiles,
                                    ps2png.grey_ramp())
            files.append({"file": name})
        out.append({"level": a["level"], "source": a["art"],
                    "tile": a["vram"] // 32, "period": TILE_ANIM_PERIOD,
                    "frames": files, "sequence": sequence})
    (data_dir / "level_anims.json").write_text(json.dumps(
        {"source": "loc_118F4", "anims": out}, indent=1), newline="\n")
    print(f"{len(out)} tile animations -> {data_dir / 'level_anims.json'}")


def level_labels(rom):
    """The Level_* labels, in order.

    LevelData is immediately followed by one label per level, so walk the
    labels forward from it and stop at the first that is not a level.
    """
    labels = []
    for name, _ in rom.labels[rom.index("LevelData") + 1:]:
        if not name.startswith("Level_"):
            break
        rom.addr(name)
        labels.append(name)
    return labels


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("rom", type=Path, help="the cartridge image")
    ap.add_argument("-o", "--outdir", type=Path, default=Path("assets"))
    args = ap.parse_args()

    rom = RomMap(args.rom)
    music_ids = music_names(rom)

    lvl_dir = args.outdir / "levels"
    tile_dir = args.outdir / "tiles"
    lvl_dir.mkdir(parents=True, exist_ok=True)
    tile_dir.mkdir(parents=True, exist_ok=True)

    labels = level_labels(rom)
    records = {lab: parse_level(rom, lab, music_ids) for lab in labels}
    used_tilesets = {records[lab]["tileset"] for lab in labels}

    # The block sheet is rendered with a palette so it is legible in
    # Tiled. Blocks are shared between levels with different palettes, so
    # this picks the first level that uses the tileset; lines 0 and 1 are
    # shared by every level anyway (PalettePtrs entry 2).
    pal_entries = parse_table(rom, "PalettePtrs")

    def palette_words(index):
        out = []
        for e in (pal_entries[2], pal_entries[index]):
            blob = rom.blob(e["symbol"])
            out += [int.from_bytes(blob[i:i + 2], "big")
                    for i in range(0, min(len(blob), 64), 2)]
        return out[:64]

    first_palette = {}
    for lab in labels:
        first_palette.setdefault(records[lab]["tileset"],
                                 records[lab]["palette"])

    levels, problems = [], []

    # The tilesets, decoded once and shared. Slot 4 is referenced by no
    # level and is not a tileset at all: its "graphics" pointer lands on
    # loc_412F8, the 23-tile mist loc_9222 loads at VRAM $3020 (tiles
    # $181-$197; the VDP command $70200000) for tilesets 1, 3 and 6, in the
    # art format rather than Nemesis. Decoding it as Nemesis fails,
    # correctly; (build 141b) it is written below, as level_mist.png. Only
    # the slots levels ask for are extracted, and the rest are reported.
    tilesets = {}
    for n, (art_sym, def_sym) in enumerate(tileset_pointers(rom)):
        if n not in used_tilesets:
            problems.append(f"tileset {n} ({art_sym}/{def_sym}):"
                            " no level uses it, skipped")
            continue
        tiles, art_used = nemesis.decompress(stream(rom, art_sym))
        blocks, def_used = ps2art.decompress(stream(rom, def_sym))
        defs = b"".join(blocks)
        # (build 97b) Named by the levels on them (tools/asset_names.py);
        # before, level_tiles<n>.png and blocks<n>.txt/.png.
        tsn = asset_names.TILESET_NAMES[n]
        ps2png.write_tile_sheet(tile_dir / f"tileset_{tsn}.png", tiles,
                                ps2png.grey_ramp())
        write_blocks_text(lvl_dir / f"blocks_{tsn}.txt", defs, n)
        sheet = render_blocks(lvl_dir / f"blocks_{tsn}.png", defs, tiles,
                              palette_words(first_palette[n]))
        tilesets[n] = {
            "art": art_sym, "art_tiles": len(tiles),
            "art_compressed": art_used,
            "defs": def_sym, "blocks": len(defs) // 32,
            "defs_compressed": def_used,
            "sheet_across": sheet[0], "sheet_down": sheet[1],
            "max_tile_used": max(
                int.from_bytes(defs[i:i + 2], "big") & 0x7FF
                for i in range(0, len(defs), 2)) if defs else 0,
        }

    # (build 141b) loc_412F8, the mist the backdrop blocks of tilesets 1, 3
    # and 6 use (tiles $181-$197); game.c's level_mist places it.
    mist, _ = ps2art.decompress(stream(rom, "loc_412F8"))
    ps2png.write_tile_sheet(tile_dir / "level_mist.png", mist,
                            ps2png.grey_ramp())

    for index, label in enumerate(labels):
        lvl = records[label]
        lvl["index"] = index

        cols = lvl["width_px"] // 32
        rows = lvl["height_px"] // 32

        # A plane whose bit is set in the +10 flag byte is not the level
        # map at all: loc_8F70 substitutes a fixed $200 x $100 extent for
        # it, so it is a 16 x 8 block backdrop that wraps, and loc_8E00
        # never refreshes it. Every dungeon does this with plane A. All
        # 101 levels agree with the bit.
        size = {}
        for key, bit in (("map_a", 0x08), ("map_b", 0x04)):
            size[key] = ((BACKDROP_COLS, BACKDROP_ROWS)
                         if lvl["collision_flags"] & bit else (cols, rows))
            lvl[key + "_backdrop"] = bool(lvl["collision_flags"] & bit)

        ts = tilesets[lvl["tileset"]]
        maps = {}
        for key in ("map_b", "map_a"):
            want = size[key][0] * size[key][1]
            chunks, used = ps2art.decompress(stream(rom, lvl[key]))
            data = b"".join(chunks)
            lvl[key + "_compressed"] = used
            if len(data) < want:
                problems.append(f"{label}: {key} decodes to {len(data)}"
                                f" bytes, needs {want}")
            maps[key] = data[:want]
            top = max(maps[key]) if maps[key] else 0
            if top >= ts["blocks"]:
                problems.append(
                    f"{label}: {key} block id {top} >= {ts['blocks']}")

        # A backdrop goes in its own file by its flag, not its size:
        # level_load looks for <stem>_A.tmx whenever the bit is set.
        # Deciding by size put the backdrop of the two 16 x 8 levels,
        # $10 EsperMansionB1 and $52 IkutoGroundF, in the level's own
        # .tmx beside plane B, where the loader never looks for it.
        stem = f"{index:02x}_{lvl['name']}"
        full, apart = [], []
        for layer, key in (("planeB", "map_b"), ("planeA", "map_a")):
            w, h = size[key]
            entry = (layer, w, h, maps[key])
            (apart if lvl[key + "_backdrop"] else full).append(entry)

        props = {
            "index": index,
            "cols": cols,
            "rows": rows,
            "tileset": lvl["tileset"],
            "palette": lvl["palette"],
            "music": lvl["music_id"] or 0,
            "flags": lvl["collision_flags"],
            "enemy_group": lvl["enemy_group"],
            "enemy_group_late": lvl["enemy_group_late"],
            # +18: the people/chest art list in bits 0-6, and in bit 7
            # $FFFFF73C, which makes plane A's camera take double steps
            # (the dungeons' overhead parallax).
            "extra_art": lvl["extra_art"],
        }
        if full:
            write_tmx(lvl_dir / f"{stem}.tmx", lvl["tileset"], ts["blocks"],
                      ts["sheet_across"], ts["sheet_down"], full, props)
        for entry in apart:
            suffix = entry[0][-1]        # "A" or "B"
            write_tmx(lvl_dir / f"{stem}_{suffix}.tmx", lvl["tileset"],
                      ts["blocks"], ts["sheet_across"], ts["sheet_down"],
                      [entry], props)

        lvl["cols"], lvl["rows"] = cols, rows
        lvl["files"] = {
            "map": f"{stem}.tmx" if full else None,
            "backdrop": {e[0][-1]: f"{stem}_{e[0][-1]}.tmx" for e in apart},
            "blocks": f"blocks_{asset_names.TILESET_NAMES[lvl['tileset']]}.txt",
            "tiles": f"tileset_{asset_names.TILESET_NAMES[lvl['tileset']]}.png",
        }
        (lvl_dir / f"{stem}.json").write_text(json.dumps(lvl, indent=2), newline="\n")
        lvl["collision_table"] = len(rom.blob(lvl["collision"]))
        lvl["interaction_bytes"] = len(rom.blob(lvl["interactions"]))
        levels.append(lvl)

    (lvl_dir / "levels.json").write_text(json.dumps(
        {"tilesets": tilesets, "levels": levels}, indent=2), newline="\n")

    write_tile_anims(rom, args.outdir)
    write_map_patches(rom, args.outdir)

    print(f"{len(levels)} levels, {len(tilesets)} tilesets")
    for n, ts in tilesets.items():
        print(f"  tileset {n}: {ts['art_tiles']} tiles, {ts['blocks']} blocks,"
              f" highest tile referenced {ts['max_tile_used']}")
    for p in problems:
        print(f"  {p}", file=sys.stderr)
    if not problems:
        print("  every map decodes to at least its full size,"
              " every block id in range")


if __name__ == "__main__":
    main()
