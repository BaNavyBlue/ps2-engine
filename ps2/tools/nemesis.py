"""Decoder for Nemesis compression (NemDec, ps2.asm:8830).

This is a second, unrelated codec to the one in `ps2art.py`. The two are
used side by side: every level loads its tile *graphics* through NemDec and
its block definitions and block maps through DecompressArt2. Anything that
only knew the art format would decode a level's maps and then fail on its
tileset, which is what the `*_tile_ind.bin` note in the README was really
about.

Format
------

    header : u16be   bit 15  XOR mode
                     bits 0-14  tile count; output is count * 8 longs

    code table, terminated by $FF:
        a byte >= $80 opens a new palette-index group and is the index
        a byte <  $80 is an entry in the open group:
            bits 4-6   repeat count, minus one
            bits 0-3   code length in bits
        followed by one byte holding the code itself, left-aligned.
        The entry is replicated over every 8-bit prefix the code covers,
        so lookup is a flat 256-entry table indexed by the next 8 bits.

    bitstream, MSB first:
        a prefix of six set bits ($FC..$FF) introduces a 7-bit inline
        symbol -- 3 bits of repeat count, 4 bits of palette index --
        for pixels too rare to have earned a code.
        anything else indexes the code table.

Symbols emit `repeat + 1` nibbles. Eight nibbles make a long, which is one
8-pixel row. In XOR mode each long is XORed into a running accumulator
before it is stored, so a row is a delta against the row before it -- and
the accumulator runs across tile boundaries, never reset.

`NemDec` writes to the VDP data port with auto-increment, so the two write
routines (`WriteAndStay`, `WriteAndAdvance`) differ only in whether the
destination pointer moves; from here both are a flat sequential stream.
"""

TILE_BYTES = 32


class NemesisError(Exception):
    pass


def _build_table(data, pos):
    """Flat 256-entry table: high byte = code length, low byte = (repeat<<4)|index."""
    table = [0] * 256
    if pos >= len(data):
        raise NemesisError("code table runs off the end")

    d0 = data[pos]
    pos += 1
    while d0 != 0xFF:
        group = d0
        while True:
            if pos >= len(data):
                raise NemesisError("unterminated code table")
            d0 = data[pos]
            pos += 1
            if d0 >= 0x80:
                break                      # next group, d0 is its index
            entry = (group & 0x0F) | (d0 & 0x70) | ((d0 & 0x0F) << 8)
            length = d0 & 0x0F
            code = data[pos]
            pos += 1
            shift = 8 - length
            if shift == 0:
                table[code] = entry
            else:
                base = code << shift
                for k in range(1 << shift):
                    table[base + k] = entry
    return table, pos


def decompress(data, offset=0):
    """Decode a Nemesis stream into 32-byte tiles.

    Returns (tiles, consumed_bytes) to match ps2art.decompress.
    """
    pos = offset
    header = int.from_bytes(data[pos:pos + 2], "big")
    pos += 2
    xor = bool(header & 0x8000)
    count = header & 0x7FFF
    longs_left = count * 8

    table, pos = _build_table(data, pos)

    window = (data[pos] << 8) | data[pos + 1]
    pos += 2
    bits = 16

    def refill():
        nonlocal window, bits, pos
        if bits < 9:
            if pos >= len(data):
                raise NemesisError("bitstream truncated")
            bits += 8
            window = ((window << 8) | data[pos]) & 0xFFFF
            pos += 1

    out = bytearray()
    acc = 0          # nibbles accumulated toward the current long
    nibbles = 8      # how many still wanted
    running = 0      # the XOR accumulator

    while longs_left:
        prefix = (window >> (bits - 8)) & 0xFF
        if prefix >= 0xFC:
            bits -= 6
            refill()
            bits -= 7
            symbol = window >> bits
            index = symbol & 0x0F
            repeat = (symbol & 0x70) >> 4
            refill()
        else:
            entry = table[prefix]
            length = (entry >> 8) & 0xFF
            if length == 0:
                raise NemesisError(f"no code for prefix {prefix:#04x}")
            bits -= length
            refill()
            index = entry & 0x0F
            repeat = (entry & 0xF0) >> 4

        for _ in range(repeat + 1):
            acc = ((acc << 4) | index) & 0xFFFFFFFF
            nibbles -= 1
            if nibbles:
                continue
            if xor:
                running ^= acc
                out += running.to_bytes(4, "big")
            else:
                out += acc.to_bytes(4, "big")
            acc = 0
            nibbles = 8
            longs_left -= 1
            if not longs_left:
                break

    tiles = [bytes(out[i:i + TILE_BYTES])
             for i in range(0, len(out), TILE_BYTES)]
    return tiles, pos - offset
