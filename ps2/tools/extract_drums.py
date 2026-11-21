#!/usr/bin/env python3
"""Extract the DAC drums: the Z80 program's sample table and the PCM data.

`LoadPCMDrums` copies `ps2_pcm_drums.asm` -- a Z80 program followed by its
samples -- into Z80 RAM at boot, and that is all the Z80 ever runs. The
program is small enough to read in full:

    zloc_68   reads the drum byte the 68k dropped into $1FFF and tests
              its bits 0..6 in that order; the first set bit picks a
              sample, a length and a delay. $80 with no bits set is a
              sample of its own.
    zloc_EE   clears $1FFF to acknowledge, then per sample byte: spins
              `djnz` delay times, halves the byte with `srl a`, writes
              it to register $2A, and re-reads $1FFF -- a new drum
              byte interrupts the one playing.

So the format is raw unsigned 8-bit PCM at half amplitude, and the
playback rate is set by the Z80 instruction timing rather than any
clock: T-states per byte are 240 + 13 * delay, worked out instruction
by instruction in src/audio/fm.c where the rate is computed.

(build 185) The dispatch is read from the program's own bytes in the
cartridge image (tools/rommap.py), not from ps2_pcm_drums.asm and no
longer transcribed by hand: zloc_68's `bit n, a` / `jp nz` pairs give the
drum byte each branch takes, and each branch's `ld de` / `ld hl` / `ld a`
its sample's Z80 address, the length and the delay. The file is assembled
at Z80 address 0 from PCMDrums on, so a Z80 address is PCMDrums plus it; a
sample runs from the label `de` points at to the file's next label.

One data bug is real and kept. The last sample, `zloc_1A9C`, is 18 bytes
long in the ROM but the program plays $201 = 513 from it, so drum $90
runs 495 bytes past the end of what was copied -- into Z80 RAM that
nothing initialised. Emulators leave that RAM zeroed; the image here is
padded to the full 8 KB the same way, so $90 plays 18 bytes near the
midpoint and then drops to the DAC's floor, which is what the ROM
actually gives you. No song uses $90.

Emits (build 136b; the "PS2D" container PCMDrums.bin until then, deleted):
    drums.json      the samples (file, Z80 address) and the drums (byte,
                    name, sample, length, delay), read by drums_load
                    (src/audio/drums.c)
    drums/<n>.wav   one per sample, the ROM's bytes as 8-bit unsigned mono
                    PCM, unhalved; the WAV's rate is the first drum's that
                    plays it, a label only (the delay sets the rate)
    --wav DIR       one 8-bit WAV per drum at its playback rate, halved
                    as the Z80 halves it, to listen to: drum_84_snare.wav
"""

import argparse
import json
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from asset_names import DRUM_SAMPLE_FILES  # noqa: E402
from rommap import RomMap  # noqa: E402

Z80_CLOCK = 3579545          # NTSC, master / 15
Z80_RAM = 0x2000
T_FIXED = 240                # see fm.c: everything in the loop but djnz
T_PER_DELAY = 13

# The drums by the byte a song sends, and their names. zloc_68 tests bits
# 0,1,2,3,4,5,6 in that order, so a byte with several bits set takes the
# lowest, and $80 with none is a drum of its own; the music only ever sends
# single-bit bytes and $80. What each plays is read from the program
# (dispatch, below; build 185).
#
# (build 130) The names, the port's. The toms' are the disassembly's
# (UpdateDrumTrack's comments: "High Tom" on the left speaker, "Low Tom" on
# the right); $81 is the same sample between them. The rest are named for
# what the samples measure as (docs/audio.md, "Drums"): $84 a body near 200
# Hz under noise at 2-6 kHz, a snare; $88 33 ms with 86% of its energy below
# 200 Hz, a kick; $A0 and $C0 noise centred near 3.2 kHz, 125 and 42 ms, the
# open and the closed hi-hat. $90 plays past its 18 bytes (below), so it has
# no sound to name: "unused". src/audio/fm.h's DRUM_* carry the same names.
DRUMS = [
    (0x80, "high_tom"),     # the disassembly: "Left Speaker (High Tom)"
    (0x81, "mid_tom"),
    (0x82, "low_tom"),      # the disassembly: "Right Speaker (Low Tom)"
    (0x84, "snare"),
    (0x88, "kick"),
    (0x90, "unused"),
    (0xA0, "open_hat"),
    (0xC0, "closed_hat"),
]

# (build 185) The Z80 opcodes the dispatch is made of; a word operand
# follows low byte first.
Z80_LD_A_MEM = 0x3A         # ld a, (nn): the drum byte from its mailbox
Z80_BIT_A = 0xCB            # bit n, a is $CB, $47 + 8n
Z80_JP_Z = 0xCA             # jp z, nn
Z80_JP_NZ = 0xC2            # jp nz, nn
Z80_JP = 0xC3               # jp nn
Z80_LD_DE = 0x11            # ld de, nn: the sample's address
Z80_LD_HL = 0x21            # ld hl, nn: how many bytes it plays
Z80_LD_A = 0x3E             # ld a, n: the delay


class Z80Code:
    """Reads the drum program's instructions from the ROM, by Z80
    address."""

    def __init__(self, rom):
        self.rom = rom
        self.base = rom.addr("PCMDrums")

    def byte(self, z80):
        return self.rom.u8(self.base + z80)

    def word(self, z80):
        return self.byte(z80) | self.byte(z80 + 1) << 8

    def expect(self, z80, opcode, what):
        if self.byte(z80) != opcode:
            raise SystemExit(f"the drum program at Z80 ${z80:04X}: "
                             f"${self.byte(z80):02X}, not {what} "
                             f"(${opcode:02X})")


def dispatch(rom):
    """(build 185) {drum byte: (sample's Z80 address, length, delay)} as
    zloc_68 dispatches: `ld a, (mailbox)`, `bit 7, a` / `jp z` (no drum
    yet), then a `bit n, a` / `jp nz` pair a bit, then `jp` for $80 alone;
    each branch `ld de, sample` / `ld hl, length` / `ld a, delay`."""
    code = Z80Code(rom)
    at = rom.addr("zloc_68") - code.base
    code.expect(at, Z80_LD_A_MEM, "ld a, (nn)")
    at += 3
    branches = []
    while code.byte(at) == Z80_BIT_A:
        bit, rest = divmod(code.byte(at + 1) - 0x47, 8)
        if rest or not 0 <= bit <= 7:
            raise SystemExit(f"the drum program at Z80 ${at:04X}: $CB "
                             f"${code.byte(at + 1):02X} is not bit n, a")
        jump = code.byte(at + 2)
        if bit == 7:
            if jump != Z80_JP_Z:
                raise SystemExit("the drum program: bit 7, a without jp z")
        else:
            code.expect(at + 2, Z80_JP_NZ, "jp nz, nn")
            branches.append((0x80 | 1 << bit, code.word(at + 3)))
        at += 5
    code.expect(at, Z80_JP, "jp nn")
    branches.append((0x80, code.word(at + 1)))
    table = {}
    for byte, branch in branches:
        code.expect(branch, Z80_LD_DE, "ld de, nn")
        code.expect(branch + 3, Z80_LD_HL, "ld hl, nn")
        code.expect(branch + 6, Z80_LD_A, "ld a, n")
        table[byte] = (code.word(branch + 1), code.word(branch + 4),
                       code.byte(branch + 7))
    return table


def read_samples(rom, starts):
    """(build 185) Each sample (label, Z80 address, bytes): from each
    address in `starts` (where the dispatch's `ld de` points) to the drum
    file's next label, the bytes the ROM has there."""
    base = rom.addr("PCMDrums")
    labels = sorted((a - base, n) for n, a in rom.drums)
    out = []
    for z80 in sorted(set(starts)):
        here = [n for a, n in labels if a == z80]
        later = [(a, n) for a, n in labels if a > z80]
        if len(here) != 1 or not later:
            raise SystemExit(f"Z80 ${z80:04X}: a sample with "
                             f"{len(here)} labels and "
                             f"{'a' if later else 'no'} label after it")
        # Looked up by name, so that the map's list of the names the
        # extractors use holds both ends of the sample.
        rom.addr(here[0])
        rom.addr(later[0][1])
        out.append((here[0], z80, rom.read(base + z80, later[0][0] - z80)))
    return out


def build_image(samples):
    """Places every sample at its Z80 address, in 8 KB of Z80 RAM that is
    zero elsewhere (the program's own bytes are left out: only the samples
    matter)."""
    image = bytearray(Z80_RAM)
    end = 0
    for _label, addr, data in samples:
        image[addr:addr + len(data)] = data
        end = max(end, addr + len(data))
    return bytes(image), end


def rate_for(delay):
    return Z80_CLOCK / (T_FIXED + T_PER_DELAY * delay)


def write_wav(path, data, rate):
    """8-bit unsigned mono, the bytes as the DAC would see them."""
    with path.open("wb") as fh:
        fh.write(b"RIFF")
        fh.write(struct.pack("<I", 36 + len(data)))
        fh.write(b"WAVEfmt ")
        fh.write(struct.pack("<IHHIIHH", 16, 1, 1, rate, rate, 1, 8))
        fh.write(b"data")
        fh.write(struct.pack("<I", len(data)))
        fh.write(data)


DRUMS_ABOUT = (
    "The DAC drums (ps2_pcm_drums.asm, which LoadPCMDrums copies into Z80 "
    "RAM). Each sample is its WAV's bytes -- unsigned 8-bit PCM, the ROM's "
    "own; the Z80 halves each as it plays it -- placed in the 8 KB of Z80 "
    "RAM at z80_address (from $118, past the program's own bytes); RAM no "
    "sample covers is zero. A drum byte a song sends plays `length` bytes "
    "from its sample's address, one every 240 + 13 * delay Z80 cycles "
    "(3579545 Hz), so the WAV's own rate is only a label: the toms are one "
    "sample at three delays. Drum $90 plays 513 bytes of an 18-byte sample, "
    "running on into that zeroed RAM, as in the ROM; no song sends it.")


def write_drum_set(outdir, image, samples, drums):
    """(build 136b) drums/<name>.wav for each sample (name, Z80 address,
    bytes) and drums.json naming them and the drums (byte, name, sample,
    length, delay). `image` is the Z80 RAM they make (tools/roundtrip.py
    records it); drums_load rebuilds it."""
    wav_dir = outdir / "drums"
    wav_dir.mkdir(parents=True, exist_ok=True)
    first_delay = {}
    for _byte, _name, sample, _length, delay in drums:
        first_delay.setdefault(sample, delay)
    lines = []
    for name, addr, data in samples:
        write_wav(wav_dir / f"{name}.wav", data,
                  round(rate_for(first_delay.get(name, 4))))
        lines.append(f'    {{"name": "{name}", "file": "drums/{name}.wav", '
                     f'"z80_address": {addr}}}')
    dlines = [f'    {{"byte": {byte}, "name": "{name}", "sample": "{sample}", '
              f'"length": {length}, "delay": {delay}}}'
              for byte, name, sample, length, delay in drums]
    text = ('{\n  "source": "ps2_pcm_drums.asm",\n  "about": '
            + json.dumps(DRUMS_ABOUT) + ',\n  "samples": [\n'
            + ",\n".join(lines) + '\n  ],\n  "drums": [\n'
            + ",\n".join(dlines) + "\n  ]\n}\n")
    (outdir / "drums.json").write_text(text, newline="\n")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("rom", type=Path, help="the cartridge image")
    ap.add_argument("-o", "--outdir", type=Path, default=Path("assets/music"))
    ap.add_argument("--wav", type=Path, help="also write one WAV per drum here")
    args = ap.parse_args()

    rom = RomMap(args.rom)
    table = dispatch(rom)
    if sorted(table) != [byte for byte, _ in DRUMS]:
        raise SystemExit("the drum program dispatches bytes "
                         + ", ".join(f"${b:02X}" for b in sorted(table))
                         + ", not the drums named here")
    found = read_samples(rom, [z80 for z80, _, _ in table.values()])
    image, end = build_image(found)
    label_at = {z80: label for label, z80, _ in found}
    size_at = {z80: len(data) for _, z80, data in found}

    for byte, name in DRUMS:
        addr, length, delay = table[byte]
        have = size_at[addr]
        note = "" if have >= length else f"  (only {have} bytes in ROM)"
        print(f"  ${byte:02X}  {name:<10}  {label_at[addr]:<10} at {addr:04X}  "
              f"{length:4d} bytes  delay {delay}  ~{rate_for(delay):5.0f} Hz"
              f"{note}")

    args.outdir.mkdir(parents=True, exist_ok=True)
    samples = []
    for label, addr, data in found:
        if label not in DRUM_SAMPLE_FILES:
            raise SystemExit(f"{label}: a sample asset_names.DRUM_SAMPLE_FILES "
                             f"does not name")
        samples.append((DRUM_SAMPLE_FILES[label], addr, data))
    drums = [(byte, name, DRUM_SAMPLE_FILES[label_at[table[byte][0]]],
              table[byte][1], table[byte][2]) for byte, name in DRUMS]
    write_drum_set(args.outdir, image, samples, drums)
    old = args.outdir / "PCMDrums.bin"
    if old.exists():
        old.unlink()
        print(f"deleted {old} (drums.json and its WAVs now)")
    print(f"drums: {len(DRUMS)} drums, {len(samples)} samples, {end} bytes "
          f"of ROM -> {args.outdir / 'drums.json'}")

    if args.wav:
        args.wav.mkdir(parents=True, exist_ok=True)
        for byte, name in DRUMS:
            addr, length, delay = table[byte]
            data = bytes(b >> 1 for b in image[addr:addr + length])
            path = args.wav / f"drum_{byte:02X}_{name}.wav"
            write_wav(path, data, round(rate_for(delay)))
        print(f"WAVs -> {args.wav}")


if __name__ == "__main__":
    main()
