#!/usr/bin/env python3
"""(build 137) The sound driver's data as text: every song and effect.

Until build 137 the songs, the effects and the driver's tables were one
image of the ROM's bytes (assets/music/SoundData.bin). This module turns
that image into text a person can read and edit and builds it back, byte
for byte:

    assets/music/sounds.txt      the sound ids, their priorities, and where
                                 the song streams sit in the image
    assets/music/envelopes.txt   the four vibrato envelopes, by name
    assets/music/effects.txt     every effect: its header, then its streams
    assets/music/songs/<n>.txt   a song: its header, then its streams in
                                 blocks (two songs that share a passage are
                                 one file)

with the instruments from fm_instruments.json (by name), the notes named
from note_frequencies.json and the drums from drums.json. docs/audio.md,
"The song text", describes the format; each file's header says enough to
read it.

The streams are read the way the sequencer reads them (ps2.asm, ProcFMTrack
and its three variants, UpdateDrumTrack, cfHandler): flag by flag, and a
note by the track's mode -- raw-frequency mode takes a word then a length,
pitch-slide mode a note, a byte the driver skips, a speed and a length,
chord mode one more note byte for each extra part. Loops are followed with
their counters, calls with their returns, as the driver does, so every
byte has one meaning; a byte no stream reaches is written as `bytes`.

    python3 tools/song_text.py check   [--music DIR]   build the image, report
    python3 tools/song_text.py annotate [--music DIR]  rewrite the heard notes
"""

import argparse
import json
import re
import sys
from pathlib import Path

# ---------------------------------------------------------------------------
# The driver's vocabulary.

CHANNELS = {0x80: "drums", 0: "fm1", 1: "fm2", 2: "fm3", 4: "fm4", 5: "fm5",
            6: "fm6"}
CHANNEL_BYTES = {v: k for k, v in CHANNELS.items()}

# A track header's flags byte: bit 7 the track in use (the loader sets it
# unless `not_in_use`), the rest by name.
HEADER_FLAG_BITS = [(0x20, "pitch_slide"), (0x08, "raw_freq"),
                    (0x40, "flag_6"), (0x10, "flag_4"), (0x04, "lent"),
                    (0x02, "held"), (0x01, "fm3_special")]

# cfE3's animation types (StartPanAnim / ExecPanAnim).
PAN_ANIM = {0: "off", 1: "sweep", 2: "cycle", 3: "left_first",
            4: "right_first", 5: "sweep_5", 6: "sweep_6"}
PAN_ANIM_BYTES = {v: k for k, v in PAN_ANIM.items()}
# cfFF's byte: $B4 bit 7 the left speaker, bit 6 the right.
PAN = {0xC0: "center", 0x80: "left", 0x40: "right", 0x00: "none"}
PAN_BYTES = {v: k for k, v in PAN.items()}

# The coordination flags' operand counts (cfHandler and each flag's reads).
OPERANDS = {0xE0: 1, 0xE1: 0, 0xE2: 1, 0xE3: 2, 0xE4: 1, 0xE5: 1, 0xE6: 0,
            0xE7: 0, 0xE8: 1, 0xE9: 1, 0xEA: 1, 0xEB: 1, 0xEC: 1, 0xED: 0,
            0xEE: 1, 0xEF: 1, 0xF0: 1, 0xF1: 0, 0xF2: 0, 0xF3: 1, 0xF4: 1,
            0xF5: 1, 0xF6: 2, 0xF7: 4, 0xF8: 2, 0xF9: 0, 0xFA: 1, 0xFB: 1,
            0xFC: 1, 0xFD: 1, 0xFE: 4, 0xFF: 1}
ENDS = (0xF2, 0xE1)          # the track ends: nothing after is read

# The envelope codes (DoModEnv); any other byte is a step.
ENV_CODES = {0x80: "restart", 0x81: "hold", 0x83: "stop"}
ENV_CODE_BYTES = {v: k for k, v in ENV_CODES.items()}

# The image's fixed places (the driver reads them by address, not through
# a root pointer): the PAL tempo and tick tables.
PAL_TEMPO_AT = 0x100
PAL_TICK_AT = 0x120
TEMPO_ENTRIES = 32           # MusicTempo, loc_B8100, loc_B8120
ROOTS = 7                    # the z80ptr words at SoundDriverBase
FIRST_ID = 0x81
FIRST_EFFECT_ID = 0xA0
FIRST_SPECIAL_ID = 0xD8
INSTRUMENT_BYTES = 26
CHORD_MAX = 16               # (build 184) a chord's notes (smps.h)

NOTE_NAMES = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"]
NOTE_RE = re.compile(r"^([A-G]#?)(-?\d+)$")


class TextError(Exception):
    """A line the loader cannot read: `where` is file:line."""

    def __init__(self, where, message):
        super().__init__(f"{where}: {message}")
        self.where = where


def semitone(name):
    m = NOTE_RE.match(name)
    if not m:
        return None
    return NOTE_NAMES.index(m.group(1)) + 12 * int(m.group(2))


def semitone_name(semi):
    return f"{NOTE_NAMES[semi % 12]}{semi // 12}"


def signed(b):
    return b - 256 if b > 127 else b


def snake(name):
    """ExcitingTown -> exciting_town, MovementVer2 -> movement_ver2."""
    s = re.sub(r"(?<=[a-z0-9])(?=[A-Z])", "_", name)
    s = re.sub(r"(?<=[A-Z])(?=[A-Z][a-z])", "_", s)
    return s.lower()


def sound_name(label):
    """A ROM label (Music_ExcitingTown, SFX_B8) -> the text's name."""
    stem = label.split("_", 1)[1]
    if stem == "Null":
        return "none"
    if re.fullmatch(r"[A-F0-9]{2}", stem):
        return "effect_" + stem.lower()
    return snake(stem)


# ---------------------------------------------------------------------------
# Names: notes, drums, instruments, envelopes, sounds.

class Names:
    def __init__(self, freqs_json, drums_json, instruments, envelopes=None):
        entries = freqs_json["entries"]
        # Entry n is the note byte $80 + n; past 63 a semitone a step, as the
        # bytes $C0-$DF are written (GetFMFreq keeps six bits, so they play
        # an entry 0-63: the heard notes say which).
        first = semitone(entries[1]["note"])
        if first is None:
            raise SystemExit("note_frequencies.json: entry 1 has no note name")
        self.note_of_entry = ["silence"]
        for n in range(1, 0x60):
            if n < 64:
                got = semitone(entries[n]["note"]) if n < len(entries) else None
                if got != first + n - 1:
                    raise SystemExit(f"note_frequencies.json: entry {n} is "
                                     f"{entries[n].get('note')!r}, not a "
                                     f"semitone above entry {n - 1}")
            self.note_of_entry.append(semitone_name(first + n - 1))
        self.note_bytes = {nm: 0x80 + n
                           for n, nm in enumerate(self.note_of_entry) if n}
        self.note_bytes["rest"] = 0x80
        self.drum_of_byte = {d["byte"]: d["name"] for d in drums_json["drums"]}
        self.drum_bytes = {v: k for k, v in self.drum_of_byte.items()}
        self.instruments = list(instruments)
        self.instrument_numbers = {n: i for i, n in enumerate(instruments)}
        self.envelopes = list(envelopes or [])
        self.sounds = {}            # id -> name

    def note(self, b):
        return "rest" if b == 0x80 else self.note_of_entry[b - 0x80]

    def heard(self, b, transpose):
        """The entry GetFMFreq reads for a note byte at a transpose."""
        n = (b + transpose) & 0x3F
        return self.note_of_entry[n]

    def pitch(self, b, drums):
        if drums and b in self.drum_of_byte:
            return self.drum_of_byte[b]
        return self.note(b)

    def instrument(self, n):
        return self.instruments[n] if n < len(self.instruments) else f"${n:02X}"

    def envelope(self, n):
        if n == 0:
            return "off"
        if n - 1 < len(self.envelopes):
            return self.envelopes[n - 1]
        return f"${n:02X}"

    def sound(self, sid):
        return self.sounds.get(sid, f"${sid:02X}")


# ---------------------------------------------------------------------------
# The walk: a track's stream as the sequencer reads it.

class Event:
    __slots__ = ("at", "size", "kind", "info")

    def __init__(self, at, size, kind, info):
        self.at, self.size, self.kind, self.info = at, size, kind, info


def disp_target(image, second):
    """cfF6/F7/F8: the displacement's second byte plus the 16-bit word."""
    d = image[second - 1] | (image[second] << 8)
    return (second + d) & 0xFFFF


class Track:
    """One header's track: where it starts and its starting state."""

    def __init__(self, sound, header_at, raw, counted):
        self.sound = sound
        self.header_at = header_at
        self.raw = bytes(raw)
        self.counted = counted
        self.flags, self.channel, self.tick = raw[0], raw[1], raw[2]
        self.start = raw[3] | (raw[4] << 8)
        self.transpose = signed(raw[5])
        self.mod, self.instrument, self.volume = raw[6], raw[7], raw[8]

    @property
    def drums(self):
        return bool(self.channel & 0x80)

    @property
    def name(self):
        return f"{self.sound}_{CHANNELS.get(self.channel, 'ch%d' % self.channel)}"


def walk(image, track, on_event, limit=400000):
    """Follows `track` through `image` as the sequencer does, calling
    on_event(event, state) for each event; state is (transpose, chord part
    transposes). Loops run with their counters, calls return; the walk
    stops at a track end, an empty return, or a state already seen (the
    song's repeat)."""
    seen = set()
    a = track.start
    raw = bool(track.flags & 0x08)
    slide = bool(track.flags & 0x20)
    chord = 0
    tr = track.transpose
    part_tr = ()
    counters = {}
    stack = ()
    steps = 0
    size = len(image)

    def byte(i):
        if i >= size:
            raise IndexError(f"{track.name}: the stream runs past the image "
                             f"at ${i:04X}")
        return image[i]

    while steps < limit:
        state = (a, raw, slide, chord, tr, part_tr,
                 tuple(sorted(counters.items())), stack)
        if state in seen:
            return
        seen.add(state)
        entry_raw = raw
        # The flags before the note.
        while True:
            steps += 1
            b = byte(a)
            if b < 0xE0:
                break
            n = OPERANDS[b]
            args = bytes(byte(a + 1 + k) for k in range(n))
            ev = Event(a, 1 + n, "flag", (b, args))
            if b == 0xF6:
                on_event(ev, (tr, part_tr))
                a = disp_target(image, a + 2)
                continue
            if b == 0xF7:
                on_event(ev, (tr, part_tr))
                slot, count = args[0], args[1]
                c = counters.get(slot, 0)
                if c == 0:
                    c = count
                c = (c - 1) & 0xFF
                counters[slot] = c
                a = disp_target(image, a + 4) if c else a + 5
                continue
            if b == 0xF8:
                on_event(ev, (tr, part_tr))
                stack = stack + (a + 1,)
                a = disp_target(image, a + 2)
                continue
            if b == 0xF9:
                on_event(ev, (tr, part_tr))
                if not stack:
                    return
                a = stack[-1] + 2
                stack = stack[:-1]
                continue
            on_event(ev, (tr, part_tr))
            if b in ENDS:
                return
            if b == 0xFC:
                slide = args[0] != 0
            elif b == 0xFD:
                raw = args[0] != 0
            elif b == 0xFB:
                tr = signed((tr + args[0]) & 0xFF)
            elif b == 0xE5:
                chord = args[0]
                part_tr = (tr,) * max(chord - 1, 0)
            a += 1 + n
        # The note, by the mode: raw-ness as it was when the read began
        # (ProcFMTrack_Raw never looks again), slide as it is now.
        b = byte(a)
        if track.drums:
            if b >= 0x80:
                ln = byte(a + 1) if byte(a + 1) < 0x80 else None
                ev = Event(a, 1 if ln is None else 2, "drum", (b, ln))
            else:
                ev = Event(a, 1, "again", (b,))
        elif entry_raw:
            word = (b << 8) | byte(a + 1)
            speed = byte(a + 2) if slide else None
            k = a + (3 if slide else 2)
            ev = Event(a, k + 1 - a, "raw", (word, speed, byte(k)))
        elif slide:
            parts = []
            k = a + 1
            if b != 0x80 and chord:
                parts = [byte(k + i) for i in range(chord - 1)]
                k += chord - 1
            ev = Event(a, k + 3 - a, "slide",
                       (b, parts, byte(k), byte(k + 1), byte(k + 2)))
        elif b < 0x80:
            ev = Event(a, 1, "again", (b,))
        else:
            parts = []
            k = a + 1
            if b != 0x80 and chord:
                parts = [byte(k + i) for i in range(chord - 1)]
                k += chord - 1
            ln = byte(k) if byte(k) < 0x80 else None
            ev = Event(a, k - a + (0 if ln is None else 1), "note",
                       (b, parts, ln))
        on_event(ev, (tr, part_tr))
        a += ev.size
        steps += 1


# ---------------------------------------------------------------------------
# Events to text.

def number(v):
    return str(v)


def signed_text(b):
    v = signed(b)
    return f"+{v}" if v >= 0 else str(v)


def flag_text(b, args, names, label_of, at):
    """A flag's words; `label_of(addr)` names a jump target."""
    if b == 0xE0:
        return "music stop" if args[0] == 0 else (
            "music restart" if args[0] == 1 else f"music restart {args[0]}")
    if b == 0xE1:
        return "end2"
    if b == 0xE2:
        return f"sound {names.sound(args[0])}"
    if b == 0xE3:
        kind = PAN_ANIM.get(args[0], str(args[0]))
        return f"pan_anim {kind} {args[1]}"
    if b == 0xE4:
        return f"tempo {args[0]}"
    if b == 0xE5:
        return "chord off" if args[0] == 0 else f"chord {args[0]}"
    if b == 0xE6:
        return "hold"
    if b == 0xE7:
        return "release"
    if b == 0xE8:
        return "pause off" if args[0] == 0 else (
            "pause on" if args[0] == 1 else f"pause {args[0]}")
    if b == 0xE9:
        return f"comm {args[0]}"
    if b == 0xEA:
        return f"fms {args[0]}"
    if b == 0xEB:
        return f"ams {args[0]}"
    if b == 0xEC:
        return f"volume {signed_text(args[0])}"
    if b == 0xED:
        return "unused_ed"
    if b == 0xEE:
        return "legato off" if args[0] == 0 else (
            "legato on" if args[0] == 1 else f"legato {args[0]}")
    if b == 0xEF:
        return f"instrument {names.instrument(args[0])}"
    if b == 0xF0:
        return f"volume {args[0]}"
    if b == 0xF1:
        return "nop"
    if b == 0xF2:
        return "end"
    if b == 0xF3:
        return f"noise {args[0]}"
    if b == 0xF4:
        return f"mod {names.envelope(args[0])}"
    if b == 0xF5:
        return f"psg_instrument {args[0]}"
    if b == 0xF6:
        return f"goto {label_of(disp_target_args(at, args, 2))}"
    if b == 0xF7:
        text = f"loop {label_of(disp_target_args(at, args, 4))} {args[1]} times"
        return text + (f" counter {args[0]}" if args[0] else "")
    if b == 0xF8:
        return f"call {label_of(disp_target_args(at, args, 2))}"
    if b == 0xF9:
        return "return"
    if b == 0xFA:
        return f"tick {args[0]}"
    if b == 0xFB:
        return f"transpose {signed_text(args[0])}"
    if b == 0xFC:
        return "pitch_slide off" if args[0] == 0 else (
            "pitch_slide on" if args[0] == 1 else f"pitch_slide {args[0]}")
    if b == 0xFD:
        return "raw_freq off" if args[0] == 0 else (
            "raw_freq on" if args[0] == 1 else f"raw_freq {args[0]}")
    if b == 0xFE:
        return "fm3_special " + " ".join(str(x) for x in args)
    if b == 0xFF:
        return "pan " + PAN.get(args[0], f"${args[0]:02X}")
    raise ValueError(f"flag ${b:02X}")


def disp_target_args(at, args, second_index):
    """The target of a jump at `at` whose displacement is the operand bytes
    ending at operand `second_index` (1-based: the flag byte is 0)."""
    second = at + second_index
    d = args[second_index - 2] | (args[second_index - 1] << 8)
    return (second + d) & 0xFFFF


def event_text(ev, names, drums, label_of):
    k = ev.kind
    if k == "flag":
        b, args = ev.info
        return flag_text(b, args, names, label_of, ev.at)
    if k == "again":
        return f"again {ev.info[0]}"
    if k == "drum":
        b, ln = ev.info
        name = names.pitch(b, True)
        return name if ln is None else f"{name} {ln}"
    if k == "note":
        b, parts, ln = ev.info
        text = "+".join([names.pitch(b, drums)] + [names.note(p) for p in parts])
        return text if ln is None else f"{text} {ln}"
    if k == "slide":
        b, parts, unused, speed, ln = ev.info
        text = "+".join([names.note(b)] + [names.note(p) for p in parts])
        text += f" {ln} slide {signed_text(speed)}"
        return text + (f" unused {unused}" if unused else "")
    if k == "raw":
        word, speed, ln = ev.info
        text = f"freq ${word:04X} {ln}"
        return text if speed is None else text + f" slide {signed_text(speed)}"
    raise ValueError(k)


# ---------------------------------------------------------------------------
# The ROM's image to text.

def header_text(track, names, label_of, word="track"):
    t = track
    parts = [f"{word} {CHANNELS.get(t.channel, '$%02X' % t.channel):<5}",
             label_of(t.start), f"tick {t.tick}",
             f"transpose {signed_text(t.raw[5])}",
             f"mod {names.envelope(t.mod)}",
             f"instrument {names.instrument(t.instrument)}",
             f"volume {t.volume}"]
    flags = t.flags
    if not flags & 0x80:
        parts.append("not_in_use")
    for bit, name in HEADER_FLAG_BITS:
        if flags & bit:
            parts.append(name)
    return "  ".join(parts)


def bytes_text(data):
    return "bytes " + " ".join(f"${b:02X}" for b in data)


SOUNDS_HEADER = """\
# The sounds (build 137): every sound id the game can ask the driver for,
# what it plays, and the driver's own tables. Built back into the driver's
# image (SoundDriverBase) with envelopes.txt, effects.txt, songs/*.txt,
# fm_instruments.json and note_frequencies.json -- docs/audio.md, "The song
# text". Lines from # are comments.
#
# One line a sound id, $81 to $D8 in order: the id, the song or effect it
# plays (the name on a `song` line in songs/ or an `effect` line in
# effects.txt; `none` is an empty sound) and its priority (SoundPriorities):
# a sound with bit 7 set always plays, and with bit 0 too becomes the song
# an effect's `music restart` brings back; any other plays if its priority
# is at least the sounding effect's. $81-$9F are songs (MusicPtrs), $A0-$D7
# effects (SFXPtrs), $D8 on the special effects (SpcSFXPtrs), which take
# their effect's first track only. `song_file` lines name the files under
# songs/ to read; a song file not named is not read.
"""

LAYOUT_HEADER = """\
# Where things sit in the image, after the effects. `driver_code` is the
# driver's own code, zeros here; then the note frequencies; then `streams`:
# the songs' blocks in this order, and `bytes` no stream reaches. The order
# only places things -- a jump finds its label wherever it sits -- and keeps
# the image the ROM's.
"""

INSTRUMENTS_HEADER = """\
# The instruments (FMInsPtrs, then the bank from fm_instruments.json, in
# number order), each stored whole but for these lines: `same A B ...`
# stores B (and the rest) at A's place when they are the same bytes, and
# `share A B N` stores B starting in A's last N bytes when those are B's
# first N, as the ROM stores them; when the bytes differ (an instrument
# edited), each is stored whole.
"""

ENVELOPES_HEADER = """\
# The vibrato envelopes (build 137; ModEnvelopePtrs, DoModEnv). A track's
# header or a `mod` line names one. Each tick a note sounds, the driver reads
# the envelope's next byte and sends the note's frequency plus that byte
# times the track's multiplier -- an offset from the note, not a sum. A new
# note sets the multiplier back to 0 but not the place in the envelope.
#
#   a number       a step, -128 to 127 (but not the codes' bytes below).
#                  The ROM extends a negative step wrongly: every one of
#                  them plays as -1 times the multiplier.
#   depth +N       ($84 N) add N to the multiplier
#   jump LABEL     ($85) go on from LABEL, a `name:` line in the envelope
#   restart        ($80) go on from the envelope's first byte
#   hold, stop     ($81, $83) stop moving: the offset stays where it is
#
# The envelopes sit together in the image, ending at $100 at most (the PAL
# tables are read there by address); they are padded with zeros to it.
"""

EFFECTS_HEADER = """\
# The sound effects (build 137; SFXPtrs). docs/audio.md, "The song text",
# has the whole format; in short:
#
#   effect NAME            an effect (sounds.txt gives its id), then its
#   track CH LABEL ...     tracks' headers: the channel (fm1-fm6, drums),
#                          where its stream starts and its starting state
#   unused_track ...       header bytes after the counted ones; never read
#   NAME:                  a label: where a goto, loop or call lands
#   C5 12                  a note and its length in ticks (times the tick
#                          multiplier); a note with no length keeps the last
#   C5+E5+G5 12            chord mode: one note a part
#   rest 12                silence
#   again 12               the last note struck again, for 12
#   tie 12                 the note held on 12 more, not struck again
#   C5 12 slide +3         pitch-slide mode: the note, then a slide of +3
#                          F-number units a tick (`unused N`: a byte the
#                          driver skips)
#   freq $2060 12          raw-frequency mode: the chip's frequency word
#   instrument NAME, volume +4 (or volume 20), transpose -12, mod NAME,
#   pan left, chord 3, tick 2, tempo 6, ...  state, until changed
#   goto L, loop L 4 times, call L, return, end
#   bytes $80 $00          bytes as they are: nothing reads them
#
# Notes are concert names (note_frequencies.json): byte $81 is B0. A track's
# transpose moves what is heard; `# heard:` comments say what (they are
# written by `python3 tools/song_text.py annotate`, never read).
"""

SONG_HEADER = """\
# {title} (build 137). docs/audio.md, "The song text", has the format;
# effects.txt's header has it in short. `song` lines give the tempo (a tick
# is skipped every N frames; 0 never), the PAL tempo and the PAL tick
# multiplier, then a `track` line a part. The streams follow in blocks,
# which sounds.txt places in the image. Notes are concert names; `# heard:`
# comments are written by `python3 tools/song_text.py annotate`.
"""


def disassemble(image, labels, music, sfx, special, base, names_in,
                driver_gap_end):
    """The ROM's image to the text files: returns {relative path: text}.

    `labels` label -> ROM address, for the driver's tables, FMFreqs and
    every sound's header (extract_music reads them from the ROM's map,
    build 185); `music`, `sfx`, `special` the pointer tables' label names
    in order."""
    names = names_in
    rel = {k: v - base for k, v in labels.items()}

    def ptr(off):
        return image[off] | (image[off + 1] << 8)

    # The sounds, by id.
    ids = []
    for i, lbl in enumerate(music):
        ids.append((FIRST_ID + i, lbl))
    for i, lbl in enumerate(sfx):
        ids.append((FIRST_EFFECT_ID + i, lbl))
    for i, lbl in enumerate(special):
        ids.append((FIRST_SPECIAL_ID + i, lbl))
    for sid, lbl in ids:
        names.sounds.setdefault(sid, sound_name(lbl))

    # Headers and their tracks.
    tracks = []
    header_of = {}           # label -> (offset, [Track])
    for sid, lbl in ids:
        if lbl in header_of or lbl.endswith("_Null"):
            continue
        off = rel[lbl]
        n = image[off]
        ts = [Track(sound_name(lbl), off + 1 + 9 * i,
                    image[off + 1 + 9 * i: off + 10 + 9 * i], True)
              for i in range(n)]
        header_of[lbl] = (off, ts)
        tracks += ts

    # Walk every track.
    events = {}
    owner = {}
    targets = {}             # address -> first track to land there
    starts = {}
    for t in tracks:
        starts.setdefault(t.start, t)

        def on(ev, state, t=t):
            old = events.get(ev.at)
            if old is not None and (old.size != ev.size or old.kind != ev.kind
                                    or old.info != ev.info):
                raise SystemExit(f"${ev.at:04X} read two ways: {old.kind} and "
                                 f"{ev.kind} ({t.name})")
            events[ev.at] = ev
            for k in range(ev.at, ev.at + ev.size):
                owner.setdefault(k, set()).add(t.sound)
            if ev.kind == "flag" and ev.info[0] in (0xF6, 0xF7, 0xF8):
                second = {0xF6: 2, 0xF7: 4, 0xF8: 2}[ev.info[0]]
                targets.setdefault(disp_target_args(ev.at, ev.info[1], second), t)
        walk(image, t, on)

    # Labels: a track's start by its track, others by the first track to
    # land there, numbered in address order.
    label_at = {}
    for a, t in sorted(starts.items()):
        label_at[a] = t.name
    count = {}
    for a in sorted(targets):
        if a in label_at:
            continue
        t = targets[a]
        count[t.name] = count.get(t.name, 0) + 1
        label_at[a] = f"{t.name}_{count[t.name]}"

    def label_of(a):
        if a not in label_at:
            raise SystemExit(f"no label at ${a:04X}")
        return label_at[a]

    # The songs' files: songs sharing a stream byte go in one file.
    song_ids = [(sid, lbl) for sid, lbl in ids
                if sid < FIRST_EFFECT_ID and not lbl.endswith("_Null")]
    group = {sound_name(lbl): sound_name(lbl) for _, lbl in song_ids}

    def find(x):
        while group[x] != x:
            x = group[x]
        return x
    for a, owners in owner.items():
        songs = sorted(o for o in owners if o in group)
        for o in songs[1:]:
            group[find(o)] = find(songs[0])
    files = {}
    for _, lbl in song_ids:
        files.setdefault(find(sound_name(lbl)), []).append(sound_name(lbl))
    file_of = {s: "_and_".join(members) for members in files.values()
               for s in members}

    def lines_for(a0, a1, drums_at, first_label=False):
        """Text lines for image bytes a0..a1: labels, events, dead bytes
        (the label at a0 too when `first_label`; a block's line names it)."""
        out = []
        row = []
        a = a0

        def flush():
            if row:
                out.append("    " + "  ".join(row))
                row.clear()
        while a < a1:
            if a in label_at and (a != a0 or first_label):
                flush()
                out.append(f"{label_at[a]}:")
            ev = events.get(a)
            if ev is None:
                b = a
                while b < a1 and b not in events and (b == a or b not in label_at):
                    b += 1
                flush()
                out.append("    " + bytes_text(image[a:b]) +
                           "    # no stream reaches these")
                a = b
                continue
            drums = drums_at(a)
            if ev.kind == "flag":
                b = ev.info[0]
                nxt = events.get(a + ev.size)
                if (b in (0xE6, 0xE7) and nxt is not None
                        and nxt.kind == "again" and a + ev.size not in label_at):
                    word = "tie" if b == 0xE6 else "release"
                    if row and row[-1].split()[0] in FLAG_WORDS - {"tie", "release"}:
                        flush()
                    row.append(f"{word} {nxt.info[0]}")
                    if len(row) >= 8:
                        flush()
                    a += ev.size + nxt.size
                    continue
                text = event_text(ev, names, drums, label_of)
                before = ends.get(a)
                if b == 0xE6 and before is not None and not drums and \
                        before.kind in ("note", "again", "slide", "raw"):
                    # (build 139b) Where the track's pointer rests after a
                    # note, a rest or a length: DoNoteStop finds the hold
                    # there and leaves the note on (cfE6 itself, a note
                    # next, does nothing).
                    flush()
                    out.append(f"    {text}    # holds the note before up "
                               f"to the next")
                elif b in (0xE6, 0xE7):
                    flush()
                    out.append(f"    {text}    # does nothing: the next byte "
                               f"is not a length")
                elif b in (0xF6, 0xF7, 0xF8, 0xF9, 0xF2, 0xE1):
                    flush()
                    out.append(f"    {text}")
                else:
                    if row and not row[-1].startswith(("instrument", "volume",
                                                       "transpose", "mod",
                                                       "pan", "chord", "tick",
                                                       "tempo", "pitch_slide",
                                                       "raw_freq", "legato",
                                                       "fm3_special", "music",
                                                       "sound", "pause",
                                                       "pan_anim")):
                        flush()
                    row.append(text)
            else:
                if row and row[-1].split()[0] in FLAG_WORDS:
                    flush()
                text = event_text(ev, names, drums, label_of)
                inner = [x for x in range(a + 1, a + ev.size) if x in label_at]
                if inner:
                    # A loop that lands on a note's length byte: it reads
                    # the length alone, `again`, each time round.
                    ln = ev.info[-1]
                    if ev.kind not in ("note", "drum") or ln is None or \
                            inner != [a + ev.size - 1]:
                        raise SystemExit(f"${inner[0]:04X}: a label inside "
                                         f"{ev.kind} at ${a:04X}")
                    head = text.rsplit(" ", 1)[0]
                    row.append(f"{head} {label_at[inner[0]]}: {ln}")
                    flush()
                    out[-1] += (f"    # the loop comes back to the {ln}: "
                                f"`again {ln}` each time round")
                else:
                    row.append(text)
                if len(row) >= 8:
                    flush()
            a += ev.size
        flush()
        return out

    drums_owner = {}
    for t in tracks:
        if t.drums:
            def mark(ev, state):
                drums_owner[ev.at] = True
            walk(image, t, mark)

    def drums_at(a):
        return drums_owner.get(a, False)

    ends = {ev.at + ev.size: ev for ev in events.values()}

    out = {}

    # sounds.txt
    sl = [SOUNDS_HEADER]
    prio = rel["SoundPriorities"]
    for k, (sid, lbl) in enumerate(ids):
        nm = sound_name(lbl)
        sl.append(f"${sid:02X}  {nm:<22} ${image[prio + k]:02X}")
    sl.append("")
    sl.append("# The song files, songs/<name>.txt, read in this order.")
    for f in sorted("_and_".join(m) for m in files.values()):
        sl.append(f"song_file {f}")
    sl.append("")
    sl.append(INSTRUMENTS_HEADER)
    sl.append("instruments")
    table = rel["FMInsPtrs"]
    count_ins = len(names.instruments)
    ptrs = [ptr(table + 2 * i) for i in range(count_ins)]
    i = 1
    while i < count_ins:
        j = i
        while j < count_ins and ptrs[j] == ptrs[i - 1]:
            j += 1
        if j > i:
            sl.append("same " + " ".join(names.instrument(k)
                                         for k in range(i - 1, j)))
            i = j
            continue
        gap = ptrs[i] - ptrs[i - 1]
        i += 1
        if 0 < gap < INSTRUMENT_BYTES:
            sl.append(f"share {names.instrument(i - 2)} {names.instrument(i - 1)} "
                      f"{INSTRUMENT_BYTES - gap}")
    sl.append("")
    sl.append(LAYOUT_HEADER)
    effects_end = rel["SFX_Null"] + 1
    sl.append(f"driver_code {driver_gap_end - effects_end}")
    sl.append("streams")
    freqs_end = rel["FMFreqs"] + 2 * len_freqs(image, rel)
    a = freqs_end
    end = len(image)
    blocks = {}              # file -> [lines]
    while a < end:
        if a not in events:
            b = a
            while b < end and b not in events:
                b += 1
            sl.append("    " + bytes_text(image[a:b]) +
                      "    # " + ("padding" if image[a:b] == b"\0" else
                                 "no stream reaches these"))
            a = b
            continue
        f = file_of[next(iter(sorted(owner[a])))]
        b = a
        while b < end and b in owner and file_of[next(iter(sorted(owner[b])))] == f:
            b += 1
        name = label_of(a)
        sl.append(f"    block {name}")
        blocks.setdefault(f, []).append(
            ["", f"block {name}"] + lines_for(a, b, drums_at))
        a = b
    out["sounds.txt"] = "\n".join(sl) + "\n"

    # envelopes.txt
    el = [ENVELOPES_HEADER]
    env_table = rel["ModEnvelopePtrs"]
    env_ptrs = [ptr(env_table + 2 * i) for i in range(len(names.envelopes))]
    env_end = PAL_TEMPO_AT
    for i, p in enumerate(env_ptrs):
        q = env_ptrs[i + 1] if i + 1 < len(env_ptrs) else env_end
        el.append(f"envelope {names.envelopes[i]}")
        el += envelope_lines(image[p:q], names.envelopes[i])
        el.append("")
    pad = env_end - (env_ptrs[-1] + len(image[env_ptrs[-1]:env_end]))
    out["envelopes.txt"] = "\n".join(el) + ("\n" if pad == 0 else "")

    # effects.txt: from the first effect header to the empty one.
    xl = [EFFECTS_HEADER]
    first = min(header_of[l][0] for _, l in ids
                if FIRST_EFFECT_ID <= _ < FIRST_SPECIAL_ID and l in header_of)
    hdr_at = {header_of[l][0]: l for l in header_of}
    a = first
    while a < rel["SFX_Null"]:
        if a in hdr_at:
            lbl = hdr_at[a]
            off, ts = header_of[lbl]
            xl.append("")
            xl.append(f"effect {sound_name(lbl)}")
            for t in ts:
                xl.append(header_text(t, names, label_of))
            a = off + 1 + 9 * len(ts)
            # Header bytes past the count that read as a header whose stream
            # is a label: the ROM's, never read (SFX_Slasher's second).
            while a + 9 <= rel["SFX_Null"] and a not in events and \
                    a not in hdr_at and looks_like_header(image, a, label_at):
                t = Track(sound_name(lbl), a, image[a:a + 9], False)
                xl.append(header_text(t, names, label_of, "unused_track") +
                          "    # past the header's count: never read")
                a += 9
            continue
        b = a + 1
        while b < rel["SFX_Null"] and b not in hdr_at:
            b += 1
        xl += lines_for(a, b, drums_at, first_label=True)
        a = b
    out["effects.txt"] = "\n".join(xl) + "\n"

    # songs/<file>.txt
    for members in files.values():
        f = "_and_".join(members)
        fl = [SONG_HEADER.format(title=" and ".join(
            m.replace("_", " ").title() for m in members))]
        for m in members:
            sid, lbl = next((s, l) for s, l in song_ids if sound_name(l) == m)
            idx = sid - FIRST_ID
            fl.append(f"song {m}  tempo {image[rel['MusicTempo'] + idx]}  "
                      f"pal_tempo {image[PAL_TEMPO_AT + idx]}  "
                      f"pal_tick {image[PAL_TICK_AT + idx]}")
            for t in header_of[lbl][1]:
                fl.append(header_text(t, names, label_of))
            fl.append("")
        for blk in blocks.get(f, []):
            fl += blk
        out[f"songs/{f}.txt"] = "\n".join(fl) + "\n"
    return out


FLAG_WORDS = {"music", "end2", "sound", "pan_anim", "tempo", "chord", "hold",
              "tie", "release", "pause", "comm", "fms", "ams", "volume",
              "unused_ed", "legato", "instrument", "nop", "end", "noise",
              "mod", "psg_instrument", "goto", "loop", "call", "return",
              "tick", "transpose", "pitch_slide", "raw_freq", "fm3_special",
              "pan"}


def looks_like_header(image, a, label_at):
    h = image[a:a + 9]
    return (len(h) == 9 and h[0] & 0x80 and h[1] in CHANNELS
            and (h[3] | (h[4] << 8)) in label_at)


def len_freqs(image, rel):
    """FMFreqs' entries: from its label to the first stream byte (85)."""
    return 85


def envelope_lines(data, name):
    """One envelope's bytes to text lines: steps eight a line, the codes on
    lines of their own, a label where a jump lands."""
    jumps = set()
    i = 0
    while i < len(data):
        b = data[i]
        if b in (0x84, 0x85):
            if b == 0x85:
                jumps.add(data[i + 1])
            i += 2
        else:
            i += 1
    label = {j: (f"{name}_cycle" if len(jumps) == 1 else f"{name}_{j}")
             for j in jumps}
    out, row = [], []

    def flush():
        if row:
            out.append("    " + " ".join(row))
            row.clear()
    i = 0
    while i < len(data):
        if i in jumps:
            flush()
            out.append(f"  {label[i]}:")
        b = data[i]
        if b == 0x84:
            flush()
            out.append(f"    depth {signed_text(data[i + 1])}")
            i += 2
        elif b == 0x85:
            flush()
            out.append(f"    jump {label[data[i + 1]]}")
            i += 2
        elif b in ENV_CODES:
            flush()
            out.append(f"    {ENV_CODES[b]}")
            i += 1
        else:
            row.append(str(signed(b)))
            if len(row) >= 16:
                flush()
            i += 1
    flush()
    return out


# ---------------------------------------------------------------------------
# Text to the image.

NUMBER_RE = re.compile(r"^(\$[0-9A-Fa-f]+|\d+)$")
SIGNED_RE = re.compile(r"^[+-]\d+$")
NAME_RE = re.compile(r"^[A-Za-z_][A-Za-z0-9_]*$")


def tokens(line):
    """A line's words, the comment cut off: a comment is a word that starts
    with # (a note name has its # inside the word)."""
    out = []
    for w in line.split():
        if w.startswith("#"):
            break
        out.append(w)
    return out


class Reader:
    """A token cursor over one line, raising TextError at file:line."""

    def __init__(self, where, toks):
        self.where, self.toks, self.i = where, toks, 0

    def more(self):
        return self.i < len(self.toks)

    def peek(self):
        return self.toks[self.i] if self.more() else None

    def take(self, what):
        if not self.more():
            raise TextError(self.where, f"{what} missing at the end of the line")
        self.i += 1
        return self.toks[self.i - 1]

    def fail(self, message):
        raise TextError(self.where, message)

    def byte(self, what, top=255):
        w = self.take(what)
        if not NUMBER_RE.match(w):
            self.fail(f"{what}: {w!r} is not a number")
        v = int(w[1:], 16) if w.startswith("$") else int(w)
        if v > top:
            self.fail(f"{what}: {w} is past {top}")
        return v

    def signed(self, what):
        w = self.take(what)
        if not SIGNED_RE.match(w):
            self.fail(f"{what}: {w!r} wants a sign, + or -")
        v = int(w)
        if not -128 <= v <= 127:
            self.fail(f"{what}: {w} is outside -128 to +127")
        return v & 0xFF

    def word(self, what, choices):
        w = self.take(what)
        if w not in choices:
            self.fail(f"{what}: {w!r} is not one of " + ", ".join(choices))
        return choices[w]

    def on_off(self, what):
        w = self.take(what)
        if w == "on":
            return 1
        if w == "off":
            return 0
        if NUMBER_RE.match(w):
            self.i -= 1
            return self.byte(what)
        self.fail(f"{what}: {w!r} is not on or off")

    def name(self, what):
        w = self.take(what)
        if not NAME_RE.match(w):
            self.fail(f"{what}: {w!r} is not a name")
        return w


class Item:
    """Assembled bytes, with jump fix-ups: (offset in data, label, second
    byte's offset) -- the displacement is from the second byte."""

    def __init__(self, where, data, fixups=()):
        self.where, self.data, self.fixups = where, bytearray(data), list(fixups)


def parse_flag(r, w, names):
    """A flag word's Item, its operands read from `r`."""
    where = r.where
    if w == "music":
        k = r.take("music: stop or restart")
        if k == "stop":
            return Item(where, [0xE0, 0])
        if k != "restart":
            r.fail(f"music: {k!r} is not stop or restart")
        n = r.byte("music restart") if r.more() and NUMBER_RE.match(r.peek()) else 1
        if n == 0:
            r.fail("music restart: 0 is `music stop`")
        return Item(where, [0xE0, n])
    if w == "end2":
        return Item(where, [0xE1])
    if w == "sound":
        v = r.take("sound: a sound's name")
        if NUMBER_RE.match(v):
            r.i -= 1
            return Item(where, [0xE2, r.byte("sound")])
        if v not in names.sound_ids:
            r.fail(f"sound: no sound is named {v!r} (sounds.txt)")
        return Item(where, [0xE2, names.sound_ids[v]])
    if w == "pan_anim":
        v = r.peek()
        kind = PAN_ANIM_BYTES[r.take("")] if v in PAN_ANIM_BYTES else \
            r.byte("pan_anim: a type")
        return Item(where, [0xE3, kind, r.byte("pan_anim: its timeout")])
    if w == "tempo":
        return Item(where, [0xE4, r.byte("tempo")])
    if w == "chord":
        if r.peek() == "off":
            r.take("")
            return Item(where, [0xE5, 0])
        # (build 184) at most 16 notes, as the game's reader (smps.h's
        # SMPS_CHORD_MAX): the chord voices past the chip stop there
        return Item(where, [0xE5, r.byte("chord: the notes", CHORD_MAX)])
    if w == "hold":
        return Item(where, [0xE6])
    if w == "tie":
        return Item(where, [0xE6, r.byte("tie: a length", 127)])
    if w == "release":
        if r.more() and NUMBER_RE.match(r.peek()):
            return Item(where, [0xE7, r.byte("release: a length", 127)])
        return Item(where, [0xE7])
    if w == "pause":
        return Item(where, [0xE8, r.on_off("pause")])
    if w == "comm":
        return Item(where, [0xE9, r.byte("comm")])
    if w == "fms":
        return Item(where, [0xEA, r.byte("fms")])
    if w == "ams":
        return Item(where, [0xEB, r.byte("ams")])
    if w == "volume":
        v = r.peek()
        if v is not None and SIGNED_RE.match(v):
            return Item(where, [0xEC, r.signed("volume")])
        return Item(where, [0xF0, r.byte("volume")])
    if w == "unused_ed":
        return Item(where, [0xED])
    if w == "legato":
        return Item(where, [0xEE, r.on_off("legato")])
    if w == "instrument":
        v = r.take("instrument: a name")
        if v not in names.instrument_numbers:
            r.fail(f"instrument: no instrument is named {v!r} "
                   f"(fm_instruments.json)")
        return Item(where, [0xEF, names.instrument_numbers[v]])
    if w == "nop":
        return Item(where, [0xF1])
    if w == "end":
        return Item(where, [0xF2])
    if w == "noise":
        return Item(where, [0xF3, r.byte("noise")])
    if w == "mod":
        return Item(where, [0xF4, envelope_number(r, names)])
    if w == "psg_instrument":
        return Item(where, [0xF5, r.byte("psg_instrument")])
    if w in ("goto", "call"):
        lbl = r.name(f"{w}: a label")
        return Item(where, [0xF6 if w == "goto" else 0xF8, 0, 0],
                    [(1, lbl, 2)])
    if w == "loop":
        lbl = r.name("loop: a label")
        count = r.byte("loop: how many times")
        if r.take("loop: `times`") != "times":
            r.fail("loop LABEL N times: `times` missing")
        slot = 0
        if r.peek() == "counter":
            r.take("")
            slot = r.byte("loop counter")
        return Item(where, [0xF7, slot, count, 0, 0], [(3, lbl, 4)])
    if w == "return":
        return Item(where, [0xF9])
    if w == "tick":
        return Item(where, [0xFA, r.byte("tick")])
    if w == "transpose":
        return Item(where, [0xFB, r.signed("transpose")])
    if w == "pitch_slide":
        return Item(where, [0xFC, r.on_off("pitch_slide")])
    if w == "raw_freq":
        return Item(where, [0xFD, r.on_off("raw_freq")])
    if w == "fm3_special":
        return Item(where, [0xFE] + [r.byte("fm3_special") for _ in range(4)])
    if w == "pan":
        v = r.take("pan: left, right, center or none")
        if v in PAN_BYTES:
            return Item(where, [0xFF, PAN_BYTES[v]])
        r.i -= 1
        return Item(where, [0xFF, r.byte("pan")])
    return None


def envelope_number(r, names):
    v = r.take("mod: an envelope's name or off")
    if v == "off":
        return 0
    if v not in names.envelope_numbers:
        r.fail(f"mod: no envelope is named {v!r} (envelopes.txt)")
    return names.envelope_numbers[v]


def pitch_byte(r, w, names):
    if w in names.note_bytes:
        return names.note_bytes[w]
    if w in names.drum_bytes:
        return names.drum_bytes[w]
    return None


def parse_stream_line(where, toks, names):
    """A line of a stream: its Items, and a label if it is `name:`."""
    r = Reader(where, toks)
    items = []
    while r.more():
        w = r.take("")
        if w.endswith(":") and NAME_RE.match(w[:-1]):
            items.append(w[:-1])
            continue
        if w == "bytes":
            data = []
            while r.more():
                data.append(r.byte("bytes"))
            if not data:
                r.fail("bytes: none given")
            items.append(Item(where, data))
            continue
        it = parse_flag(r, w, names)
        if it is not None:
            items.append(it)
            continue
        if w == "again":
            items.append(Item(where, [r.byte("again: a length", 127)]))
            continue
        if w == "freq":
            v = r.take("freq: the chip's frequency word")
            if not re.fullmatch(r"\$[0-9A-Fa-f]{1,4}", v):
                r.fail(f"freq: {v!r} is not a word like $2060")
            word = int(v[1:], 16)
            if word >> 8 >= 0xE0:
                r.fail(f"freq: {v} starts with ${word >> 8:02X}, which the "
                       f"driver reads as a flag")
            ln = r.byte("freq: a length")
            data = [word >> 8, word & 0xFF]
            if r.peek() == "slide":
                r.take("")
                data.append(r.signed("slide: a speed"))
            items.append(Item(where, data + [ln]))
            continue
        parts = w.split("+")
        first = pitch_byte(r, parts[0], names)
        if first is None:
            r.fail(f"{w!r} is not a note, a drum or a word this file knows")
        data = [first]
        for p in parts[1:]:
            if p not in names.note_bytes:
                r.fail(f"{w!r}: {p!r} is not a note")
            data.append(names.note_bytes[p])
        if len(parts) > 1 and first == 0x80:
            r.fail(f"{w!r}: a rest takes no chord parts")
        ln = None
        inner = None
        if (r.more() and r.peek().endswith(":") and r.i + 1 < len(r.toks)
                and NUMBER_RE.match(r.toks[r.i + 1])):
            inner = r.take("")[:-1]     # a label on the length byte
            if not NAME_RE.match(inner):
                r.fail(f"{inner!r} is not a label")
        if r.more() and NUMBER_RE.match(r.peek()):
            ln = r.byte("a length", 255)
        if r.peek() == "slide":
            r.take("")
            if ln is None or inner is not None:
                r.fail(f"{w}: a sliding note needs its length, and no label "
                       f"inside it")
            speed = r.signed("slide: a speed")
            unused = 0
            if r.peek() == "unused":
                r.take("")
                unused = r.byte("unused")
            items.append(Item(where, data + [unused, speed, ln]))
            continue
        if ln is not None and ln > 127:
            r.fail(f"{w} {ln}: a length is 0-127")
        if inner is not None:
            items += [Item(where, data), inner, Item(where, [ln])]
        else:
            items.append(Item(where, data + ([] if ln is None else [ln])))
    return items


def read_text(path):
    raw = Path(path).read_bytes()
    try:
        text = raw.decode("utf-8")
    except UnicodeDecodeError:
        raise TextError(f"{path}:1", "not UTF-8 text")
    return text.replace("\r", "").split("\n")


def header_line(where, toks, names, word="track"):
    """A `track` line to its nine header bytes and its start label."""
    r = Reader(where, toks)
    r.take(word)
    ch = r.take("the channel")
    if ch not in CHANNEL_BYTES:
        r.fail(f"{ch!r} is not a channel (" + ", ".join(CHANNEL_BYTES) + ")")
    label = r.name("where the stream starts")
    got = {}
    flags = 0x80
    bits = dict((n, b) for b, n in HEADER_FLAG_BITS)
    while r.more():
        k = r.take("")
        if k in got:
            r.fail(f"{k} given twice")
        if k == "tick":
            got[k] = r.byte("tick")
        elif k == "transpose":
            got[k] = r.signed("transpose")
        elif k == "mod":
            got[k] = envelope_number(r, names)
        elif k == "instrument":
            v = r.take("instrument")
            if v not in names.instrument_numbers:
                r.fail(f"instrument: no instrument is named {v!r}")
            got[k] = names.instrument_numbers[v]
        elif k == "volume":
            got[k] = r.byte("volume")
        elif k == "not_in_use":
            flags &= 0x7F
        elif k in bits:
            flags |= bits[k]
        else:
            r.fail(f"{k!r} is not a header field")
    for k in ("tick", "transpose", "mod", "instrument", "volume"):
        if k not in got:
            r.fail(f"the header has no {k}")
    return label, [flags, CHANNEL_BYTES[ch], got["tick"], 0, 0,
                   got["transpose"], got["mod"], got["instrument"],
                   got["volume"]]


def instrument_bank(fm_json):
    """fm_instruments.json to its names and each instrument's 26 bytes in
    the upload order, as instruments_load composes them."""
    import extract_music as em
    reg_of = {em.register_name(r): r for r in
              [0xB0, 0xB4] + [b + o for b in em.OPERATOR_REGISTERS
                              for o in em.OP_OF_OFFSET]}
    order = [reg_of[n] for n in fm_json["upload_order"]]
    pan_value = {v: k for k, v in em.PAN_NAMES.items()}
    bank, names = [], []
    for i, ins in enumerate(fm_json["instruments"]):
        if ins.get("number") != i:
            raise SystemExit(f"fm_instruments.json: instrument {i}'s number "
                             f"is not {i}")
        val = {r: 0 for r in order}
        for name, base, shift, bits in em.CHANNEL_FIELDS:
            v = ins[name]
            if name == "pan":
                v = pan_value[v]
            val[base] |= (v & ((1 << bits) - 1)) << shift
        for name, base, shift, bits in em.OPERATOR_FIELDS:
            for k, off in enumerate((0x0, 0x8, 0x4, 0xC)):
                v = ins[name][k]
                v = int(v) if not isinstance(v, bool) else int(v)
                val[base + off] |= (v & ((1 << bits) - 1)) << shift
        for reg_name, bit_list in ins.get("ignored_bits", {}).items():
            for b in bit_list:
                val[reg_of[reg_name]] |= 1 << b
        bank.append(bytes(val[r] for r in order))
        names.append(ins.get("name"))
    return bank, names


class Built:
    """The assembled image, and for each image byte the file:line it came
    from (for annotate and the loader's messages)."""

    def __init__(self):
        self.image = bytearray()
        self.where = {}
        self.tracks = []            # Track objects, for the heard walk
        self.labels = {}


def assemble(music_dir, freqs_json=None, drums_json=None, fm_json=None):
    """The text under `music_dir` (assets/music) to the driver's image."""
    md = Path(music_dir)
    freqs_json = freqs_json or json.loads((md / "note_frequencies.json").read_text())
    drums_json = drums_json or json.loads((md / "drums.json").read_text())
    fm_json = fm_json or json.loads((md / "fm_instruments.json").read_text())
    bank, ins_names = instrument_bank(fm_json)
    if any(n is None for n in ins_names):
        import asset_names
        ins_names = asset_names.INSTRUMENT_NAMES
    names = Names(freqs_json, drums_json, ins_names)
    if len(set(ins_names)) != len(ins_names):
        raise SystemExit("fm_instruments.json: two instruments share a name")

    # sounds.txt
    sounds = []                  # (id, name, priority, where)
    song_files = []
    shares = []
    sames = {}                   # instrument -> the one it is stored as
    gap = None
    layout = []                  # ("block", name, where) | ("bytes", Item)
    section = None
    path = md / "sounds.txt"
    for n, line in enumerate(read_text(path), 1):
        where = f"{path}:{n}"
        toks = tokens(line)
        if not toks:
            continue
        r = Reader(where, toks)
        w = toks[0]
        if w.startswith("$") and section is None:
            sid = r.byte("the sound id")
            nm = r.name("the sound's name")
            pr = r.byte("its priority")
            if r.more():
                r.fail("a sound line is: id, name, priority")
            want = FIRST_ID + len(sounds)
            if sid != want:
                r.fail(f"${sid:02X}: the ids run from $81 in order; ${want:02X} next")
            sounds.append((sid, nm, pr, where))
        elif w == "song_file" and section is None:
            r.take("")
            song_files.append((r.name("song_file: a name"), where))
            if r.more():
                r.fail("song_file NAME: one name a line")
        elif w == "instruments":
            section = "instruments"
        elif w == "same" and section == "instruments":
            r.take("")
            group = []
            while r.more():
                x = r.take("same")
                if x not in names.instrument_numbers:
                    r.fail(f"same: no instrument is named {x!r}")
                group.append(names.instrument_numbers[x])
            if len(group) < 2 or group != list(range(group[0], group[0] + len(group))):
                r.fail("same: two or more instruments, one after another")
            for k in group[1:]:
                sames[k] = group[0]
        elif w == "share" and section == "instruments":
            r.take("")
            a, b = r.take("share A"), r.take("share B")
            k = r.byte("share: how many bytes", INSTRUMENT_BYTES - 1)
            for x in (a, b):
                if x not in names.instrument_numbers:
                    r.fail(f"share: no instrument is named {x!r}")
            if names.instrument_numbers[b] != names.instrument_numbers[a] + 1:
                r.fail(f"share: {b} is not the instrument after {a}")
            shares.append((names.instrument_numbers[b], k))
        elif w == "driver_code":
            r.take("")
            gap = r.byte("driver_code: its bytes", 0xFFFF)
        elif w == "streams":
            section = "streams"
        elif section == "streams" and w == "block":
            r.take("")
            layout.append(("block", r.name("block: a name"), where))
        elif section == "streams" and w == "bytes":
            items = parse_stream_line(where, toks, names)
            layout.append(("bytes", items[0], where))
        else:
            r.fail(f"{w!r}: not a line sounds.txt has")
    if gap is None:
        raise TextError(f"{path}", "no driver_code line")
    names.sound_ids = {}
    for sid, nm, _, _ in sounds:
        names.sound_ids.setdefault(nm, sid)
        names.sounds[sid] = nm
    if len(sounds) < FIRST_SPECIAL_ID - FIRST_ID + 1:
        raise TextError(f"{path}", "the sound ids stop before $D8")

    # envelopes.txt
    envs = []                    # (name, [Item], {label: index})
    path = md / "envelopes.txt"
    cur = None
    for n, line in enumerate(read_text(path), 1):
        where = f"{path}:{n}"
        toks = tokens(line)
        if not toks:
            continue
        r = Reader(where, toks)
        if toks[0] == "envelope":
            r.take("")
            cur = (r.name("envelope: a name"), [], {})
            envs.append(cur)
            continue
        if cur is None:
            r.fail("a line before the first `envelope`")
        if len(toks) == 1 and toks[0].endswith(":"):
            cur[2][toks[0][:-1]] = sum(len(i.data) for i in cur[1])
            continue
        while r.more():
            w = r.take("")
            if w == "depth":
                cur[1].append(Item(where, [0x84, r.signed("depth")]))
            elif w == "jump":
                cur[1].append(Item(where, [0x85, 0], [(1, r.name("jump"), None)]))
            elif w in ENV_CODE_BYTES:
                cur[1].append(Item(where, [ENV_CODE_BYTES[w]]))
            elif SIGNED_RE.match(w) or w.isdigit():
                v = int(w)
                if not -128 <= v <= 127 or (v & 0xFF) in (0x80, 0x81, 0x83, 0x84, 0x85):
                    r.fail(f"{w}: a step is -128 to 127, but not -128, -127, "
                           f"-125, -124 or -123 (the codes' bytes)")
                cur[1].append(Item(where, [v & 0xFF]))
            else:
                r.fail(f"{w!r} is not a step or an envelope word")
    names.envelopes = [e[0] for e in envs]
    names.envelope_numbers = {e[0]: i + 1 for i, e in enumerate(envs)}

    # The songs and the effects.
    songs = {}                   # name -> dict(tempo, pal_tempo, pal_tick, tracks)
    blocks = {}                  # name -> [(label|None, Item)]
    effects = []                 # [(name, tracks, unused, [(label, Item)])]

    def read_streams(path, kind):
        cur_song = None
        cur_block = None
        cur_effect = None
        for n, line in enumerate(read_text(path), 1):
            where = f"{path}:{n}"
            toks = tokens(line)
            if not toks:
                continue
            w = toks[0]
            r = Reader(where, toks)
            if kind == "song" and w == "song":
                if cur_block is not None:
                    r.fail("a `song` line after the blocks")
                r.take("")
                nm = r.name("song: a name")
                if nm in songs:
                    r.fail(f"song {nm} given twice")
                got = {}
                while r.more():
                    k = r.take("")
                    if k not in ("tempo", "pal_tempo", "pal_tick") or k in got:
                        r.fail(f"{k!r}: a song line is `song NAME tempo N "
                               f"pal_tempo N pal_tick N`")
                    got[k] = r.byte(k)
                for k in ("tempo", "pal_tempo", "pal_tick"):
                    if k not in got:
                        r.fail(f"song {nm}: no {k}")
                cur_song = dict(got, tracks=[], where=where)
                songs[nm] = cur_song
                continue
            if kind == "effect" and w == "effect":
                r.take("")
                cur_effect = (r.name("effect: a name"), [], [], [], where)
                effects.append(cur_effect)
                continue
            if w in ("track", "unused_track"):
                owner = cur_song if kind == "song" else cur_effect
                if owner is None or (kind == "song" and cur_block is not None):
                    r.fail(f"a `{w}` line outside a song's or effect's header")
                lbl, hb = header_line(where, toks, names, w)
                if kind == "song":
                    if w != "track":
                        r.fail("unused_track: effects only")
                    owner["tracks"].append((lbl, hb, where))
                else:
                    if owner[3]:
                        r.fail(f"a `{w}` line after the effect's stream began")
                    (owner[1] if w == "track" else owner[2]).append((lbl, hb, where))
                    if w == "track" and owner[2]:
                        r.fail("a `track` line after an `unused_track`")
                continue
            if kind == "song" and w == "block":
                r.take("")
                nm = r.name("block: a name")
                if nm in blocks:
                    r.fail(f"block {nm} given twice")
                cur_block = [(nm, None)]
                blocks[nm] = (cur_block, where)
                continue
            target = cur_block if kind == "song" else (cur_effect[3] if cur_effect else None)
            if target is None:
                r.fail("a stream line outside a block" if kind == "song"
                       else "a stream line before the first `effect`")
            for it in parse_stream_line(where, toks, names):
                target.append((it, None) if isinstance(it, str) else (None, it))

    read_streams(md / "effects.txt", "effect")
    for f, where in song_files:
        p = md / "songs" / f"{f}.txt"
        if not p.is_file():
            raise TextError(where, f"song_file {f}: no {p}")
        read_streams(p, "song")

    # Lay the image out.
    b = Built()
    img = b.image
    where_of = b.where
    labels = b.labels
    fixups = []                  # (image offset, label, second byte offset|None, where)

    def put(item_or_bytes, where=None):
        at = len(img)
        if isinstance(item_or_bytes, Item):
            img.extend(item_or_bytes.data)
            for k in range(len(item_or_bytes.data)):
                where_of[at + k] = item_or_bytes.where
            for off, lbl, second in item_or_bytes.fixups:
                fixups.append((at + off, lbl, None if second is None else at + second,
                               item_or_bytes.where))
        else:
            img.extend(item_or_bytes)
            if where is not None:
                for k in range(len(item_or_bytes)):
                    where_of[at + k] = where
        return at

    def define(lbl, where):
        if lbl in labels:
            raise TextError(where, f"label {lbl} defined twice")
        labels[lbl] = len(img)

    def place(entries):
        for lbl, it in entries:
            if lbl is not None:
                define(lbl, "")
            else:
                put(it)

    roots_at = put(bytes(2 * ROOTS))
    prio_at = put(bytes(pr for _, _, pr, _ in sounds[:FIRST_SPECIAL_ID - FIRST_ID + 1]))
    music_ids = [s for s in sounds if s[0] < FIRST_EFFECT_ID]
    effect_ids = [s for s in sounds if FIRST_EFFECT_ID <= s[0] < FIRST_SPECIAL_ID]
    special_ids = [s for s in sounds if s[0] >= FIRST_SPECIAL_ID]
    for sid, nm, _, where in music_ids:
        if nm != "none" and nm not in songs:
            raise TextError(where, f"no song is named {nm!r} (songs/)")
    for sid, nm, _, where in effect_ids + special_ids:
        if nm != "none" and nm not in [e[0] for e in effects]:
            raise TextError(where, f"no effect is named {nm!r} (effects.txt)")
    tempo_at = put(bytes(songs[nm]["tempo"] if nm in songs else 0
                         for _, nm, _, _ in (music_ids + [(0, "", 0, "")] * TEMPO_ENTRIES)[:TEMPO_ENTRIES]))
    env_ptrs_at = put(bytes(2 * len(envs)))
    for i, (nm, items, lbls) in enumerate(envs):
        at = len(img)
        img[env_ptrs_at + 2 * i: env_ptrs_at + 2 * i + 2] = bytes([at & 0xFF, at >> 8])
        for it in items:
            for off, lbl, _ in it.fixups:
                if lbl not in lbls:
                    raise TextError(it.where, f"jump: no label {lbl} in envelope {nm}")
                it.data[off] = lbls[lbl]
            it.fixups = []
            put(it)
    if len(img) > PAL_TEMPO_AT:
        raise TextError(str(md / "envelopes.txt"),
                        f"the envelopes end at ${len(img):X}, past $100 where "
                        f"the PAL tables are read")
    put(bytes(PAL_TEMPO_AT - len(img)))
    put(bytes(songs[nm]["pal_tempo"] if nm in songs else 0
              for _, nm, _, _ in (music_ids + [(0, "", 0, "")] * TEMPO_ENTRIES)[:TEMPO_ENTRIES]))
    put(bytes(songs[nm]["pal_tick"] if nm in songs else 0
              for _, nm, _, _ in (music_ids + [(0, "", 0, "")] * TEMPO_ENTRIES)[:TEMPO_ENTRIES]))
    music_ptrs_at = put(bytes(2 * len(music_ids)))
    sfx_ptrs_at = put(bytes(2 * len(effect_ids)))
    spc_ptrs_at = put(bytes(2 * len(special_ids)))
    header_at = {}

    def put_header(nm, tracks, unused=()):
        at = put(bytes([len(tracks)]))
        for lbl, hb, where in list(tracks) + list(unused):
            h = put(Item(where, hb))
            fixups.append((h + 3, lbl, "abs", where))
        return at

    for sid, nm, _, _ in music_ids:
        if nm in header_at:
            continue
        if nm == "none":
            continue
        header_at[nm] = put_header(nm, songs[nm]["tracks"])
    if any(nm == "none" for _, nm, _, _ in music_ids):
        header_at["none_song"] = put(bytes([0]))
    ins_ptrs_at = put(bytes(2 * len(bank)))
    share = dict(shares)
    stored = {}
    for i, ins in enumerate(bank):
        if i in sames and bank[sames[i]] == ins:
            at = stored[sames[i]]
        elif i in share and i - 1 not in sames and \
                bank[i - 1][-share[i]:] == ins[:share[i]]:
            at = len(img) - share[i]
            put(ins[share[i]:])
        else:
            at = put(ins)
        stored[i] = at
        img[ins_ptrs_at + 2 * i: ins_ptrs_at + 2 * i + 2] = bytes([at & 0xFF, at >> 8])
    for nm, tracks, unused, stream, where in effects:
        if nm in header_at:
            raise TextError(where, f"effect {nm} given twice")
        header_at[nm] = put_header(nm, tracks, unused)
        place(stream)
    if any(nm == "none" for _, nm, _, _ in effect_ids):
        header_at["none_effect"] = put(bytes([0]))
    put(bytes(gap))
    freqs = freqs_json["entries"]
    for e in freqs:
        w = (e["block"] << 11) | e["fnum"]
        put(bytes([w >> 8, w & 0xFF]))
    placed = set()
    for kind, x, where in layout:
        if kind == "bytes":
            put(x)
            continue
        if x not in blocks:
            raise TextError(where, f"no block is named {x!r} (songs/)")
        if x in placed:
            raise TextError(where, f"block {x} placed twice")
        placed.add(x)
        entries, _ = blocks[x]
        place(entries)
    for nm, (_, where) in blocks.items():
        if nm not in placed:
            raise TextError(where, f"block {nm} is not placed in sounds.txt's streams")
    if len(img) > 0x10000:
        raise SystemExit(f"the image is {len(img)} bytes, past the driver's 64 KB")

    # Pointers and jumps.
    def addr(lbl, where):
        if lbl not in labels:
            raise TextError(where, f"no label {lbl}")
        return labels[lbl]
    for at, lbl, second, where in fixups:
        t = addr(lbl, where)
        if second == "abs":
            img[at:at + 2] = bytes([t & 0xFF, t >> 8])
        else:
            d = (t - second) & 0xFFFF
            img[at:at + 2] = bytes([d & 0xFF, d >> 8])
    roots = [prio_at, tempo_at, music_ptrs_at, ins_ptrs_at, env_ptrs_at,
             music_ptrs_at, sfx_ptrs_at]
    for i, v in enumerate(roots):
        img[roots_at + 2 * i: roots_at + 2 * i + 2] = bytes([v & 0xFF, v >> 8])
    for table, ids, none in ((music_ptrs_at, music_ids, "none_song"),
                             (sfx_ptrs_at, effect_ids, "none_effect"),
                             (spc_ptrs_at, special_ids, None)):
        for k, (sid, nm, _, where) in enumerate(ids):
            if nm == "none" and none is None:
                raise TextError(where, "a special id needs an effect")
            at = header_at[none if nm == "none" else nm]
            img[table + 2 * k: table + 2 * k + 2] = bytes([at & 0xFF, at >> 8])
    b.spc_sfx_ptrs = spc_ptrs_at
    b.music_count, b.sfx_count = len(music_ids), len(effect_ids)
    b.names = names
    b.headers = header_at
    b.songs, b.effects = songs, effects
    return b


# ---------------------------------------------------------------------------
# The heard notes.

HEARD_RE = re.compile(r"\s*# heard:.*$")


def built_tracks(b):
    """Every counted track of every song and effect in a Built image."""
    img = b.image
    out = []
    for nm, at in b.headers.items():
        if nm.startswith("none_"):
            continue
        for i in range(img[at]):
            h = at + 1 + 9 * i
            out.append(Track(nm, h, img[h:h + 9], True))
    return out


def heard_notes(b):
    """Image offset -> what each note event there is heard as, in the order
    first met: a note's byte plus its track's transpose, six bits kept
    (GetFMFreq), each chord part by its own part's transpose."""
    names = b.names
    heard = {}
    b.heard_parts = {}
    for t in built_tracks(b):
        if t.drums:
            continue

        def on(ev, state):
            if ev.kind not in ("note", "slide") or ev.info[0] == 0x80:
                return
            tr, part_tr = state
            parts = [names.heard(ev.info[0], tr)]
            parts += [names.heard(p, part_tr[k] if k < len(part_tr) else tr)
                      for k, p in enumerate(ev.info[1])]
            text = "+".join(parts)
            b.heard_parts[ev.at] = len(ev.info[1])
            got = heard.setdefault(ev.at, [])
            if text not in got:
                got.append(text)
        walk(b.image, t, on)
    return heard


def annotate(music_dir, write=True):
    """Rewrites every `# heard:` comment: on each line with a note heard as
    other than it is written (a transpose, or a byte past $BF wrapping),
    the line's notes as heard. Returns the files changed."""
    b = assemble(music_dir)
    names = b.names
    heard = heard_notes(b)
    by_line = {}
    for at in sorted(heard):
        by_line.setdefault(b.where[at], []).append(at)
    changed = []
    files = sorted({w.rsplit(":", 1)[0] for w in b.where.values()})
    for path in files:
        lines = read_text(path)
        new = []
        for n, line in enumerate(lines, 1):
            line = HEARD_RE.sub("", line)
            ats = by_line.get(f"{path}:{n}", [])
            differs = False
            texts = []
            for at in ats:
                ev_written = written_note(b, at)
                hs = heard[at]
                if hs != [ev_written]:
                    differs = True
                texts.append("|".join(hs))
            if differs:
                line = f"{line}    # heard: " + "  ".join(texts)
            new.append(line)
        text = "\n".join(new)
        if text != "\n".join(read_text(path)):
            changed.append(path)
            if write:
                Path(path).write_text(text, newline="\n")
    return changed


def written_note(b, at):
    """The note event at `at` as written: its first byte and, in chord mode,
    the parts after it (as many as the heard text has)."""
    names = b.names
    img = b.image
    n_parts = 0
    if at in b.heard_parts:
        n_parts = b.heard_parts[at]
    return "+".join(names.note(img[at + k]) for k in range(1 + n_parts))


# ---------------------------------------------------------------------------
# The command line.

def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("action", choices=["check", "annotate"])
    ap.add_argument("--music", type=Path, default=Path("assets/music"),
                    help="the music directory (assets/music)")
    args = ap.parse_args()
    try:
        if args.action == "check":
            b = assemble(args.music)
            print(f"{args.music}: {len(b.image)} bytes, "
                  f"{len(built_tracks(b))} tracks, {len(b.labels)} labels")
        else:
            changed = annotate(args.music)
            print(f"heard notes rewritten in {len(changed)} files")
            for p in changed:
                print(f"  {p}")
    except TextError as exc:
        print(f"song_text: {exc}", file=sys.stderr)
        sys.exit(1)


if __name__ == "__main__":
    main()
