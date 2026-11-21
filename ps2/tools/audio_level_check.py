#!/usr/bin/env python3
"""(build 109a) The mix's level, end to end through the game's own mixer.

Runs the headless game through the attract sequence's first 2400 frames --
the SEGA screen, then the title's theme under the title screen -- with
PS2_WAV, the WAV audio_render writes (full scale 32000, platform_headless.c),
and holds the title's theme against the level build 109a set:

  * its peak between -12 and -1 dBFS (before 109a it was -22: the master
    was 12 dB lower);
  * no sample at full scale (the soft knee in src/audio/mix.h bends
    everything above -3 dBFS toward it and never reaches it).

    make headless && python3 tools/audio_level_check.py
    python3 tools/audio_level_check.py --binary OTHER/ps2_headless

(build 110) `--synth chip|q` names the synth backend (PS2_SYNTH), so a
settings.json that chose Q cannot change the chip's check; chip unless given.
`make audio_check_q` runs `--synth q --binary ./ps2_headless_q`: the Q
backend's title theme with no sample at full scale, (110b) its peak under -1
dBFS and within 3 dB of the chip's through ./ps2_headless (the same run with
PS2_SYNTH=chip) -- (111) its loudest 400 ms within 3 dB of the chip's: the
chip's loudest sample in the run is a write-queue transient, not music.

Writes route_out/audio_level_check/report.txt (report_q.txt for Q); prints
one PASS or FAIL line; exits non-zero on a FAIL. The standard library
only."""

import argparse
import array
import math
import os
import subprocess
import sys
import tempfile
import wave

FULL_SCALE = 32000.0
FRAMES = 2400
PEAK_LOW_DB = -12.0
PEAK_HIGH_DB = -1.0
Q_AGAINST_CHIP_DB = 3.0


def measure(path):
    with wave.open(path) as w:
        rate = w.getframerate()
        data = array.array("h")
        data.frombytes(w.readframes(w.getnframes()))
    if sys.byteorder != "little":
        data.byteswap()
    peak = max((abs(v) for v in data), default=0)
    at_full = sum(1 for v in data if abs(v) >= FULL_SCALE)
    acc = sum(float(v) * float(v) for v in data)
    rms = math.sqrt(acc / max(1, len(data)))
    seconds = len(data) / 2 / rate

    # (build 111) The loudest passage: the highest RMS over 400 ms windows
    # (200 ms apart). A peak can be a moment: the chip's loudest sample in
    # the attract run is a 3 ms transient at 36.45 s, an instrument upload
    # whose total levels land for a millisecond through the chip's write
    # queue under a sounding tail -- 7 dB above anything around it, and not
    # music a backend that takes a tick's writes at once can have.
    win = int(0.4 * rate) * 2
    hop = int(0.2 * rate) * 2
    loud = 0.0
    for at in range(0, max(1, len(data) - win), hop):
        seg = data[at:at + win]
        e = sum(float(v) * float(v) for v in seg) / max(1, len(seg))
        loud = max(loud, e)

    def db(x):
        return 20.0 * math.log10(max(x, 1e-9) / FULL_SCALE)

    return db(peak), db(rms), at_full, seconds, db(math.sqrt(loud))


def run_title(binary, synth):
    """The attract sequence's WAV through `binary` on `synth`: peak, RMS,
    samples at full scale, seconds; None when the binary fails."""
    with tempfile.TemporaryDirectory() as tmp:
        wav = os.path.join(tmp, "title.wav")
        # (111a) the default oversampling by name, as the synth: a
        # settings.json that chose another cannot move the check
        env = dict(os.environ, PS2_FRAMES=str(FRAMES), PS2_WAV=wav,
                   PS2_OUT=os.path.join(tmp, "frame.ppm"), PS2_SYNTH=synth,
                   PS2_Q_OVERSAMPLE="4")
        run = subprocess.run([binary], env=env, stdout=subprocess.PIPE,
                             stderr=subprocess.STDOUT)
        if run.returncode != 0:
            print("FAIL  %s exited %d" % (binary, run.returncode))
            return None
        return measure(wav)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--binary", default="./ps2_headless")
    ap.add_argument("--synth", default="chip", choices=("chip", "q"))
    ap.add_argument("--chip-binary", default="./ps2_headless",
                    help="for --synth q: the chip's run to hold it against")
    ap.add_argument("--out", default="route_out/audio_level_check")
    args = ap.parse_args()

    got = run_title(args.binary, args.synth)
    if got is None:
        return 1
    peak, rms, at_full, seconds, loud = got

    ok = PEAK_LOW_DB <= peak <= PEAK_HIGH_DB and at_full == 0
    against = ""
    if args.synth == "q":
        # (build 110b) Q is held to the chip's own peak, +-3 dB, in place of
        # the absolute window: that window was set for the chip (109a) to
        # catch a mix far too quiet, which the chip's peak does more tightly.
        # Q's trim puts its peaks with the chip's by the median over 23 songs;
        # the title's first 34 s sit 2.7 dB under (and later passages 2.7
        # over), past the window's floor by 0.7 dB.
        # (build 111) Held by the loudest passage, not the loudest sample:
        # the chip's peak in this run is one transient (measure(), above);
        # Q's peak must still stay under -1 dBFS.
        chip = run_title(args.chip_binary, "chip")
        if chip is None:
            return 1
        gap = loud - chip[4]
        ok = abs(gap) <= Q_AGAINST_CHIP_DB and peak <= PEAK_HIGH_DB \
            and at_full == 0
        against = (", loudest 400 ms %.1f dBFS, %+.1f dB against the chip's "
                   "%.1f (wanted within %.0f)"
                   % (loud, gap, chip[4], Q_AGAINST_CHIP_DB))
    what = "the chip" if args.synth == "chip" else "Q"
    window = ("wanted %.0f to %.0f" % (PEAK_LOW_DB, PEAK_HIGH_DB)
              if args.synth == "chip" else "under %.0f" % PEAK_HIGH_DB)
    line = ("%s  the title's theme through the game's mixer on %s, %.1f s: "
            "peak %.1f dBFS (%s)%s, RMS %.1f dBFS, %d samples at full scale"
            % ("PASS" if ok else "FAIL", what, seconds, peak, window, against,
               rms, at_full))
    os.makedirs(args.out, exist_ok=True)
    name = "report.txt" if args.synth == "chip" else "report_q.txt"
    with open(os.path.join(args.out, name), "w", newline="\n") as fh:
        fh.write("# The mix's level (tools/audio_level_check.py, builds "
                 "109a, 110)\n\n")
        fh.write("binary: %s, PS2_SYNTH=%s, %d frames of the attract "
                 "sequence\n\n" % (args.binary, args.synth, FRAMES))
        fh.write(line + "\n")
    print(line)
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
