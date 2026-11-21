#!/usr/bin/env python3
"""(build 142) The pictures: the art as the game draws it
(docs/asset_format.md) -- for now the portraits, LoadPortraits' (ps2.asm:
11969) and Rolf's (BuildingScreen's), one full-colour picture each,
from what the other extractors wrote out of the ROM -- each portrait's
tile sheet (assets/tiles/<name>_portrait.png), its palette line
(PalettePtrs, assets/palettes) and its window's map (assets/winart, the
WindowArtLayoutPtrs entry). Run after them (`make extract` does).
(build 156) The art itself is read from the disassembly (disasm_art.py:
the tiles the sheet would hold), not from assets/tiles -- the portraits'
and the scenes' sheets are not kept there any more. (build 185) From the
cartridge image, by the disassembly's names for the art (tools/rommap.py):

    python3 tools/extract_pictures.py ROM --assets assets -o assets/art


A portrait window is the font's frame round an interior of the portrait's
tiles; the picture is that interior -- every cell the map draws from the
portrait's sheet, in the window's palette line (2: base $C400), pixel 0
transparent -- "the portrait without its window frame".

Writes, under the output folder (assets/art):
  portraits/<thing>/<thing>.json   the description
  portraits/<thing>/<thing>.png    the native picture, RGBA
  portraits/<thing>/<thing>_slots.png
                                   (build 146) where an effect recolours
                                   the picture: its slot map (SLOTS below)
  portraits/portraits.json         portrait window number -> thing, and
                                   (146) the library graph's thing

(build 146) The library graph is a picture too: LibrGraphPortArt through
its own window (WinID_LibraryGraphPortrait, WinArt_LibraryGraphPortrait,
base $A400 like Rolf's: line 1, the art at $4A0) in Pal_GraphPortrait
(PalettePtrs $2B), as BuildingScreen's library case loads it (loc_8304).

(build 147) And the cut-scene pictures (asset_format.md, "Cut-scene
pictures"), one a nametable (assets/nametables, extract_nametables.py):
the table drawn whole -- each cell its tile from its tileset's sheet, in
that tileset's palette line, flipped as the cell says, pixel 0
transparent -- in the palettes the game loads for that screen (SCENES),
cut to the cells that draw something. Writes
  scenes/<thing>/<thing>.json      the description; "at" is the cell of
                                   the table its top left is in
  scenes/<thing>/<thing>.png       the native picture, RGBA
"""

import argparse
import json
import re
import sys
from pathlib import Path

from PIL import Image

sys.path.insert(0, str(Path(__file__).parent))
from disasm_art import DisasmArt  # noqa: E402

PORTRAIT_LINE = 2          # WIN_BASE_PORTRAIT $C400: line 2

# Portrait numbers whose window the game never opens, so no picture: the
# table pairs them with art and a window all the same, and drawn they come
# out scrambled.
#   $19 (25), CentralTowerOutside2 -- the tower art in its second palette
#        through the teleport employee's window map; nothing sets
#        portrait_index to $19 and no door names it.
#   $1C (28), CentralTowerOutside3 -- the tower art through the battle's
#        empty-spots box (WinArt_BattleEmptySpots); loc_CC26 sets $1C for
#        Tyler's spaceship, whose case in BuildingScreen (loc_8326) skips
#        the portrait window (addq.w #1, (event_routine).w): the art and
#        palette load, no window shows them.
#   $1D (29), CentralTowerOutside3_Copy -- the same; set nowhere.
# And $1A (26), LibraryGraph: set nowhere either; the library loads the
# graph itself, at Rolf's $4A0 (BuildingScreen, the LibrGraphPortArt load),
# and shows it through Rolf's window -- its picture comes with his.
NEVER_SHOWN = {0x19, 0x1A, 0x1C, 0x1D}

# Window 0 is Rolf's, not LoadPortraits': BuildingScreen (ps2.asm:11876)
# decompresses RolfPortraitArt to VRAM $9400 -- tile $4A0, byte $A0 past the
# windows' $400 -- and loads Pal_RolfPortrait (PalettePtrs $11) on line 1,
# in every building but the library, the roof, Tyler's spaceship and the
# Esper mansion. His window (WinID_RolfPortrait, base $A400) draws it. In
# the library the same VRAM holds LibrGraphPortArt and its own palette
# ($2B), shown through WinID_RolfPortrait2 -- that picture changes colour
# with the overlay (a glow mask's job) and comes later.
BUILDING_PORTRAITS = {
    0: {"art": "rolf_portrait", "palette": 0x11, "name": "Rolf",
        "line": 1, "art_byte": 0xA0},
}

# (build 146) The library graph: not a portrait window, so not in
# portrait_windows.json; WinID_LibraryGraphPortrait ($55) draws it.
LIBRARY_GRAPH = {"art": "graph_portrait", "palette": 0x2B,
                 "name": "LibraryGraph", "line": 1, "art_byte": 0xA0,
                 "window": "WinArt_LibraryGraphPortrait",
                 "thing": "library_graph"}

# (build 146) Slot maps: the palette entries (1-15, in the picture's line)
# an effect writes while the picture is shown, so the compositor still
# knows which of its pixels take the effect's colour once there are no
# tiles behind it (docs/asset_format.md, "Colour effects": the glow mask).
# A slot map is an indexed PNG the size of the picture, a pixel's index
# its entry where the effect writes it, 0 elsewhere.
#   radar          Gaira's control panel cycles line 2 entry 5 (loc_66F6,
#                  ps2.asm:9527, through loc_6790's four colours;
#                  game.c's building_palette_cycle).
#   library_graph  loc_C4B2's PaletteLoad2 $32 (ps2.asm:18022) writes all
#                  of line 1 under the graph: its hidden lines appear.
#   (build 149) The cut-scene pictures' colour cycles:
#   sega_logo      PaletteCycle (ps2.asm:9226) slides CyclingPal_SegaLogo
#                  through line 0 entries 2-12, the sweep; entry 1 stays.
#   ending_stars   loc_6AE0, the credits' fade: line 3 entries 9-12, from
#                  black to the ramp's first row (ending_credits_fade.png).
#   tyler_monitor  loc_66F6 from event_routine $A: line 3 entries 12-15
#                  through loc_6798's four rows.
#   (build 155) mother_brain_stars  Her chase (loc_67FE): one of line 3's
#                  entries 8-15 lit at a time, every fourth frame.
SLOTS = {"radar": set([5]), "library_graph": set(range(1, 16)),
         "sega_logo": set(range(2, 13)), "ending_stars": set(range(9, 13)),
         "tyler_monitor": set(range(12, 16)),
         "mother_brain_stars": set(range(8, 16))}

# (build 147) The cut-scene pictures: the nametable each is drawn from, and
# the PalettePtrs entry the game loads into each palette line it uses.
#   lutz  The Esper mansion (BuildingScreen's loc_8364, ps2.asm:11914):
#         LutzPortraitArt at VRAM $0020, LutzPortraitTileInd 30x14 into
#         plane A at $C10A (column 5, row 2), PaletteLoad1 $3B
#         (Pal_LutzPortrait) for line 3.
#   (build 148) The pictures nothing but the screen's fades touches:
#   flight_planet_to_dezolis, flight_planet_to_motavia
#         The roof's flight (loc_C58A): loc_716BA 6x6 into plane B at row
#         7, column 22, loc_71362 at tile 1; PaletteLoad1 $39 when the
#         destination is DezolisSkure, $3A otherwise, for line 3.
#   nei_*  The two pictures after Neifirst (loc_7E02 cases 1 and 2,
#         ps2.asm:11498-11536): loc_AED00 at tile 1, NeiDeathTileInd 28x14
#         at plane A row 2, column 6, in PalettePtrs $35 (case 1) or $36
#         (case 2, which adds NeiBodyTileInd 6x4 at row 9, column 17).
#   battle_floor  BattleFloorTileInd at plane B row 12, tile $781, in
#         line 0 of the battle's palette ($2F, all four lines).
#   ending_*  The epilogue's six pages (loc_7932's pictures through
#         loc_7914, line 2 in PalettePtrs $44-$49, one a page) and the last
#         picture, wiped in band by band in PalettePtrs $4A.
#   (build 149) The colour cycles (their slot maps: SLOTS):
#   sega_logo  SegaScreen: SegaLogoArt at tile 0, SegaLogoTileInd 12x4
#         into plane A at column 14, row 12, Pal_Sega (PalettePtrs 0).
#   ending_stars  The ending's plane B, EndingStarfieldTileInd 40x28 at
#         tile $480, line 3: cleared, so black, until the PaletteLoad1 $4B
#         (lines 2-3) after loc_634E's fade to white, before the final
#         days' page (ps2.asm:10939).
#   tyler_monitor  Tyler's spaceship (loc_CD50, loc_CDDE):
#         TylerSpcshp_MonitorTileInd's five 15x12 frames ($168 bytes
#         apart), one at a time into plane A at column 19, row 3, in
#         PalettePtrs $2A (lines 2-3); frames keyed by loc_CDDE's number.
#   (build 150) The last of the fades-only group, missed in 148:
#   dream_waking  The dream's second picture, Rolf waking (loc_D59C,
#         ps2.asm:19577): DreamWakingTileInd 28x14 into plane A at $C10C
#         (column 6, row 2), loc_6FF22 at tile 1, PaletteLoad1 $30 for
#         line 3 into the fade target and loc_61F4 fading line 3 alone in.
#   (build 151) Dissolved in and out (the reveal mask, VideoPicture's
#   `hidden`), in line 3 of the title's palette (Pal_TitleScr, PalettePtrs
#   1, all four lines), which IntroScreen and Tyler's spaceship
#   (loc_8326) both load:
#   dream_battle  The dream's fight (loc_D534, ps2.asm:19549; Tyler's
#         event_routine 3): loc_70A32 28x14 into plane A at column 6, row
#         2, loc_6D7BA at tile 1, loc_6BBE in and loc_6C54 out on $FA tiles.
#   dream_help  Tyler's second picture, "Help!!!": loc_70D42 28x14 in the
#         same place over loc_6F040, in and out on $96 tiles.
#   (build 152) tyler_portrait  Tyler's spaceship's last view (event_routine
#         7, loc_CD50): TylerPortraitTileInd 13x12 into plane A at column 6,
#         row 3, in line 2 of PalettePtrs $2A (lines 2-3; the monitor's
#         frames are its line 3), dissolved in on $B4 tiles with the monitor
#         from TylerSpaceshipArt at tile 1.
#   (build 154) The title screen (TitleScreen, ps2.asm:10632), both planes
#   in Pal_TitleScr (PalettePtrs 1):
#   title  TitleScrPlaneATileInd 40x28 in plane A: the figures
#         (TitleScrWomenArt, tile $E1, line 2) and the logo
#         (TitScrPhantasyStarLogoArt, tile $200, line 2), one picture,
#         each dissolved in by FadeArtTiles on its own range (287 tiles,
#         then 256) -- the game binds the picture once a range.
#   title_background  TitleScrBGTileInd 40x28 in plane B, TitleScrBGArt at
#         tile 1, line 1: only the screen's fades.
#   gameover_background  GameOverScreen's plane B (ps2.asm:12255):
#         loc_6C5DA (GameOverBGTileInd, build 153) over the same art and
#         palette -- the title's background two rows up, two rows of its
#         own below; only fades.
#   (build 155) mother_brain_stars  The star field behind Mother Brain
#         (BattleScreen, ps2.asm:12088): loc_AE8A0 40x14 into plane B from
#         row 0, over her art at tile 1, in line 3 of the battle's $2F
#         (her one group's colours go to line 2); dissolved in with all
#         the enemy art (FadeArtTiles on $200 tiles from 0), the chase
#         along entries 8-15 (SLOTS).
SCENES = {
    "lutz": {"nametable": "LutzPortraitTileInd", "palettes": {3: 0x3B}},
    "flight_planet_to_dezolis": {"nametable": "SpaceshipPlanetTileInd",
                                 "palettes": {3: 0x39}},
    "flight_planet_to_motavia": {"nametable": "SpaceshipPlanetTileInd",
                                 "palettes": {3: 0x3A}},
    "nei_face": {"nametable": "NeiDeathTileInd", "palettes": {1: 0x35}},
    "nei_face_starry": {"nametable": "NeiDeathTileInd",
                        "palettes": {1: 0x36}},
    "nei_eyes_closed": {"nametable": "NeiBodyTileInd",
                        "palettes": {1: 0x36}},
    "battle_floor": {"nametable": "BattleFloorTileInd",
                     "palettes": {0: 0x2F}},
    "ending_rudo": {"nametable": "EndingPage1PortraitTileInd",
                    "palettes": {2: 0x44}},
    "ending_amy": {"nametable": "EndingPage2PortraitTileInd",
                   "palettes": {2: 0x45}},
    "ending_kain": {"nametable": "EndingPage3PortraitTileInd",
                    "palettes": {2: 0x46}},
    "ending_shir": {"nametable": "EndingPage4PortraitTileInd",
                    "palettes": {2: 0x47}},
    "ending_hugh": {"nametable": "EndingPage5PortraitTileInd",
                    "palettes": {2: 0x48}},
    "ending_anna": {"nametable": "EndingPage6PortraitTileInd",
                    "palettes": {2: 0x49}},
    "ending_rolf": {"nametable": "EndingLastPictureTileInd",
                    "palettes": {2: 0x4A}},
    "sega_logo": {"nametable": "SegaLogoTileInd", "palettes": {0: 0x00}},
    "ending_stars": {"nametable": "EndingStarfieldTileInd",
                     "palettes": {3: 0x4B}},
    "tyler_monitor": {"nametable": "TylerSpcshp_MonitorTileInd",
                      "palettes": {3: 0x2A}, "rows": 12,
                      "frames": {str(n): n * 12 for n in range(5)}},
    "dream_waking": {"nametable": "DreamWakingTileInd",
                     "palettes": {3: 0x30}},
    "dream_battle": {"nametable": "DreamBattleTileInd",
                     "palettes": {3: 0x01}},
    "dream_help": {"nametable": "DreamHelpTileInd",
                   "palettes": {3: 0x01}},
    "tyler_portrait": {"nametable": "TylerPortraitTileInd",
                       "palettes": {2: 0x2A}},
    "title": {"nametable": "TitleScrPlaneATileInd", "palettes": {2: 0x01}},
    "title_background": {"nametable": "TitleScrBGTileInd",
                         "palettes": {1: 0x01}},
    "gameover_background": {"nametable": "GameOverBGTileInd",
                            "palettes": {1: 0x01}},
    "mother_brain_stars": {"nametable": "MotherBrainMachineTileInd",
                           "palettes": {3: 0x2F}},
}


def tmx_layer(path):
    """The tmx's tilesets (firstgid, byte_base) and its one layer's gids."""
    text = Path(path).read_text()
    sets = []
    for m in re.finditer(r'<tileset firstgid="(\d+)"(.*?)</tileset>', text,
                         re.S):
        base = re.search(r'name="byte_base" type="int" value="(-?\d+)"',
                         m.group(2))
        sets.append((int(m.group(1)), int(base.group(1)) if base else 0))
    sets.sort()
    w = int(re.search(r'<layer[^>]* width="(\d+)"', text).group(1))
    h = int(re.search(r'<layer[^>]* height="(\d+)"', text).group(1))
    csv = re.search(r'<data encoding="csv">(.*?)</data>', text, re.S).group(1)
    gids = [int(v) for v in csv.replace("\n", "").split(",") if v.strip()]
    return sets, w, h, gids


def palette_line(asset_dir, index, ptrs):
    """PalettePtrs entry `index`'s 16 colours, as its PNG holds them."""
    entry = next(e for e in ptrs if e["index"] == index)
    im = Image.open(asset_dir / "palettes" / entry["file"])
    pal = im.getpalette()[:16 * 3]
    return [tuple(pal[i * 3:i * 3 + 3]) for i in range(16)], entry


def write_json(path, obj):
    """JSON as the tree keeps it: two-space indents, LF line ends on any
    system (build 146: Windows's write_text wrote CRLF)."""
    path.write_text(json.dumps(obj, indent=2) + "\n", newline="\n")


def make_picture(a, da, out, win_art, p, ptrs, made, problems,
                 thing=None):
    """One picture: window `win_art`'s interior cells drawn from `p`'s art
    in `p`'s palette line. Returns its thing, or None. Windows that draw
    the same art the same way share one (`made`, picture -> thing)."""
    art_byte = p.get("art_byte", 0)
    line = p.get("line", PORTRAIT_LINE)
    sheet = p["art"]
    sets, w, h, gids = tmx_layer(a / "winart" / (win_art + ".tmx"))
    tiles = da.tiles(sheet)
    count = len(tiles)
    colours, pentry = palette_line(a, p["palette"], ptrs)

    def byte_of(gid):
        """The window map's byte for a cell: what loc_980A adds to the
        base, whichever tileset the .tmx shows it with."""
        first, base = max(t for t in sets if t[0] <= gid)
        return gid - first + base

    # The interior: rows 1 .. h-2 (rows 0 and h-1 are the frame's top
    # and bottom, the font's), every column, drawn from base $C400:
    # tile $400 + byte, the portrait's art for a byte inside it.
    cells = [(r, c) for r in range(1, h - 1) for c in range(w)
             if 0 <= byte_of(gids[r * w + c]) - art_byte < count]
    if not cells:
        problems.append(f"{win_art}: no cell from {sheet}")
        return None
    r0 = min(r for r, _ in cells)
    r1 = max(r for r, _ in cells)
    c0 = min(c for _, c in cells)
    c1 = max(c for _, c in cells)
    pw, ph = (c1 - c0 + 1) * 8, (r1 - r0 + 1) * 8
    im = Image.new("RGBA", (pw, ph), (0, 0, 0, 0))
    entries = Image.new("P", (pw, ph), 0)
    px = im.load()
    ex = entries.load()
    for r, c in cells:
        t = tiles[byte_of(gids[r * w + c]) - art_byte]
        for y in range(8):
            for x in range(8):
                i = t[y][x]
                if i != 0:
                    px[(c - c0) * 8 + x, (r - r0) * 8 + y] = \
                        colours[i] + (255,)
                    ex[(c - c0) * 8 + x, (r - r0) * 8 + y] = i
    # A thing a picture: windows that draw the same art the same way
    # share one (the four Dezolians); the same art in another window
    # arrangement is a thing of its own, named for its portrait.
    if thing is None:
        thing = sheet[:-len("_portrait")] if sheet.endswith("_portrait") \
            else sheet
    key = im.tobytes() + bytes(f"{pw}x{ph}", "ascii")
    if key in made:
        return made[key]
    if thing in made.values():
        thing = re.sub(r"(?<!^)(?=[A-Z])", "_", p["name"]).lower()
        thing = re.sub(r"_+", "_", thing)
    made[key] = thing
    d = out / "portraits" / thing
    d.mkdir(parents=True, exist_ok=True)
    im.save(d / (thing + ".png"), optimize=True)
    frame = {"file": thing + ".png"}
    slots = SLOTS.get(thing)
    if slots:
        # (build 146) The entries the effect writes, 0 elsewhere; a grey
        # ramp so the map can be looked at.
        sm = entries.point(lambda i: i if i in slots else 0)
        sm.putpalette([v for i in range(16) for v in (i * 17,) * 3])
        sm.save(d / (thing + "_slots.png"), optimize=True)
        frame["slots"] = thing + "_slots.png"
    desc = {
        "thing": thing,
        "kind": "portrait",
        "size": [pw, ph],
        "anchor": [0, 0],
        "frames": {"1": frame},
        "window": win_art,
        # The picture's top left in the window, in cells from the
        # frame's top-left corner (interior column j is frame column
        # j + 1).
        "at": [c0 + 1, r0],
        "source": f"{sheet}.png, PalettePtrs {p['palette']} "
                  f"({pentry['name']}) in line {line}, "
                  f"{win_art}",
    }
    write_json(d / (thing + ".json"), desc)
    return thing


def nametable_tmx(path):
    """A nametable's tilesets (firstgid, properties, sheet path) and its
    layer: width, height and the gids with Tiled's flip bits."""
    text = Path(path).read_text()
    sets = []
    for m in re.finditer(r'<tileset firstgid="(\d+)"(.*?)</tileset>', text,
                         re.S):
        body = m.group(2)
        props = {k: int(v) for k, v in re.findall(
            r'<property name="(\w+)" type="int" value="(-?\d+)"', body)}
        image = re.search(r'<image source="([^"]+)"', body).group(1)
        sets.append((int(m.group(1)), props, Path(path).parent / image))
    sets.sort(key=lambda t: t[0])
    w = int(re.search(r'<layer[^>]* width="(\d+)"', text).group(1))
    h = int(re.search(r'<layer[^>]* height="(\d+)"', text).group(1))
    csv = re.search(r'<data encoding="csv">(.*?)</data>', text, re.S).group(1)
    gids = [int(v) for v in csv.replace("\n", "").split(",") if v.strip()]
    return sets, w, h, gids


H_FLIP, V_FLIP, D_FLIP = 0x80000000, 0x40000000, 0x20000000


def palette_for_line(a, index, ptrs, line):
    """(build 148) PalettePtrs entry `index`'s colours for palette line
    `line`: its PNG's colour k is RAM entry (its line * 16 + its entry + k),
    for its `words` colours -- so a four-line palette (the battle's) gives
    each of its lines. None for an entry it does not load. Returns the 16
    and the entry."""
    entry = next(e for e in ptrs if e["index"] == index)
    pal = Image.open(a / "palettes" / entry["file"]).getpalette()
    start = entry["line"] * 16 + entry["entry"]
    colours = []
    for e in range(16):
        k = line * 16 + e - start
        colours.append(tuple(pal[k * 3:k * 3 + 3])
                       if 0 <= k < entry["words"] else None)
    return colours, entry


def scene_picture(a, da, out, thing, spec, ptrs, problems):
    """(build 147) One cut-scene picture: `spec`'s nametable drawn whole in
    its palettes, cut to the cells that draw something. Returns True when
    it was written. (build 148) Cut to the cells that name a tile, drawn or
    not: once the art is not loaded, a cell the picture left out would
    show whatever else is in VRAM at its tile (the Nei scenes' art goes
    over the level's). (build 149) A table that stacks frames (`spec`'s
    "frames": the game's number for each -> its first row, "rows" tall)
    gives a picture a frame, every one cut to the cells any of them names
    so they share "size" and "at" (from the frame's first row); a thing in
    SLOTS gets a slot map a frame."""
    name = spec["nametable"]
    sets, w, h, gids = nametable_tmx(a / "nametables" / (name + ".tmx"))
    lines = {line: palette_for_line(a, index, ptrs, line)
             for line, index in spec["palettes"].items()}
    frames = spec.get("frames", {"1": 0})
    rows = spec.get("rows", h)
    named = set()
    sheets = {}
    drawn = {}
    for key, first in frames.items():
        for r in range(rows):
            for c in range(w):
                g = gids[(first + r) * w + c]
                gid = g & ~(H_FLIP | V_FLIP | D_FLIP)
                if gid == 0:
                    continue
                at = f"{c},{first + r}"
                if g & D_FLIP:
                    problems.append(f"{name}: a diagonal flip at {at}")
                    continue
                first_gid, props, sheet = max((t for t in sets
                                               if t[0] <= gid),
                                              key=lambda t: t[0])
                if props.get("vram_base", 0) + gid - first_gid != 0:
                    named.add((r, c))
                # (build 156) The tileset's sheet by name, its tiles
                # from the cartridge image (build 185; the disassembly
                # until then).
                sheet = sheet.stem
                if sheet not in sheets:
                    sheets[sheet] = da.tiles(sheet)
                tile = sheets[sheet][gid - first_gid]
                if g & V_FLIP:
                    tile = tile[::-1]
                if g & H_FLIP:
                    tile = [row[::-1] for row in tile]
                if all(i == 0 for row in tile for i in row):
                    continue
                line = props.get("palette_line", 0)
                if line not in lines:
                    problems.append(f"{name}: cell {at} draws in line "
                                    f"{line}, which SCENES gives no palette")
                    continue
                colours = lines[line][0]
                if any(colours[i] is None for row in tile for i in row if i):
                    problems.append(f"{name}: cell {at} draws a colour of "
                                    f"line {line} its palette does not load")
                    continue
                drawn[(key, r, c)] = (tile, colours)
    if not drawn:
        problems.append(f"{name}: nothing drawn")
        return False
    r0 = min(r for r, _ in named)
    r1 = max(r for r, _ in named)
    c0 = min(c for _, c in named)
    c1 = max(c for _, c in named)
    pw, ph = (c1 - c0 + 1) * 8, (r1 - r0 + 1) * 8
    slots = SLOTS.get(thing, set())
    d = out / "scenes" / thing
    d.mkdir(parents=True, exist_ok=True)
    described = {}
    for key in frames:
        stem = thing if len(frames) == 1 else f"{thing}_{key}"
        im = Image.new("RGBA", (pw, ph), (0, 0, 0, 0))
        entries = Image.new("P", (pw, ph), 0)
        px = im.load()
        ex = entries.load()
        for (k, r, c), (tile, colours) in drawn.items():
            if k != key:
                continue
            for y in range(8):
                for x in range(8):
                    i = tile[y][x]
                    if i != 0:
                        at = ((c - c0) * 8 + x, (r - r0) * 8 + y)
                        px[at] = colours[i] + (255,)
                        ex[at] = i if i in slots else 0
        im.save(d / (stem + ".png"), optimize=True)
        frame = {"file": stem + ".png"}
        if slots:
            # As make_picture's: the entries the effect writes, a grey ramp
            # so the map can be looked at.
            entries.putpalette([v for i in range(16) for v in (i * 17,) * 3])
            entries.save(d / (stem + "_slots.png"), optimize=True)
            frame["slots"] = stem + "_slots.png"
        described[key] = frame
    used = ", ".join(f"line {line} PalettePtrs {index} "
                     f"({lines[line][1]['name']})"
                     for line, index in sorted(spec["palettes"].items()))
    table = name if len(frames) == 1 else \
        f"{name} ({rows} rows a frame, from row " + \
        ", ".join(f"{first} for {key}" for key, first in frames.items()) + ")"
    desc = {
        "thing": thing,
        "kind": "scene",
        "size": [pw, ph],
        "anchor": [0, 0],
        "frames": described,
        "nametable": name,
        # The picture's top left in the table (in a frame of a stacked
        # table: from the frame's first row), in cells.
        "at": [c0, r0],
        "source": f"{', '.join(sorted(s + '.png' for s in sheets))}, "
                  f"{used}, {table}",
    }
    write_json(d / (thing + ".json"), desc)
    return True


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("rom", type=Path, help="the cartridge image")
    ap.add_argument("--assets", default="assets", type=Path)
    ap.add_argument("-o", "--out", default="assets/art", type=Path)
    args = ap.parse_args()

    a = args.assets
    da = DisasmArt(args.rom)
    windows = json.loads((a / "data/portrait_windows.json").read_text())
    portraits = {e["index"]: e for e in
                 json.loads((a / "data/portraits.json").read_text())}
    ptrs = json.loads((a / "palettes/paletteptrs.json").read_text())

    index = {}
    made = {}
    problems = []
    for win in windows:
        n = win["portrait"]
        p = portraits.get(n, BUILDING_PORTRAITS.get(n))
        if n in NEVER_SHOWN:
            continue
        if p is None:
            problems.append(f"window {n}: no portrait")
            continue
        thing = make_picture(a, da, args.out, win["art"], p, ptrs, made,
                             problems)
        if thing is not None:
            index[str(n)] = thing
    graph = make_picture(a, da, args.out, LIBRARY_GRAPH["window"],
                         LIBRARY_GRAPH, ptrs, made, problems,
                         LIBRARY_GRAPH["thing"])

    (args.out / "portraits").mkdir(parents=True, exist_ok=True)
    write_json(args.out / "portraits" / "portraits.json",
               {"windows": index, "library_graph": graph})
    print(f"portraits: {len(index)} pictures and the library graph -> "
          f"{args.out / 'portraits'}")
    scenes = [t for t, spec in SCENES.items()
              if scene_picture(a, da, args.out, t, spec, ptrs, problems)]
    print(f"scenes: {len(scenes)} pictures -> {args.out / 'scenes'}")
    for p in problems:
        print("  " + p, file=sys.stderr)
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
