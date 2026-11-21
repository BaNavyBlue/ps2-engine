"""The art dump: every monster, weapon and technique animation, every
portrait, the cut-scene pictures and the field sprites, and (build 139a)
every level whole, the text windows, the font and the palettes, as images
at native pixel size with their palettes applied.

Run from ps2/ after tools/art_dump has written its captures -- or `make
art` (build 127), which runs all of it, the routes it reads first:

    make art_dump
    python3 tools/art_dump_plan.py > /tmp/plan.txt
    ./art_dump /tmp/plan.txt /tmp/adm
    python3 tools/art_export.py ROM /tmp/adm ../ps2_art

(build 185) ROM is the cartridge image, for the art the pictures replaced
(see disasm()); until then PS2_ASM and ART_DIR named the disassembly.

(build 158) The cut-scene pictures are the game's own, assets/art/scenes
(builds 147-155: full colour, each thing's frames, slot maps and
description), copied as they are into cutscenes/<thing>/. Until 158 they
were drawn here from their tiles under a palette read from a route's
shot, and the shots of pictures carry no palette since build 146a (they
are RGB): the dump stopped at Mother Brain's frame. That frame, the
game's own top of the screen mid-fight, is still kept from
mother_brain_battle's shot (`make art` runs the route).

Every image is an indexed PNG carrying the 64-colour palette it is drawn
with (the palette lines as the game loads them, through the port's DAC
ladder); index 0 is transparent. Animations are APNG (an animated PNG:
plays in any web browser, keeps the palette and the real frame timing)
plus a multi-page TIFF of the same frames in the same order, for stepping
through in Fiji/ImageJ. Each file's source art, palette and script are in
index.json."""

import json
import os
import re
import sys

from PIL import Image

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import adm  # noqa: E402
from disasm_art import DisasmArt  # noqa: E402

A = 'assets'
CHARS = ['rolf', 'nei', 'rudo', 'amy', 'hugh', 'anna', 'kain', 'shir']
INDEX = []


def slug(s):
    s = re.sub(r'([a-z])([A-Z])', r'\1_\2', s.strip())
    s = re.sub(r'[^A-Za-z0-9]+', '_', s).strip('_')
    return s.lower()


def rel(out, path):
    return os.path.relpath(path, out).replace(os.sep, '/')


# ---------------------------------------------------------------- assets

# (build 156) The art the pictures replaced has no sheet under
# assets/tiles (asset_names.PICTURE_ART): its tiles come from the
# cartridge (disasm_art.py). (build 185) Read from the image main() is
# given (ROM), where it was the disassembly PS2_ASM and ART_DIR named;
# opened the first time a sheet needs it.
ROM = None
_DISASM = []


def disasm():
    if not _DISASM:
        _DISASM.append(DisasmArt(ROM))
    return _DISASM[0]


def sheet_source(name):
    """Where a sheet's tiles come from, for index.json."""
    path = f'{A}/tiles/{name}.png'
    return path if os.path.exists(path) else f'{name} (the cartridge image)'


def load_tiles(name):
    if not os.path.exists(f'{A}/tiles/{name}.png'):
        return [bytes(t) for t in disasm().sheet(name)[0]]
    im = Image.open(f'{A}/tiles/{name}.png')
    w, h = im.size
    px = im.tobytes()
    tiles = []
    for ty in range(h // 8):
        for tx in range(w // 8):
            t = bytearray(64)
            for y in range(8):
                row = (ty * 8 + y) * w + tx * 8
                t[y * 8:y * 8 + 8] = px[row:row + 8]
            tiles.append(bytes(t))
    return tiles


def pal_rows(name):
    im = Image.open(f'{A}/palettes/{name}.png')
    p = im.getpalette()
    rows = []
    for r in range(im.size[1]):
        rows.append([tuple(p[(r * 16 + i) * 3:(r * 16 + i) * 3 + 3])
                     for i in range(16)])
    return rows


class Scene:
    """A VRAM mirror and a 64-entry palette, as a screen's setup leaves
    them."""

    def __init__(self):
        self.vram = {}
        self.pal = [(0, 0, 0)] * 64
        self.sources = {'art': [], 'palette': []}

    def place(self, art, base):
        for i, t in enumerate(load_tiles(art)):
            self.vram[base + i] = t
        self.sources['art'].append(f'{sheet_source(art)} at tile {base:#x}')

    def palette(self, name, line):
        for k, row in enumerate(pal_rows(name)):
            if line + k < 4:
                self.pal[(line + k) * 16:(line + k + 1) * 16] = row
        self.sources['palette'].append(
            f'{A}/palettes/{name}.png on line {line}')

    def plte(self):
        flat = []
        for c in self.pal:
            flat.extend(c)
        return flat + [0] * (768 - len(flat))


def put_tile(buf, bw, x, y, tile, line, fh, fv):
    for yy in range(8):
        sy = 7 - yy if fv else yy
        for xx in range(8):
            sx = 7 - xx if fh else xx
            v = tile[sy * 8 + sx] & 0xF
            if v:
                buf[(y + yy) * bw + x + xx] = line * 16 + v


def image_from(buf, w, h, scene):
    im = Image.frombytes('P', (w, h), bytes(buf))
    im.putpalette(scene.plte())
    im.info['transparency'] = 0
    return im


def load_winmap(name):
    # (build 134) a Tiled map, read as winmap.c reads it: each cell's byte
    # from its tileset's firstgid range and byte_base property.
    import xml.etree.ElementTree as ET
    root = ET.parse(f'{A}/winart/{name}.tmx').getroot()
    sets = []
    for ts in root.iter('tileset'):
        base = next((int(p.get('value')) for p in ts.iter('property')
                     if p.get('name') == 'byte_base'), 0)
        sets.append((int(ts.get('firstgid')), int(ts.get('tilecount')), base))
    layer = root.find('layer')
    w, h = int(layer.get('width')), int(layer.get('height'))
    gids = [int(t) for t in layer.find('data').text.replace('\n', '').split(',')
            if t.strip()]
    cells = [next(g - fg + b for fg, n, b in sets if fg <= g < fg + n)
             for g in gids]
    return w, h, cells


def save_png(out, path, im, kind, opaque=False, **info):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    if opaque:
        im.save(path)
    else:
        im.save(path, transparency=0)
    INDEX.append(dict(file=rel(out, path), kind=kind,
                      size=list(im.size), **info))


# ------------------------------------------------------------- portraits

def portraits(out):
    ports = {p['index']: p for p in json.load(open(f'{A}/data/portraits.json'))}
    wins = json.load(open(f'{A}/data/portrait_windows.json'))
    ptrs = {p['index']: p for p in json.load(open(f'{A}/palettes/paletteptrs.json'))}
    done = set()
    for w in wins:
        n, art_map = w['portrait'], w['art']
        p = ports.get(n, {})
        note = ''
        # (build 127) Slots 25, 28 and 29 pair PortraitTable's tower-view
        # art (in palettes of their own) with window maps not its own --
        # the teleport clerk's, and WinArt_BattleEmptySpots twice -- and no
        # door or code opens them (docs/history.md, Build 127). The view is
        # drawn with its own map in each palette instead, a palette
        # variant; 29 is 28 again.
        if n in MISMATCHED:
            note = MISMATCHED[n]
            art_map = 'WinArt_CentTowerOutsidePortrait'
        elif not (art_map.endswith('Portrait') or n == 22):
            continue
        key = (art_map, p.get('art'), p.get('palette'))
        if key in done:
            INDEX.append(dict(file=None, kind='portrait slot', slot=n,
                              notes=note or 'the same as an earlier slot'))
            continue
        done.add(key)
        s = Scene()
        if n == 26:
            # The library's graph: BuildingScreen puts it where Rolf's
            # portrait goes ($4A0) with its palette on line 1, and the
            # window draws it with Rolf's base ($A400).
            art, base, line, tile0 = p['art'], 0x400, 1, 0x4A0
            s.palette(os.path.splitext(ptrs[p['palette']]['file'])[0], 1)
            name = 'library_graph'
        elif n == 0:
            # Win_RolfPortrait: base $A400 -- line 1, tile $400 + index --
            # with RolfPortraitArt at $4A0 and pal_id_rolf_port on line 1.
            art, base, line, tile0 = 'rolf_portrait', 0x400, 1, 0x4A0
            s.palette('portrait_rolf', 1)
            name = 'rolf'
        else:
            if not p:
                continue
            # Win_Portraits: base $C400 -- line 2, tile $400 + index --
            # over LoadPortraits' art at $400, its palette on line 2.
            art, base, line, tile0 = p['art'], 0x400, 2, 0x400
            s.palette(os.path.splitext(ptrs[p['palette']]['file'])[0], 2)
            # named by its palette: the four Dezolians share one art
            name = os.path.splitext(ptrs[p['palette']]['file'])[0]
            name = slug(name.replace('portrait_', ''))
            if n == 22:
                # (build 127) its own map, the view with the man who runs
                # up at "Something's wrong..." (building.c's tower_outside)
                name = 'central_tower_outside_alarm'
            elif n in MISMATCHED:
                name = f"central_tower_outside_palette_{p['palette']:02x}"
        s.place(art, tile0)
        mw, mh, cells = load_winmap(art_map)
        rows = mh - 2              # the $B9 / $BE rows are the frame's
        buf = bytearray(mw * 8 * rows * 8)
        missing = 0
        for r in range(rows):
            for c in range(mw):
                v = cells[(r + 1) * mw + c]
                tile = s.vram.get(base + v)
                if tile is None:
                    missing += 1
                    continue
                put_tile(buf, mw * 8, c * 8, r * 8, tile, line, 0, 0)
        im = image_from(buf, mw * 8, rows * 8, s)
        if missing:
            note = (note + '; ' if note else '') + f'{missing} cells had no art'
        save_png(out, f'{out}/portraits/{n:02d}_{name}.png', im, 'portrait',
                 window_map=f'{A}/winart/{art_map}.tmx', **s.sources,
                 notes=note)


# ---------------------------------------------------------- the pictures

# (build 127) Slots whose two tables do not pair (portraits()).
MISMATCHED = {
    25: ("an unused mismatched pair: PortraitTable gives slot 25 the tower "
         "view's art in palette $2A (Pal_CentTow2Port), PtrWin_PortraitStart "
         "the teleport clerk's window map; no door or code opens it. Drawn "
         "here with the view's own map in $2A -- the palette Tyler's ship "
         "loads on lines 2-3 when it shows Tyler (loc_CD50)"),
    28: ("an unused mismatched pair: the tower view's art in its third "
         "palette with WinArt_BattleEmptySpots, a battle window's map; "
         "Tyler's ship sets portrait_index 28 (loc_CC26) but skips "
         "LoadPortraits (loc_8326). Drawn here with the view's own map"),
    29: "slot 28 again: the same art, palette and map",
}


def pictures(out):
    """(build 158) The cut-scene pictures, assets/art/scenes, as they are:
    each thing's frames, slot maps and description into cutscenes/<thing>/.
    Then Mother Brain's battle as the game shows it mid-fight, from
    mother_brain_battle's shot (RGB since build 146a)."""
    import shutil
    base = f'{A}/art/scenes'
    for thing in sorted(os.listdir(base)):
        src = f'{base}/{thing}'
        desc_path = f'{src}/{thing}.json'
        if not os.path.isfile(desc_path):
            continue
        with open(desc_path) as fh:
            desc = json.load(fh)
        dst = f'{out}/cutscenes/{thing}'
        os.makedirs(dst, exist_ok=True)
        for name in sorted(os.listdir(src)):
            shutil.copyfile(f'{src}/{name}', f'{dst}/{name}')
        INDEX.append(dict(file=rel(out, dst), kind='picture',
                          size=desc.get('size'),
                          frames=len(desc.get('frames', {})),
                          picture=src, source=desc.get('source', ''),
                          notes='the game\'s own picture, full colour'))

    shot = ('mother_brain_battle', '04_rainbow')
    path = f'route_out/{shot[0]}/{shot[1]}.png'
    if not os.path.isfile(path):
        print(f'no {path}: no mother_brain_battle.png (make routes)',
              file=sys.stderr)
        return
    im = Image.open(path).crop((0, 0, 320, 112))
    save_png(out, f'{out}/cutscenes/mother_brain_battle.png', im,
             'picture', opaque=(im.mode != 'P'), shot=[path],
             notes=('the game\'s own frame, 360 ticks after FIGHT/STGY: '
                    'the star field and her figure under her lights'))


# ------------------------------------------------------------ field sprites

def draw_pieces(pieces, get_tile, line_of):
    """Sprite pieces: w x h tiles, column-major as the VDP lays them."""
    xs, ys = [], []
    for p in pieces:
        xs += [p['x'], p['x'] + p['w'] * 8]
        ys += [p['y'], p['y'] + p['h'] * 8]
    x0, y0 = min(xs), min(ys)
    w, h = max(xs) - x0, max(ys) - y0
    buf = bytearray(w * h)
    missing = 0
    # the first piece is in front: draw back to front
    for p in reversed(pieces):
        for c in range(p['w']):
            for r in range(p['h']):
                t = get_tile(p['tile'] + c * p['h'] + r)
                if t is None:
                    missing += 1
                    continue
                cc = p['w'] - 1 - c if p['flip_h'] else c
                rr = p['h'] - 1 - r if p['flip_v'] else r
                put_tile(buf, w, p['x'] - x0 + cc * 8, p['y'] - y0 + rr * 8,
                         t, line_of(p), p['flip_h'], p['flip_v'])
    return buf, w, h, missing


FACING = ['up_0', 'up_1', 'up_2', 'down_0', 'down_1', 'down_2',
          'left_0', 'left_1', 'left_2', 'right_0', 'right_1', 'right_2']


def field_sprites(out):
    # The party on the map: CharWalkArt, nine 16x32 frames a character,
    # the right-facing ones the left mirrored (RenderCharSprites).
    walk = load_tiles('CharWalkArt')
    TILES = [0x18, 0x20, 0x28, 0x00, 0x08, 0x10, 0x30, 0x38, 0x40,
             0x30, 0x38, 0x40]
    ART = [(0, 0), (1, 1), (4, 0), (2, 0), (5, 1), (3, 0), (5, 0), (2, 1)]
    s = Scene()
    s.palette('field_common', 0)
    for c, name in enumerate(CHARS):
        art, line = ART[c]
        for f in range(12):
            base = art * 72 + TILES[f]
            piece = dict(x=0, y=0, w=2, h=4, tile=0, flip_h=f >= 9,
                         flip_v=False)
            buf, w, h, _ = draw_pieces(
                [piece], lambda k: walk[base + k] if base + k < len(walk)
                else None, lambda p: line)
            save_png(out, f'{out}/field_sprites/party/{c}_{name}/{FACING[f]}.png',
                     image_from(buf, w, h, s), 'field sprite',
                     art=[f'{A}/tiles/CharWalkArt.png tiles {base:#x}+8'],
                     palette=[f'{A}/palettes/field_common.png line {line}'])

    # Everyone else: the object's tile word is its art's VRAM tile, which
    # the level's art list decides; the art is found through a level the
    # object stands in.
    ls = json.load(open(f'{A}/data/level_sprites.json'))
    levels = json.load(open(f'{A}/levels/levels.json'))['levels']
    ptrs = {p['index']: p for p in json.load(open(f'{A}/palettes/paletteptrs.json'))}
    level_pal = {l['index']: l['palette'] for l in levels}
    seen = {}
    for L in ls['levels']:
        for o in L['objects']:
            seen.setdefault(o['object'], (L['level'], L['name'], L['art_list']))

    # Objects no level lists: what places their art (the code, not a list).
    EXTRA = {
        0x1D: ('list', 0),                             # the second young man
        0x2B: ('list', 4), 0x2D: ('list', 4),          # explosions
        0x2C: ('list', 12), 0x45: ('list', 12),        # Noah, Mother Brain's image
        0x44: ('art', [('SpaceshipArt', 0x400)], [('flight_spaceship', 2)]),
        0x07: ('art', [('tyler_spaceship_art', 1)], [('title_screen', 0)]),
        0x08: ('art', [('EndingStarfieldUfoArt', 0x480)], [('title_screen', 0)]),
    }

    def scene_for(obj):
        s = Scene()
        s.palette('field_common', 0)
        where = seen.get(obj)
        extra = EXTRA.get(obj)
        if extra and extra[0] == 'art':
            for art, base in extra[1]:
                s.place(art, base)
            for pal, line in extra[2]:
                s.palette(pal, line)
            return s, None
        lst = where[2] if where else (extra[1] if extra else None)
        if lst is None:
            return None, None
        for a in ls['art_lists'][lst]:
            s.place(f"sprite_{a['art']}", a['tile'])
        lp = level_pal.get(where[0]) if where else None
        if lp is not None and lp in ptrs:
            s.palette(os.path.splitext(ptrs[lp]['file'])[0], 2)
        return s, where

    groups = [('people', json.load(open(f'{A}/data/npc_types.json')), True),
              ('objects', json.load(open(f'{A}/data/scene_objects.json')), False)]
    chests = json.load(open(f'{A}/data/chests.json'))['types']
    groups.append(('chests', chests, False))
    for folder, types, facing in groups:
        for t in types:
            s, where = scene_for(t['object'])
            if s is None:
                print('no art for object', hex(t['object']), t['name'])
                continue
            name = f"{t['object']:02X}_{slug(t['name'])}"
            frames = t['frames']
            done = set()
            for i, pieces in enumerate(frames):
                if not pieces:
                    continue
                key = json.dumps(pieces, sort_keys=True)
                if key in done:
                    continue
                done.add(key)
                if facing and i < 12:
                    label = FACING[i]
                elif folder == 'chests' and i < 2:
                    label = ('closed', 'open')[i]
                else:
                    label = f'frame_{i:02d}'
                buf, w, h, missing = draw_pieces(
                    pieces, lambda k: s.vram.get(t['tile'] + k),
                    lambda p: t['palette_line'])
                save_png(out, f'{out}/field_sprites/{folder}/{name}/{label}.png',
                         image_from(buf, w, h, s), 'field sprite',
                         tile=t['tile'], line=t['palette_line'], **s.sources,
                         level=(where[1] if where else None),
                         notes=(f'{missing} tiles with no art in VRAM'
                                if missing else ''))



# ------------------------------------------------------------- the world
# (build 139a) The levels as whole pictures, their backdrops and animated
# tiles, every text window, the font, the credits' letters and the
# palettes -- the rest of what the game draws, so the dump holds it all.

def tmx_layers(path):
    """A level's Tiled map: (block sheet path, firstgid, {layer: (w, h,
    gids)})."""
    import xml.etree.ElementTree as ET
    root = ET.parse(path).getroot()
    ts = root.find('tileset')
    sheet = os.path.join(os.path.dirname(path), ts.find('image').get('source'))
    layers = {}
    for layer in root.iter('layer'):
        gids = [int(t) for t in layer.find('data').text.replace('\n', '').split(',')
                if t.strip()]
        layers[layer.get('name')] = (int(layer.get('width')),
                                     int(layer.get('height')), gids)
    return sheet, int(ts.get('firstgid')), layers


def draw_blocks(im, sheet, firstgid, w, h, gids, over):
    """Pastes a block map onto `im` (32x32 blocks, the sheet 16 across);
    `over` leaves index 0 alone, as the plane in front shows the one behind
    through its colour 0."""
    cache = {}
    for r in range(h):
        for c in range(w):
            g = gids[r * w + c]
            if g < firstgid:
                continue
            b = g - firstgid
            if b not in cache:
                bx, by = (b % 16) * 32, (b // 16) * 32
                blk = sheet.crop((bx, by, bx + 32, by + 32))
                mask = Image.frombytes('L', (32, 32), bytes(
                    255 if v else 0 for v in blk.tobytes()))
                cache[b] = (blk, mask)
            blk, mask = cache[b]
            im.paste(blk, (c * 32, r * 32), mask if over else None)


def level_scene(L, ptrs):
    s = Scene()
    s.palette('field_common', 0)
    pal = ptrs.get(L['palette'])
    if pal is not None:
        s.palette(os.path.splitext(pal['file'])[0], 2)
    return s


def levels_out(out):
    levels = json.load(open(f'{A}/levels/levels.json'))['levels']
    ptrs = {p['index']: p for p in json.load(open(f'{A}/palettes/paletteptrs.json'))}
    for L in levels:
        s = level_scene(L, ptrs)
        stem = os.path.splitext(L['files']['map'])[0]
        path = f'{A}/levels/{L["files"]["map"]}'
        sheet_path, firstgid, layers = tmx_layers(path)
        sheet = Image.open(sheet_path)
        w, h = L['cols'], L['rows']
        im = Image.new('P', (w * 32, h * 32), 0)
        drawn = []
        for name in ('planeB', 'planeA'):
            if name in layers:
                lw, lh, gids = layers[name]
                draw_blocks(im, sheet, firstgid, lw, lh, gids, name == 'planeA')
                drawn.append(name)
        im.putpalette(s.plte())
        backs = L['files'].get('backdrop') or {}
        note = ('plane B, then plane A over it' if len(drawn) == 2 else
                f'{drawn[0]} only; the other plane is a backdrop'
                if backs else drawn[0])
        save_png(out, f'{out}/levels/{stem}.png', im, 'level map', opaque=True,
                 map=path, blocks=sheet_path, **s.sources, notes=note)
        for plane, f in sorted(backs.items()):
            bpath = f'{A}/levels/{f}'
            bsheet, bfirst, blayers = tmx_layers(bpath)
            lw, lh, gids = next(iter(blayers.values()))
            bim = Image.new('P', (lw * 32, lh * 32), 0)
            draw_blocks(bim, Image.open(bsheet), bfirst, lw, lh, gids, False)
            bim.putpalette(s.plte())
            save_png(out, f'{out}/levels/{stem}_backdrop_{plane}.png', bim,
                     'level backdrop', opaque=True, map=bpath, **s.sources,
                     notes=f'plane {plane}: a 16x8-block map that wraps and '
                           f'scrolls on its own (parallax), so it is not '
                           f'drawn into the level picture')
    # The animated tiles (LevelAnims): each frame's tiles on the line the
    # level's blocks draw them with.
    anims = json.load(open(f'{A}/data/level_anims.json'))['anims']
    by_index = {L['index']: L for L in levels}
    for a in anims:
        L = by_index[a['level']]
        s = level_scene(L, ptrs)
        line = 2
        words = []
        for row in open(f'{A}/levels/{L["files"]["blocks"]}'):
            if not row.startswith('#'):
                words += [int(x, 16) for x in row.split()]
        frames = [load_tiles(os.path.splitext(f['file'])[0]) for f in a['frames']]
        span = range(a['tile'], a['tile'] + len(frames[0]))
        for wd in words:
            if wd & 0x7FF in span:
                line = (wd >> 13) & 3
                break
        stem = os.path.splitext(L['files']['map'])[0]
        for k, tiles in enumerate(frames):
            n = len(tiles)
            buf = bytearray(n * 8 * 8)
            for i, t in enumerate(tiles):
                put_tile(buf, n * 8, i * 8, 0, t, line, 0, 0)
            save_png(out, f'{out}/levels/animated_tiles/{stem}_frame_{k}.png',
                     image_from(buf, n * 8, 8, s), 'level tile animation',
                     tiles=f'VRAM tile {a["tile"]} on, line {line}',
                     period_ticks=a['period'], sequence=a['sequence'],
                     **s.sources)


def windows_out(out):
    """Every text window's map (WinArt_*: menus, lists, the battle's boxes,
    the name input) with the font at $500 on line 0, as WIN_BASE_FONT
    draws them; the battle's in the battle palette, the rest in the field's.
    The portrait windows are portraits/."""
    import glob
    import xml.etree.ElementTree as ET
    for path in sorted(glob.glob(f'{A}/winart/*.tmx')):
        name = os.path.splitext(os.path.basename(path))[0]
        sets = {ts.get('name') for ts in ET.parse(path).getroot().iter('tileset')}
        if sets != {'FontsIconsArt'} or 'Portrait' in name:
            continue        # a portrait's window: portraits/
        battle = name.startswith(('WinArt_Battle', 'WinArt_Enemy'))
        s = Scene()
        s.palette('battle' if battle else 'field_common', 0)
        s.place('FontsIconsArt', 0x500)
        mw, mh, cells = load_winmap(name)
        buf = bytearray(mw * 8 * mh * 8)
        for r in range(mh):
            for c in range(mw):
                t = s.vram.get(0x500 + cells[r * mw + c])
                if t is not None:
                    put_tile(buf, mw * 8, c * 8, r * 8, t, 0, 0, 0)
        save_png(out, f'{out}/windows/{slug(name.replace("WinArt_", ""))}.png',
                 image_from(buf, mw * 8, mh * 8, s), 'window',
                 window_map=path, **s.sources)
    # The sheets the windows and the credits draw from.
    for art, pal, file, note in (
            ('FontsIconsArt', 'field_common', 'font_and_icons',
             'the font and icons, line 0 of the field palette'),
            ('EndingCreditsLettersArt', 'title_screen', 'credits_letters',
             "the credits' capitals, line 0 of the title palette (the "
             "credits fade them in and out)")):
        s = Scene()
        s.palette(pal, 0)
        tiles = load_tiles(art)
        across = 16
        rows = -(-len(tiles) // across)
        buf = bytearray(across * 8 * rows * 8)
        for i, t in enumerate(tiles):
            put_tile(buf, across * 8, (i % across) * 8, (i // across) * 8, t,
                     0, 0, 0)
        save_png(out, f'{out}/windows/{file}.png',
                 image_from(buf, across * 8, rows * 8, s), 'sheet',
                 palette=s.sources['palette'], sheet=sheet_source(art),
                 notes=note)


def palettes_out(out):
    """Every palette the game loads, as a swatch: 16 colours a row, a row a
    palette line, each colour a 16x16 square."""
    import glob
    for path in sorted(glob.glob(f'{A}/palettes/*.png')):
        im = Image.open(path)
        big = im.resize((im.size[0] * 16, im.size[1] * 16), Image.NEAREST)
        name = os.path.basename(path)
        save_png(out, f'{out}/palettes/{name}', big, 'palette', opaque=True,
                 palette=path)

# ------------------------------------------------------- battle captures

def export_anim(out, src, dst_base, kind, loop, **info):
    a = adm.read(src)
    a['frames'] = adm.trim_blank_ends(a['frames'])
    images, ticks = adm.to_images(a)
    if not images:
        return 0
    os.makedirs(os.path.dirname(dst_base), exist_ok=True)
    adm.save_apng(dst_base + '.png', images, ticks, loop=loop)
    adm.save_tiff_stack(dst_base + '.tif', images)
    INDEX.append(dict(file=rel(out, dst_base + '.png'),
                      stack=rel(out, dst_base + '.tif'), kind=kind,
                      size=list(images[0].size), frames=len(images),
                      ticks=ticks, total_ticks=sum(ticks), **info))
    return len(images)


def battle(out):
    bj = json.load(open(f'{A}/data/battle.json'))
    items = {i['id']: i for i in json.load(open(f'{A}/data/items.json'))}
    techs = {t['id']: t for t in json.load(open(f'{A}/data/techniques.json'))['techniques']}
    enemies = {e['id']: e for e in bj['enemies']}
    tabs = bj['anim_tables']

    def label(table, i):
        idx = tabs[table]['index']
        return idx[i] if 0 <= i < len(idx) else None

    mdir = f'{ADM}/monster'
    for name in sorted(os.listdir(mdir)) if os.path.isdir(mdir) else []:
        eid = int(name.split('_')[0])
        e = enemies[eid]
        for anim, table in (('idle', 'idle'), ('attack', 'enemy_attack'),
                            ('technique', 'enemy_tech')):
            src = f'{mdir}/{name}/{anim}.adm'
            if os.path.exists(src):
                export_anim(out, src, f'{out}/monsters/{name}/{anim}', 'monster',
                            loop=True, enemy=e['name'], enemy_id=eid,
                            art=f"{A}/tiles/{e['art_file']}.png",
                            map=e['map'], script=label(table, e['anim']),
                            palette='PalettePtrs battle, the enemy\'s 15 '
                                    'colours on its group\'s line')

    for sub, kind in (('effect_weapon', 'weapon effect'),
                      ('effect_technique', 'technique effect')):
        d = f'{ADM}/{sub}'
        for f in sorted(os.listdir(d)) if os.path.isdir(d) else []:
            n = f[:-4]
            info = {}
            num = int(n.split('_')[0])
            if sub == 'effect_weapon':
                p = bj['weapon_props'][num]
                info = dict(weapon=p['label'], map=p['map'], map2=p['map2'])
                folder = 'weapons'
            else:
                p = bj['tech_props'][num - 1]
                info = dict(technique=techs[num]['name'], map=p['map'],
                            map2=p['map2'])
                folder = 'techniques'
            export_anim(out, f'{d}/{f}', f'{out}/effects/{folder}/{n}', kind,
                        loop=True, **info)

    pdir = f'{ADM}/party'
    for c in sorted(os.listdir(pdir)) if os.path.isdir(pdir) else []:
        ci = int(c)
        for f in sorted(os.listdir(f'{pdir}/{c}')):
            n = f[:-4]
            is_tech = os.path.exists(f'{ADM}/effect_technique/{f}')
            sub = 'techniques' if is_tech else 'attacks'
            export_anim(out, f'{pdir}/{c}/{f}',
                        f'{out}/party_battle/{ci}_{CHARS[ci]}/{sub}/{n}',
                        'party action', loop=True, character=CHARS[ci])


README = """PHANTASY STAR II -- the art, from the port (build 127; levels,
windows and palettes added in build 139a)

Everything is at native pixel size (1 image pixel = 1 Mega Drive pixel).
Every image is an indexed PNG carrying the palette it is drawn with --
the four 16-colour lines as the game loads them, the colours through the
port's measured DAC ladder -- so it opens in colour and stays editable as
indexed art. Index 0 is transparent.

Animations (monsters/, effects/, party_battle/) come in two forms, the
same frames in the same order:
  NAME.png  an animated PNG (APNG) with the real frame timing -- open it
            in a web browser (Firefox, Chrome) to watch it play; plain
            image viewers show the first frame
  NAME.tif  a multi-page TIFF stack -- Fiji/ImageJ opens it as a stack
            to step through frame by frame
They were recorded by running the port's own battle objects (the code the
fight runs), so the order, the timing, an enemy's extra pieces and the
effects it throws are the game's. Frames that stay up for several ticks
are one frame with a longer duration; index.json lists every frame's
ticks (1 tick = 1/59.92 s).

  monsters/NNN_name/          idle (the standby loop, once round),
                              attack, technique (when it has one)
  effects/weapons/            each weapon's swing / shot, effect only
  effects/techniques/         each technique's effect, effect only
  party_battle/N_name/        the character doing it, with the effect
  portraits/                  the portrait pictures (without the
                              window frame), by their slot in the game's
                              two portrait tables: 22 is the Central
                              Tower view with the man who runs up at
                              "Something's wrong..."; 25 and 28 pair the
                              tower's art with window maps not its own
                              and are never opened, so they are drawn
                              with the view's own map in their palettes
                              (palette variants); 29 is 28 again
                              (index.json says which)
  cutscenes/                  the full-screen pictures, each in the
                              palette the game shows it in: taken from a
                              frame of the game itself where it loads,
                              fades or cycles its colours (the dreams,
                              Lutz, Tyler, the ending, Mother Brain's
                              lights); mother_brain_starfield is the stars
                              behind her, mother_brain_battle the scene
                              with her figure
  field_sprites/party/        the party on the map, by facing and step
  field_sprites/people/       townspeople types, by facing and step
  field_sprites/objects/      scene objects' frames
  field_sprites/chests/       treasure chests, closed and open
  levels/NN_Name.png          (139a) every level whole, in its palette:
                              plane B with plane A over it; a dungeon's
                              plane A is a backdrop, written beside it as
                              NN_Name_backdrop_A.png (it wraps and scrolls
                              on its own, so it is not drawn in)
  levels/animated_tiles/      the water's frames (Motavia, the Underwater
                              Passage)
  windows/                    every text window (menus, lists, the
                              battle's boxes, the name input) as the game
                              draws it, the font sheet and the credits'
                              letters
  palettes/                   every palette the game loads, a swatch:
                              a row a palette line
  levels/                     (build 139a) every level as one picture,
                              plane B with plane A over it, in its
                              palette; NN_Name_backdrop_A/B the 16x8-block
                              backdrops dungeons scroll on their own
                              (parallax), so not drawn into the level;
                              animated_tiles/ the water's frames
  windows/                    (build 139a) every text window (menus,
                              lists, the battle's boxes, the name input)
                              with the font, as the game draws it; the
                              font and icons, the credits' capitals
  palettes/                   (build 139a) every palette the game loads,
                              16x16 squares, a row a palette line
  levels/                     (139a) every level as one picture: plane B
                              with plane A over it, field palette on lines
                              0-1 and the level's on 2-3; a dungeon's
                              plane A is a wrapping backdrop that scrolls
                              on its own, so it is NN_name_backdrop_A.png
                              beside it; animated_tiles/ the water's frames
  windows/                    (139a) every text window (menus, lists, the
                              battle's boxes, name input), the font and
                              icons, the credits' capitals
  palettes/                   (139a) every palette, a 16x16 square a colour
  index.json                  every file: its source art, palette,
                              map, script and frame timing

This is an export to look at: editing these files does not change the
game, which still reads the tile sheets, maps and palettes under assets/.

Not applied (static palette): the Sega logo's colour cycle, the Nei
Sword's glow, a hit's red flash, a level's colour cycles (the towers'
alarm). The levels are drawn as they load, before any map patch a story
flag puts in (the dams, the roads opened). Mother Brain's lights are one moment of
their run (360 ticks into her battle).
"""


def main():
    global ADM, ROM
    if len(sys.argv) != 4:
        print(__doc__)
        return 2
    ROM, ADM, out = sys.argv[1], sys.argv[2], sys.argv[3]
    os.makedirs(out, exist_ok=True)
    battle(out)
    portraits(out)
    pictures(out)
    field_sprites(out)
    levels_out(out)
    windows_out(out)
    palettes_out(out)
    with open(f'{out}/index.json', 'w') as fh:
        json.dump(INDEX, fh, indent=1)
    with open(f'{out}/README.txt', 'w') as fh:
        fh.write(README)
    kinds = {}
    for e in INDEX:
        kinds[e['kind']] = kinds.get(e['kind'], 0) + 1
    print(kinds)
    for e in INDEX:
        if e.get('notes'):
            print('note:', e['file'], e['notes'])
    return 0


if __name__ == '__main__':
    sys.exit(main())
