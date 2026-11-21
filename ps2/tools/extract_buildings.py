#!/usr/bin/env python3
"""What BuildingScreen looks up, as JSON.

  PortraitTable      portrait_index 1..$1E: (palette id << 24) | art. The
                     art decompresses to VRAM $8000 (tile $400) and the
                     palette goes to the line its PalettePtrs entry names.
  BuildingMusicPtrs  one music id per BuildingID_.
  PtrWin_PortraitStart  the portraits' windows in WindowArtLayoutPtrs.

Output: assets/data/portraits.json, assets/data/buildings.json,
assets/data/portrait_windows.json. Art is named by the file the
disassembly binclude's there, which is what extract_art.py names the PNG
after.

(build 185) From the ROM itself (tools/rommap.py), not ps2.asm: a pointer
is named by the label (or binclude'd file) at its address, a byte by the
constant of its family with that value.
"""

import argparse
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
import asset_names  # noqa: E402
from rommap import RomMap  # noqa: E402
from extract_gamedata import (constant_names, file_at, file_stem,  # noqa: E402
                              id_keys, label_at, run_of)

PAL_PTR_SIZE = 8        # a PalettePtrs entry: pointer, CRAM address, count
WINDOW_SIZE = 8         # a WindowArtLayoutPtrs entry: position, art, w, h
MAX_PORTRAIT_WINDOWS = 0x38


def palette_entry(rom, pal_id):
    """The PalettePtrs entry a portrait's palette id loads, as the first
    entry that points at the same colours: two entries can be the same
    bytes (PtrPal_RadarPort and PtrPal_CTwOut3Port, both
    Pal_RadarPortrait), and the first one's PNG is the one kept."""
    table = rom.addr("PalettePtrs")
    want = rom.u32(table + PAL_PTR_SIZE * pal_id) & 0xFFFFFF
    return next(k for k in range(pal_id + 1)
                if rom.u32(table + PAL_PTR_SIZE * k) & 0xFFFFFF == want)


def art_name(rom, address):
    """The PNG stem for the art at an address: its binclude'd file's, or
    the label's when the art is written out in place."""
    name = file_at(rom, address)
    if name is not None and name.replace("\\", "/").startswith("art/") \
            and name.endswith(".bin"):
        return asset_names.art_name(file_stem(name))
    label = label_at(rom, address)
    if label is None:
        sys.exit(f"no art at ${address:X}")
    return asset_names.art_name(label)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("rom", type=Path, help="the cartridge image")
    ap.add_argument("-o", "--out", type=Path, default=Path("assets/data"))
    args = ap.parse_args()

    rom = RomMap(args.rom)

    # PortraitTable: one long a portrait, each under its PtrPortrait_ label
    entries, _ = run_of(rom, "PortraitTable",
                        lambda n: n.startswith("PtrPortrait_"))
    portraits = []
    for label, at in entries:
        long_ = rom.u32(at)
        portraits.append({
            "index": len(portraits) + 1,
            "name": label[len("PtrPortrait_"):],
            "art": art_name(rom, long_ & 0xFFFFFF),
            "palette": palette_entry(rom, long_ >> 24),
        })

    # Building music: a byte per BuildingID_, in id order (an `even` byte
    # follows the table).
    names = id_keys(rom, "BuildingID_")
    music_names = constant_names(rom, "MusicID_")
    raw = rom.blob("BuildingMusicPtrs")
    if not len(names) <= len(raw) <= len(names) + 1:
        sys.exit(f"BuildingMusicPtrs is {len(raw)} bytes for "
                 f"{len(names)} buildings")
    buildings = []
    for i, name in enumerate(names):
        if raw[i] not in music_names:
            sys.exit(f"building {name}: music ${raw[i]:02X} is no MusicID_")
        buildings.append({"index": i, "name": name, "music": raw[i],
                          "music_name": music_names[raw[i]]})

    # Portrait windows: WindowArtLayoutPtrs entries from PtrWin_PortraitStart
    # on, eight bytes each -- position word, art pointer, width and height
    # bytes. Building_CheckRoutine opens WinID_PortraitStart +
    # portrait_index, so the entry number is the portrait index. Counted
    # by entry, not by label: not every entry has one. They end at the
    # first window whose art is not a WinArt_ block in the ROM (the next,
    # PtrWin_HouseLVEXP, draws from RAM).
    at = rom.addr("PtrWin_PortraitStart")
    windows = []
    while len(windows) < MAX_PORTRAIT_WINDOWS:
        pos, art, w, h = (rom.u16(at), rom.u32(at + 2),
                          rom.u8(at + 6), rom.u8(at + 7))
        label = label_at(rom, art, "WinArt_") if art < len(rom.rom) else None
        if label is None:
            break
        windows.append({"portrait": len(windows), "pos": pos,
                        "art": label, "w": w, "h": h})
        at += WINDOW_SIZE

    args.out.mkdir(parents=True, exist_ok=True)
    (args.out / "portrait_windows.json").write_text(
        json.dumps(windows, indent=1), newline="\n")
    (args.out / "portraits.json").write_text(json.dumps(portraits, indent=1), newline="\n")
    (args.out / "buildings.json").write_text(json.dumps(buildings, indent=1), newline="\n")
    print(f"{len(portraits)} portraits, {len(buildings)} buildings -> {args.out}")


if __name__ == "__main__":
    main()
