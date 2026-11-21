"""Reads tools/art_dump's .adm captures and writes them as images.

An .adm is one animation: the battle palette (64 entries, already through
the port's DAC ladder) and a run of records, each a frame and how many
ticks it stays up. Every frame of one animation is placed on one canvas,
the union of all their bounding boxes, so the frames line up when stepped
through. Index 0 is empty (transparent)."""

import struct

from PIL import Image

TICK_HZ = 59.92


def read(path):
    with open(path, 'rb') as fh:
        data = fh.read()
    if data[:4] != b'ADM1':
        raise ValueError(f'{path}: not an ADM1 file')
    w, h = struct.unpack_from('<HH', data, 4)
    pal = list(data[8:8 + 64 * 3])
    pos = 8 + 64 * 3
    frames = []
    while pos < len(data):
        ticks, x, y, fw, fh_ = struct.unpack_from('<HHHHH', data, pos)
        pos += 10
        px = data[pos:pos + fw * fh_]
        pos += fw * fh_
        frames.append({'ticks': ticks, 'x': x, 'y': y, 'w': fw, 'h': fh_,
                       'px': px})
    return {'w': w, 'h': h, 'palette': pal, 'frames': frames}


def union_box(frames):
    boxes = [(f['x'], f['y'], f['x'] + f['w'], f['y'] + f['h'])
             for f in frames if f['w'] > 0]
    if not boxes:
        return None
    return (min(b[0] for b in boxes), min(b[1] for b in boxes),
            max(b[2] for b in boxes), max(b[3] for b in boxes))


def trim_blank_ends(frames):
    """Drops empty frames at the start and the end (the ticks before an
    object shows and after it is gone); blank frames in the middle stay,
    since a script can hide its object on purpose."""
    lo, hi = 0, len(frames)
    while lo < hi and frames[lo]['w'] == 0:
        lo += 1
    while hi > lo and frames[hi - 1]['w'] == 0:
        hi -= 1
    return frames[lo:hi]


def to_images(anim, box=None):
    frames = anim['frames']
    if box is None:
        box = union_box(frames)
    if box is None:
        return [], []
    x0, y0, x1, y1 = box
    cw, ch = x1 - x0, y1 - y0
    pal = anim['palette'] + [0] * (768 - len(anim['palette']))
    images, ticks = [], []
    for f in frames:
        im = Image.new('P', (cw, ch), 0)
        im.putpalette(pal)
        if f['w'] > 0:
            src = Image.frombytes('P', (f['w'], f['h']), bytes(f['px']))
            src.putpalette(pal)
            # index 0 is empty: paste through a mask of the drawn pixels
            mask = src.point(lambda v: 255 if v else 0, mode='L')
            im.paste(src, (f['x'] - x0, f['y'] - y0), mask)
        im.info['transparency'] = 0
        images.append(im)
        ticks.append(f['ticks'])
    return images, ticks


def ms(ticks):
    return max(1, round(ticks * 1000.0 / TICK_HZ))


def save_apng(path, images, ticks, loop=True):
    if not images:
        return False
    first = images[0]
    if len(images) == 1:
        first.save(path, transparency=0, optimize=False)
        return True
    first.save(path, save_all=True, append_images=images[1:],
               duration=[ms(t) for t in ticks], loop=0 if loop else 1,
               transparency=0, disposal=0, blend=0, default_image=False)
    return True


def save_tiff_stack(path, images):
    if not images:
        return False
    rgba = [im.convert('RGBA') for im in images]
    rgba[0].save(path, save_all=True, append_images=rgba[1:],
                 compression='tiff_adobe_deflate')
    return True


def preview_strip(images, scale=2, bg=(40, 40, 60)):
    """A checking aid only: the frames side by side on a flat colour."""
    if not images:
        return None
    w, h = images[0].size
    out = Image.new('RGB', (w * len(images) + len(images) - 1, h), bg)
    for i, im in enumerate(images):
        rgba = im.convert('RGBA')
        out.paste(rgba, (i * (w + 1), 0), rgba)
    return out.resize((out.width * scale, out.height * scale), Image.NEAREST)
