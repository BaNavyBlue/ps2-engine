#!/usr/bin/env python3
"""(build 102) Motavia's map with its random encounters' regions: the group
of each 16x16-cell region on foot (play.encounter_group, ProcessRandomBattle's
table), the ground the Paseo and Arima grinds walk (story.mild_ground: the
open cells of groups 0-2 north of Paseo's tube) dotted, the walks' anchors
and the places near it named. A view of the planes from the level's own
block maps (plane A over plane B, index 0 clear), not the game's frame.

    python3 tools/ground_map.py [OUT.png]    # route_out/mild_ground/...
"""

import sys
import xml.etree.ElementTree as ET
from pathlib import Path

from PIL import Image, ImageDraw

sys.path.insert(0, str(Path(__file__).parent))
import play                                                 # noqa: E402
import story                                                # noqa: E402

PLACES = {"Paseo": (73, 57), "Arima": (89, 51), "Paseo's tube": (73, 52),
          "Darum's tube": (63, 21), "Oputa": (57, 41), "Shure": (131, 25)}
CROP = (40, 0, 128, 84)                 # cells: x0, y0, x1, y1


def render_planes(level):
    lv = play._levels()[level]
    stem = f"{level:02x}_{lv['name']}"
    root = ET.parse(f"assets/levels/{stem}.tmx").getroot()
    atlas_name = root.find("tileset/image").get("source")
    atlas = Image.open(f"assets/levels/{atlas_name}")
    w, h = int(root.get("width")), int(root.get("height"))
    img = Image.new("P", (w * 32, h * 32))
    img.putpalette(atlas.getpalette())
    for name in ("planeB", "planeA"):
        layer = next(l for l in root.iter("layer") if l.get("name") == name)
        ids = [int(x) - 1 for x in layer.find("data").text.replace("\n", "")
               .split(",") if x]
        for i, b in enumerate(ids):
            if b < 0:
                continue
            bx, by = (b % 16) * 32, (b // 16) * 32
            tile = atlas.crop((bx, by, bx + 32, by + 32))
            mask = tile.point(lambda v: 0 if v % 16 == 0 else 255, "L") \
                if name == "planeA" else None
            img.paste(tile, ((i % w) * 32, (i // w) * 32), mask)
    return img.convert("RGBA")


def main():
    out = Path(sys.argv[1] if len(sys.argv) > 1
               else "route_out/mild_ground/motavia_mild_ground.png")
    img = render_planes(play.MOTAVIA)
    ground = story.mild_ground()
    ov = Image.new("RGBA", img.size, (0, 0, 0, 0))
    d = ImageDraw.Draw(ov)
    for x, y in ground.cells:
        d.rectangle((x * 16 + 5, y * 16 + 5, x * 16 + 10, y * 16 + 10),
                    fill=(255, 255, 255, 150))
    for c in range(13):
        d.line((c * 256, 0, c * 256, img.height), fill=(255, 255, 0, 220), width=3)
    for r in range(9):
        d.line((0, r * 256, img.width, r * 256), fill=(255, 255, 0, 220), width=3)
    for r in range(8):
        for c in range(12):
            g = play.encounter_group(play.MOTAVIA, (c * 16, r * 16))
            col = (80, 255, 80, 255) if g in story.MILD_GROUPS \
                else (255, 90, 90, 255)
            d.rectangle((c * 256 + 4, r * 256 + 4, c * 256 + 84, r * 256 + 44),
                        fill=(0, 0, 0, 210))
            d.text((c * 256 + 12, r * 256 + 12), f"group {g}", fill=col)
    for n, (x, y) in PLACES.items():
        d.rectangle((x * 16, y * 16, x * 16 + 15, y * 16 + 15),
                    outline=(255, 0, 255, 255), width=3)
        d.text((x * 16 + 20, y * 16 + 2), n, fill=(255, 0, 255, 255))
    for i, (x, y) in enumerate(ground.anchors):
        d.ellipse((x * 16 - 6, y * 16 - 6, x * 16 + 22, y * 16 + 22),
                  outline=(0, 255, 255, 255), width=4)
        d.text((x * 16 + 26, y * 16 + 4), f"anchor {i + 1}",
               fill=(0, 255, 255, 255))
    x0, y0, x1, y1 = CROP
    pic = Image.alpha_composite(img, ov).crop((x0 * 16, y0 * 16, x1 * 16,
                                               y1 * 16))
    pic = pic.resize((pic.width // 2, pic.height // 2), Image.LANCZOS)
    out.parent.mkdir(parents=True, exist_ok=True)
    pic.convert("RGB").save(out)
    print(out, pic.size, len(ground.cells), "cells,", "anchors",
          ground.anchors)


if __name__ == "__main__":
    main()
