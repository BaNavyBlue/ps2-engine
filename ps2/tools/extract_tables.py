#!/usr/bin/env python3
"""Extract small inline data tables that drive screen effects.

CyclingPal_SegaLogo is a ramp that PaletteCycle slides a window over:
every 4 frames the byte offset drops by 2 and 11 words are copied into
CRAM entries 2..12, which is the SEGA logo's sweep.

FadeEffectArray is a 64-entry order in which pixel positions of every tile
are revealed, giving the ordered dissolve the title screen uses for the
figures and the logo.

(build 185) Each table is read from the cartridge image (tools/rommap.py),
not ps2.asm: a table is the bytes from its label to the next label, the
credits' script up to its end code (the zero after it is `even`'s pad).

(build 136a) The dissolve order is assets/tables/dissolve_order.json, its
64 positions as [x, y] (asset_names.TABLE_FILES), read by
table_load_dissolve.

(build 136) The two colour ramps -- the Sega logo's sweep and the credits'
fade (loc_6B10) -- are pictures now, assets/palettes/<name>.png
(ps2png.write_ramp, names and row widths in asset_names.RAMP_NAMES), read by
table_load_ramp. Their old tables/*.bin are deleted.

(build 136d) The FM instruments and their register order are
assets/music/fm_instruments.json, extract_music.py's now (read from the
sound driver's image, as SetFMIns reads them); this deletes the
FMInstruments.bin and FMRegisterOrder.bin it wrote until then.

(build 136e) The ending's credits (EndingCredits_Script) are text beside the
window text, assets/script/ending_credits.txt (asset_names.SCRIPT_FILES),
read by core/credits.c's credits_load: a line of the file a line of the
credits as it lands on the screen, its tags the codes that end it. The
file's own header says how to read it. It is checked here to give back the
ROM's bytes before it is written. The last of this extractor's .bin files,
EndingCredits_Script.bin, is deleted.
"""

import argparse
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
import ps2png  # noqa: E402
from asset_names import RAMP_NAMES, SCRIPT_FILES, TABLE_FILES  # noqa: E402
from rommap import RomMap  # noqa: E402

# (build 136) The .bin files the ramps were, deleted when the PNGs are made;
# then the rest of the containers this wrote, each as it became readable.
STALE = ("CyclingPal_SegaLogo.bin", "EndingCreditsFadeRamp.bin",
         "FadeEffectArray.bin",     # (build 136a)
         "FMInstruments.bin", "FMRegisterOrder.bin",    # (build 136d)
         "EndingCredits_Script.bin")                    # (build 136e)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("rom", type=Path, help="the cartridge image")
    ap.add_argument("-o", "--outdir", type=Path, default=Path("assets/tables"))
    ap.add_argument("--palettes", type=Path, default=None,
                    help="where the colour ramps go (default: palettes/ "
                         "beside the outdir)")
    ap.add_argument("--script", type=Path, default=None,
                    help="(build 136e) where the credits go (default: "
                         "script/ beside the outdir)")
    args = ap.parse_args()

    rom = RomMap(args.rom)
    args.outdir.mkdir(parents=True, exist_ok=True)
    extract_ramps(rom, args.palettes or args.outdir.parent / "palettes")
    write_dissolve(args.outdir / TABLE_FILES["FadeEffectArray"],
                   rom.blob("FadeEffectArray"))
    script_dir = args.script or args.outdir.parent / "script"
    script_dir.mkdir(parents=True, exist_ok=True)
    write_credits(script_dir / SCRIPT_FILES["EndingCredits_Script"],
                  credits_bytes_in_rom(rom))
    delete_stale(args.outdir)


def extract_ramps(rom, paldir):
    """(build 136) Each colour ramp as a picture, RAMP_NAMES's rows wide
    (delete_stale deletes the .bin files they were); (build 185) a ramp is
    its label's bytes in the ROM, to the next label, a CRAM word each two."""
    paldir.mkdir(parents=True, exist_ok=True)
    for label, (name, across) in RAMP_NAMES.items():
        data = rom.blob(label)
        if len(data) % 2:
            raise ValueError(f"{label}: {len(data)} bytes, not whole words")
        words = [int.from_bytes(data[i:i + 2], "big")
                 for i in range(0, len(data), 2)]
        out = paldir / f"{name}.png"
        ps2png.write_ramp(out, words, across)
        print(f"{label}: {len(words)} colours, {len(words) // across} "
              f"row(s) of {across} -> {out}")


def delete_stale(outdir):
    """The .bin containers this wrote until each kind was made readable,
    deleted once everything is written."""
    for stale in STALE:
        old = outdir / stale
        if old.exists():
            old.unlink()
            print(f"deleted {old} (in a readable form now)")


DISSOLVE_STEPS = 64
DISSOLVE_ABOUT = (
    "The dissolve order (FadeEffectArray): step i of a dissolve in reveals "
    "pixel [x, y] of every tile in its range at once (FadeArtFill, from "
    "FadeArtTiles). The dissolve out (loc_6C54) steps the first 32 and "
    "clears the two-pixel byte each falls in: x 0-1, 2-3, 4-5 or 6-7. Each "
    "position is the ROM's byte y * 8 + x, its place in the tile.")


def write_dissolve(path, data):
    """(build 136a) FadeEffectArray's 64 bytes as [x, y] pixel positions,
    eight to a line; the byte is y * 8 + x (FadeArtFill takes the word
    byte >> 2 of the tile and the pixel byte & 3 in it)."""
    if len(data) != DISSOLVE_STEPS or max(data) >= 64:
        raise ValueError(f"FadeEffectArray: {len(data)} bytes, not "
                         f"{DISSOLVE_STEPS} positions 0-63")
    pairs = [f"[{b & 7}, {b >> 3}]" for b in data]
    rows = [", ".join(pairs[i:i + 8]) for i in range(0, len(pairs), 8)]
    import json
    text = ('{\n  "source": "FadeEffectArray",\n  "about": '
            + json.dumps(DISSOLVE_ABOUT) + ',\n  "order": [\n    '
            + ",\n    ".join(rows) + "\n  ]\n}\n")
    path.write_text(text, newline="\n")
    print(f"FadeEffectArray: {len(data)} positions -> {path}")


# (build 136e) The credits writer's codes (loc_787E): core/credits.h's.
CREDITS_SPACE, CREDITS_BLANK_RUN_BELOW = 0x20, 0x14
CREDITS_NEXT_LINE, CREDITS_END = 0xC1, 0xC5
CREDITS_LINE_ENDS = {0xC1: "", 0xC3: "{close}", 0xC4: "{section}"}
CREDITS_LINE_CELLS = 39     # the writer counts 40 a row; a 40th runs it out
CREDITS_LETTERS = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz"
CREDITS_MAX = 4096          # credits.h: the bytes credits_load holds

CREDITS_HEADER = """\
# The ending's credits (EndingCredits_Script), as the credits writer
# (loc_787E) scrolls them up over the stars under "Never Dream".
#
# Each line below is a line of the credits as it lands on the screen, 40
# cells across: its leading spaces are its indent and every other space is
# one blank cell, so the text sits here where it shows there. A line holds
# letters and spaces only, 39 cells at most (the writer counts 40 a row,
# and a 40th runs its count out).
#
# Capitals are drawn in white (palette line 0) and lower case in the
# titles' blue (line 1). The credits' alphabet has capitals only, so both
# show as capitals: here the case is the colour.
#
# Two blank rows follow a line. A line ending {close} has one after it, and
# a line ending {section} has 28, the gap before the next section. {empty}
# on its own is a line with nothing on it, three rows of nothing; {end} on
# its own stops the credits there, the screen held until A, B or C. Lines
# starting # and empty lines are skipped: the empty lines here are only for
# the eye.
"""


def credits_text(data):
    """(build 136e) The credits writer's bytes as lines of text: blank runs
    (a byte n under $14, n + 1 cells) and $20 as spaces, letters as
    themselves, a line's end code as its tag. Anything the text cannot say
    is refused."""
    lines, cur, ended = [], "", False
    for k, b in enumerate(data):
        if ended:
            raise ValueError(f"EndingCredits_Script: byte {k} after the end "
                             f"code")
        if b < CREDITS_BLANK_RUN_BELOW:
            cur += " " * (b + 1)
        elif b == CREDITS_SPACE:
            cur += " "
        elif 0x41 <= b <= 0x5A or 0x61 <= b <= 0x7A:
            cur += chr(b)
        elif b == CREDITS_NEXT_LINE and cur == "":
            lines.append("{empty}")
        elif b in CREDITS_LINE_ENDS:
            lines.append(cur + CREDITS_LINE_ENDS[b])
            if CREDITS_LINE_ENDS[b] == "{section}":
                lines.append("")        # for the eye: skipped when read
            cur = ""
        elif b == CREDITS_END and cur == "":
            lines.append("{end}")
            ended = True
        else:
            raise ValueError(f"EndingCredits_Script: byte {k} is ${b:02X}, "
                             f"which the text does not say")
    if not ended:
        raise ValueError("EndingCredits_Script: no end code")
    return CREDITS_HEADER + "\n" + "\n".join(lines) + "\n"


def credits_bytes(text):
    """(build 136e) The text back to the writer's bytes, as credits_load
    reads it: leading spaces as blank runs of 20 cells at most, other
    spaces $20, letters their ASCII bytes, then the line's end code."""
    out, ended = bytearray(), False
    for n, line in enumerate(text.split("\n"), start=1):
        if line.startswith("#") or line == "":
            continue
        if ended:
            raise ValueError(f"line {n} is after {{end}}")
        if line in ("{end}", "{empty}"):
            out.append(CREDITS_END if line == "{end}" else CREDITS_NEXT_LINE)
            ended = line == "{end}"
            continue
        body, brace, tag = line.partition("{")
        ends = {v: k for k, v in CREDITS_LINE_ENDS.items()}
        if brace + tag not in ends:
            raise ValueError(f"line {n}: {brace + tag} out of place")
        if len(body) > CREDITS_LINE_CELLS:
            raise ValueError(f"line {n} is {len(body)} cells wide")
        if any(ch != " " and ch not in CREDITS_LETTERS for ch in body):
            raise ValueError(f"line {n}: not letters and spaces")
        indent = len(body) - len(body.lstrip(" "))
        left = indent
        while left:
            run = min(left, CREDITS_BLANK_RUN_BELOW)
            out.append(run - 1)
            left -= run
        out += body[indent:].encode("ascii")
        out.append(ends[brace + tag])
    if not ended:
        raise ValueError("no {end}")
    if len(out) > CREDITS_MAX:
        raise ValueError(f"{len(out)} bytes, more than {CREDITS_MAX}")
    return bytes(out)


def credits_bytes_in_rom(rom, label="EndingCredits_Script"):
    """(build 185) The credits writer's bytes in the ROM: from the label to
    the end code, where the writer stops. Its length is odd, so `even`
    puts a zero after it before the next label; anything more there would
    be bytes the text does not hold, and is refused."""
    block = rom.blob(label)
    end = block.find(bytes([CREDITS_END]))
    if end < 0:
        raise ValueError(f"{label}: no end code before the next label")
    rest = block[end + 1:]
    if len(rest) > 1 or rest.strip(b"\0"):
        raise ValueError(f"{label}: {len(rest)} bytes after the end code "
                         f"that are not `even`'s pad")
    return block[:end + 1]


def write_credits(path, data):
    """(build 136e) The credits as text, checked to give the ROM's bytes
    back before it is written."""
    text = credits_text(data)
    if credits_bytes(text) != bytes(data):
        raise ValueError("EndingCredits_Script: the text does not give the "
                         "ROM's bytes back")
    path.write_text(text, newline="\n")
    print(f"EndingCredits_Script: {len(data)} bytes, "
          f"{sum(1 for l in text.split(chr(10)) if l and l[0] != '#')} "
          f"lines -> {path}")


if __name__ == "__main__":
    main()
