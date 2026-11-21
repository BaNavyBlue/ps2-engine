#!/usr/bin/env python3
"""(build 123) The game's icon: Rolf's portrait.

    python3 tools/make_icon.py --from-shot route_out/new_game/05_house.png
    python3 tools/make_icon.py

The first cuts the portrait's picture out of the new_game route's shot of
Rolf's house (the window's interior, PORTRAIT_BOX: 80 x 80) into
assets/icon/rolf_portrait.png. The shot, not rolf_portrait.png's tiles:
those are in the order they load, and the portrait window's map puts them
in place. The second, from that PNG, writes assets/icon/ps2.ico, which
src/platform/ps2.rc links into the Windows game: 256 pixels the 80 x 80
picture tripled by whole pixels on a clear square, so it keeps the art's
own pixels; 16-128 that scaled down smoothly. Needs Pillow.
"""
import argparse
import sys
from pathlib import Path

from PIL import Image

PORTRAIT_BOX = (224, 40, 304, 120)      # new_game's 05_house.png, 320 x 224
PNG = Path("assets/icon/rolf_portrait.png")
ICO = Path("assets/icon/ps2.ico")
SMALL = (16, 24, 32, 48, 64, 128)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--from-shot", default=None, metavar="PNG",
                    help="cut the portrait from this shot first")
    args = ap.parse_args()

    if args.from_shot:
        shot = Image.open(args.from_shot).convert("RGB")
        if shot.size != (320, 224):
            print(f"make_icon: {args.from_shot} is {shot.size}, not 320 x 224",
                  file=sys.stderr)
            return 1
        PNG.parent.mkdir(parents=True, exist_ok=True)
        shot.crop(PORTRAIT_BOX).save(PNG)
        print(f"make_icon: {PNG}")

    art = Image.open(PNG).convert("RGBA")
    # The art's pixels tripled (240 x 240): 256 is that on a clear square;
    # every smaller size is it scaled down smoothly.
    big = art.resize((art.width * 3, art.height * 3), Image.NEAREST)
    images = [big.resize((n, n), Image.LANCZOS) for n in SMALL]
    square = Image.new("RGBA", (256, 256), (0, 0, 0, 0))
    square.paste(big, ((256 - big.width) // 2, (256 - big.height) // 2))
    images.append(square)
    images[-1].save(ICO, format="ICO", sizes=[im.size for im in images],
                    append_images=images[:-1])
    print(f"make_icon: {ICO} ({', '.join(str(im.width) for im in images)})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
