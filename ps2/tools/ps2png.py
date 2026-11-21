"""Writing the asset formats: indexed PNG for anything made of pixels.

PNG colour type 3 stores an index plane plus a palette, which is what the
hardware stored. One file carries both, it opens in any image editor, the
palette is editable in the same window as the art, and the index plane
loads with no unpacking and no colour conversion.

Three shapes are used here:

    tile sheet    a grid of 8x8 tiles, 16 across, row-major; the tile
                  order is the tileset's own, so a nametable entry or a
                  block definition indexes the sheet directly
    flat image    art that is a rectangle, assembled from its tiles at
                  extraction time -- portraits and the like
    palette strip 16 wide, one row per palette line, pixel (x, y) holding
                  index y * 16 + x, so the image is the palette
    colour ramp   (build 136) a table of CRAM words a routine copies into
                  the live palette piece by piece; a row as wide as the
                  table's rows, pixel i holding index i, so the picture is
                  the ramp (the game reads each pixel's colour)

The CRAM word is three bits per channel. What lands in the PNG is that
run through the measured DAC ladder, and `cram_from_rgb8` in the C side
inverts it by nearest level. The eight levels are far enough apart that
the round trip is exact.
"""

from PIL import Image

TILE_W = TILE_H = 8
SHEET_TILES = 16          # tiles across a tile sheet

# Measured VA4 DAC output for the eight levels a 3-bit CRAM channel can
# hold. Not a linear ramp, which is why fades band the way they do.
DAC_LEVELS = [0, 52, 87, 116, 144, 172, 206, 255]


def cram_to_rgb(word):
    """CRAM word (0000 BBB0 GGG0 RRR0) to 8-bit RGB."""
    return (DAC_LEVELS[(word >> 1) & 7],
            DAC_LEVELS[(word >> 5) & 7],
            DAC_LEVELS[(word >> 9) & 7])


def grey_ramp(n=16):
    """Stand-in for art whose palette has not been identified yet."""
    return [(i * 255 // (n - 1),) * 3 for i in range(n)]


def _put_palette(img, rgb):
    flat = []
    for c in rgb:
        flat += list(c)
    flat += [0] * (768 - len(flat))
    img.putpalette(flat)


def tile_indices(tile):
    """32 packed bytes to 64 palette indices, high nibble first."""
    px = []
    for b in tile:
        px.append(b >> 4)
        px.append(b & 0x0F)
    return px


def write_tile_sheet(path, tiles, rgb, across=SHEET_TILES,
                     packed=True):
    """A grid of 8x8 tiles in tileset order.

    The grid is padded to a full last row. Padding tiles are all index 0,
    which is transparent, so a tile count rounded up to the grid costs
    nothing: nothing ever references them.
    """
    across = max(1, min(across, len(tiles) or 1))
    rows = (len(tiles) + across - 1) // across

    img = Image.new("P", (across * TILE_W, rows * TILE_H), 0)
    _put_palette(img, rgb)

    px = img.load()
    for t, tile in enumerate(tiles):
        ox, oy = (t % across) * TILE_W, (t // across) * TILE_H
        for i, v in enumerate(tile_indices(tile) if packed else tile):
            px[ox + i % TILE_W, oy + i // TILE_W] = v
    img.save(path, optimize=True)
    return across, rows


def write_flat(path, width, height, indices, rgb):
    """Art that is a rectangle, already assembled."""
    img = Image.new("P", (width, height), 0)
    _put_palette(img, rgb)
    px = img.load()
    for i, v in enumerate(indices):
        px[i % width, i // width] = v
    img.save(path, optimize=True)


def write_palette(path, words, per_line=16):
    """A palette as an image: 16 wide, one row per line.

    Editing a colour in an image editor edits the PLTE, which is the
    palette. No separate container and nothing to convert on load.
    """
    rgb = [cram_to_rgb(w) for w in words]
    rows = (len(words) + per_line - 1) // per_line

    img = Image.new("P", (per_line, rows), 0)
    _put_palette(img, rgb + [(0, 0, 0)] * (256 - len(rgb)))
    px = img.load()
    for i in range(len(words)):
        px[i % per_line, i // per_line] = i
    img.save(path, optimize=True)


def write_ramp(path, words, across):
    """(build 136) A colour ramp as an image: `across` colours a row, pixel
    (x, y) holding index y * across + x, whose PLTE entry is that word's
    colour -- so the picture is the ramp, top row first. table_load_ramp
    reads each pixel's colour, in reading order, back to a word.

    Written here rather than through write_palette, which tools/roundtrip.py
    records as a 16-wide palette strip.
    """
    if not words or len(words) % across:
        raise ValueError(f"{path}: {len(words)} words do not fill rows "
                         f"of {across}")
    rgb = [cram_to_rgb(w) for w in words]
    img = Image.new("P", (across, len(words) // across), 0)
    _put_palette(img, rgb + [(0, 0, 0)] * (256 - len(rgb)))
    px = img.load()
    for i in range(len(words)):
        px[i % across, i // across] = i
    img.save(path, optimize=True)
