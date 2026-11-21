#!/usr/bin/env python3
"""Extract the SMPS68k sound data: track headers, note streams and tables.

The driver addresses all of its own data as 16-bit offsets from
`SoundDriverBase`, byte-swapped (the disassembly's `z80ptr`) so the Z80
could have read them.  `SndDrv_ReadPtr` reads such a word, `rol.w #8`
swaps it back and adds the base.

So rather than extracting each song into its own file and inventing a
container for it, this pulls out one flat image of the whole data region
and keeps every offset exactly where the ROM had it.  The C side then
follows the same relative pointers the 68k did, which means the note
streams, the `$F6` gotos, the `$F8` gosubs and the pointer tables all
stay byte-identical -- nothing is relocated, so nothing can be relocated
wrongly.

(build 185) The image is read from the cartridge image (tools/rommap.py),
not from ps2.asm: the ROM's bytes from SoundDriverBase up to PCMDrums,
with the driver's own code between SoundDriverInput and FMFreqs zeroed
(nothing points into it; the song text keeps only its length) and the
`even` pad before PCMDrums left off. The pointer tables are read as the
driver reads them, a byte-swapped word an entry, and each entry's name is
the label at the address it points to.

Emits:
    (SoundData.bin, the image as a "PS2S" container, until build 137a)
    note_frequencies.json   (build 136c) FMFreqs, the 85 frequency words
                    GetFMFreq reads entries 0-63 of, as block and F-number
                    with each slot's concert name (FMFreqs.bin until then)
    fm_instruments.json     (build 136d) the FM instruments FMInsPtrs points
                    at, each register's fields named, and the order the
                    upload routine (SetFMIns) writes them in -- read from
                    the image as the 68k reads them (tables/FMInstruments.bin
                    and FMRegisterOrder.bin until then, extract_tables')
    songs.json      names and ids, for tools and for reading by eye
    sounds.txt, envelopes.txt, effects.txt, songs/*.txt
                    (build 137) the image as text (tools/song_text.py):
                    every song and effect, notes by name; checked to build
                    back to the image byte for byte before it is written;
                    the game builds its image from it (137a)
    songs/*.trk     (build 139b) every song laid out as a tracker
                    (tools/tracker.py), exported from that text; the game
                    reads a song's .trk in its stream text's place
"""

import argparse
import json
import math
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from asset_names import ENVELOPE_NAMES, INSTRUMENT_NAMES, MUSIC_FILES  # noqa: E402
from rommap import RomMap  # noqa: E402

# The two data regions, by the labels that bracket them. Between them sits
# the driver's own code, zeroed in the image: nothing points into it, and
# the song text keeps only its length (`driver_code`). `FMFreqs`, data
# again after the code, is reached by an absolute `lea`, so it is read by
# name.
REGIONS = [
    ("SoundDriverBase", "SoundDriverInput"),
    ("FMFreqs", "PCMDrums"),
]

# The driver's tables, in songs.json's "roots".
ROOTS = ["SoundPriorities", "MusicTempo", "MusicPtrs", "FMInsPtrs",
         "ModEnvelopePtrs", "SFXPtrs", "SpcSFXPtrs"]

# A pointer table runs from its label to the first label after it whose
# name does not start with this: MusicPtrs and SFXPtrs give each entry a
# label of its own (PtrMusic_Phantasy), the first at the table's address.
ENTRY_PREFIX = "Ptr"


def build_image(rom):
    """(build 185) The driver's data as one image: the ROM's bytes from
    SoundDriverBase to PCMDrums, the code between the regions zeroed.
    Returns the image, which bytes of it are data (1) and the base."""
    base = rom.addr("SoundDriverBase")
    end = rom.addr(REGIONS[-1][1])
    image = bytearray(rom.read(base, end - base))
    placed = bytearray(len(image))
    for first, last in REGIONS:
        a, b = rom.addr(first) - base, rom.addr(last) - base
        placed[a:b] = b"\1" * (b - a)
    for i, p in enumerate(placed):
        if not p:
            image[i] = 0
    return image, placed, base


def ptr_word(image, off):
    """A pointer word as SndDrv_ReadPtr reads it: byte-swapped, an offset
    from SoundDriverBase."""
    return image[off] | (image[off + 1] << 8)


def table_entries(rom, image, base, table):
    """(build 185) A pointer table's words, from its label to the first
    label after it that is not an entry's own (ENTRY_PREFIX)."""
    start = rom.addr(table)
    for name, _at in rom.labels[rom.index(table) + 1:]:
        if not name.startswith(ENTRY_PREFIX):
            end = rom.addr(name)
            break
    else:
        raise SystemExit(f"{table}: no label after it")
    if (end - start) % 2:
        raise SystemExit(f"{table}: {end - start} bytes, not whole words")
    return [ptr_word(image, a - base) for a in range(start, end, 2)]


def pointer_names(rom, image, base, table):
    """(build 185) The labels a pointer table points at, in order: for each
    entry the one label at that address. Two labels there would leave the
    name in doubt, and are refused."""
    names = []
    for k, off in enumerate(table_entries(rom, image, base, table)):
        found = rom.names_at(base + off)
        if len(found) != 1:
            raise SystemExit(f"{table} entry {k} points at ${base + off:X}, "
                             f"where the map has "
                             f"{', '.join(found) or 'no label'}: not one name")
        names.append(found[0])
    return names


def stream_reach(image, headers):
    """(build 185) Which bytes of the image a stream reads (1): every track
    of every sound's header (`headers`, their offsets), walked as the
    sequencer walks them (song_text.walk)."""
    import song_text
    reached = bytearray(len(image))

    def on(ev, _state):
        reached[ev.at:ev.at + ev.size] = b"\1" * ev.size
    for off in headers:
        for i in range(image[off]):
            at = off + 1 + 9 * i
            song_text.walk(image, song_text.Track("", at, image[at:at + 9],
                                                  True), on)
    return reached


def drop_end_pad(image, base, reached):
    """(build 185) The image ends with the last byte of data. Where the
    streams before PCMDrums end at an odd address (as they do), AS's `even`
    pads them out with a zero that is no data: told from a data byte by its
    place (the last, at an odd address) and that no stream reads it."""
    last = len(image) - 1
    if image[last] == 0 and (base + last) % 2 and not reached[last]:
        return image[:last]
    return image


def fm_freqs_count(image, base, labels, reached):
    """(build 185) FMFreqs' entries: as many as the song text's layout
    holds (song_text.len_freqs, 85: a silence and seven octaves). Nothing
    in the ROM marks the table's end -- the 48 bytes after it, up to the
    next label, are stream bytes no stream reaches -- which the disassembly
    told by writing the table in words and those in bytes. What can be
    checked is: no stream reads a byte of the table."""
    import song_text
    rel = {k: v - base for k, v in labels.items()}
    count = song_text.len_freqs(image, rel)
    if any(reached[rel["FMFreqs"]:rel["FMFreqs"] + 2 * count]):
        raise SystemExit(f"FMFreqs: a stream reads its {count} entries")
    return count


def describe(image, off):
    """Reads a track header back out of the image, as the driver would."""
    count = image[off]
    tracks = []
    for i in range(count):
        h = image[off + 1 + i * 9: off + 10 + i * 9]
        tracks.append({
            "flags": h[0],
            "channel": h[1],
            "tick_multiplier": h[2],
            "stream": h[3] | (h[4] << 8),
            "transpose": h[5] - 256 if h[5] > 127 else h[5],
            "mod_envelope": h[6],
            "instrument": h[7],
            "volume": h[8],
        })
    return tracks


FM_CLOCK = 7670454           # the YM2612's master clock, NTSC
NOTE_NAMES = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"]
ENTRY1_SEMITONE = 11         # entry 1 sounds as B0 (+26 cents)
NOTES_ABOUT = (
    "The note frequencies (FMFreqs). GetFMFreq adds the track's transpose "
    "to a note byte ($81 up), keeps the low six bits and reads that entry, "
    "0-63, of this table: a YM2612 frequency word, block (the octave) and "
    "F-number. Entry 0 is silence; entries 64-84 are in the ROM but nothing "
    "reads them. \"note\" names each slot by the nearest concert note "
    "(A440) -- the table is equal-tempered about a quarter of a semitone "
    "sharp, entry 1 being B0 + 26 cents -- and \"hz\" and \"cents\" are "
    "what the chip makes of block and F-number at its 7670454 Hz clock: the "
    "extractor's arithmetic, for reading; the game reads block and fnum.")


def note_slot_name(i):
    """(build 136c) Entry i's concert name: entry 1 is B0, each entry a
    semitone up; entry 0 is silence."""
    if i == 0:
        return "silence"
    semi = ENTRY1_SEMITONE + i - 1
    return f"{NOTE_NAMES[semi % 12]}{semi // 12}"


def write_note_frequencies(path, words):
    """(build 136c) FMFreqs as JSON: each word as block and F-number, with
    the slot's name and what it sounds at."""
    lines = []
    for i, w in enumerate(words):
        if w & ~0x3FFF:
            raise SystemExit(f"FMFreqs entry {i} is ${w:04X}: bits above "
                             f"the block that a block and F-number cannot "
                             f"say")
        block, fnum = w >> 11, w & 0x7FF
        hz = fnum * 2.0 ** (block - 1) * FM_CLOCK / 144 / 2 ** 20
        text = (f'    {{"note": "{note_slot_name(i)}", "block": {block}, '
                f'"fnum": {fnum}')
        if fnum:
            semi = 12 * math.log2(hz / 440.0) + 57
            cents = round((semi - round(semi)) * 100)
            text += f', "hz": {hz:.2f}, "cents": {cents}'
        lines.append(text + "}")
    path.write_text('{\n  "source": "FMFreqs",\n  "about": '
                    + json.dumps(NOTES_ABOUT) + ',\n  "entries": [\n'
                    + ",\n".join(lines) + "\n  ]\n}\n", newline="\n")


# (build 136d) The FM instruments. SetFMIns (the code after loc_BA818)
# writes 26 registers to the track's channel, the register numbers from the
# 26-byte table at loc_BA818 and the values from the instrument, byte i to
# register i; cfEF_SetFMIns and RestoreBGMChn find the instrument through
# FMInsPtrs, SoundDriverBase plus a byte-swapped word (SndDrv_ReadPtr). Each
# register is named by the fields it holds, an operator's as opN.<name> --
# operators in the chip's numbering, 1-4 at register offsets +0, +8, +4, +C.
FM_INS_REGISTERS = 26
OP_OF_OFFSET = {0x0: 1, 0x8: 2, 0x4: 3, 0xC: 4}
CHANNEL_REGISTERS = {0xB0: "feedback_algorithm", 0xB4: "pan_lfo"}
OPERATOR_REGISTERS = {0x30: "detune_multiple", 0x40: "total_level",
                      0x50: "rate_scaling_attack", 0x60: "am_decay",
                      0x70: "sustain_rate", 0x80: "sustain_level_release"}
# (name, register base, low bit, bits): the channel's, then an operator's
# in the order an envelope runs, as the file lists them.
CHANNEL_FIELDS = [("algorithm", 0xB0, 0, 3), ("feedback", 0xB0, 3, 3),
                  ("pan", 0xB4, 6, 2), ("lfo_am_sensitivity", 0xB4, 4, 2),
                  ("lfo_fm_sensitivity", 0xB4, 0, 3)]
OPERATOR_FIELDS = [("multiple", 0x30, 0, 4), ("detune", 0x30, 4, 3),
                   ("total_level", 0x40, 0, 7), ("attack_rate", 0x50, 0, 5),
                   ("decay_rate", 0x60, 0, 5), ("sustain_level", 0x80, 4, 4),
                   ("sustain_rate", 0x70, 0, 5), ("release_rate", 0x80, 0, 4),
                   ("rate_scaling", 0x50, 6, 2), ("lfo_am", 0x60, 7, 1)]
# $B4 bit 7 is the left speaker, bit 6 the right.
PAN_NAMES = {3: "center", 2: "left", 1: "right", 0: "none"}
INSTRUMENTS_ABOUT = (
    "The FM instruments (FMInsPtrs). A track's header or its instrument "
    "command (cfEF) names one by \"number\", its place here, and SetFMIns "
    "writes its 26 registers to the track's channel, in \"upload_order\" "
    "(the ROM's table at loc_BA818); then, if the track's volume is not 0, "
    "RefreshVolume writes each carrier's total_level plus that volume. Each "
    "operator field is a row of four, operators 1-4 in the chip's numbering "
    "(register offsets +0, +8, +4, +C). The algorithm decides the carriers, "
    "as RefreshVolume reads it: 0-3 operator 4; 4 operators 2 and 4; 5-6 "
    "operators 2-4; 7 all four. multiple 0-15 (0 is a half); detune 0-7 "
    "(1-3 up, 5-7 down, 0 and 4 none); total_level 0-127, attenuation, 0.75 "
    "dB a step; attack_rate, decay_rate and sustain_rate 0-31, "
    "release_rate 0-15, higher faster; sustain_level 0-15, where the first "
    "decay hands over to the second, 3 dB a step; rate_scaling 0-3, how "
    "much faster the rates run for higher notes; lfo_am whether the LFO's "
    "tremolo reaches the operator, at the channel's lfo_am_sensitivity "
    "(0-3), its vibrato at lfo_fm_sensitivity (0-7); feedback 0-7, operator "
    "1 into itself; pan center, left, right or none. \"name\" is what the "
    "song text calls it (build 137; one word, no two the same). "
    "\"ignored_bits\" are "
    "bits set in the ROM's byte that the chip does not read, kept so the "
    "bytes are the ROM's: they make no sound. The game reads its "
    "instruments from SoundData.bin's image; this file is read by "
    "tools/fm_demo and q_demo and the song text's tools until the game "
    "builds that image from the text.")


def register_name(reg):
    """(build 136d) A register SetFMIns writes, by the fields it holds."""
    if reg in CHANNEL_REGISTERS:
        return CHANNEL_REGISTERS[reg]
    base, offset = reg & 0xF0, reg & 0x0F
    if base not in OPERATOR_REGISTERS or offset not in OP_OF_OFFSET:
        raise SystemExit(f"SetFMIns writes ${reg:02X}, which is not a "
                         f"channel 1 instrument register")
    return f"op{OP_OF_OFFSET[offset]}.{OPERATOR_REGISTERS[base]}"


def read_mask(base):
    """The bits of a register the chip reads: its fields'."""
    mask = 0
    for _, b, shift, bits in CHANNEL_FIELDS + OPERATOR_FIELDS:
        if b == base:
            mask |= ((1 << bits) - 1) << shift
    return mask


def instrument_text(number, values):
    """One instrument, `values` its register -> byte, as the file holds it."""
    def field(base, shift, bits):
        return (values[base] >> shift) & ((1 << bits) - 1)

    head = [f'"number": {number}']
    if number < len(INSTRUMENT_NAMES):
        head.append(f'"name": "{INSTRUMENT_NAMES[number]}"')
    for name, base, shift, bits in CHANNEL_FIELDS:
        v = field(base, shift, bits)
        head.append(f'"{name}": ' + (f'"{PAN_NAMES[v]}"' if name == "pan"
                                     else str(v)))
    rows = []
    for name, base, shift, bits in OPERATOR_FIELDS:
        cells = []
        for offset in (0x0, 0x8, 0x4, 0xC):
            v = field(base + offset, shift, bits)
            cells.append(("true" if v else "false") if name == "lfo_am"
                         else f"{v:3d}")
        rows.append(f'{json.dumps(name) + ":":<17}[' + ", ".join(cells) + "]")
    ignored = []
    for reg in values:              # in upload order
        base = reg if reg in CHANNEL_REGISTERS else reg & 0xF0
        stray = values[reg] & ~read_mask(base) & 0xFF
        if stray:
            bits = ", ".join(str(b) for b in range(8) if stray >> b & 1)
            ignored.append(f'"{register_name(reg)}": [{bits}]')
    if ignored:
        rows.append('"ignored_bits":  {' + ", ".join(ignored) + "}")
    return ("    {" + ", ".join(head[:2]) + ",\n     "
            + ", ".join(head[2:]) + ",\n     "
            + ",\n     ".join(rows) + "}")


def drum_names():
    """(build 137) The drums by byte and name, as drums.json gives them:
    extract_drums' names (the port's, by drum byte; build 185: no longer
    with what each plays, which it reads from the ROM), so this needs no
    drums.json written first, and no ROM."""
    import extract_drums
    return {"drums": [{"byte": b, "name": n}
                      for b, n in extract_drums.DRUMS]}


def write_song_text(outdir, files, image):
    """(build 137) The songs, effects and the driver's tables as text:
    `files` is song_text.disassemble's {relative path: text}. Written only
    if it builds back to `image` byte for byte; stale song files deleted."""
    import song_text
    import tempfile
    with tempfile.TemporaryDirectory() as tmp:
        t = Path(tmp)
        for name in ("note_frequencies.json", "fm_instruments.json"):
            (t / name).write_bytes((outdir / name).read_bytes())
        (t / "drums.json").write_text(json.dumps(drum_names()))
        (t / "songs").mkdir()
        for rel, text in files.items():
            (t / rel).write_text(text, newline="\n")
        song_text.annotate(t)
        built = bytes(song_text.assemble(t).image)
        if built != image:
            k = next((i for i in range(min(len(built), len(image)))
                      if built[i] != image[i]), min(len(built), len(image)))
            raise SystemExit(f"the song text builds {len(built)} bytes, not "
                             f"the image's {len(image)}; first difference at "
                             f"${k:04X}")
        (outdir / "songs").mkdir(exist_ok=True)
        keep = set()
        for rel in files:
            (outdir / rel).write_bytes((t / rel).read_bytes())
            keep.add((outdir / rel).resolve())
        for old in (outdir / "songs").glob("*.txt"):
            if old.resolve() not in keep:
                old.unlink()
                print(f"deleted {old} (no such song now)")
    return len(files)


def write_trackers(outdir, image):
    """(build 139b) Every song laid out as a tracker (tools/tracker.py),
    songs/<song>.trk beside the stream text, which the game reads in the
    song's stream text's place: exported from the text write_song_text has
    just written, checked again to build `image` (the drums named as
    write_song_text names them: `make extract` writes drums.json after
    this). A .trk for a song that is gone is deleted."""
    import song_text
    import tracker
    b = song_text.assemble(outdir, drums_json=drum_names())
    if bytes(b.image) != image:
        raise SystemExit("the song text written does not build the image")
    keep = set()
    for name in b.songs:
        path = outdir / "songs" / f"{name}.trk"
        path.write_text(tracker.write_song(b, name), newline="\n")
        keep.add(path.resolve())
    for old in (outdir / "songs").glob("*.trk"):
        if old.resolve() not in keep:
            old.unlink()
            print(f"deleted {old} (no such song now)")
    return len(keep)


def write_fm_instruments(path, order, bank):
    """(build 136d) The register order and every instrument FMInsPtrs points
    at, `bank` a list of 26-byte instruments in the ROM's upload order."""
    names = [register_name(r) for r in order]
    if len(order) != FM_INS_REGISTERS or len(set(order)) != len(order):
        raise SystemExit(f"loc_BA818: {len(order)} registers, not "
                         f"{FM_INS_REGISTERS} different ones")
    for want in (*CHANNEL_REGISTERS, *(b + o for b in OPERATOR_REGISTERS
                                       for o in OP_OF_OFFSET)):
        if want not in order:
            raise SystemExit(f"loc_BA818 does not write ${want:02X}")
    # The channel's two, then each kind of operator register four to a line.
    order_lines = [", ".join(json.dumps(n) for n in names[:2])]
    order_lines += [", ".join(json.dumps(n) for n in names[i:i + 4])
                    for i in range(2, len(names), 4)]
    blocks = [instrument_text(i, dict(zip(order, ins)))
              for i, ins in enumerate(bank)]
    path.write_text('{\n  "source": "FMInsPtrs, loc_BA818 (SetFMIns)",\n'
                    '  "about": ' + json.dumps(INSTRUMENTS_ABOUT) + ',\n'
                    '  "upload_order": [\n    '
                    + ",\n    ".join(order_lines) + '\n  ],\n'
                    '  "instruments": [\n' + ",\n".join(blocks)
                    + "\n  ]\n}\n", newline="\n")


def instrument_bank(rom, image, base):
    """(build 136d) Every instrument FMInsPtrs names, 26 bytes from where its
    pointer word points in the image, as SetFMIns reads them -- straight on
    past any label, so the two whose last bytes are the next one's first
    come out whole (build 111)."""
    bank = []
    for n, at in enumerate(table_entries(rom, image, base, "FMInsPtrs")):
        ins = image[at:at + FM_INS_REGISTERS]
        if len(ins) != FM_INS_REGISTERS:
            raise SystemExit(f"FMInsPtrs entry {n} runs past the image")
        bank.append(bytes(ins))
    return bank


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("rom", type=Path, help="the cartridge image")
    ap.add_argument("-o", "--outdir", type=Path, default=Path("assets/music"))
    ap.add_argument("--report", action="store_true",
                    help="print the channel map of every song")
    args = ap.parse_args()

    rom = RomMap(args.rom)
    image, placed, base = build_image(rom)
    args.outdir.mkdir(parents=True, exist_ok=True)

    music = pointer_names(rom, image, base, "MusicPtrs")
    sfx = pointer_names(rom, image, base, "SFXPtrs")
    special = pointer_names(rom, image, base, "SpcSFXPtrs")

    # The labels the song text and songs.json name: the tables', FMFreqs
    # and every sound's header.
    labels = {name: rom.addr(name) for name in ROOTS + ["FMFreqs"]}
    labels.update((name, rom.addr(name)) for name in music + sfx + special)
    roots = {name: labels[name] for name in ROOTS}

    headers = sorted({labels[n] - base for n in music + sfx + special})
    reached = stream_reach(image, headers)
    image = bytes(drop_end_pad(image, base, reached))

    gaps = placed[:len(image)].count(0)
    print(f"the driver's image: {len(image)} bytes ({gaps} not data: the "
          f"driver's own code), as text below")
    old = args.outdir / "SoundData.bin"
    if old.exists():
        old.unlink()
        print(f"deleted {old} (the song text now, build 137a)")

    at = labels["FMFreqs"] - base
    count = fm_freqs_count(image, base, labels, reached)
    words = [int.from_bytes(image[at + 2 * i:at + 2 * i + 2], "big")
             for i in range(count)]
    out = args.outdir / MUSIC_FILES["FMFreqs"]
    write_note_frequencies(out, words)
    old = args.outdir / "FMFreqs.bin"
    if old.exists():
        old.unlink()
        print(f"deleted {old} (note_frequencies.json now)")
    print(f"FMFreqs: {len(words)} entries -> {out}")

    # SetFMIns's register order: inside the driver's code, so from the ROM
    # (build 185), the bytes from loc_BA818 to SetFMIns.
    order = rom.blob("loc_BA818")
    bank = instrument_bank(rom, image, base)
    out = args.outdir / MUSIC_FILES["FMInsPtrs"]
    write_fm_instruments(out, order, bank)
    print(f"FMInsPtrs: {len(bank)} instruments, {len(order)} registers "
          f"each -> {out}")

    # (build 137) The same image as text.
    import song_text
    names = song_text.Names(
        json.loads((args.outdir / MUSIC_FILES["FMFreqs"]).read_text()),
        drum_names(),
        INSTRUMENT_NAMES, ENVELOPE_NAMES)
    files = song_text.disassemble(
        image, labels, music, sfx, special, base, names,
        labels["FMFreqs"] - base)
    n = write_song_text(args.outdir, files, image)
    print(f"song text: {n} files, built back to the image byte for byte "
          f"-> {args.outdir}/sounds.txt, envelopes.txt, effects.txt, songs/")
    n = write_trackers(args.outdir, image)
    print(f"tracker files: {n} songs -> {args.outdir}/songs/<song>.trk")

    songs = {
        "base": base,
        "roots": roots,
        "music": [{"id": 0x81 + i, "name": n,
                   "offset": labels[n] - base,
                   "tracks": describe(image, labels[n] - base)}
                  for i, n in enumerate(music)],
        "sfx": [{"id": 0xA0 + i, "name": n,
                 "offset": labels[n] - base,
                 "tracks": describe(image, labels[n] - base)}
                for i, n in enumerate(sfx)],
    }
    out = args.outdir / "songs.json"
    out.write_text(json.dumps(songs, indent=1), newline="\n")
    print(f"songs: {len(songs['music'])} music, {len(songs['sfx'])} sfx "
          f"-> {out}")

    if args.report:
        report(songs)


CHANNEL_NAMES = {0x80: "DAC", 0: "FM1", 1: "FM2", 2: "FM3",
                 4: "FM4", 5: "FM5", 6: "FM6"}


def report(songs):
    print()
    for group in ("music", "sfx"):
        for entry in songs[group]:
            chans = " ".join(
                CHANNEL_NAMES.get(t["channel"], f"?{t['channel']:02X}")
                for t in entry["tracks"])
            print(f"  {entry['id']:02X} {entry['name']:<28} {chans}")
        print()


if __name__ == "__main__":
    main()
