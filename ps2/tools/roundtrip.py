#!/usr/bin/env python3
"""(build 130a) The asset round-trip: are the files under assets/ the ROM's
data, and do the conversions lose anything?

    python3 tools/roundtrip.py --rom ROM --asm PS2_ASM --const PS2_CONST \
        --art ART_DIR
    make roundtrip ROM=image.bin   # the same, with the Makefile's paths

(build 185) The extractors read the cartridge image, ROM, so level 1 runs
them on it; level 2's ROM side is still assembled from the disassembly, so
this tool needs both, and is the private tree's alone.

Two levels, reported to stdout and route_out/roundtrip/report.txt:

Level 1, re-extraction. Every extractor in `make extract` (the Makefile's
EXTRACTORS, their recipes read from the Makefile) runs again, in this
process, into a scratch tree, and every file it writes is compared with
assets/ byte for byte. A difference means assets/ is not what the ROM gives
today: stale, or edited by hand. Files under assets/ that no extractor
writes are listed (the icon, the hand-written patch file; the drum image
when the Z80 drum file is missing). The manifest is checked against
make_manifest.py's rule applied to assets/ itself.

Level 2, packing back. While the extractors run, their writers are wrapped
so each converted file is paired with the ROM bytes it was made from: the
tile sheets and flat pictures (ps2png.write_tile_sheet, write_flat), the
palettes (write_palette), the block definitions and Tiled maps
(extract_levels' write_blocks_text, write_tmx), (build 134) the window maps
(extract_winart's write_winart_tmx), (build 135) the screen nametables
(extract_nametables' write_nametable_tmx), the collision tables
(extract_collision's write_table), (build 136) the colour ramps
(ps2png.write_ramp) and (build 136b) the drums (extract_drums'
write_drum_set: drums.json and its WAVs packed back into the Z80 RAM image
and drum table, as drums_load reads them) and (build 136e) the credits
(extract_tables' write_credits: the text packed back into the credits
writer's bytes, as credits_load reads it). Each checked-in file is then read the
way the game reads it and packed back into the ROM's layout -- 4-bit tile
pairs, CRAM words by the DAC ladder, big-endian nametable words, block ids,
collision bits -- and compared with those bytes. The scripts' ROM side does
not come from their extractor: the text is assembled here from ps2.asm by
the character map in force at its line (the CHARSET directives), and the
file's text is encoded the way script.c and window.c read it. A difference
at level 2 with none at level 1 means the conversion loses something.

(build 131) The JSON tables: tools/roundtrip_json.py reads each file the
way its C loader does (the first of two equal keys, a loader's fallback for
a missing or mistyped field, a field cut to its width, names looked up
where the game looks them up) and packs it into the ROM table's layout; the
ROM side is assembled fresh from ps2.asm and the constants by
tools/asmdata.py, symbols and all (ItemID_, id(), nametxt, charset). A
pointer compares by the label it names; (131a) an object type's init
operands are read fresh from its init and laid out beside its mappings.
A ROM byte the game never reads is
listed as not packed; a difference read in the ROM and found harmless is in
roundtrip_json.CHECKED, printed as a note. The report lists, per file, the
fields packed and the fields no loader reads. `--prove-edits` (`make
roundtrip-edits`) changes one field a table on a scratch copy and checks
both levels fail.

What neither level can see: a decoder that reads the wrong bytes out of the
ROM (the extractors' own checks and the routes answer for those). (build
131c) Every JSON file is packed or listed as level 1 only -- the port's
own (flag names, Q's patches) or read by no loader (the per-level .json,
songs.json, nametables.json, palettes.json, winart.json); a new one would
be named at the end of the report. The binary
containers (tables/*.bin, music/*.bin, as build 130a found them) hold the
ROM's bytes behind a header: level 1 covers them. (build 134) The window maps' .map containers
are Tiled maps now, packed at level 2; (build 135) so are the screen
nametables' .nam containers; (build 136) the colour ramps are pictures,
packed at level 2 as table_load_ramp reads them; (builds 136a, 136c, 136d)
the dissolve order, the note frequencies and the FM instruments with their
register order are JSON, packed by roundtrip_json.py; (build 136e) the
credits are text, assets/script/ending_credits.txt, packed at level 2 as
credits_load reads them; (build 137) the songs, effects and the sound
driver's tables are text, assets/music/sounds.txt and the files beside it,
packed at level 2 as tools/song_text.py builds them (the game's songtext.c
reads them the same way, 137a). No container is left. (build 139b) The
tracker files (extract_music's write_trackers, songs/<song>.trk, which the
game reads in place of the song's stream text): each read as the game
reads it -- tracker.py's game_music, the song alone from its .trk -- and
what it plays, every channel's cells (struck, held, cut, the changes)
unrolled through its loop over the song's span (tracker.py's timeline),
compared with what the ROM's image plays.
"""

import argparse
import bisect
import contextlib
import importlib
import io
import json
import os
import re
import shlex
import struct
import sys
import tempfile
import xml.etree.ElementTree as ET
import zlib
from pathlib import Path

TOOLS = Path(__file__).resolve().parent
TREE = TOOLS.parent
sys.path.insert(0, str(TOOLS))

import ps2png  # noqa: E402
from ps2asm import Asm  # noqa: E402
from make_manifest import read_by_game  # noqa: E402

# ---------------------------------------------------------------------------
# The extractors, as the Makefile runs them


def makefile_runs(makefile, paths):
    """[(name, script, argv)] for each of EXTRACTORS in order, from the
    recipes; the Makefile's variables replaced by `paths`."""
    text = makefile.read_text().replace("\\\n", " ")
    m = re.search(r"^EXTRACTORS\s*:=\s*(.*)$", text, re.M)
    names = m.group(1).split()
    runs = []
    for name in names:
        rule = re.search(rf"^extract-{re.escape(name)}:.*\n((?:\t.*\n)+)",
                         text, re.M)
        if rule is None:
            raise SystemExit(f"roundtrip: no extract-{name} rule in the "
                             f"Makefile")
        cmd = re.search(r"python3 (tools/\S+\.py)([^;\n]*)", rule.group(1))
        args = cmd.group(2)
        for var, value in paths.items():
            args = args.replace(f"$({var})", value)
        runs.append((name, cmd.group(1), shlex.split(args)))
    return runs


class Recorder:
    """The converted files' ROM bytes, by path under assets/."""

    def __init__(self):
        self.items = {}

    def put(self, path, kind, data):
        rel = Path(path).as_posix()
        rel = rel[rel.index("assets/") + len("assets/"):]
        self.items[rel] = (kind, data)


def wrap(module, attr, before):
    original = getattr(module, attr)

    def wrapped(*args, **kwargs):
        before(*args, **kwargs)
        return original(*args, **kwargs)

    setattr(module, attr, wrapped)
    return original


def record_trackers(rec, extract_music, outdir):
    """(build 139b) write_trackers' songs, each with the image (a
    song_text.Built) of the stream text just written, the ROM's."""
    import song_text
    b = song_text.assemble(outdir, drums_json=extract_music.drum_names())
    for name in b.songs:
        rec.put(Path(outdir) / "songs" / f"{name}.trk", "tracker", (b, name))


def run_extractors(runs, scratch, rec):
    """Runs each extractor's main() with cwd at the scratch tree."""
    log = io.StringIO()
    skipped = []
    here = Path.cwd()
    os.chdir(scratch)
    saved = {}
    try:
        saved["tile"] = wrap(ps2png, "write_tile_sheet",
                             lambda path, tiles, rgb, across=16, packed=True:
                             rec.put(path, "tiles", ([bytes(t) for t in tiles],
                                                     packed)))
        saved["flat"] = wrap(ps2png, "write_flat",
                             lambda path, w, h, indices, rgb:
                             rec.put(path, "flat", (w, h, bytes(indices))))
        saved["pal"] = wrap(ps2png, "write_palette",
                            lambda path, words, per_line=16:
                            rec.put(path, "palette", list(words)))
        # (build 136) the colour ramps: the ROM's words
        saved["ramp"] = wrap(ps2png, "write_ramp",
                             lambda path, words, across:
                             rec.put(path, "ramp", list(words)))
        for name, script, argv in runs:
            first = Path(argv[0]) if argv else None
            if first is not None and not first.exists():
                skipped.append(f"{name}: {first} not found")
                continue
            mod = importlib.import_module(Path(script).stem)
            if mod.__name__ == "extract_levels":
                wrap(mod, "write_blocks_text",
                     lambda path, defs, tileset:
                     rec.put(path, "blocks", bytes(defs)))
                wrap(mod, "write_tmx",
                     lambda path, tileset, count, across, down, layers,
                     props=None:
                     rec.put(path, "tmx", [(l[0], l[1], l[2], bytes(l[3]))
                                           for l in layers]))
            if mod.__name__ == "extract_winart":
                # (build 134) the window maps: the ROM's bytes as parsed
                wrap(mod, "write_winart_tmx",
                     lambda path, width, height, data, tilesets, row_sets:
                     rec.put(path, "wintmx", (width, height, bytes(data))))
            if mod.__name__ == "extract_nametables":
                # (build 135) the screen nametables: the ROM's words
                wrap(mod, "write_nametable_tmx",
                     lambda path, width, height, words, sheets, assets:
                     rec.put(path, "namtmx", (width, height, list(words))))
            if mod.__name__ == "extract_drums":
                # (build 136b) the drums: the Z80 RAM image and drum table
                # the ROM's file makes
                wrap(mod, "write_drum_set",
                     lambda outdir, image, samples, drums:
                     rec.put(Path(outdir) / "drums.json", "drums",
                             (bytes(image),
                              [(b, d, a, ln) for b, _n, s, ln, d in drums
                               for nm, a, _x in samples if nm == s])))
            if mod.__name__ == "extract_tables":
                # (build 136e) the credits: the label's bytes as read
                wrap(mod, "write_credits",
                     lambda path, data:
                     rec.put(path, "credits", bytes(data)))
            if mod.__name__ == "extract_music":
                # (build 137) the songs, effects and tables as text: the
                # driver's whole image, as extract_music built it
                wrap(mod, "write_song_text",
                     lambda outdir, files, image:
                     rec.put(Path(outdir) / "sounds.txt", "songs",
                             bytes(image)))
                # (build 139b) the tracker files: each song as the image
                # of the stream text it is exported from plays it
                wrap(mod, "write_trackers",
                     lambda outdir, image, m=mod:
                     record_trackers(rec, m, outdir))
            if mod.__name__ == "extract_collision":
                wrap(mod, "write_table",
                     lambda path, title, table:
                     rec.put(path, "collision", bytes(table)))
            sys.argv = [script] + argv
            with contextlib.redirect_stdout(log), \
                    contextlib.redirect_stderr(log):
                mod.main()
    finally:
        ps2png.write_tile_sheet = saved.get("tile", ps2png.write_tile_sheet)
        ps2png.write_flat = saved.get("flat", ps2png.write_flat)
        ps2png.write_palette = saved.get("pal", ps2png.write_palette)
        ps2png.write_ramp = saved.get("ramp", ps2png.write_ramp)
        os.chdir(here)
    return skipped, log.getvalue()


# ---------------------------------------------------------------------------
# Reading the checked-in files the way the game does


def png_chunks(path):
    """IHDR's size and depth, PLTE's colours, the index plane (8 bits a
    pixel) -- the indexed PNGs core/png.c reads: colour type 3, depths 1,
    2, 4 and 8, not interlaced."""
    data = path.read_bytes()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError("not a PNG")
    pos, idat, plte, head = 8, b"", [], None
    while pos < len(data):
        length, kind = struct.unpack(">I4s", data[pos:pos + 8])
        body = data[pos + 8:pos + 8 + length]
        pos += 12 + length
        if kind == b"IHDR":
            head = struct.unpack(">IIBBBBB", body)
        elif kind == b"PLTE":
            plte = [tuple(body[i:i + 3]) for i in range(0, len(body), 3)]
        elif kind == b"IDAT":
            idat += body
        elif kind == b"IEND":
            break
    width, height, depth, ctype, _, _, interlace = head
    if ctype != 3 or interlace != 0 or depth not in (1, 2, 4, 8):
        raise ValueError(f"colour type {ctype}, depth {depth}, interlace "
                         f"{interlace}: not what the game reads")
    raw = zlib.decompress(idat)
    stride = (width * depth + 7) // 8
    rows, prev, pos = [], bytearray(stride), 0
    for _ in range(height):
        ftype, line = raw[pos], bytearray(raw[pos + 1:pos + 1 + stride])
        pos += 1 + stride
        bpp = 1
        for i in range(stride):
            a = line[i - bpp] if i >= bpp else 0
            b = prev[i]
            c = prev[i - bpp] if i >= bpp else 0
            if ftype == 1:
                line[i] = (line[i] + a) & 0xFF
            elif ftype == 2:
                line[i] = (line[i] + b) & 0xFF
            elif ftype == 3:
                line[i] = (line[i] + (a + b) // 2) & 0xFF
            elif ftype == 4:
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                pred = a if pa <= pb and pa <= pc else (b if pb <= pc else c)
                line[i] = (line[i] + pred) & 0xFF
        prev = line
        per = 8 // depth
        px = bytearray(width)
        for x in range(width):
            byte = line[x // per]
            shift = 8 - depth * (x % per + 1)
            px[x] = (byte >> shift) & ((1 << depth) - 1)
        rows.append(bytes(px))
    return width, height, plte, b"".join(rows)


def pack_tiles(path):
    """A tile sheet as the tileset loader takes it: 8x8 tiles row-major
    across the image, each to 32 bytes, the high nibble first."""
    width, height, _, px = png_chunks(path)
    if width % 8 or height % 8:
        raise ValueError(f"{width}x{height} is not whole tiles")
    if max(px, default=0) > 15:
        raise ValueError(f"index {max(px)} above 15")
    out = []
    for ty in range(height // 8):
        for tx in range(width // 8):
            tile = bytearray()
            for y in range(8):
                row = px[(ty * 8 + y) * width + tx * 8:][:8]
                for x in range(0, 8, 2):
                    tile.append(row[x] << 4 | row[x + 1])
            out.append(bytes(tile))
    return out


DAC_LADDER = ps2png.DAC_LEVELS


def ladder_level(v):
    """palette.c's ladder_level: the nearest of the eight, the first on a
    tie."""
    best, best_d = 0, 256
    for i, level in enumerate(DAC_LADDER):
        if abs(v - level) < best_d:
            best, best_d = i, abs(v - level)
    return best


# A CRAM word is 0000 BBB0 GGG0 RRR0; a ROM word with any other bit set
# cannot go through a PNG. Colour RAM drops those bits, but the fades
# (PaletteFadeIn, ps2.asm:9034) compare and step whole words, so a stray bit
# changes a fade's path wherever one runs over it. Each such word is checked
# against how its palette is used, and listed here once it has been.
CRAM_BITS = 0x0EEE
STRAY_BITS_CHECKED = {
    # $05CC: bit 8 over blue 2, green 6, red 6. The ending's fifth page
    # goes straight into the live palette by the page routine
    # (PaletteLoad2, ps2.asm:10907 and 11064), and the sixth page's
    # replaces that line before the fade to white: nothing fades it, so
    # the bit is never seen (build 130a).
    ("palettes/ending_page_5.png", 13): 0x05CC,
    # (build 136) $0100: bit 8 over black, in the credits' fade's eighth
    # row (loc_6B10). loc_6AE0 copies the row straight into the live
    # palette (line 3, entries 9-12) and the next row replaces it eight
    # steps later; nothing in the credits' loop (loc_7824: RunObjects,
    # BuildSprites, the fade, the scroll, the writer; VBlankSub_14: pads,
    # DMA, windows) fades or compares it, and the fade ends before the
    # credits can (the UFO starts on its last row): shown only, as black.
    ("palettes/ending_credits_fade.png", 29): 0x0100,
}


def pack_palette(path):
    """palette_load: the PLTE's first height x 16 entries (no more than it
    has), each to a CRAM word by the nearest DAC level."""
    _, height, plte, _ = png_chunks(path)
    count = min(len(plte), height * 16)
    return [(ladder_level(b) << 9) | (ladder_level(g) << 5)
            | (ladder_level(r) << 1) for r, g, b in plte[:count]]


TABLE_MAX = 512     # core/table.h


def pack_ramp(path):
    """(build 136) table_load_ramp: each pixel in reading order, its PLTE
    colour to a CRAM word by the nearest DAC level; a pixel past the PLTE,
    or more than TABLE_MAX of them, refused."""
    width, height, plte, px = png_chunks(path)
    if width * height > TABLE_MAX:
        raise ValueError(f"{width * height} colours, more than {TABLE_MAX}")
    words = []
    for i, ix in enumerate(px):
        if ix >= len(plte):
            raise ValueError(f"pixel ({i % width}, {i // width}) is colour "
                             f"{ix}, past the palette's {len(plte)}")
        r, g, b = plte[ix]
        words.append((ladder_level(b) << 9) | (ladder_level(g) << 5)
                     | (ladder_level(r) << 1))
    return words


DRUM_Z80_RAM, DRUM_MAX, DRUM_SAMPLES_MAX = 0x2000, 8, 16   # audio/drums.[ch]
DRUM_PROGRAM_END = 0x118


def jnum(obj, key, fallback):
    """json_int: a number, truncated; else the fallback."""
    v = obj.get(key) if isinstance(obj, dict) else None
    ok = isinstance(v, (int, float)) and not isinstance(v, bool)
    return int(v) if ok else fallback


def jtext(obj, key):
    """json_str: a string, else None."""
    v = obj.get(key) if isinstance(obj, dict) else None
    return v if isinstance(v, str) else None


def wav_pcm8(path):
    """drums.c wav_pcm8: RIFF/WAVE, "fmt " PCM mono 8-bit, then "data"."""
    b = path.read_bytes()
    if len(b) < 12 or b[:4] != b"RIFF" or b[8:12] != b"WAVE":
        raise ValueError(f"{path.name} is not a WAV")
    pos, fmt_ok = 12, False
    while pos + 8 <= len(b):
        size = struct.unpack("<I", b[pos + 4:pos + 8])[0]
        body = b[pos + 8:pos + 8 + size]
        if size > len(b) - pos - 8:
            raise ValueError(f"{path.name}: a chunk runs past the file's end")
        if b[pos:pos + 4] == b"fmt ":
            fmt, ch = struct.unpack("<HH", body[:4]) if size >= 4 else (0, 0)
            bits = struct.unpack("<H", body[14:16])[0] if size >= 16 else 0
            fmt_ok = size >= 16 and fmt == 1 and ch == 1 and bits == 8
            if not fmt_ok:
                raise ValueError(f"{path.name} is not 8-bit mono PCM")
        elif b[pos:pos + 4] == b"data":
            if not fmt_ok:
                raise ValueError(f"{path.name}: data before its format")
            return body
        pos += 8 + size + (size & 1)
    raise ValueError(f"{path.name} has no data")


def pack_drums(path):
    """(build 136b) drums_load: each sample's WAV into the Z80 RAM image at
    its address, each drum's (byte, delay, address, length); refused where
    drums.c refuses."""
    root = json.loads(path.read_text())
    root = root if isinstance(root, dict) else {}
    samples, drums = root.get("samples"), root.get("drums")
    if not isinstance(samples, list) or not 1 <= len(samples) <= \
            DRUM_SAMPLES_MAX:
        raise ValueError('"samples" is not 1-16 samples')
    if not isinstance(drums, list) or not 1 <= len(drums) <= DRUM_MAX:
        raise ValueError('"drums" is not 1-8 drums')
    ram, placed = bytearray(DRUM_Z80_RAM), []
    for i, s in enumerate(samples):
        name, file = jtext(s, "name"), jtext(s, "file")
        addr = jnum(s, "z80_address", -1)
        if name is None or file is None:
            raise ValueError(f"sample {i} has no name or no file")
        data = wav_pcm8(path.parent / file)
        if addr < DRUM_PROGRAM_END or addr + len(data) > DRUM_Z80_RAM:
            raise ValueError(f"sample {name} at ${addr:X} is not inside "
                             f"$118-$2000")
        for other, a, n in placed:
            if addr < a + n and a < addr + len(data):
                raise ValueError(f"sample {name} overlaps sample {other}")
        ram[addr:addr + len(data)] = data
        placed.append((name, addr, len(data)))
    table = []
    for i, d in enumerate(drums):
        byte, sample = jnum(d, "byte", -1), jtext(d, "sample")
        length, delay = jnum(d, "length", -1), jnum(d, "delay", -1)
        if not 0x80 <= byte <= 0xFF:
            raise ValueError(f"drum {i}'s byte {byte} is not $80-$FF")
        if byte in [t[0] for t in table]:
            raise ValueError(f"drum ${byte:02X} is given twice")
        at = [a for n, a, _ in placed if n == sample]
        if sample is None or not at:
            raise ValueError(f"drum ${byte:02X} names no sample")
        if not 1 <= delay <= 255:
            raise ValueError(f"drum ${byte:02X}'s delay {delay}")
        if length < 1 or at[-1] + length > DRUM_Z80_RAM:
            raise ValueError(f"drum ${byte:02X} plays past Z80 RAM")
        table.append((byte, delay, at[-1], length))
    return bytes(ram), table


# (build 136e) core/credits.h: the credits writer's codes and limits.
CREDITS_SPACE, CREDITS_BLANK_RUN_BELOW = 0x20, 0x14
CREDITS_NEXT_LINE, CREDITS_CLOSE_LINE = 0xC1, 0xC3
CREDITS_END_SECTION, CREDITS_END = 0xC4, 0xC5
CREDITS_LINE_CELLS, CREDITS_MAX = 39, 4096
CREDITS_TEXT_LINE_MAX = 256     # credits.c's line buffer, NUL included


def pack_songs(path):
    """(build 137) The song text (sounds.txt beside envelopes.txt,
    effects.txt and songs/) built into the driver's image as
    tools/song_text.py's assemble does, with the instruments, note
    frequencies and drum names from the JSON beside it."""
    import song_text
    try:
        return bytes(song_text.assemble(Path(path).parent).image)
    except song_text.TextError as exc:
        raise ValueError(str(exc))


def pack_tracker(path, rom, name):
    """(build 139b) A song's .trk read as the game reads it (tracker.py's
    game_music, songtext.c's choice written out as stream text, the song
    alone from its tracker file) and what it plays over the song's span,
    against what `rom` (the ROM's image) plays: None when the same, else
    the first difference."""
    import song_text
    import tracker
    music = Path(path).parent.parent
    with tempfile.TemporaryDirectory(prefix="roundtrip_trk_") as tmp:
        try:
            tracker.game_music(music, Path(tmp) / "music", only={name})
            mine = song_text.assemble(Path(tmp) / "music")
        except song_text.TextError as exc:
            raise ValueError(str(exc))
    span = tracker.song_span(rom, name)
    return tracker.timeline_difference(tracker.timeline(rom, name, span),
                                       tracker.timeline(mine, name, span))


def pack_credits(path):
    """(build 136e) credits_load: the file's lines as assetfs_next_line
    gives them (split at a newline, every carriage return dropped, cut to
    the buffer), # lines and empty ones skipped; {end} and {empty} alone;
    otherwise a line's leading spaces as blank runs of 20 cells at most,
    its other characters their own bytes (letters and spaces only, 39
    cells at most), and the code its tag ends it with. Refused where
    credits.c refuses."""
    out, ended = bytearray(), False
    for n, raw in enumerate(path.read_bytes().split(b"\n"), start=1):
        line = raw.replace(b"\r", b"")[:CREDITS_TEXT_LINE_MAX - 1]
        if line[:1] == b"#" or line == b"":
            continue
        if ended:
            raise ValueError(f"line {n} is after {{end}}")
        if line == b"{end}":
            out.append(CREDITS_END)
            ended = True
            continue
        if line == b"{empty}":
            out.append(CREDITS_NEXT_LINE)
            continue
        brace = line.find(b"{")
        body, tag = (line, b"") if brace < 0 else (line[:brace], line[brace:])
        ending = {b"": CREDITS_NEXT_LINE, b"{close}": CREDITS_CLOSE_LINE,
                  b"{section}": CREDITS_END_SECTION}.get(tag)
        if ending is None:
            raise ValueError(f"line {n}: {tag!r} is not {{close}} or "
                             f"{{section}} at a line's end")
        if len(body) > CREDITS_LINE_CELLS:
            raise ValueError(f"line {n} is {len(body)} cells wide")
        indent = len(body) - len(body.lstrip(b" "))
        for k, ch in enumerate(body[indent:], start=indent + 1):
            if ch != CREDITS_SPACE and not (0x41 <= ch <= 0x5A
                                            or 0x61 <= ch <= 0x7A):
                raise ValueError(f"line {n}, column {k}: byte ${ch:02X} is "
                                 f"not a letter or a space")
        left = indent
        while left > 0:
            run = min(left, CREDITS_BLANK_RUN_BELOW)
            out.append(run - 1)
            left -= run
        out += body[indent:]
        out.append(ending)
    if not ended:
        raise ValueError("no {end}")
    if len(out) > CREDITS_MAX:
        raise ValueError(f"{len(out)} bytes, more than {CREDITS_MAX}")
    return bytes(out)


def pack_blocks(path):
    out = bytearray()
    for line in path.read_text().splitlines():
        if not line.strip() or line.startswith("#"):
            continue
        words = line.split()
        if len(words) != 16:
            raise ValueError(f"a block of {len(words)} words: {line[:40]}")
        for w in words:
            out += int(w, 16).to_bytes(2, "big")
    return bytes(out)


def pack_tmx(path):
    """[(layer, width, height, block ids)]: each gid less one."""
    root = ET.parse(path).getroot()
    out = []
    for layer in root.iter("layer"):
        data = layer.find("data")
        if data.get("encoding") != "csv":
            raise ValueError(f"layer {layer.get('name')}: not CSV")
        gids = [int(t) for t in data.text.replace("\n", "").split(",")
                if t.strip()]
        if any(g < 1 or g > 256 for g in gids):
            raise ValueError(f"layer {layer.get('name')}: a gid outside "
                             f"1-256 (an empty or flipped cell)")
        out.append((layer.get("name"), int(layer.get("width")),
                    int(layer.get("height")), bytes(g - 1 for g in gids)))
    return out


def pack_winart_tmx(path):
    """(build 134) (width, height, bytes) of a window map, read as
    winmap.c's winmap_load reads it: each cell's tileset by its firstgid
    range, the byte its tile index plus the tileset's byte_base. An empty,
    flipped or stray cell is an error, as there."""
    root = ET.parse(path).getroot()
    sets = []
    for ts in root.iter("tileset"):
        if ts.get("source") is not None:
            raise ValueError("an external tileset")
        base = 0
        for prop in ts.iter("property"):
            if prop.get("name") == "byte_base":
                base = int(prop.get("value"))
        sets.append((int(ts.get("firstgid")), int(ts.get("tilecount")), base))
    layer = root.find("layer")
    data = layer.find("data")
    if data.get("encoding") != "csv":
        raise ValueError("the layer is not CSV")
    w, h = int(layer.get("width")), int(layer.get("height"))
    gids = [int(t) for t in data.text.replace("\n", "").split(",")
            if t.strip()]
    if len(gids) != w * h:
        raise ValueError(f"{len(gids)} cells for {w}x{h}")
    out = bytearray()
    for k, g in enumerate(gids):
        if g & 0xF0000000:
            raise ValueError(f"cell {k % w},{k // w} is flipped")
        hit = [(fg, b) for fg, n, b in sets if fg <= g < fg + n]
        if not hit:
            raise ValueError(f"cell {k % w},{k // w} is "
                             f"{'empty' if g == 0 else 'in no tileset'}")
        byte = g - hit[0][0] + hit[0][1]
        if not 0 <= byte <= 0xFF:
            raise ValueError(f"cell {k % w},{k // w} is byte {byte}")
        out.append(byte)
    return (w, h, bytes(out))


def pack_nametable_tmx(path):
    """(build 135) (width, height, words) of a screen nametable, read as
    nametable.c's nametable_load reads it: each cell's tileset by its
    firstgid range, the word priority << 15 | palette_line << 13 | V << 12 |
    H << 11 | (vram_base + the tile's index), H and V from the gid's flip
    flags; an empty cell is tile 0 in the map's empty_palette_line. A
    diagonal flip, a tile in no tileset or past $7FF, or a line or priority
    out of range is an error, as there."""
    root = ET.parse(path).getroot()
    empty_line = 0
    props = root.find("properties")
    for prop in (props if props is not None else []):
        if prop.get("name") == "empty_palette_line":
            empty_line = int(prop.get("value"))
    if not 0 <= empty_line <= 3:
        raise ValueError(f"empty_palette_line {empty_line} is not 0-3")
    sets = []
    for ts in root.iter("tileset"):
        if ts.get("source") is not None:
            raise ValueError("an external tileset")
        pr = {p.get("name"): int(p.get("value")) for p in ts.iter("property")}
        if "vram_base" not in pr:
            raise ValueError(f"tileset {ts.get('name')} has no vram_base")
        if not 0 <= pr.get("palette_line", -1) <= 3:
            raise ValueError(f"tileset {ts.get('name')}: palette_line")
        if not 0 <= pr.get("priority", 0) <= 1:
            raise ValueError(f"tileset {ts.get('name')}: priority")
        sets.append((int(ts.get("firstgid")), int(ts.get("tilecount")), pr))
    layer = next((l for l in root.iter("layer")
                  if l.get("name") == "nametable"), None)
    if layer is None:
        raise ValueError("no layer named nametable")
    data = layer.find("data")
    if data.get("encoding") != "csv":
        raise ValueError("the layer is not CSV")
    w, h = int(layer.get("width")), int(layer.get("height"))
    gids = [int(t) for t in data.text.replace("\n", "").split(",")
            if t.strip()]
    if len(gids) != w * h:
        raise ValueError(f"{len(gids)} cells for {w}x{h}")
    words = []
    for k, g in enumerate(gids):
        flags, gid = g & 0xF0000000, g & 0x0FFFFFFF
        where = f"cell {k % w},{k // w}"
        if gid == 0:
            if flags:
                raise ValueError(f"{where} is empty but flipped")
            words.append(empty_line << 13)
            continue
        if flags & 0x30000000:
            raise ValueError(f"{where} is flipped diagonally")
        hit = [(fg, pr) for fg, n, pr in sets if fg <= gid < fg + n]
        if not hit:
            raise ValueError(f"{where} is in no tileset")
        fg, pr = hit[0]
        tile = pr["vram_base"] + gid - fg
        if tile > 0x7FF:
            raise ValueError(f"{where} is tile {tile:#x}, past $7FF")
        words.append(pr.get("priority", 0) << 15 | pr["palette_line"] << 13
                     | (0x1000 if flags & 0x40000000 else 0)
                     | (0x0800 if flags & 0x80000000 else 0) | tile)
    return (w, h, words)


def pack_collision(path):
    out = bytearray()
    for line in path.read_text().splitlines():
        if not line.strip() or line.startswith("#"):
            continue
        _, solid, special = line.split()
        byte = 0
        for q in range(4):
            byte |= (solid[q] == "#") << q
            byte |= (special[q] == "#") << (q + 4)
        out.append(byte)
    return bytes(out)


# ---------------------------------------------------------------------------
# The scripts: the ROM's bytes assembled from ps2.asm, the file's encoded


CHARSET_RE = re.compile(r"^\s*charset\b(.*)$", re.I)


def charset_char(tok):
    tok = tok.strip()
    if tok.startswith("'") and tok.endswith("'") and len(tok) == 3:
        return ord(tok[1])
    return int(tok[1:], 16) if tok.startswith("$") else int(tok)


class Charsets:
    """AS's CHARSET directives in file order: `charset 'A', "\\11\\12..."`
    maps a run of characters to the listed values (decimal escapes),
    `charset 'x', $NN` or `charset 39, $NN` one, a bare `charset` resets
    to ASCII."""

    def __init__(self, lines):
        self.at, self.maps = [-1], [{}]
        current = {}
        for i, line in enumerate(lines):
            m = CHARSET_RE.match(strip_comment(line))
            if not m:
                continue
            body = m.group(1).strip()
            if not body:
                current = {}
            else:
                if body.startswith("'"):
                    first, rest = body[:3], body[3:]
                else:
                    first, rest = body.split(",", 1)
                start = charset_char(first)
                rest = rest.strip().removeprefix(",").strip()
                current = dict(current)
                if rest.startswith('"'):
                    values = [int(v) for v in re.findall(r"\\(\d+)", rest)]
                    for k, v in enumerate(values):
                        current[start + k] = v
                else:
                    current[start] = charset_char(rest)
            self.at.append(i)
            self.maps.append(current)

    def at_line(self, i):
        return self.maps[bisect.bisect_left(self.at, i) - 1]


STRING_ESCAPES = {"I": '"'}     # AS's escape for the quote (build 129a)


def assemble_dcb(operands, cmap):
    """A dc.b line's operands to bytes under a character map."""
    out = bytearray()
    for tok in split_operands(operands):
        if tok.startswith('"') and tok.endswith('"'):
            body, i = tok[1:-1], 0
            while i < len(body):
                ch = body[i]
                if ch == "\\":
                    ch = STRING_ESCAPES[body[i + 1]]
                    i += 1
                out.append(cmap.get(ord(ch), ord(ch)))
                i += 1
        elif tok.startswith("$"):
            out.append(int(tok[1:], 16))
        elif tok.isdigit():
            out.append(int(tok))
        else:
            raise ValueError(f"operand {tok}")
    return bytes(out)


def split_operands(text):
    toks, cur, quoted = [], "", False
    for ch in text:
        if ch == '"':
            quoted = not quoted
        if ch == "," and not quoted:
            toks.append(cur.strip())
            cur = ""
        else:
            cur += ch
    if cur.strip():
        toks.append(cur.strip())
    return toks


def strip_comment(text):
    """Everything before a `;` outside quotes, single or double
    (`charset ';', $41` and `charset '"', $42` both appear)."""
    quote = None
    for i, ch in enumerate(text):
        if quote is None and ch in "'\"":
            quote = ch
        elif ch == quote:
            quote = None
        elif ch == ";" and quote is None:
            return text[:i]
    return text


LABEL_RE = re.compile(r"^([A-Za-z_][A-Za-z0-9_]*):")
DCB_RE = re.compile(r"^(?:[A-Za-z_][A-Za-z0-9_]*:)?\s*dc\.b\s+(.*)$")
DELTA_RE = re.compile(r"^\s*dc\.b\s+([A-Za-z_][A-Za-z0-9_]*)\s*-")


def rom_scripts(asm, charsets, block):
    """The block's scripts in the delta table's order, each as the bytes
    from its label to the next."""
    start = asm.label_line[block]
    end = next((i for i in asm.marks
                if i > start and not asm.lines[i].startswith("loc_")),
               len(asm.lines))
    order, by_label, current = [], {}, None
    for i in range(start + 1, end):
        line = asm.lines[i]
        m = LABEL_RE.match(line)
        if m:
            current = m.group(1)
            by_label[current] = bytearray()
            continue
        if current is None:
            d = DELTA_RE.match(line)
            if d:
                order.append(d.group(1))
            continue
        if re.match(r"^\s*rept\b", line, re.I):
            # The free space after the last block's last script (`rept
            # $2A8` of `dc.b 0`): not text, and a line-by-line reader
            # cannot expand it anyway.
            break
        d = DCB_RE.match(line)
        if d:
            by_label[current] += assemble_dcb(strip_comment(d.group(1)),
                                              charsets.at_line(i))
    return [bytes(by_label.get(label, b"")) for label in order]


# window.c's win_charset and read_token
GLYPH = {**{chr(ord("0") + i): 1 + i for i in range(10)},
         **{chr(ord("A") + i): 11 + i for i in range(26)},
         **{chr(ord("a") + i): 37 + i for i in range(26)},
         ",": 0x3F, ".": 0x40, ";": 0x41, '"': 0x42, "?": 0x43, "!": 0x44,
         "'": 0x45, "-": 0x46, ":": 0x77, " ": 0}
TOKENS = {"char": 0xBB, "char2": 0xBC, "enemy": 0xBD, "tech": 0xBE,
          "item": 0xBF, "meseta": 0xC0, "cont": 0xC2, "pause": 0xC3}


def file_scripts(path):
    """script.c's entries: `[n]` opens one, # lines are comments, blank
    lines before an entry's first line are skipped, the rest joined with
    newlines."""
    entries = {}
    current = None
    for line in path.read_text().split("\n"):
        if line.startswith("#"):
            continue
        m = re.match(r"^\[(\d+)\]", line)
        if m:
            current = int(m.group(1))
            entries[current] = ""
            continue
        if current is None or (line == "" and entries[current] == ""):
            continue
        entries[current] += ("\n" if entries[current] else "") + line
    # script.c reads line by line, so the file's last newline ends a line
    # rather than adding an empty one.
    if current is not None and path.read_text().endswith("\n") \
            and entries[current].endswith("\n"):
        entries[current] = entries[current][:-1]
    return [entries.get(n, "") for n in range(1, max(entries, default=0) + 1)]


def encode_script(text):
    """window.c's load_script over one entry: a glyph by win_charset, a
    token by read_token, a newline $C1 -- but of the newlines at an
    entry's end, one is the file's own."""
    out, i = bytearray(), 0
    tail = len(text) - len(text.rstrip("\n"))
    body = text[:len(text) - tail] if tail else text
    while i < len(body):
        ch = body[i]
        if ch == "\n":
            out.append(0xC1)
            i += 1
        elif ch == "{":
            j = body.index("}", i)
            tok = body[i + 1:j]
            if tok.startswith("end:"):
                out.append(int(tok[4:], 16))
            elif re.fullmatch(r"[0-9A-Fa-f]{2}", tok):
                out.append(int(tok, 16))
            else:
                out.append(TOKENS[tok])
            i = j + 1
        else:
            out.append(GLYPH.get(ch, 0))
            i += 1
    out += b"\xC1" * max(0, tail - 1)
    return bytes(out)


def check_scripts(asm_path, assets, report):
    asm = Asm(asm_path)
    charsets = Charsets(asm.lines)
    import json
    blocks = json.loads((assets / "script" / "script.json").read_text())
    total = same = 0
    for block, info in blocks.items():
        rom = rom_scripts(asm, charsets, block)
        mine = [encode_script(t)
                for t in file_scripts(assets / "script" / info["file"])]
        for n in range(max(len(rom), len(mine))):
            a = rom[n] if n < len(rom) else None
            b = mine[n] if n < len(mine) else None
            total += 1
            if a == b:
                same += 1
                continue
            if a is None or b is None:
                report.fail(f"script {block} [{n + 1}]: only in "
                            f"{'the file' if a is None else 'the ROM'}")
                continue
            k = next((k for k in range(min(len(a), len(b)))
                      if a[k] != b[k]), min(len(a), len(b)))
            report.fail(f"script {block} [{n + 1}]: byte {k} of {len(a)}: "
                        f"ROM {a[max(0, k - 3):k + 4].hex(' ')}, "
                        f"file {b[max(0, k - 3):k + 4].hex(' ')}")
    return total, same


# ---------------------------------------------------------------------------
# (build 131) The JSON tables, by tools/roundtrip_json.py


def json_rom(paths):
    from asmdata import Rom
    return Rom(Path(paths["PS2_ASM"]), Path(paths["PS2_CONST"]),
               Path(paths["ART_DIR"]))


def check_json(paths, assets, report):
    import roundtrip_json
    results, docs = roundtrip_json.run(assets, json_rom(paths))
    report.say(f"  {'JSON tables':<18} "
               f"{sum(r.ok for r in results):4d} of {len(results):4d} the "
               f"same (assembled here from ps2.asm and the constants):")
    _, rest = roundtrip_json.to_report(results, docs, assets, report)
    return rest


# ---------------------------------------------------------------------------


class Report:
    def __init__(self):
        self.lines, self.failures = [], 0

    def say(self, text=""):
        self.lines.append(text)

    def note(self, text):
        self.lines.append("  note " + text)

    def fail(self, text):
        self.failures += 1
        if self.failures <= 60:
            self.lines.append("  FAIL " + text)
        elif self.failures == 61:
            self.lines.append("  (further failures not listed)")


def first_diff(a, b):
    return next((k for k in range(min(len(a), len(b))) if a[k] != b[k]),
                min(len(a), len(b)))


def level2(rec, assets, report):
    counts = {}

    def tally(kind, ok):
        n = counts.setdefault(kind, [0, 0])
        n[0] += 1
        n[1] += ok

    for rel, (kind, rom) in sorted(rec.items.items()):
        path = assets / rel
        if not path.exists():
            report.fail(f"{rel}: not in assets/")
            tally(kind, False)
            continue
        try:
            if kind == "tiles":
                tiles, packed = rom
                if not packed:
                    tiles = [bytes(t[i] << 4 | t[i + 1]
                                   for i in range(0, 64, 2)) for t in tiles]
                mine = pack_tiles(path)
                head, pad = mine[:len(tiles)], mine[len(tiles):]
                ok = head == tiles and all(t == bytes(32) for t in pad)
                if not ok:
                    bad = next((i for i in range(min(len(head), len(tiles)))
                                if head[i] != tiles[i]), None)
                    why = (f"tile {bad} of {len(tiles)}" if bad is not None
                           else f"{len(mine)} tiles for the ROM's "
                                f"{len(tiles)}, or padding not blank")
                    report.fail(f"{rel}: {why}")
            elif kind == "flat":
                w, h, indices = rom
                width, height, _, px = png_chunks(path)
                ok = (width, height) == (w, h) and px == indices
                if not ok:
                    report.fail(f"{rel}: {width}x{height} against {w}x{h}, "
                                f"or pixel {first_diff(px, indices)}")
            elif kind in ("palette", "ramp"):
                mine = (pack_palette(path) if kind == "palette"
                        else pack_ramp(path))
                stray = [(k, w) for k, w in enumerate(rom) if w & ~CRAM_BITS]
                for k, w in stray:
                    if STRAY_BITS_CHECKED.get((rel, k)) == w:
                        report.note(f"{rel}: word {k} is ${w:04X} in the "
                                    f"ROM, ${w & CRAM_BITS:04X} in the PNG "
                                    f"-- a bit colour RAM drops, checked: "
                                    f"never faded")
                    else:
                        report.fail(f"{rel}: word {k} is ${w:04X}, bits "
                                    f"outside the colour's that a PNG "
                                    f"cannot carry; see how it is loaded "
                                    f"and faded (STRAY_BITS_CHECKED)")
                rom = [w & CRAM_BITS for w in rom]
                ok = mine == rom and not [x for x in stray
                                          if STRAY_BITS_CHECKED.get(
                                              (rel, x[0])) != x[1]]
                if not ok and mine != rom:
                    k = first_diff(mine, rom)
                    report.fail(f"{rel}: {len(mine)} words for the ROM's "
                                f"{len(rom)}; word {k}: file "
                                f"{mine[k] if k < len(mine) else None}, ROM "
                                f"{rom[k] if k < len(rom) else None}")
            elif kind == "drums":
                image, table = rom
                mine_image, mine_table = pack_drums(path)
                ok = mine_image == image and mine_table == table
                if mine_table != table:
                    report.fail(f"{rel}: drum table {mine_table}, the ROM's "
                                f"{table}")
                elif not ok:
                    k = first_diff(mine_image, image)
                    report.fail(f"{rel}: Z80 RAM ${k:04X}: "
                                f"${mine_image[k]:02X}, the ROM's "
                                f"${image[k]:02X}")
            elif kind == "blocks":
                mine = pack_blocks(path)
                ok = mine == rom
                if not ok:
                    report.fail(f"{rel}: byte {first_diff(mine, rom)}")
            elif kind == "tmx":
                mine = pack_tmx(path)
                ok = mine == rom
                if not ok:
                    shapes = ([(l[0], l[1], l[2]) for l in mine],
                              [(l[0], l[1], l[2]) for l in rom])
                    if shapes[0] != shapes[1]:
                        why = f"layers {shapes[0]} against {shapes[1]}"
                    else:
                        li = next(i for i in range(len(rom))
                                  if mine[i][3] != rom[i][3])
                        k = first_diff(mine[li][3], rom[li][3])
                        w = rom[li][1]
                        why = (f"{rom[li][0]} at column {k % w}, row "
                               f"{k // w}: block {mine[li][3][k]:#04x}, "
                               f"the ROM's {rom[li][3][k]:#04x}")
                    report.fail(f"{rel}: {why}")
            elif kind == "wintmx":
                mine = pack_winart_tmx(path)
                ok = mine == rom
                if not ok:
                    if mine[:2] != rom[:2]:
                        why = f"{mine[0]}x{mine[1]}, the ROM's {rom[0]}x{rom[1]}"
                    else:
                        k = first_diff(mine[2], rom[2])
                        why = (f"column {k % rom[0]}, row {k // rom[0]}: byte "
                               f"${mine[2][k]:02X}, the ROM's ${rom[2][k]:02X}")
                    report.fail(f"{rel}: {why}")
            elif kind == "namtmx":
                mine = pack_nametable_tmx(path)
                ok = mine == rom
                if not ok:
                    if mine[:2] != rom[:2]:
                        why = f"{mine[0]}x{mine[1]}, the ROM's {rom[0]}x{rom[1]}"
                    else:
                        k = first_diff(mine[2], rom[2])
                        why = (f"column {k % rom[0]}, row {k // rom[0]}: "
                               f"${mine[2][k]:04X}, the ROM's "
                               f"${rom[2][k]:04X}")
                    report.fail(f"{rel}: {why}")
            elif kind == "credits":
                mine = pack_credits(path)
                ok = mine == rom
                if not ok:
                    k = first_diff(mine, rom)
                    report.fail(f"{rel}: {len(mine)} bytes for the ROM's "
                                f"{len(rom)}; byte {k}: file "
                                f"{mine[max(0, k - 3):k + 4].hex(' ')}, ROM "
                                f"{rom[max(0, k - 3):k + 4].hex(' ')}")
            elif kind == "songs":
                mine = pack_songs(path)
                ok = mine == rom
                if not ok:
                    k = first_diff(mine, rom)
                    report.fail(f"{rel}: {len(mine)} bytes for the ROM's "
                                f"{len(rom)}; first difference at ${k:04X}")
            elif kind == "tracker":
                b, name = rom
                why = pack_tracker(path, b, name)
                ok = why is None
                if not ok:
                    report.fail(f"{rel}: {why}")
            elif kind == "collision":
                mine = pack_collision(path)
                ok = mine == rom
                if not ok:
                    report.fail(f"{rel}: block {first_diff(mine, rom)}")
            else:
                raise ValueError(f"kind {kind}")
        except (ValueError, KeyError, IndexError) as exc:
            report.fail(f"{rel}: {exc}")
            ok = False
        tally(kind, ok)
    return counts


KIND_NAMES = {"tiles": "tile sheets", "flat": "flat pictures",
              "palette": "palettes", "ramp": "colour ramps",
              "drums": "drum sets", "credits": "credits",
              "songs": "songs and effects", "tracker": "tracker songs",
              "blocks": "block definitions",
              "tmx": "Tiled maps", "wintmx": "window maps",
              "namtmx": "nametables", "collision": "collision tables"}


def main():
    ap = argparse.ArgumentParser()
    # (build 185) The extractors read the cartridge image (--rom); the
    # packing back still reads the disassembly (--asm, --const, --art).
    ap.add_argument("--rom", default="")
    ap.add_argument("--asm", required=True)
    ap.add_argument("--const", required=True)
    ap.add_argument("--art", required=True)
    ap.add_argument("--pcm", default="")
    ap.add_argument("--assets", type=Path, default=TREE / "assets")
    ap.add_argument("--out", type=Path,
                    default=TREE / "route_out" / "roundtrip" / "report.txt")
    ap.add_argument("--prove-edits", action="store_true",
                    help="(build 131) one edit a JSON table on a scratch "
                         "copy: both levels must fail")
    args = ap.parse_args()

    if not args.rom:
        ap.error("--rom: the cartridge image, which the extractors read")
    if args.prove_edits:
        import roundtrip_json
        paths = {"PS2_ASM": str(Path(args.asm).resolve()),
                 "PS2_CONST": str(Path(args.const).resolve()),
                 "ART_DIR": str(Path(args.art).resolve())}
        bad = roundtrip_json.prove_edits(args.assets, json_rom(paths),
                                         Path(args.rom).resolve())
        print(f"edits: {'every one caught' if not bad else f'{bad} NOT caught'}")
        return 1 if bad else 0

    paths = {"ROM": str(Path(args.rom).resolve()),
             "PS2_ASM": str(Path(args.asm).resolve()),
             "PS2_CONST": str(Path(args.const).resolve()),
             "ART_DIR": str(Path(args.art).resolve()),
             "PCM_ASM": str(Path(args.pcm).resolve()) if args.pcm
             else "/nonexistent/ps2.pcm_drums.asm"}
    runs = makefile_runs(TREE / "Makefile", paths)
    report = Report()
    report.say(f"roundtrip: {paths['PS2_ASM']}")

    rec = Recorder()
    with tempfile.TemporaryDirectory(prefix="roundtrip_") as tmp:
        scratch = Path(tmp)
        (scratch / "assets").mkdir()
        skipped, _log = run_extractors(runs, scratch, rec)
        mine = {p.relative_to(scratch / "assets").as_posix(): p
                for p in (scratch / "assets").rglob("*") if p.is_file()}
        theirs = {p.relative_to(args.assets).as_posix(): p
                  for p in args.assets.rglob("*") if p.is_file()}
        mine.pop("manifest.txt", None)
        theirs_manifest = theirs.pop("manifest.txt", None)

        report.say()
        report.say(f"Level 1 -- {len(runs)} extractors run again from the "
                   f"ROM into a scratch tree:")
        for s in skipped:
            report.say(f"  skipped {s}")
        common = sorted(set(mine) & set(theirs))
        differ = [f for f in common
                  if mine[f].read_bytes() != theirs[f].read_bytes()]
        for f in differ:
            report.fail(f"{f}: not what the extractor writes")
        report.say(f"  {len(common) - len(differ)} of {len(common)} files "
                   f"they write the same bytes as assets/")
        for f in sorted(set(mine) - set(theirs)):
            report.fail(f"{f}: written by an extractor, not in assets/")
        unmade = sorted(set(theirs) - set(mine))
        report.say(f"  not written by any extractor: "
                   f"{', '.join(unmade) if unmade else 'none'}")

    listed = sorted(p.as_posix() for p in args.assets.rglob("*")
                    if p.is_file() and p.name != "manifest.txt"
                    and read_by_game(p, args.assets)
                    and p.suffix not in {".tsx", ".bak"}
                    and not p.name.startswith("."))
    listed = ["assets/" + str(Path(p).relative_to(args.assets).as_posix())
              for p in listed]
    have = [l for l in theirs_manifest.read_text().splitlines()
            if l and not l.startswith("#")] if theirs_manifest else []
    if listed == have:
        report.say(f"  manifest: the {len(have)} files make_manifest.py "
                   f"lists, as checked in")
    else:
        report.fail(f"manifest: {len(have)} listed, make_manifest.py would "
                    f"list {len(listed)} (run `make manifest`)")

    report.say()
    report.say("Level 2 -- each converted file read as the game reads it, "
               "packed back, and compared with the ROM's bytes:")
    counts = level2(rec, args.assets, report)
    for kind in ("tiles", "flat", "palette", "ramp", "blocks", "tmx",
                 "wintmx", "namtmx", "collision", "drums", "credits",
                 "songs", "tracker"):
        n, ok = counts.get(kind, (0, 0))
        report.say(f"  {KIND_NAMES[kind]:<18} {ok:4d} of {n:4d} the same")
    total, same = check_scripts(Path(paths["PS2_ASM"]), args.assets, report)
    report.say(f"  {'scripts':<18} {same:4d} of {total:4d} the same "
               f"(assembled here from ps2.asm by its CHARSET directives)")
    rest = check_json(paths, args.assets, report)
    report.say()
    # (build 131c) Every JSON file is packed or said above to be level 1
    # only; a new one the tables do not know is named here.
    # (build 137a) No container is left: the song text was the last.
    report.say("Not packed back (level 1 only): " + (
        "the JSON files not packed yet -- " + ", ".join(rest) + "." if rest
        else "nothing; no container holding the ROM's bytes is left "
        "(music/SoundData.bin, the last, is the song text since build "
        "137a)."))
    report.say()
    report.say(f"{'PASS' if report.failures == 0 else 'FAIL'}  "
               f"{report.failures} failures")

    text = "\n".join(report.lines) + "\n"
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(text, newline="\n")
    sys.stdout.write(text)
    return 1 if report.failures else 0


if __name__ == "__main__":
    sys.exit(main())
