"""Decoder for the art compression used by DecompressArt / DecompressArt2
(ps2.asm lines 8600 and 8658).

The format emits one 32-byte tile at a time. Each tile is:

    count : u8      0x00        -> whole tile is literal bytes
                    0x01..0x7F  -> that many run groups follow
                    0x80..0xFF  -> end of stream
    run group, repeated `count` times:
        value : u8      byte to store
        mask  : u32be   one bit per byte of the tile, MSB first;
                        a set bit stores `value` at that offset
    literals:
        one byte for every bit still clear in the OR of all the masks

Groups may overlap; a later group wins. Between the masks and the literals
every one of the 32 positions is covered exactly once, so the tile buffer
needs no clearing.
"""

TILE_BYTES = 32


class ArtError(Exception):
    pass


def decompress(data, offset=0):
    """Decode a stream into a list of 32-byte tiles.

    Returns (tiles, consumed_bytes).
    """
    pos = offset
    tiles = []

    while True:
        if pos >= len(data):
            raise ArtError(f"ran off the end at {pos:#x}")

        count = data[pos]
        pos += 1

        if count >= 0x80:
            break

        tile = bytearray(TILE_BYTES)
        covered = 0

        for _ in range(count):
            if pos + 5 > len(data):
                raise ArtError(f"truncated run group at {pos:#x}")
            value = data[pos]
            mask = int.from_bytes(data[pos + 1:pos + 5], "big")
            pos += 5
            covered |= mask

            for i in range(TILE_BYTES):
                if mask & (1 << (31 - i)):
                    tile[i] = value

        for i in range(TILE_BYTES):
            if not (covered & (1 << (31 - i))):
                if pos >= len(data):
                    raise ArtError(f"truncated literals at {pos:#x}")
                tile[i] = data[pos]
                pos += 1

        tiles.append(bytes(tile))

    return tiles, pos - offset


def tile_to_indices(tile):
    """32 packed bytes -> 64 palette indices, high nibble first."""
    px = []
    for b in tile:
        px.append(b >> 4)
        px.append(b & 0x0F)
    return px
