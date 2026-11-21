#!/usr/bin/env python3
"""(build 158) How faithful an HD picture is to its native one
(docs/hd_art.md).

  python3 tools/hd_check.py NATIVE.png HD.png [--preview OUT.png]

The HD picture must be exactly scale x the native one's size, the scale a
whole number (it is read from the sizes, and anything else is refused).
Each scale x scale block of it is averaged and held to the native pixel it
stands for: the colour difference (CIE76 Delta E, in Lab) over every pixel
the native picture draws, its mean, its largest and how many are past 10
(a difference plainly seen) -- shape and colour drift in one number. The
transparency must stay inside the native one enlarged: nothing drawn
where the native pixel is clear. Inside, the edge may be smooth -- an
outline that follows the sprite's shape and cuts the corners of its
stair-steps (hd_generate.py's default, "definitely don't want it to look
like giant pixels") -- or the blocks exactly (`--edge exact`); the
report says which, and how much of the native coverage is drawn.
"""
import argparse
import json
import sys

from PIL import Image

FAR = 10.0      # Delta E past which a block has drifted where anyone sees it


def _lin(c):
    c /= 255.0
    return c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4


def _f(t):
    return t ** (1.0 / 3.0) if t > 216.0 / 24389.0 else (24389.0 / 27.0 * t + 16.0) / 116.0


def lab(rgb):
    """sRGB (0-255) to CIE Lab, D65."""
    r, g, b = (_lin(float(v)) for v in rgb)
    x = (0.4124 * r + 0.3576 * g + 0.1805 * b) / 0.95047
    y = 0.2126 * r + 0.7152 * g + 0.0722 * b
    z = (0.0193 * r + 0.1192 * g + 0.9505 * b) / 1.08883
    fx, fy, fz = _f(x), _f(y), _f(z)
    return 116.0 * fy - 16.0, 500.0 * (fx - fy), 200.0 * (fy - fz)


def scale_of(native, hd):
    w, h = native.size
    if hd.width % w or hd.height % h or hd.width // w != hd.height // h:
        raise SystemExit(f"{hd.size} is not a whole-number scale of {native.size}")
    return hd.width // w


def measure(native, hd, k=None):
    native = native.convert("RGBA")
    hd = hd.convert("RGBA")
    if k is None:
        k = scale_of(native, hd)
    if hd.size != (native.width * k, native.height * k):
        raise SystemExit(f"{hd.size} is not {k}x {native.size}")
    want = list(native.getchannel("A").resize(hd.size, Image.NEAREST)
                .getdata())
    got = list(hd.getchannel("A").getdata())
    outside = sum(1 for w_, g in zip(want, got) if w_ == 0 and g > 0)
    covered = sum(want)
    coverage = round(sum(min(w_, g) for w_, g in zip(want, got)) / covered, 4) \
        if covered else 1.0
    avg = hd.convert("RGB").resize(native.size, Image.BOX)
    diffs = []
    worst = (0.0, None)
    for y in range(native.height):
        for x in range(native.width):
            if native.getpixel((x, y))[3] == 0:
                continue
            a = lab(native.getpixel((x, y))[:3])
            b = lab(avg.getpixel((x, y)))
            d = sum((p - q) ** 2 for p, q in zip(a, b)) ** 0.5
            diffs.append(d)
            if d > worst[0]:
                worst = (d, [x, y])
    n = len(diffs)
    return {
        "native_size": list(native.size),
        "hd_size": list(hd.size),
        "pixels_drawn": n,
        "alpha_inside": outside == 0,
        "alpha_exact": want == got,
        "alpha_outside_pixels": outside,
        "alpha_coverage": coverage,
        "delta_e_mean": round(sum(diffs) / n, 2) if n else 0.0,
        "delta_e_max": round(worst[0], 2),
        "delta_e_max_at": worst[1],
        "past_10": sum(1 for d in diffs if d > FAR),
        "past_10_share": round(sum(1 for d in diffs if d > FAR) / n, 4) if n else 0.0,
    }


def summary(r):
    return (f"Delta E mean {r['delta_e_mean']}, max {r['delta_e_max']} at "
            f"{r['delta_e_max_at']}, {r['past_10']} of {r['pixels_drawn']} "
            f"pixels past {FAR:g} ({100 * r['past_10_share']:.1f}%), "
            f"transparency {edge_text(r)} "
            f"({100 * r['alpha_coverage']:.1f}% of the sprite's pixels drawn)")


def edge_text(r):
    if r["alpha_exact"]:
        return "exact"
    return "inside, smooth edge" if r["alpha_inside"] else "OUTSIDE the sprite"


def preview(native, hd, k):
    """The native picture enlarged k times, the HD one, and the HD one
    averaged back and enlarged again, side by side on grey."""
    native = native.convert("RGBA")
    w, h = hd.size
    sheet = Image.new("RGBA", (3 * w + 20, h), (64, 64, 64, 255))
    big = native.resize((w, h), Image.NEAREST)
    back = hd.resize(native.size, Image.BOX).resize((w, h), Image.NEAREST)
    for i, im in enumerate((big, hd, back)):
        sheet.alpha_composite(im, (i * (w + 10), 0))
    return sheet


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("native")
    ap.add_argument("hd")
    ap.add_argument("--preview", default="")
    a = ap.parse_args()
    native, hd = Image.open(a.native), Image.open(a.hd)
    k = scale_of(native, hd)
    r = measure(native, hd, k)
    print(json.dumps(r, indent=2))
    print(f"{k}x: {summary(r)}")
    if a.preview:
        preview(native, hd, k).save(a.preview)
    return 0 if r["alpha_inside"] else 1


if __name__ == "__main__":
    sys.exit(main())
