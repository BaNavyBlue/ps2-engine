#!/usr/bin/env python3
"""(build 184) A mod's chord past the chip (docs/audio.md, "Build 184").

  python3 tools/chords_check.py [--trace ./smps_trace] [--keep DIR]

`make smps_trace` first. Each case is a scratch copy of assets/music with
one song or effect edited the way a mod would, run through smps_trace
(the sequencer alone, no synth):

  game      the game's own music: no chord voices (smps_trace --chords)
  chord6    Phantasy's FM3 chord made six notes (each note's octave added,
            within B0-C#6): three chord voices; every write the Mega
            Drive's chip gets the unedited song's, frame for frame, for
            3000 frames; each voice struck at its added note, as often as
            the chord's lead; the fade ($E0) steps each voice's level as it
            steps FM4's and FM5's, and the end silences them
  chord16   the same made sixteen notes, the most: thirteen voices, each
            struck
  chord17   seventeen: refused by the song text, naming the file and line
  effect    the level-change effect ($A4, on FM3) made a chord of three:
            two effect voices, struck over the music
  heard_chip, heard_q
            chord6 heard: the headless game (`make headless`,
            `make ps2_headless_q`) through the chip and through Q, the mod in
            its mods folder, against the game's own -- the same to the bit
            until the first chord, and from it their difference the three
            added notes alone (numpy). --no-heard leaves these out.

A case passes when all its lines hold. Exit 0 when every case passes.
"""
import argparse
import json
import math
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

TREE = Path(__file__).resolve().parent.parent
NOTE_RE = re.compile(r"^([A-G])([-#])(\d)$")
NAMES = ["C-", "C#", "D-", "D#", "E-", "F-", "F#", "G-", "G#", "A-", "A#", "B-"]
LOWEST = 11          # B0, in semitones from C0
HIGHEST = 6 * 12 + 1  # C#6


def semis(note):
    m = NOTE_RE.match(note)
    return NAMES.index(m.group(1) + m.group(2)) + 12 * int(m.group(3))


def name(s):
    return f"{NAMES[s % 12]}{s // 12}"


def added_notes(notes, want):
    """`want` more notes for a chord of `notes`: octaves of them, up first,
    within B0-C#6."""
    out = []
    shifts = [12, -12, 24, -24, 36, -36, 48, -48]
    for sh in shifts:
        for n in notes:
            s = semis(n) + sh
            if LOWEST <= s <= HIGHEST and len(out) < want:
                out.append(name(s))
    while len(out) < want:
        out.append(notes[len(out) % len(notes)])
    return out


def widen_trk(text, column, size):
    """Every cell of `column` (fm3) that strikes a chord of three gets
    size - 3 notes more, and `chord 3` becomes `chord SIZE`."""
    lines = text.split("\n")
    cols = None
    for i, line in enumerate(lines):
        # a comment is a `#` word; G#3's is a note's
        hit = re.search(r"(^|\s)#(\s|$)", line)
        bare = line[:hit.start()] if hit else line
        cells = bare.split("|")
        if len(cells) > 1 and cells[0].strip() == "row":
            cols = [c.strip().split()[0] if c.strip() else "" for c in cells]
            continue
        if cols is None or len(cells) != len(cols) \
                or not cells[0].strip().isdigit():
            continue
        k = cols.index(column) if column in cols else -1
        if k < 0:
            continue
        toks = cells[k].split()
        notes = []
        while len(notes) < len(toks) and NOTE_RE.match(toks[len(notes)]):
            notes.append(toks[len(notes)])
        rest = toks[len(notes):]
        for j in range(len(rest) - 1):
            if rest[j] == "chord" and rest[j + 1] == "3":
                rest[j + 1] = str(size)
        if len(notes) == 3:
            notes = notes + added_notes(notes, size - 3)
        if toks:
            cells[k] = " " + " ".join(notes + rest) + " "
            comment = line[len(bare):]
            lines[i] = "|".join(cells).rstrip() + comment
    return "\n".join(lines)


def write(path, text):
    """The song text's own line ends (LF), on Windows too."""
    with open(path, "w", newline="\n") as fh:
        fh.write(text)


def scratch_music(root, case):
    d = root / case / "music"
    shutil.copytree(TREE / "assets" / "music", d)
    return d


def trace(args, *more):
    r = subprocess.run([args.trace, *more], capture_output=True, text=True,
                       cwd=TREE)
    return r.returncode, r.stdout, r.stderr


def chords_line(out):
    m = re.search(r"chord voices: music (\d+), effects (\d+)", out)
    return (int(m.group(1)), int(m.group(2))) if m else None


def voice_writes(out):
    """{voice: [(frame, part, reg, value)]} from --voices lines."""
    got = {}
    for line in out.splitlines():
        m = re.match(r"\s*(\d+)\s+V(\d+)\s+([I2])\s+([0-9A-F]{2}) <- "
                     r"([0-9A-F]{2})", line)
        if m:
            got.setdefault(int(m.group(2)), []).append(
                (int(m.group(1)), m.group(3), int(m.group(4), 16),
                 int(m.group(5), 16)))
    return got


def chip_lines(out):
    return [ln for ln in out.splitlines()
            if re.match(r"\s*\d+\s+(FM[I2]|DAC|PSG)", ln)]


def freqs():
    j = json.loads((TREE / "assets/music/note_frequencies.json").read_text())
    return [(e["block"] << 11) | e["fnum"] for e in j["entries"]]


class Case:
    def __init__(self, name):
        self.name = name
        self.lines = []
        self.ok = True

    def check(self, good, what):
        self.lines.append(("ok  " if good else "BAD ") + what)
        self.ok = self.ok and bool(good)


def struck(writes):
    """Frames a voice's $28 key-on lands on."""
    return [f for (f, p, r, v) in writes if r == 0x28 and v & 0xF0]


def case_game(args, root):
    c = Case("game")
    code, out, err = trace(args, "--chords")
    c.check(code == 0 and chords_line(out) == (0, 0),
            f"the game's music needs no chord voices ({out.strip()})")
    return c


def case_wide(args, root, size, frames=3000):
    c = Case(f"chord{size}")
    music = scratch_music(root, c.name)
    trk = music / "songs" / "phantasy.trk"
    write(trk, widen_trk(trk.read_text(), "fm3", size))
    code, out, err = trace(args, "--assets", str(music), "--chords")
    want = (size - 1) - 2           # parts, less tracks 4 and 5
    got = chords_line(out)
    c.check(code == 0 and got == (want, 0),
            f"{want} music voices counted ({out.strip() or err.strip()})")
    if code != 0:
        return c
    _, own, _ = trace(args, "--song", "0", "--frames", str(frames), "--regs")
    code, mod, err = trace(args, "--assets", str(music), "--song", "0",
                           "--frames", str(frames), "--regs", "--voices")
    a, b = chip_lines(own), chip_lines(mod)
    c.check(code == 0 and a == b and len(a) > 1000,
            f"the chip's writes the song's own: {len(b)} of {len(a)} the "
            "same" if a == b else f"the chip's writes differ ({len(a)} "
            f"against {len(b)})")
    v = voice_writes(mod)
    # the lead's strikes: FM3's key-ons ($28 = $F2) on the chip
    lead = [int(ln.split()[0]) for ln in b
            if re.search(r"FMI 28 <- F2$", ln)]
    each = [len(struck(v.get(k, []))) for k in range(want)]
    c.check(len(v) == want and all(n == len(lead) for n in each),
            f"voices V0-V{want - 1} each struck {len(lead)} times with the "
            f"lead (got {each})")
    # the first strike's pitch on each voice: the added notes of the first
    # chord, in order
    first = widen_trk((TREE / "assets/music/songs/phantasy.trk").read_text(),
                      "fm3", size)
    m = re.search(r"\|\s*((?:[A-G][-#]\d\s+){%d})chord %d" % (size, size),
                  first)
    want_notes = m.group(1).split()[3:] if m else []
    table = freqs()
    ok = len(want_notes) == want
    for k in range(min(want, len(want_notes))):
        hi = [val for (f, p, r, val) in v.get(k, []) if r & ~3 == 0xA4]
        lo = [val for (f, p, r, val) in v.get(k, []) if r & ~3 == 0xA0]
        index = semis(want_notes[k]) - LOWEST + 1
        ok = ok and bool(hi) and bool(lo) \
            and ((hi[0] << 8) | lo[0]) == table[index]
    c.check(ok, f"each voice's first pitch its added note ({' '.join(want_notes)})")
    # the fade: from frame 600, the voices' carrier levels step up with
    # FM4's ($4E on part 2) until the end silences them
    code, fad, _ = trace(args, "--assets", str(music), "--song", "0",
                         "--frames", "900", "--regs", "--voices",
                         "--play", "E0@600")
    fv = voice_writes(fad)
    fm4 = [ln for ln in chip_lines(fad) if int(ln.split()[0]) > 600
           and re.search(r"FM2 4D <- ", ln)]
    steps = []
    for k in range(want):
        tl = [val for (f, p, r, val) in fv.get(k, []) if f > 600
              and (r & ~3) == 0x4C]
        steps.append(len(tl))
    c.check(code == 0 and len(fm4) > 30
            and all(n >= len(fm4) - 1 for n in steps),
            f"the fade steps every voice with FM5 ({len(fm4)} steps of FM5's "
            f"level; the voices' {steps})")
    ends = [any(r == 0x28 and val & 0xF0 == 0 and f > 790
                for (f, p, r, val) in fv.get(k, [])) for k in range(want)]
    c.check(all(ends), "the fade's end keys every voice off")
    # the pause ($D5 at 600) and the unpause ($D6 at 700, the game's resume
    # first): each voice's levels go down and back up with its lead's, FM3's
    code, pz, _ = trace(args, "--assets", str(music), "--song", "0",
                        "--frames", "800", "--regs", "--voices",
                        "--play", "D5@600", "--resume", "700",
                        "--play", "D6@700")
    pv = voice_writes(pz)
    walks = []
    for lo, hi in ((600, 700), (700, 800)):
        lead = {}
        for ln in chip_lines(pz):
            w = ln.split()
            if w[1] != "FMI":
                continue
            f, r, val = w[0], int(w[2], 16), w[4]
            if lo < int(f) < hi and 0x40 <= r < 0x50 and r & 3 == 2:
                lead.setdefault(int(f), {})[r & ~3] = int(val, 16)
        at = min(lead) if lead else None
        same = at is not None and all(
            {r & ~3: val for (f, p, r, val) in pv.get(k, [])
             if f == at and 0x40 <= r < 0x50} == lead[at]
            for k in range(want))
        walks.append((at, lead.get(at, {}), same))
    (p_at, p_lv, p_ok), (u_at, u_lv, u_ok) = walks
    c.check(code == 0 and p_ok and u_ok
            and all(p_lv[r] > u_lv.get(r, 0x7F) for r in p_lv),
            f"the pause turns every voice down with FM3 (frame {p_at}: "
            f"{fmt_levels(p_lv)}) and the unpause back up (frame {u_at}: "
            f"{fmt_levels(u_lv)})")
    return c


def fmt_levels(levels):
    return " ".join(f"${r:02X}={v:02X}" for r, v in sorted(levels.items()))


def case_refused(args, root):
    c = Case("chord17")
    music = scratch_music(root, c.name)
    trk = music / "songs" / "phantasy.trk"
    write(trk, widen_trk(trk.read_text(), "fm3", 17))
    code, out, err = trace(args, "--assets", str(music), "--chords")
    c.check(code != 0 and "phantasy.trk:136:" in err
            and "more than 16 notes in a cell" in err,
            "a tracker cell of 17 notes refused: "
            + (err.strip().splitlines()[0] if err.strip() else "(nothing)"))
    # the stream text's `chord 17`, in an effect
    music = scratch_music(root, "chord17_text")
    fx = music / "effects.txt"
    old = "    instrument level_changed  fm3_special 1 1 1 0\n"
    text = fx.read_text()
    write(fx, text.replace(old, "    chord 17" + old[3:]))
    code, out, err = trace(args, "--assets", str(music), "--chords")
    c.check(old in text and code != 0 and "effects.txt:" in err
            and "chord: the notes: 17 is past 16" in err,
            "the song text's `chord 17` refused: "
            + (err.strip().splitlines()[0] if err.strip() else "(nothing)"))
    return c


def case_effect(args, root):
    c = Case("effect")
    music = scratch_music(root, c.name)
    fx = music / "effects.txt"
    text = fx.read_text()
    old = ("    instrument level_changed  fm3_special 1 1 1 0\n"
           "    A#3 16  again 16  again 16  again 32\n")
    new = ("    chord 3  instrument level_changed  fm3_special 1 1 1 0\n"
           "    A#3+D4+F4 16  again 16  again 16  again 32\n")
    c.check(old in text, "the level-change effect's stream found")
    write(fx, text.replace(old, new))
    code, out, err = trace(args, "--assets", str(music), "--chords")
    c.check(code == 0 and chords_line(out) == (0, 2),
            f"two effect voices counted ({out.strip() or err.strip()})")
    code, out, err = trace(args, "--assets", str(music), "--song", "0",
                           "--frames", "400", "--regs", "--voices",
                           "--play", "A4@200")
    v = voice_writes(out)
    each = [len(struck(v.get(k, []))) for k in range(2)]
    c.check(code == 0 and sorted(v) == [0, 1] and each == [4, 4],
            f"V0 and V1 struck four times with the effect (got {each})")
    return c


def exe_path(name):
    p = TREE / name
    if not p.exists() and Path(str(p) + ".exe").exists():
        return Path(str(p) + ".exe")
    return p


def headless_run(game, synth, scratch, mods):
    """The headless game (main.c) from power-on for 2400 frames -- the
    title's theme, Phantasy, from frame 344 -- with `mods` (assets-relative
    path: text) in a scratch mods folder, through `synth`. The WAV's
    samples, stereo floats, and stderr."""
    import numpy as np
    import os
    import wave

    def link(src, to):
        try:
            os.link(src, to)
        except OSError:
            shutil.copy2(src, to)
    shutil.copytree(TREE / "assets", scratch / "assets", copy_function=link)
    modsdir = scratch / "mods"
    modsdir.mkdir()
    for rel, text in mods.items():
        f = modsdir / rel
        f.parent.mkdir(parents=True, exist_ok=True)
        write(f, text)
    env = dict(os.environ, PS2_FRAMES="2400", PS2_WAV=str(scratch / "out.wav"),
               PS2_OUT=str(scratch / "out.ppm"), PS2_MODS=str(modsdir),
               PS2_SYNTH=synth)
    p = subprocess.run([str(game)], cwd=scratch, env=env, capture_output=True,
                       text=True, timeout=900)
    if p.returncode != 0 or not (scratch / "out.wav").exists():
        return None, 0, p.stderr
    with wave.open(str(scratch / "out.wav")) as w:
        raw = w.readframes(w.getnframes())
        rate = w.getframerate()
    a = np.frombuffer(raw, dtype=np.int16).astype(np.float64)
    return a.reshape(-1, 2) / 32768.0, rate, p.stderr


def case_heard(args, root, synth, game_name):
    """Phantasy's chords made six notes in a mod, heard: the headless game
    through the synth, against the game's own. The two the same to the bit
    until the first chord; from it, through the first chord (frame 344, 1.9
    s), their difference -- the chord voices alone, as the mix adds them --
    is the three added notes and their overtones, and nothing else."""
    import numpy as np

    c = Case(f"heard_{synth}")
    game = exe_path(game_name)
    if not game.exists():
        c.check(False, f"{game.name} not built (make "
                       f"{'headless' if synth == 'chip' else 'ps2_headless_q'})")
        return c
    phantasy = (TREE / "assets/music/songs/phantasy.trk").read_text()
    mod = {"music/songs/phantasy.trk": widen_trk(phantasy, "fm3", 6)}
    own, rate, _ = headless_run(game, synth, root / f"{c.name}_own", {})
    got, _, err = headless_run(game, synth, root / f"{c.name}_mod", mod)
    c.check(own is not None and got is not None
            and "phantasy.trk in place of the game's own" in err,
            "the mod read, the game run to its end")
    if own is None or got is None:
        return c
    onset = int(344 / 60 * rate)
    c.check(np.array_equal(own[:onset], got[:onset]),
            "the same to the bit before the first chord")
    diff = (got - own).mean(axis=1)
    s0, s1 = onset + int(0.1 * rate), onset + int(1.8 * rate)
    spec = np.abs(np.fft.rfft(diff[s0:s1] * np.hanning(s1 - s0))) ** 2
    freq = np.fft.rfftfreq(s1 - s0, 1 / rate)
    table = json.loads((TREE / "assets/music/note_frequencies.json")
                       .read_text())["entries"]
    near = np.zeros_like(freq, dtype=bool)
    for note in ("C-5", "G#4", "D#5"):
        hz = table[semis(note) - LOWEST + 1]["hz"]
        for k in range(1, 40):
            near |= (freq > k * hz * 0.99) & (freq < k * hz * 1.01)
    share = spec[near].sum() / max(spec.sum(), 1e-30)
    rms = float(np.sqrt((diff[s0:s1] ** 2).mean()))
    level = 20 * math.log10(rms) if rms > 0 else -999.0
    c.check(level > -45 and share > 0.99,
            f"the difference the added notes C-5 G#4 D#5: {100 * share:.1f}% "
            f"of it on their overtones, at {level:.1f} dBFS")
    return c


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--trace", default=str(TREE / "smps_trace"))
    ap.add_argument("--keep", default=None)
    ap.add_argument("--no-heard", action="store_true",
                    help="the sequencer's cases alone, no headless game")
    args = ap.parse_args()
    exe = Path(args.trace)
    if not exe.exists() and Path(str(exe) + ".exe").exists():
        args.trace = str(exe) + ".exe"
    root = Path(args.keep) if args.keep else Path(tempfile.mkdtemp())
    root.mkdir(parents=True, exist_ok=True)
    cases = [case_game(args, root), case_wide(args, root, 6),
             case_wide(args, root, 16), case_refused(args, root),
             case_effect(args, root)]
    if not args.no_heard:
        cases.append(case_heard(args, root, "chip", "ps2_headless"))
        if exe_path("ps2_headless_q").exists():
            cases.append(case_heard(args, root, "q", "ps2_headless_q"))
        else:
            print("(no ps2_headless_q: Q's heard case left out; "
                  "make ps2_headless_q)")
    for c in cases:
        print(f"{'PASS' if c.ok else 'FAIL'}  {c.name}")
        for line in c.lines:
            print(f"      {line}")
    if not args.keep:
        shutil.rmtree(root, ignore_errors=True)
    good = sum(c.ok for c in cases)
    print(f"chords_check: {good} of {len(cases)}")
    return 0 if good == len(cases) else 1


if __name__ == "__main__":
    sys.exit(main())
