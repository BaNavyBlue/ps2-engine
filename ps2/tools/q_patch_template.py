#!/usr/bin/env python3
"""(build 112) An instrument of the game's FM bank in the patch file's shape
(assets/music/q_patches.json; src/audio/patches.h; docs/audio.md, "The patch
file"): its ROM values written out, every field, to start a hand-voiced patch
from. Read from the sequencer's own image -- (build 137a) built from the song
text under assets/music (tools/song_text.py, as the game builds it;
SoundData.bin until then) -- through FMInsPtrs, the root pointer at $06, as
cfEF_SetFMIns reads it.

    python3 tools/q_patch_template.py 70 74          # to stdout
    python3 tools/q_patch_template.py --all > all.json

Paste an entry into q_patches.json's "patches" list, then change what you
want; a field left out keeps the ROM's value, so an entry can be cut down to
only what it changes."""

import argparse
import json
import os
import sys

# SetFMIns's upload order (loc_BA818), and each operator's offset in it
ORDER = [0xB0, 0xB4, 0x40, 0x48, 0x44, 0x4C, 0x30, 0x38, 0x34, 0x3C,
         0x50, 0x58, 0x54, 0x5C, 0x60, 0x68, 0x64, 0x6C, 0x70, 0x78,
         0x74, 0x7C, 0x80, 0x88, 0x84, 0x8C]
OP_OFFSET = [0x0, 0x8, 0x4, 0xC]
CARRIERS = [0x8, 0x8, 0x8, 0x8, 0xA, 0xE, 0xE, 0xF]
INSTRUMENTS = 105


def load_image(music):
    """(build 137a) The driver's image from the song text in `music`."""
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    import song_text
    try:
        return bytes(song_text.assemble(music).image)
    except song_text.TextError as exc:
        sys.exit("q_patch_template: %s" % exc)


def instrument(img, n):
    ptr = lambda at: img[at] | (img[at + 1] << 8)
    at = ptr(ptr(6) + 2 * n)
    return dict(zip(ORDER, img[at:at + 26]))


def detune(dt):
    return -(dt & 3) if dt & 4 else dt & 3


def patch(img, n):
    r = instrument(img, n)
    alg = r[0xB0] & 7
    ops = []
    for i, off in enumerate(OP_OFFSET):
        ops.append({
            "role": "carrier" if CARRIERS[alg] >> i & 1 else "modulator",
            "multiple": r[0x30 + off] & 15,
            "detune": detune(r[0x30 + off] >> 4 & 7),
            "level": r[0x40 + off] & 0x7F,
            "key_scale": r[0x50 + off] >> 6,
            "attack": r[0x50 + off] & 31,
            "decay": r[0x60 + off] & 31,
            "sustain_rate": r[0x70 + off] & 31,
            "sustain_level": r[0x80 + off] >> 4,
            "release": r[0x80 + off] & 15,
            "fine_cents": 0.0,
        })
    return {"instrument": n, "name": "instrument %d, the ROM's" % n,
            "enabled": True, "algorithm": alg,
            "feedback": r[0xB0] >> 3 & 7, "volume_db": 0.0, "tail": "row",
            "operators": ops}


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("instrument", type=int, nargs="*")
    ap.add_argument("--all", action="store_true")
    ap.add_argument("--music", default=os.path.join(here, "..", "assets",
                                                    "music"))
    args = ap.parse_args()
    img = load_image(args.music)
    wanted = range(INSTRUMENTS) if args.all else args.instrument
    if not wanted:
        ap.error("give instrument numbers, or --all")
    for n in wanted:
        if not 0 <= n < INSTRUMENTS:
            sys.exit("no instrument %d (0-%d)" % (n, INSTRUMENTS - 1))
    out = [patch(img, n) for n in wanted]
    print(json.dumps({"patches": out} if args.all or len(out) > 1 else out[0],
                     indent=2))


if __name__ == "__main__":
    main()
