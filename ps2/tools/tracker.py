#!/usr/bin/env python3
"""(build 139) The songs laid out as a tracker: rows of ticks, a column
group a channel, patterns and per-channel order lists.

The owner, after build 138e: the stream text (songs/*.txt, build 137) reads a
part at a time; laid out like an old tracker (FastTracker II), the parts can
be read against each other and edited by position. docs/audio.md, "The
tracker format", has the format; this module

    export   walks every track of a song as the sequencer does (the timed
             walk below) and writes songs/<name>.trk
    read     reads a .trk file back into the stream text's words, which
             song_text.py's assembler (and the game's songtext.c) builds

and the proof that the two play the same: the chip writes of the stream
text's image and the tracker's, frame for frame (smps_trace --regs).
(build 139b) The game reads assets/music/songs/<song>.trk in place of the
song's stream text (`make extract-music` writes them); game_music is that
choice in Python, `image` the image it builds -- songtext.c's, byte for
byte (`smps_trace --image`).

    python3 tools/tracker.py export [--music DIR] [--out DIR] [SONG ...]
    python3 tools/tracker.py streams FILE.trk       the stream text it reads as
    python3 tools/tracker.py prove [--music DIR] [--smps-trace PROGRAM]
    python3 tools/tracker.py image OUT [--music DIR]   the game's image

The timed walk mirrors song_text.walk (ps2.asm: ProcFMTrack, UpdateDrumTrack,
cfHandler, TickMultiplier, FinishTrkUpdate) and adds the time: a note's
length is its written length times the track's tick multiplier
(TickMultiplier), in counted ticks; a note without a length byte keeps the
last multiplied length (FinishTrkUpdate copies $E). A counted tick is one
update of the track: one 60 Hz frame, less the frames DoTempo steals (every
`tempo`-th frame adds 1 to every music track's remaining length, so that
frame does not advance the notes).
"""

import json
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import song_text as stx            # noqa: E402

# ---------------------------------------------------------------------------
# The timed walk.

FLOW = (0xF6, 0xF7, 0xF8, 0xF9)    # goto, loop, call, return
HOLD = 0xE6                        # cfE6_HoldOn: also what DoNoteStop looks for


class Ev:
    """One timed event of a track: what starts at `time` and lasts `dur`
    counted ticks, with the flags read just before it (in order)."""

    __slots__ = ("time", "dur", "kind", "at", "size", "flags", "note",
                 "parts", "length", "held", "state", "mult")

    def __init__(self, **kw):
        for k in self.__slots__:
            setattr(self, k, kw.get(k))


class TrackTime:
    """A track's events from its start, and where it repeats: `loop` the
    index of the event the track comes back to (the song's repeat), None
    when the track ends."""

    def __init__(self, track, events, loop, end_flags):
        self.track = track
        self.events = events
        self.loop = loop
        self.end_flags = end_flags      # flags read before the track's end

    @property
    def loop_time(self):
        return None if self.loop is None else self.events[self.loop].time

    @property
    def length(self):
        if not self.events:
            return 0
        e = self.events[-1]
        return e.time + e.dur


def timed_walk(image, track, limit=200000):
    """Follows `track` as the sequencer does (song_text.walk's rules) and
    returns its TrackTime. The repeat is found when the whole state comes
    round again -- the place in the stream, the modes, chord parts,
    transposes, loop counters, the call stack, the tick multiplier, the
    last length, the volume and the instrument -- so a part whose loop
    drifts (a `volume +2` each time round) is followed until it does not."""
    seen = {}
    events = []
    a = track.start
    raw = bool(track.flags & 0x08)
    slide = bool(track.flags & 0x20)
    chord = 0
    tr = track.transpose
    part_tr = ()
    counters = {}
    stack = ()
    mult = track.tick
    final = 0
    volume = track.volume
    instrument = track.instrument
    time = 0
    flags = []
    steps = 0

    def byte(i):
        if i >= len(image):
            raise SystemExit(f"{track.name}: the stream runs past the image "
                             f"at ${i:04X}")
        return image[i]

    while steps < limit:
        state = (a, raw, slide, chord, tr, part_tr,
                 tuple(sorted(counters.items())), stack, mult, final,
                 volume, instrument, tuple(flags))
        if state in seen:
            return TrackTime(track, events, seen[state], None)
        seen[state] = len(events)
        entry_raw = raw
        while True:
            steps += 1
            b = byte(a)
            if b < 0xE0:
                break
            n = stx.OPERANDS[b]
            args = bytes(byte(a + 1 + k) for k in range(n))
            if b == 0xF6:
                a = stx.disp_target(image, a + 2)
                continue
            if b == 0xF7:
                slot, count = args[0], args[1]
                c = counters.get(slot, 0)
                if c == 0:
                    c = count
                c = (c - 1) & 0xFF
                counters[slot] = c
                a = stx.disp_target(image, a + 4) if c else a + 5
                continue
            if b == 0xF8:
                stack = stack + (a + 1,)
                a = stx.disp_target(image, a + 2)
                continue
            if b == 0xF9:
                if not stack:
                    return TrackTime(track, events, None, flags)
                a = stack[-1] + 2
                stack = stack[:-1]
                continue
            if b in stx.ENDS:
                flags.append((b, args))
                return TrackTime(track, events, None, flags)
            flags.append((b, args))
            if b == 0xFC:
                slide = args[0] != 0
            elif b == 0xFD:
                raw = args[0] != 0
            elif b == 0xFB:
                tr = stx.signed((tr + args[0]) & 0xFF)
            elif b == 0xE5:
                chord = args[0]
                part_tr = (tr,) * max(chord - 1, 0)
            elif b == 0xFA:
                mult = args[0]
            elif b == 0xEC:
                volume = (volume + args[0]) & 0xFF
            elif b == 0xF0:
                volume = args[0]
            elif b == 0xEF:
                instrument = args[0]
            a += 1 + n
        b = byte(a)
        note, parts, ln, kind = None, [], None, None
        if track.drums:
            if b >= 0x80:
                kind = "drum"
                note = b
                ln = byte(a + 1) if byte(a + 1) < 0x80 else None
                size = 1 if ln is None else 2
            else:
                kind, ln, size = "again", b, 1
        elif entry_raw or slide:
            raise SystemExit(f"{track.name}: raw-frequency or pitch-slide mode "
                             f"at ${a:04X}: the tracker takes songs, which "
                             f"use neither")
        elif b < 0x80:
            kind, ln, size = "again", b, 1
        else:
            kind = "rest" if b == 0x80 else "note"
            note = b
            k = a + 1
            if b != 0x80 and chord:
                parts = [byte(k + i) for i in range(chord - 1)]
                k += chord - 1
            ln = byte(k) if byte(k) < 0x80 else None
            size = k - a + (0 if ln is None else 1)
        # cfE6 / cfE7 right before a length make it a tie (the hold bit set:
        # not struck) or a release (keyed off, then the hold bit set).
        if kind == "again" and flags and flags[-1][0] in (0xE6, 0xE7):
            kind = "tie" if flags[-1][0] == 0xE6 else "release"
            flags = flags[:-1]
        if ln is not None:
            final = (ln * mult) & 0xFFFF
        held = byte(a + size) == HOLD
        events.append(Ev(time=time, dur=final, kind=kind, at=a, size=size,
                         flags=flags, note=note, parts=parts, length=ln,
                         held=held, mult=mult,
                         state=(tr, part_tr, chord, volume, instrument)))
        flags = []
        time += final
        a += size
        steps += 1
    raise SystemExit(f"{track.name}: no repeat after {limit} steps")


# ---------------------------------------------------------------------------
# A song's channels as timelines of cells.

CHANNEL_ORDER = ["drums", "fm1", "fm2", "fm3", "fm4", "fm5", "fm6"]


class Cell:
    """What a channel does at one tick: strike notes (`notes`: entry
    numbers, the lead's first, then each chord part's), a drum, `===`
    (silence: a rest or a release), `tie` (the note held on, not struck),
    with the changes read before it (`words`, in the order the driver reads
    them) and whether the note before it ends 3 ticks early (DoNoteStop) or
    is held to it."""

    __slots__ = ("time", "kind", "notes", "drum", "words", "held", "dur",
                 "flags", "art")

    def __init__(self, time, kind, notes=(), drum=None, words=(), held=False,
                 dur=0, flags=()):
        self.time, self.kind, self.notes, self.drum = time, kind, tuple(notes), drum
        self.words, self.held, self.dur = tuple(words), held, dur
        self.flags = tuple(flags)
        self.art = None         # "hold", "cut" or None: see set_marks

    def key(self):
        return (self.kind, self.notes, self.drum, self.words, self.art,
                self.dur)


class Channel:
    def __init__(self, name, track, cells, end, loop_to, end_words):
        self.name = name
        self.track = track
        self.cells = cells          # Cells, time order, from 0 to `end`
        self.end = end              # the tick the channel's last cell ends
        self.loop_to = loop_to      # the tick it goes back to; None: it stops
        self.end_words = end_words  # changes read before it stops (`sound X`)


def channel_cells(tt, names):
    """A TrackTime's events as cells, one pass of the loop after the intro
    (times from 0), with the transposes folded into the notes and the
    volume written as the driver's value."""
    t = tt.track
    cells = []
    volume = t.volume
    last_notes = None
    last_drum = None
    for e in tt.events:
        words = []
        for b, args in e.flags:
            if b == 0xFB or b == 0xE6:
                continue        # transposes are folded in; a bare hold is `held`
            if b == 0xEC:
                volume = (volume + args[0]) & 0xFF
                words.append(f"volume {volume}")
            elif b == 0xF0:
                volume = args[0]
                words.append(f"volume {volume}")
            else:
                words.append(stx.flag_text(b, args, names, None, 0))
        tr, part_tr = e.state[0], e.state[1]
        if e.kind == "note":
            lead = (e.note + tr) & 0x3F
            parts = [(p + (part_tr[k] if k < len(part_tr) else tr)) & 0x3F
                     for k, p in enumerate(e.parts)]
            last_notes = (lead, *parts)
            c = Cell(e.time, "note", notes=last_notes)
        elif e.kind == "again" and not t.drums:
            if last_notes is None:
                raise SystemExit(f"{t.name}: `again` before any note")
            c = Cell(e.time, "note", notes=last_notes)
        elif e.kind == "drum":
            last_drum = e.note
            c = Cell(e.time, "drum", drum=e.note)
        elif e.kind == "again":
            if last_drum is None:
                raise SystemExit(f"{t.name}: `again` before any drum")
            c = Cell(e.time, "drum", drum=last_drum)
        elif e.kind in ("rest", "release"):
            c = Cell(e.time, "off")
        elif e.kind == "tie":
            c = Cell(e.time, "tie")
        else:
            raise SystemExit(f"{t.name}: {e.kind}")
        c.words = tuple(words)
        c.held = e.held and not t.drums
        c.dur = e.dur
        c.flags = tuple(e.flags)
        cells.append(c)
    return cells


def song_channels(b, song):
    """The song's channels, each unrolled to end within one event of the
    others: a channel whose part repeats sooner (Over's drums) is written out
    until the longest repeat, and every channel goes back to the latest
    place it can so the ends line up."""
    img = b.image
    at = b.headers[song]
    tracks = [stx.Track(song, at + 1 + 9 * i, img[at + 1 + 9 * i: at + 10 + 9 * i], True)
              for i in range(img[at])]
    times = [timed_walk(img, t) for t in tracks]
    periods = [tt.length - tt.loop_time for tt in times if tt.loop is not None]
    period = max(periods) if periods else None
    for p in periods:
        if period % p:
            raise SystemExit(f"{song}: parts repeat every {p} and {period} "
                             f"ticks; the tracker lays them out together only "
                             f"when one divides the other")
    target = max((tt.loop_time + period for tt in times if tt.loop is not None),
                 default=None)
    chans = []
    for t, tt in zip(tracks, times):
        name = stx.CHANNELS[t.channel]
        cells = channel_cells(tt, b.names)
        if tt.loop is None:
            chans.append(Channel(name, t, cells, tt.length, None,
                                 [stx.flag_text(f, a, b.names, None, 0)
                                  for f, a in tt.end_flags
                                  if f not in stx.ENDS]))
            continue
        # Unroll the loop until a pass ends at or past the target, then go
        # back to the latest cell that lies a whole period before the end.
        loop_cells = cells[tt.loop:]
        p = tt.length - tt.loop_time
        k = 1
        while cells[-1].time + cells[-1].dur < target:
            for c in loop_cells:
                n = Cell(c.time + k * p, c.kind, c.notes, c.drum, c.words,
                         c.held, c.dur, c.flags)
                cells.append(n)
            k += 1
        # The latest cell start t with t >= the loop and t + period <= target.
        back = max(c.time for c in cells
                   if c.time >= tt.loop_time and c.time + period <= target)
        end = back + period
        cells = [c for c in cells if c.time < end]
        chans.append(Channel(name, t, cells, end, back, []))
    for ch in chans:
        set_marks(ch)
    return chans


def set_marks(ch):
    """A note before a `tie` is held to it, any other let go 3 ticks early
    (DoNoteStop); `hold` and `cut` mark the ones that go the other way --
    the next cell after the last is the one the channel goes back to."""
    if ch.track.drums:
        return
    for i, c in enumerate(ch.cells):
        if i + 1 < len(ch.cells):
            nxt = ch.cells[i + 1]
        elif ch.loop_to is not None:
            nxt = next(x for x in ch.cells if x.time == ch.loop_to)
        else:
            nxt = None
        default = nxt is not None and nxt.kind == "tie"
        if c.held and not default:
            c.art = "hold"
        elif default and not c.held:
            c.art = "cut"


# ---------------------------------------------------------------------------
# Sections: the song cut into spans of the same length, every channel side by
# side; a channel whose span repeats an earlier one is written once.

SECTION_TICKS = (96, 144, 192, 288, 384)
ROW_TICKS = (48, 24, 12, 6, 4, 3, 2, 1)
MAX_DELAYED = 2           # cells a section may start between rows


class Section:
    def __init__(self, index, start, length):
        self.index, self.start, self.length = index, start, length
        self.row = 1
        self.copy = {}          # channel name -> the section it repeats
        self.cells = {}         # channel name -> [Cell] starting here
        self.marks = {}         # channel name -> (tick, words) its end, here

    @property
    def rows(self):
        return -(-self.length // self.row)


def column_key(sec, ch):
    cells = sec.cells.get(ch, [])
    mark = sec.marks.get(ch)
    return (sec.length,
            tuple((c.time - sec.start,) + c.key() for c in cells),
            None if mark is None else (mark[0] - sec.start, mark[1]))


def cut_sections(chans, length):
    end = max(max(c.end for c in chans), 1)
    secs = []
    start = 0
    while start < end or not secs:
        secs.append(Section(len(secs), start, min(length, end - start)))
        start += length
    # The last section holds the ends of the channels that end on it.
    last = secs[-1]
    for ch in chans:
        if ch.end == last.start + last.length:
            last.length += 1
            break
    for ch in chans:
        for c in ch.cells:
            k = min(c.time // length, len(secs) - 1)
            secs[k].cells.setdefault(ch.name, []).append(c)
        k = min(ch.end // length, len(secs) - 1)
        if ch.end >= secs[k].start + secs[k].length:
            secs[k].length = ch.end - secs[k].start + 1
        secs[k].marks[ch.name] = (ch.end, ch)
    return secs


def find_copies(secs, chans):
    for ch in chans:
        first = {}
        for s in secs:
            key = column_key(s, ch.name)
            if ch.name in s.marks or not s.cells.get(ch.name):
                first.setdefault(key, s.index)
                continue
            if key in first:
                s.copy[ch.name] = first[key]
            else:
                first[key] = s.index


def row_fits(s, row, chans, full=False):
    """Cells between rows in the section's written columns (every column
    when copies are written out too), or None when two cells of a column
    fall in one row."""
    delayed = 0
    for ch in chans:
        if ch.name in s.copy and not full:
            continue
        times = [c.time for c in s.cells.get(ch.name, [])]
        if ch.name in s.marks:
            times.append(s.marks[ch.name][0])
        rows = set()
        for t in times:
            r, d = divmod(t - s.start, row)
            if r in rows:
                return None
            rows.add(r)
            delayed += d != 0
    return delayed


def choose_rows(secs, chans, full=False):
    """Each section's row: the song's usual row where it fits, else the
    largest that does (a few cells may start between rows, `@N`)."""
    def fits(s, r):
        if s.length % r and s is not secs[-1]:
            return False
        d = row_fits(s, r, chans, full)
        return d is not None and d <= MAX_DELAYED
    usual = 1
    for r in ROW_TICKS:
        ok = sum(1 for s in secs if fits(s, r))
        if ok >= 0.8 * len(secs):
            usual = r
            break
    for s in secs:
        if fits(s, usual):
            s.row = usual
            continue
        for r in ROW_TICKS:
            if r < usual and fits(s, r):
                s.row = r
                break
        else:
            s.row = 1


def layout(chans, full=False):
    """The section length that writes the least, and its sections."""
    best = None
    for length in SECTION_TICKS:
        secs = cut_sections(chans, length)
        find_copies(secs, chans)
        choose_rows(secs, chans, full)
        written = sum(s.rows * (1 + sum(1 for ch in chans
                                        if ch.name not in s.copy))
                      for s in secs) + 4 * len(secs)
        if best is None or written < best[0]:
            best = (written, length, secs)
    return best[1], best[2]


# ---------------------------------------------------------------------------
# Writing a song.

FILE_HEADER = """\
# {title} (build 139): the song laid out as a tracker. docs/audio.md, "The
# tracker format", has the whole format; the game plays this file in place
# of the song's stream text (build 139b). In short:
#
#   song        the tempo: a tick is lost every `tempo`-th 60 Hz frame (0:
#               none), so {tps} ticks a second here; a beat of 24 ticks is
#               {bpm} BPM. The PAL tempo and tick multiplier follow.
#   channel     a part and how it starts: its tick multiplier (a length is
#               written in ticks, so this only says what lengths it can
#               take: a multiple of it, up to 127 times it), vibrato,
#               instrument and volume (an attenuation: higher is quieter)
#   section     a span of the song, every channel side by side: N rows of T
#               ticks. A column headed `fm1 = 03` plays section 03's fm1
#               again; its cells are left blank (or, if written, must match)
#   a cell      the notes struck (as heard: B0 to C#6; a chord one note a
#               part), a drum, `===` silence, `tie` the note held on, `...`
#               nothing new; then `@N` (struck N ticks after the row),
#               `hold` / `cut` (below) and the changes, in the order the
#               driver makes them: the song text's words (instrument NAME,
#               volume N, mod, pan_anim, chord, tempo, ...)
#   goto SS.RR  where a channel ends and goes back to (section SS, row RR);
#               `end` where it stops
#
# A note lasts until the next cell in its column, and the driver lets it go
# 3 ticks before then -- unless the next cell is a `tie`. `hold` on a note
# keeps it sounding up to the next note anyway; `cut` lets it go before a
# `tie` (which then holds silence), as the ROM's songs do in places.
"""


def ft2_note(name):
    """B0 -> B-0, C#5 -> C#5."""
    return name if "#" in name else name[0] + "-" + name[1:]


def ticks_a_second(tempo):
    if tempo == 0:
        return 60.0
    return 60.0 * (tempo - 1) / tempo


def number_text(v):
    return f"{v:g}" if v == int(v) else f"{v:.1f}"


def cell_text(c, names):
    if c.kind == "drum":
        toks = [names.drum_of_byte.get(c.drum, f"${c.drum:02X}")]
    elif c.kind == "note":
        toks = [ft2_note(names.note_of_entry[n]) for n in c.notes]
    elif c.kind == "off":
        toks = ["==="]
    else:
        toks = ["tie"]
    return toks


def render_cell(c, ch, sec, names):
    toks = cell_text(c, names)
    d = (c.time - sec.start) % sec.row
    if d:
        toks.append(f"@{d}")
    if c.art:
        toks.append(c.art)
    toks += list(c.words)
    return " ".join(toks)


def section_label(k):
    return f"{k:02d}"


def goto_text(secs, t):
    for s in secs:
        if s.start <= t < s.start + s.length:
            r, d = divmod(t - s.start, s.row)
            return f"goto {section_label(s.index)}.{r:02d}" + (f"@{d}" if d else "")
    raise SystemExit(f"no section holds tick {t}")


def write_song(b, song, full=False):
    """The tracker text of `song`."""
    names = b.names
    chans = song_channels(b, song)
    length, secs = layout(chans, full)
    info = b.songs[song]
    tps = ticks_a_second(info["tempo"])
    out = [FILE_HEADER.format(title=song.replace("_", " ").title(),
                              tps=number_text(tps),
                              bpm=number_text(tps * 60 / 24))]
    out.append(f"song {song}  tempo {info['tempo']}  pal_tempo "
               f"{info['pal_tempo']}  pal_tick {info['pal_tick']}")
    width = max(len(c.name) for c in chans)
    for ch in chans:
        t = ch.track
        parts = [f"channel {ch.name:<{width}}", f"tick {t.tick}",
                 f"mod {names.envelope(t.mod)}",
                 f"instrument {names.instrument(t.instrument)}",
                 f"volume {t.volume}"]
        if not t.flags & 0x80:
            parts.append("not_in_use")
        for bit, nm in stx.HEADER_FLAG_BITS:
            if t.flags & bit:
                parts.append(nm)
        out.append("  ".join(parts))
    out.append("")
    # The order, as a comment: which sections each channel writes anew.
    out.append("# The order (not read: the sections below say it). `.` a column "
               "written in its section,")
    out.append("# NN one that plays section NN's again.")
    head = "#  section  ticks        rows    " + "  ".join(f"{c.name:<5}" for c in chans)
    out.append(head.rstrip())
    for s in secs:
        cols = "  ".join(f"{(section_label(s.copy[c.name]) if c.name in s.copy else '.'):<5}"
                         for c in chans)
        out.append(f"#  {section_label(s.index):<7}  {s.start:>5}-{s.start + s.length - 1:<5}  "
                   f"{s.rows:>2} x {s.row:<2}  {cols}".rstrip())
    out.append("")

    for s in secs:
        out.append("")
        out.append(f"section {section_label(s.index)}  {s.rows} rows x {s.row} ticks"
                   f"    # ticks {s.start}-{s.start + s.length - 1}")
        grid = []
        heads = []
        for ch in chans:
            rows = [""] * s.rows
            if ch.name not in s.copy or full:
                for c in s.cells.get(ch.name, []):
                    r = (c.time - s.start) // s.row
                    rows[r] = render_cell(c, ch, s, names)
                if ch.name in s.marks:
                    t, _ = s.marks[ch.name]
                    r, d = divmod(t - s.start, s.row)
                    if ch.loop_to is None:
                        words = list(ch.end_words) + ["end"]
                    else:
                        words = [goto_text(secs, ch.loop_to)]
                    rows[r] = " ".join(([f"@{d}"] if d else []) + words)
                for r in range(s.rows):
                    if not rows[r]:
                        rows[r] = "..."
            head = ch.name + (f" = {section_label(s.copy[ch.name])}"
                              if ch.name in s.copy else "")
            heads.append(head)
            grid.append(rows)
        widths = [max([len(h)] + [len(x) for x in col]) for h, col in zip(heads, grid)]
        out.append("row | " + " | ".join(f"{h:<{w}}" for h, w in zip(heads, widths)).rstrip())
        for r in range(s.rows):
            out.append(f" {r:02d} | " + " | ".join(f"{col[r]:<{w}}" for col, w in
                                                    zip(grid, widths)).rstrip())
    return "\n".join(out) + "\n"


# ---------------------------------------------------------------------------
# Reading a song: the tracker text back to the stream text's words, which
# song_text.assemble (and the game's songtext.c) builds into the image.


ROW_RE = re.compile(r"^\s*(\d+)\s*$")
NOTE_FT2_RE = re.compile(r"^([A-G])([-#])(\d)$")
DELAY_RE = re.compile(r"^@(\d+)$")
GOTO_RE = re.compile(r"^(\d+)\.(\d+)(?:@(\d+))?$")


class TrackerError(stx.TextError):
    pass


class RCell:
    """A cell as read: where, what, its changes (stream text words, in
    order) and its marks."""

    def __init__(self, where, kind, notes=(), drum=None, delay=0,
                 art=None, words=(), items=(), end=None):
        self.where, self.kind, self.notes, self.drum = where, kind, tuple(notes), drum
        self.delay, self.art, self.words, self.items = delay, art, list(words), list(items)
        self.end = end          # None, ("end",) or ("goto", sec, row, delay)
        self.time = None

    def key(self):
        return (self.kind, self.notes, self.drum, self.delay, self.art,
                tuple(self.words), self.end)


class RSection:
    def __init__(self, where, index, rows, row):
        self.where, self.index, self.rows, self.row = where, index, rows, row
        self.copy = {}          # channel -> section
        self.cols = []          # channel names in the grid's order
        self.cells = {}         # channel -> {row: RCell}
        self.start = 0

    @property
    def length(self):
        return self.rows * self.row


class RSong:
    def __init__(self):
        self.name = None
        self.where = None       # (139b) the `song` line
        self.tempo = {}
        self.channels = []      # (name, header bytes, where)
        self.sections = []


def note_entry(tok, names):
    """`C-5` / `C#5` / `C5` -> the note's entry (1-63), or None."""
    m = NOTE_FT2_RE.match(tok)
    if m:
        tok = m.group(1) + ("#" if m.group(2) == "#" else "") + m.group(3)
    b = names.note_bytes.get(tok)
    if b is None or b == 0x80:
        return None
    return b - 0x80


def parse_cell(where, text, drums, names):
    toks = text.split()
    if not toks or toks == ["..."]:
        return None
    r = stx.Reader(where, toks)
    kind, notes, drum = None, [], None
    while r.more():
        w = r.peek()
        if w == "...":
            r.take("")
            kind = kind or "none"
            continue
        if kind in (None, "note") and not drums:
            n = note_entry(w, names)
            if n is not None:
                r.take("")
                if n > 63:
                    r.fail(f"{w}: the driver plays B0 to C#6 (six bits of a note "
                           f"and its transpose); {w} would sound as "
                           f"{names.note_of_entry[n & 0x3F]}")
                notes.append(n)
                kind = "note"
                continue
        if kind is None and drums and w in names.drum_bytes:
            r.take("")
            kind, drum = "drum", names.drum_bytes[w]
            continue
        if kind is None and w == "===":
            r.take("")
            kind = "off"
            continue
        if kind is None and w == "tie":
            r.take("")
            kind = "tie"
            continue
        break
    cell = RCell(where, kind or "none", notes, drum)
    while r.more():
        w = r.take("")
        m = DELAY_RE.match(w)
        if m:
            cell.delay = int(m.group(1))
            continue
        if w in ("hold", "cut"):
            if drums:
                r.fail(f"`{w}`: a drum is not let go early, nor held")
            cell.art = w
            continue
        if w == "end":
            cell.end = ("end",)
            if r.more():
                r.fail("nothing comes after `end`")
            break
        if w == "goto":
            g = GOTO_RE.match(r.take("goto: a section and row, SS.RR"))
            if not g:
                r.fail("goto: a section and row, like 05.03 (or 05.03@2)")
            cell.end = ("goto", int(g.group(1)), int(g.group(2)), int(g.group(3) or 0))
            if r.more():
                r.fail("nothing comes after `goto SS.RR`")
            break
        if w in ("transpose",):
            r.fail("transpose: notes are written as they are heard")
        if w in ("loop", "call", "return", "end2"):
            r.fail(f"{w}: a channel's flow is its sections, `goto` and `end`")
        if w == "hold":
            continue
        it = stx.parse_flag(r, w, names)
        if it is None:
            r.fail(f"{w!r} is not a note, a drum or a word this file knows")
        code = it.data[0]
        if code in (0xFC, 0xFD):
            r.fail(f"{w}: the tracker has no pitch slides or raw frequencies "
                   f"(no song uses them); write this song as stream text")
        cell.items.append(bytes(it.data))
        cell.words.append(stx.flag_text(code, bytes(it.data[1:]), names, None, 0))
    if cell.kind == "none" and (cell.items or cell.art or cell.delay) and cell.end is None:
        r.fail("changes need a note, a drum, `===` or `tie` in their cell "
               "(the driver makes them as it reads the next one)")
    if cell.kind != "none" and cell.end is not None:
        r.fail("a channel's `goto` or `end` stands in a cell of its own")
    return cell


def read_tracker(path, names):
    """A .trk file to an RSong; TrackerError names file:line."""
    song = RSong()
    sec = None
    for n, line in enumerate(stx.read_text(path), 1):
        where = f"{path}:{n}"
        if "|" in line and not line.lstrip().startswith("#"):
            fields = line.split("|")
            first = fields[0].strip()
            if sec is None:
                raise TrackerError(where, "a grid line before the first `section`")
            if first == "row":
                if sec.cols:
                    raise TrackerError(where, "a second `row |` line in the section")
                for f in fields[1:]:
                    toks = stx.tokens(f)
                    if not toks:
                        continue
                    ch = toks[0]
                    if ch not in [c[0] for c in song.channels]:
                        raise TrackerError(where, f"{ch!r} is not a channel the "
                                                  f"song's `channel` lines name")
                    if ch in sec.cols:
                        raise TrackerError(where, f"{ch} given twice")
                    if len(toks) == 3 and toks[1] == "=" and toks[2].isdigit():
                        sec.copy[ch] = int(toks[2])
                    elif len(toks) != 1:
                        raise TrackerError(where, f"a column heading is a channel, "
                                                  f"or `fm1 = 03` (section 03's again)")
                    sec.cols.append(ch)
                continue
            m = ROW_RE.match(first)
            if not m:
                raise TrackerError(where, "a grid line starts with its row number")
            if not sec.cols:
                raise TrackerError(where, "a row before the section's `row |` line")
            row = int(m.group(1))
            if row >= sec.rows:
                raise TrackerError(where, f"row {row}: section {sec.index:02d} has "
                                          f"{sec.rows} rows")
            cells = fields[1:]
            if len(cells) > len(sec.cols):
                extra = [c for c in cells[len(sec.cols):] if stx.tokens(c)]
                if extra:
                    raise TrackerError(where, "more cells than the section's columns")
            for ch, text in zip(sec.cols, cells):
                text = " ".join(stx.tokens(text))
                drums = dict((c[0], c[1]) for c in song.channels)[ch][1] == 0x80
                cell = parse_cell(where, text, drums, names)
                if cell is None:
                    continue
                if cell.kind == "none" and cell.end is None:
                    continue
                if row in sec.cells.setdefault(ch, {}):
                    raise TrackerError(where, f"{ch} row {row} given twice")
                sec.cells[ch][row] = cell
            continue
        toks = stx.tokens(line)
        if not toks:
            continue
        r = stx.Reader(where, toks)
        w = r.take("")
        if w == "song":
            if song.name is not None:
                r.fail("a second `song` line")
            song.name = r.name("song: a name")
            song.where = where
            while r.more():
                k = r.take("")
                if k not in ("tempo", "pal_tempo", "pal_tick") or k in song.tempo:
                    r.fail(f"{k!r}: a song line is `song NAME tempo N pal_tempo N "
                           f"pal_tick N`")
                song.tempo[k] = r.byte(k)
            for k in ("tempo", "pal_tempo", "pal_tick"):
                if k not in song.tempo:
                    r.fail(f"song {song.name}: no {k}")
        elif w == "channel":
            if song.name is None or song.sections:
                r.fail("`channel` lines come after the `song` line, before the "
                       "sections")
            ch = toks[1] if len(toks) > 1 else ""
            # A channel line is a stream text `track` line without its label
            # and transpose: hand it to song_text's reader with them added.
            if ch not in stx.CHANNEL_BYTES:
                r.fail(f"{ch!r} is not a channel (drums, fm1-fm6)")
            if ch in [c[0] for c in song.channels]:
                r.fail(f"channel {ch} given twice")
            if "transpose" in toks:
                r.fail("transpose: notes are written as they are heard")
            _, hb = stx.header_line(where, ["track", ch, "x"] + toks[2:] +
                                    ["transpose", "+0"], names)
            song.channels.append((ch, hb, where))
        elif w == "section":
            if not song.channels:
                r.fail("a section before the `channel` lines")
            idx = r.byte("section: its number", 999)
            if idx != len(song.sections):
                r.fail(f"section {idx:02d}: the sections are numbered from 00 "
                       f"in order; {len(song.sections):02d} next")
            rows = r.byte("section: how many rows", 999)
            if r.take("section N ROWS rows x T ticks: `rows`") != "rows" or \
                    r.take("`x`") != "x":
                r.fail("a section line is `section NN  ROWS rows x T ticks`")
            row = r.byte("section: ticks a row", 255)
            if r.take("`ticks`") != "ticks" or r.more():
                r.fail("a section line is `section NN  ROWS rows x T ticks`")
            if rows == 0 or row == 0:
                r.fail("a section has at least one row of at least one tick")
            sec = RSection(where, idx, rows, row)
            sec.start = sum(s.length for s in song.sections)
            song.sections.append(sec)
        else:
            r.fail(f"{w!r}: not a line a tracker file has")
    if song.name is None:
        raise TrackerError(f"{path}:1", "no `song` line")
    if not song.sections:
        raise TrackerError(f"{path}:1", "no sections")
    return song


def channel_events(song, ch):
    """One channel's cells in time order, copies resolved, and its end:
    (cells, end cell). Each cell gets its tick."""
    out = []
    end = None
    for sec in song.sections:
        src = sec
        if ch in sec.copy:
            k = sec.copy[ch]
            if not 0 <= k < sec.index:
                raise TrackerError(sec.where, f"{ch} = {k:02d}: a column plays "
                                              f"an earlier section's again")
            src = song.sections[k]
            while ch in src.copy:
                src = song.sections[src.copy[ch]]
            if src.length != sec.length:
                raise TrackerError(sec.where, f"{ch} = {k:02d}: section {k:02d} "
                                              f"is {src.length} ticks, this one "
                                              f"{sec.length}")
            mine = sec.cells.get(ch, {})
            if mine:
                have = sorted((r * sec.row + c.delay, c.key()) for r, c in mine.items())
                want = sorted((r * src.row + c.delay, c.key())
                              for r, c in src.cells.get(ch, {}).items())
                if have != want:
                    first = min(mine)
                    raise TrackerError(mine[first].where,
                                       f"{ch} in section {sec.index:02d} plays "
                                       f"section {k:02d}'s again (`= {k:02d}`) but "
                                       f"its cells differ: edit section {k:02d}'s, or "
                                       f"take away `= {k:02d}` to make these its own")
        elif ch not in sec.cols and sec.cells.get(ch):
            raise TrackerError(sec.where, f"{ch} has cells but no column")
        for r, c in sorted(src.cells.get(ch, {}).items()):
            if c.delay >= src.row:
                raise TrackerError(c.where, f"@{c.delay}: past the row's "
                                            f"{src.row} ticks")
            n = RCell(c.where, c.kind, c.notes, c.drum, c.delay, c.art,
                      c.words, c.items, c.end)
            n.time = sec.start + r * src.row + c.delay
            if end is not None:
                raise TrackerError(c.where, f"{ch}: a cell after the channel's "
                                            f"`goto` or `end` ({end.where})")
            if n.end is not None:
                end = n
            else:
                out.append(n)
    if end is None:
        raise TrackerError(song.sections[-1].where, f"{ch} has no `goto` or `end`")
    return out, end


def goto_tick(song, where, sec, row, delay):
    if sec >= len(song.sections):
        raise TrackerError(where, f"goto {sec:02d}.{row:02d}: no section {sec:02d}")
    s = song.sections[sec]
    if row >= s.rows:
        raise TrackerError(where, f"goto {sec:02d}.{row:02d}: section {sec:02d} "
                                  f"has {s.rows} rows")
    return s.start + row * s.row + delay


def stream_text(song, names, prefix=""):
    """The song as stream text (song_text's format): the `song` and `track`
    lines and a block a channel. (139b) `prefix` goes before each block's
    name and labels: the game's are `trk_<song>_<channel>`, so the other song
    of a pair file, still read from its stream text, keeps its own."""
    lines = [f"# {song.name}: built from its tracker file by tools/tracker.py",
             f"song {song.name}  tempo {song.tempo['tempo']}  pal_tempo "
             f"{song.tempo['pal_tempo']}  pal_tick {song.tempo['pal_tick']}"]
    blocks = []
    for ch, hb, where in song.channels:
        t = stx.Track(song.name, 0, bytes(hb[:3]) + bytes(2) + bytes(hb[5:]), True)
        label = f"{prefix}{song.name}_{ch}"
        lines.append(stx.header_text(t, names, lambda a, l=label: l))
        blocks.append(channel_stream(song, ch, hb, label, names))
    lines.append("")
    for blk in blocks:
        lines += [""] + blk
    return "\n".join(lines) + "\n"


def channel_stream(song, ch, hb, label, names):
    drums = hb[1] == 0x80
    cells, end = channel_events(song, ch)
    times = [c.time for c in cells]
    for a, b2 in zip(times, times[1:]):
        if b2 <= a:
            raise TrackerError(cells[times.index(b2)].where, "two cells at one tick")
    target = None
    if end.end[0] == "goto":
        t = goto_tick(song, end.where, *end.end[1:])
        hits = [i for i, c in enumerate(cells) if c.time == t]
        if not hits:
            raise TrackerError(end.where, f"goto: {ch} has no cell at that "
                                          f"place (tick {t})")
        target = hits[0]
    if cells and end.time <= cells[-1].time:
        raise TrackerError(end.where, "the end comes before the last cell ends")
    out = [f"block {label}"]
    mult = hb[2]
    chord = 0
    last_struck = None
    hold_put = False            # a bare hold was just put after an event
    ev = []                     # [label?, words, kind, text, ln, again?, after]
    for i, c in enumerate(cells):
        nxt = cells[i + 1] if i + 1 < len(cells) else (
            cells[target] if target is not None else None)
        dur = (cells[i + 1].time if i + 1 < len(cells) else end.time) - c.time
        row = []
        for it, w in zip(c.items, c.words):
            code = it[0]
            if code == 0xE5:
                chord = it[1]
                if chord == 1:
                    raise TrackerError(c.where, "chord 1 is not a chord: "
                                                "chord 2 or more, or off")
            elif code == 0xFA:
                mult = it[1]
            row.append(w)
        if mult == 0 or dur % mult or not 0 < dur // mult <= 127:
            raise TrackerError(c.where, f"{ch} lasts {dur} ticks here; with "
                                        f"tick {mult} a length is a multiple of "
                                        f"{mult} up to {127 * mult}"
                                        + (" (split it with a `tie`)"
                                           if dur > 127 * mult else ""))
        ln = dur // mult
        e = {"label": i == target, "words": row, "ln": ln, "mult": mult,
             "again": False, "after": None, "text": None, "kind": c.kind}
        if c.kind == "tie":
            if c.items and i > 0 and cells[i - 1].art != "cut" and not drums:
                raise TrackerError(c.where, "a `tie` with changes in it cannot "
                                            "hold the note before (the driver "
                                            "reads the changes between them): "
                                            "mark the cell before it `cut` ("
                                            + cells[i - 1].where + "), or move "
                                            "the changes")
            e["text"] = "tie"
        elif c.kind == "off":
            e["text"] = "rest"
        elif c.kind == "drum":
            e["again"] = c.drum == last_struck and not hold_put and i != target
            e["text"] = names.drum_of_byte[c.drum]
            last_struck = c.drum
        else:
            want = chord if chord >= 2 else 1
            if len(c.notes) != want:
                raise TrackerError(c.where, f"{len(c.notes)} notes here, and "
                                            f"the channel is in "
                                            + (f"`chord {chord}`" if chord >= 2
                                               else "single notes (no chord)"))
            e["again"] = c.notes == last_struck and not hold_put and i != target
            e["text"] = "+".join(names.note_of_entry[n] for n in c.notes)
            last_struck = c.notes
        hold_put = False
        if not drums:
            held = c.art == "hold" or (nxt is not None and nxt.kind == "tie"
                                       and c.art != "cut")
            if c.art == "hold" and nxt is not None and nxt.kind == "tie":
                raise TrackerError(c.where, "`hold` before a `tie`: a note "
                                            "before a tie is held anyway")
            plain_tie_next = (i + 1 < len(cells) and nxt.kind == "tie"
                              and not nxt.items)
            if held and not plain_tie_next:
                e["after"] = "hold"
                hold_put = True
            elif not held and plain_tie_next:
                e["after"] = "nop"
        ev.append(e)
    # The text: a length left out when it is the last one (the driver keeps
    # it) -- but not before an `again` (its length byte would be read as this
    # one's), nor at the loop's first cell, which two places reach.
    last_len = None
    for i, e in enumerate(ev):
        if e["label"]:
            out.append(f"{label}_loop:")
        nxt_again = i + 1 < len(ev) and ev[i + 1]["again"] and not e["after"]
        ticks = e["ln"] * e["mult"]
        keep = ticks == last_len and not e["label"] and not nxt_again
        last_len = ticks
        if e["again"]:
            text = f"again {e['ln']}"
        elif e["text"] == "tie":
            text = f"tie {e['ln']}"
        else:
            text = e["text"] if keep else f"{e['text']} {e['ln']}"
        out.append("    " + "  ".join(e["words"] + [text]))
        if e["after"]:
            out.append(f"    {e['after']}")
    if end.end[0] == "goto":
        out += [f"    {w}" for w in end.words]
        out.append(f"    goto {label}_loop")
    else:
        out += [f"    {w}" for w in end.words]
        out.append("    end")
    return out


# ---------------------------------------------------------------------------
# (build 139b) The music the game builds: each song from songs/<song>.trk
# where there is one, from its song file's stream text where not -- the
# Python twin of songtext.c's choice (src/audio/songtext.c, "Tracker
# files"), written out as a music directory song_text.assemble builds into
# the image songtext_build makes.

PREFIX = "trk_"             # a tracker song's blocks: trk_<song>_<channel>


def music_names(music_dir):
    """The names a tracker file is read with -- notes, drums, instruments,
    envelopes, sounds -- found as song_text.assemble finds them, without
    reading the song files (one whose songs are all trackers is not read)."""
    md = Path(music_dir)
    _, ins = stx.instrument_bank(json.loads((md / "fm_instruments.json").read_text()))
    if any(n is None for n in ins):
        import asset_names
        ins = asset_names.INSTRUMENT_NAMES
    names = stx.Names(json.loads((md / "note_frequencies.json").read_text()),
                      json.loads((md / "drums.json").read_text()), ins)
    names.sound_ids = {}
    section = None
    for line in stx.read_text(md / "sounds.txt"):
        toks = stx.tokens(line)
        if not toks:
            continue
        if toks[0] in ("instruments", "streams"):
            section = toks[0]
        elif section is None and toks[0].startswith("$") and len(toks) > 1:
            sid = int(toks[0][1:], 16)
            names.sound_ids.setdefault(toks[1], sid)
            names.sounds[sid] = toks[1]
    names.envelopes = [toks[1] for toks in map(stx.tokens,
                                               stx.read_text(md / "envelopes.txt"))
                       if len(toks) > 1 and toks[0] == "envelope"]
    names.envelope_numbers = {e: i + 1 for i, e in enumerate(names.envelopes)}
    return names


def song_files(music_dir):
    """sounds.txt's song files in order, each with the songs and blocks it
    holds: [(file, [song], [block])] -- songtext.c's scan_songs."""
    md = Path(music_dir)
    out = []
    for n, line in enumerate(stx.read_text(md / "sounds.txt"), 1):
        toks = stx.tokens(line)
        if len(toks) < 2 or toks[0] != "song_file":
            continue
        path = md / "songs" / f"{toks[1]}.txt"
        if not path.is_file():
            raise TrackerError(f"{md / 'sounds.txt'}:{n}",
                               f"song_file {toks[1]}: no {path}")
        songs, blocks = [], []
        for text in stx.read_text(path):
            w = stx.tokens(text)
            if len(w) > 1 and w[0] == "song":
                songs.append(w[1])
            elif len(w) > 1 and w[0] == "block":
                blocks.append(w[1])
        out.append((toks[1], songs, blocks))
    return out


def game_music(music_dir, out_dir, trk_dir=None, require_all=False,
               only=None):
    """Writes to `out_dir` the music the game builds from `music_dir`, as
    stream text: each song from `trk_dir`/<song>.trk (songs/ by default)
    where there is one -- every song with `require_all`; only the songs in
    `only` when it is given -- and from its song file where not. Laid out
    as songtext.c lays it: a song file whose songs all come from tracker
    files is not read and its blocks leave sounds.txt's streams; a song file
    still read loses the replaced songs' `song` and `track` lines; the
    `bytes` lines stay while any song file is read; a tracker song is
    songs/trk_<song>.txt, its blocks after the streams. Returns the tracker
    songs, in song order."""
    import shutil
    md, out = Path(music_dir), Path(out_dir)
    trk_dir = md / "songs" if trk_dir is None else Path(trk_dir)
    names = music_names(md)
    files = song_files(md)
    trackers = {}               # song -> RSong, in song order
    texts = {}                  # song -> its stream text
    for _, songs, _ in files:
        for s in songs:
            path = trk_dir / f"{s}.trk"
            if only is not None and s not in only:
                continue
            if not path.is_file():
                if require_all:
                    raise TrackerError(f"{path}:1", f"no tracker file for {s}")
                continue
            song = read_tracker(path, names)
            if song.name != s:
                raise TrackerError(song.where, f"the song line names "
                                               f"{song.name}, not {s}")
            # Its blocks made now, as songtext.c makes them before it reads
            # the next file: the same file is refused first.
            texts[s] = stream_text(song, names, PREFIX)
            trackers[s] = song
    kept = [f for f, songs, _ in files if any(s not in trackers for s in songs)]
    dropped = ({b for f, _, blocks in files if f not in kept for b in blocks}
               - {b for f, _, blocks in files if f in kept for b in blocks})
    if out.exists():
        shutil.rmtree(out)
    shutil.copytree(md, out)
    shutil.rmtree(out / "songs")
    (out / "songs").mkdir()
    for f in kept:
        lines, skip = [], False
        for line in stx.read_text(md / "songs" / f"{f}.txt"):
            toks = stx.tokens(line)
            if toks and toks[0] == "song":
                skip = len(toks) > 1 and toks[1] in trackers
            elif toks and toks[0] != "track":
                skip = False
            if not (skip and toks):
                lines.append(line)
        (out / "songs" / f"{f}.txt").write_text("\n".join(lines), newline="\n")
    for s, text in texts.items():
        (out / "songs" / f"{PREFIX}{s}.txt").write_text(text, newline="\n")
    lines, section, listed = [], None, False
    for line in stx.read_text(md / "sounds.txt"):
        toks = stx.tokens(line)
        if toks and toks[0] == "song_file":
            if not listed:
                lines += [f"song_file {f}" for f in kept]
                lines += [f"song_file {PREFIX}{s}" for s in trackers]
                listed = True
            continue
        if toks and toks[0] in ("instruments", "streams"):
            section = toks[0]
        elif section == "streams" and toks and toks[0] == "block" \
                and len(toks) > 1 and toks[1] in dropped:
            continue
        elif section == "streams" and toks and toks[0] == "bytes" and not kept:
            continue
        lines.append(line)
    if trackers:
        lines.append("streams")
        lines += [f"    block {PREFIX}{s}_{ch}" for s, song in trackers.items()
                  for ch, _, _ in song.channels]
    (out / "sounds.txt").write_text("\n".join(lines) + "\n", newline="\n")
    return list(trackers)


def music_from_trackers(music_dir, trk_dir, out_dir):
    """`music_dir` written to `out_dir` with every song read from
    `trk_dir`/<song>.trk: prove's music. Returns the songs."""
    return game_music(music_dir, out_dir, trk_dir, require_all=True)


def game_image(music_dir, scratch):
    """(139b) The image the game builds from `music_dir`: game_music's
    stream text, assembled (song_text.Built)."""
    game_music(music_dir, scratch)
    return stx.assemble(scratch)


def export(music_dir, out_dir, only=(), full=False):
    b = stx.assemble(music_dir)
    out_dir = Path(out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)
    done = []
    for name in b.songs:
        if only and name not in only:
            continue
        (out_dir / f"{name}.trk").write_text(write_song(b, name, full),
                                             newline="\n")
        done.append(name)
    return done


# ---------------------------------------------------------------------------
# The proof: the stream text's image and the tracker's play the same -- every
# chip write, in order, frame for frame (smps_trace --regs), over each
# channel's loop at least twice.

def song_span(b, name):
    """(139b) The ticks a song is followed over: its written span and two
    more turns of its longest loop."""
    chans = song_channels(b, name)
    end = max(c.end for c in chans)
    loops = [c.end - c.loop_to for c in chans if c.loop_to is not None]
    return end + 2 * max(loops, default=0)


def song_frames(b, name):
    """Frames enough for song_span's ticks: a frame is at least 4/5 of a
    tick at the songs' tempos (tempo 0 none lost, 5 or more one in five at
    most)."""
    return song_span(b, name) * 5 // 4 + 120


def timeline(b, name, span):
    """(139b) What a song plays as the tracker sees it: each channel's cells
    -- tick, what is struck or held, `hold`/`cut`, the changes -- unrolled
    through its loop up to `span` ticks; a channel that stops ends with the
    words it stops with. {channel: [entry]}, whichever image the song came
    from (its stream text's or its tracker file's). A `nop` is left out: it
    plays nothing, and stream_text puts one between a note marked `cut` and
    its plain `tie` where the ROM's stream lets the note go with a jump
    (Violation's `return` before a `tie`) -- the same chip writes."""
    def said(words):
        return tuple(w for w in words if w != "nop")

    out = {}
    for ch in song_channels(b, name):
        rows = [(c.time, c.kind, c.notes, c.drum, said(c.words), c.art)
                for c in ch.cells if c.time < span]
        if ch.loop_to is None:
            rows.append((ch.end, "end", (), None, said(ch.end_words), None))
        else:
            loop = [c for c in ch.cells if c.time >= ch.loop_to]
            shift = ch.end - ch.loop_to
            while loop[0].time + shift < span:
                rows += [(c.time + shift, c.kind, c.notes, c.drum,
                          said(c.words), c.art)
                         for c in loop if c.time + shift < span]
                shift += ch.end - ch.loop_to
        out[ch.name] = rows
    return out


def timeline_difference(want, got):
    """The first place two timelines differ, as text; None when they are
    the same."""
    for ch in want:
        if ch not in got:
            return f"{ch}: no such channel"
        for x, y in zip(want[ch], got[ch]):
            if x != y:
                return f"{ch} at tick {x[0]}: {x[1:]} against {y[1:]}"
        if len(want[ch]) != len(got[ch]):
            return (f"{ch}: {len(want[ch])} cells against "
                    f"{len(got[ch])}")
    extra = [ch for ch in got if ch not in want]
    return f"{extra[0]}: a channel more" if extra else None


def trace(smps_trace, music_dir, index, frames):
    import subprocess
    r = subprocess.run([smps_trace, "--assets", str(music_dir), "--song",
                        str(index), "--frames", str(frames), "--regs"],
                       capture_output=True, text=True)
    if r.returncode != 0:
        raise SystemExit(f"smps_trace failed on {music_dir}: {r.stderr.strip()}")
    return r.stdout.splitlines()


def prove(music_dir, smps_trace, scratch, songs=()):
    """Exports every song, reads the exports back into a scratch music
    directory and compares the two images' chip writes. Returns failures."""
    import tempfile
    music_dir = Path(music_dir)
    b = stx.assemble(music_dir)
    work = Path(scratch or tempfile.mkdtemp(prefix="tracker_prove_"))
    trk = work / "trk"
    export(music_dir, trk)
    music_from_trackers(music_dir, trk, work / "music")
    b2 = stx.assemble(work / "music")
    print(f"images: the stream text's {len(b.image)} bytes, the tracker's "
          f"{len(b2.image)}")
    failures = 0
    for name in b.songs:
        if songs and name not in songs:
            continue
        index = b.names.sound_ids[name] - stx.FIRST_ID
        frames = song_frames(b, name)
        want = trace(smps_trace, music_dir, index, frames)
        got = trace(smps_trace, work / "music", index, frames)
        writes = sum(1 for x in want if " <- " in x)
        if want == got:
            print(f"  {name:<20} PASS  {frames:6} frames  {writes:7} writes")
            continue
        failures += 1
        k = next((i for i, (x, y) in enumerate(zip(want, got)) if x != y),
                 min(len(want), len(got)))
        print(f"  {name:<20} FAIL  {frames:6} frames: first difference at "
              f"line {k}: {want[k] if k < len(want) else 'end'!r} vs "
              f"{got[k] if k < len(got) else 'end'!r}")
    return failures


# ---------------------------------------------------------------------------
# The command line.

def main():
    import argparse
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    sub = ap.add_subparsers(dest="action", required=True)
    e = sub.add_parser("export", help="the songs' stream text to tracker files")
    e.add_argument("songs", nargs="*", help="songs to write (all by default)")
    e.add_argument("--music", type=Path, default=Path("assets/music"))
    e.add_argument("--out", type=Path, default=Path("build/tracker"))
    e.add_argument("--full", action="store_true",
                   help="write repeated columns out in full too (`= NN` "
                        "columns must then match their section)")
    s = sub.add_parser("streams", help="the stream text a tracker file reads as")
    s.add_argument("file", type=Path)
    s.add_argument("--music", type=Path, default=Path("assets/music"))
    p = sub.add_parser("prove", help="the tracker files play the same as the "
                                     "stream text, chip write for chip write")
    p.add_argument("songs", nargs="*")
    p.add_argument("--music", type=Path, default=Path("assets/music"))
    p.add_argument("--smps-trace", default="./smps_trace")
    p.add_argument("--scratch", type=Path, default=Path("build/tracker_prove"))
    i = sub.add_parser("image", help="(139b) the image the game builds: a "
                                     "song's .trk where there is one")
    i.add_argument("out", type=Path)
    i.add_argument("--music", type=Path, default=Path("assets/music"))
    i.add_argument("--scratch", type=Path, default=Path("build/tracker_image"))
    args = ap.parse_args()
    try:
        if args.action == "export":
            done = export(args.music, args.out, args.songs, args.full)
            print(f"{len(done)} songs written to {args.out}/")
        elif args.action == "streams":
            b = stx.assemble(args.music)
            sys.stdout.write(stream_text(read_tracker(args.file, b.names),
                                         b.names))
        elif args.action == "image":
            b = game_image(args.music, args.scratch)
            args.out.write_bytes(bytes(b.image))
            print(f"{len(b.image)} bytes -> {args.out}")
        else:
            if not Path(args.smps_trace).exists():
                raise SystemExit(f"{args.smps_trace}: build it first (make "
                                 f"smps_trace)")
            failures = prove(args.music, args.smps_trace, args.scratch,
                             args.songs)
            print(f"tracker prove: {failures} failures")
            sys.exit(1 if failures else 0)
    except stx.TextError as exc:
        print(f"tracker: {exc}", file=sys.stderr)
        sys.exit(1)


if __name__ == "__main__":
    main()
