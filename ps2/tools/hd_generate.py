#!/usr/bin/env python3
"""(build 158) An HD picture from a native one, at an exact whole-number
scale, through a local ComfyUI (docs/hd_art.md).

  python3 tools/hd_generate.py NATIVE.png OUT_DIR [--scale 9] [--seed N]
      [--prompt TAGS] [--lora NAME [--lora-strength S]] [--denoise D]
      [--control S] [--steps N] [--cfg C] [--work M] [--server URL]

NATIVE is a picture as the game draws it (assets/art/<kind>/<thing>/...,
RGBA, transparent where nothing is drawn). The result is exactly
scale x its width by scale x its height -- never another size:

  1. The native picture enlarged `scale x work` times is both the
     starting image and the ControlNet (tile) guide: the model redraws it,
     it does not invent a layout. `--guide lanczos` (the default) enlarges
     it smoothly, so no block edges are there to be kept; `nearest` makes
     every pixel a block (the look the owner does not want: "definitely
     don't want it to look like giant pixels"). Only the guide is
     enlarged so; the result is still exactly the scale.
  2. The canvas is padded up to multiples of 8 (SDXL's latent grid) with
     the edge pixels repeated, and cropped back after: nothing resized.
  3. `work` (a whole number, 1 by default; 2 or more for a small sprite,
     so the model works near its own size) is taken back out by averaging
     each work x work block: an exact whole-number reduction.
  4. The transparency stays inside the native picture's enlarged `scale`
     times: the HD picture never covers a pixel the native one leaves
     clear. By default (`--edge smooth`) its edge is smooth: the native
     coverage enlarged smoothly (Lanczos) and ramped to a sharp but
     anti-aliased outline, then held within the blocks -- the outline
     follows the sprite's shape and cuts the corners of its stair-steps,
     no giant pixels at the edge. `--edge exact` keeps the blocks.
  5. The check: the result averaged over each scale x scale block, against
     the native pixel it came from (hd_check.py's measure), into
     OUT_DIR/<stem>.check.json, with a side-by-side preview.

OUT_DIR gets <stem>.png (the HD picture), <stem>.check.json and
<stem>.preview.png; nothing is written into assets/ (an HD picture goes
into assets/art_hd/ by hand, once it is wanted).
"""
import argparse
import json
import random
import sys
import time
import urllib.parse
import urllib.request
import uuid
from pathlib import Path

from PIL import Image, ImageChops

sys.path.insert(0, str(Path(__file__).resolve().parent))
import hd_check                                              # noqa: E402

SERVER = "http://127.0.0.1:8188"
CHECKPOINT = "Illustrious-XL-v0.1.safetensors"
CONTROLNET = "xinsir-controlnet-tile-sdxl-1.0.safetensors"
POSITIVE = ("masterpiece, best quality, highres, official art, "
            "retro artstyle, 1980s (style), anime coloring, clean lineart")
NEGATIVE = ("pixel art, pixelated, mosaic, blocky, dithering, sprite, "
            "lowres, blurry, jpeg artifacts, "
            "worst quality, low quality, bad anatomy, text, watermark, "
            "signature, border, frame")


def pad_to(img, w, h):
    """img in the top-left of a w x h canvas, its last row and column
    repeated into the padding (no hard edge for the model to draw)."""
    out = Image.new(img.mode, (w, h))
    out.paste(img, (0, 0))
    iw, ih = img.size
    if w > iw:
        col = img.crop((iw - 1, 0, iw, ih)).resize((w - iw, ih), Image.NEAREST)
        out.paste(col, (iw, 0))
    if h > ih:
        row = out.crop((0, ih - 1, w, ih)).resize((w, h - ih), Image.NEAREST)
        out.paste(row, (0, ih))
    return out


def edge(alpha, k, how):
    """The HD transparency from the native one: the blocks exactly, or a
    smooth outline held inside them (the docstring's step 4)."""
    w, h = alpha.size
    blocks = alpha.resize((w * k, h * k), Image.NEAREST)
    if how == "exact":
        return blocks
    soft = alpha.resize((w * k, h * k), Image.LANCZOS)
    # Half coverage is the outline; a ramp about an eighth of a native
    # pixel either side of it anti-aliases it without blurring.
    half = max(1.0, k / 8.0) * 32.0
    soft = soft.point(lambda v: max(0, min(255, round(
        (v - 127.5) * 127.5 / half + 127.5))))
    return ImageChops.darker(blocks, soft)


def guide(img, f, how):
    """The native picture enlarged f times for the model to start from:
    `lanczos` smooth (dithering blurs into gradients), `nearest` blocks,
    `soft` blocks softened just past their edges -- a Gaussian a sixth of
    a native pixel wide -- so the shading's pattern stays and the block
    edges go."""
    from PIL import ImageFilter
    size = (img.width * f, img.height * f)
    if how == "lanczos":
        return img.resize(size, Image.LANCZOS)
    big = img.resize(size, Image.NEAREST)
    if how == "soft":
        big = big.filter(ImageFilter.GaussianBlur(radius=max(1.0, f / 6.0)))
    return big


def box_reduce(img, m):
    """Each m x m block averaged into one pixel: an exact 1/m."""
    if m == 1:
        return img
    w, h = img.size
    assert w % m == 0 and h % m == 0
    return img.resize((w // m, h // m), Image.BOX)


def request(server, path, data=None, headers=None):
    req = urllib.request.Request(server + path, data=data,
                                 headers=headers or {})
    with urllib.request.urlopen(req, timeout=60) as r:
        return r.read()


def upload(server, img, name):
    """POST /upload/image, multipart by hand (urllib has no helper)."""
    import io
    buf = io.BytesIO()
    img.save(buf, format="PNG")
    boundary = uuid.uuid4().hex
    body = (f"--{boundary}\r\nContent-Disposition: form-data; name=\"image\"; "
            f"filename=\"{name}\"\r\nContent-Type: image/png\r\n\r\n").encode() \
        + buf.getvalue() + (f"\r\n--{boundary}\r\nContent-Disposition: "
                            f"form-data; name=\"overwrite\"\r\n\r\ntrue\r\n"
                            f"--{boundary}--\r\n").encode()
    out = json.loads(request(server, "/upload/image", body, {
        "Content-Type": f"multipart/form-data; boundary={boundary}"}))
    return out["name"]


def workflow(a, image_name, prefix):
    """ComfyUI's API format: node id -> {class_type, inputs}."""
    model, clip = ["1", 0], ["1", 1]
    nodes = {
        "1": {"class_type": "CheckpointLoaderSimple",
              "inputs": {"ckpt_name": a.checkpoint}},
    }
    if a.lora:
        nodes["2"] = {"class_type": "LoraLoader",
                      "inputs": {"model": model, "clip": clip,
                                 "lora_name": a.lora,
                                 "strength_model": a.lora_strength,
                                 "strength_clip": a.lora_strength}}
        model, clip = ["2", 0], ["2", 1]
    positive = ", ".join(t for t in (a.trigger, a.prompt, a.style) if t)
    negative = ", ".join(t for t in (NEGATIVE, a.avoid) if t)
    nodes.update({
        "3": {"class_type": "CLIPTextEncode",
              "inputs": {"clip": clip, "text": positive}},
        "4": {"class_type": "CLIPTextEncode",
              "inputs": {"clip": clip, "text": negative}},
        "5": {"class_type": "LoadImage", "inputs": {"image": image_name}},
        "6": {"class_type": "VAEEncode",
              "inputs": {"pixels": ["5", 0], "vae": ["1", 2]}},
        "7": {"class_type": "ControlNetLoader",
              "inputs": {"control_net_name": a.controlnet}},
        "8": {"class_type": "ControlNetApplyAdvanced",
              "inputs": {"positive": ["3", 0], "negative": ["4", 0],
                         "control_net": ["7", 0], "image": ["5", 0],
                         "strength": a.control, "start_percent": 0.0,
                         "end_percent": a.control_end, "vae": ["1", 2]}},
        "9": {"class_type": "KSampler",
              "inputs": {"model": model, "seed": a.seed, "steps": a.steps,
                         "cfg": a.cfg, "sampler_name": "dpmpp_2m",
                         "scheduler": "karras", "positive": ["8", 0],
                         "negative": ["8", 1], "latent_image": ["6", 0],
                         "denoise": a.denoise}},
        "10": {"class_type": "VAEDecode",
               "inputs": {"samples": ["9", 0], "vae": ["1", 2]}},
        "11": {"class_type": "SaveImage",
               "inputs": {"images": ["10", 0], "filename_prefix": prefix}},
    })
    return nodes


def run(server, nodes):
    """Queue the workflow, wait for it, return the saved image."""
    import io
    client = uuid.uuid4().hex
    pid = json.loads(request(server, "/prompt", json.dumps(
        {"prompt": nodes, "client_id": client}).encode(),
        {"Content-Type": "application/json"}))["prompt_id"]
    while True:
        hist = json.loads(request(server, f"/history/{pid}"))
        if pid in hist:
            entry = hist[pid]
            status = entry.get("status", {})
            if status.get("status_str") == "error":
                raise SystemExit(f"ComfyUI: {json.dumps(status)[:2000]}")
            for out in entry.get("outputs", {}).values():
                for im in out.get("images", []):
                    q = urllib.parse.urlencode({
                        "filename": im["filename"],
                        "subfolder": im["subfolder"], "type": im["type"]})
                    return Image.open(io.BytesIO(
                        request(server, f"/view?{q}"))).convert("RGB")
        time.sleep(0.5)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("native")
    ap.add_argument("out_dir")
    ap.add_argument("--scale", type=int, default=9)
    ap.add_argument("--work", type=int, default=1)
    ap.add_argument("--seed", type=int, default=None)
    ap.add_argument("--prompt", default="")
    ap.add_argument("--trigger", default="")
    ap.add_argument("--lora", default="")
    ap.add_argument("--lora-strength", type=float, default=1.0)
    ap.add_argument("--denoise", type=float, default=0.7)
    ap.add_argument("--control", type=float, default=0.45)
    ap.add_argument("--control-end", type=float, default=0.75)
    ap.add_argument("--guide", choices=("lanczos", "soft", "nearest"),
                    default="lanczos")
    ap.add_argument("--style", default=POSITIVE,
                    help="the style's words (replace the default's)")
    ap.add_argument("--avoid", default="",
                    help="words added to the negative prompt")
    ap.add_argument("--edge", choices=("smooth", "exact"), default="smooth")
    ap.add_argument("--steps", type=int, default=28)
    ap.add_argument("--cfg", type=float, default=6.0)
    ap.add_argument("--checkpoint", default=CHECKPOINT)
    ap.add_argument("--controlnet", default=CONTROLNET)
    ap.add_argument("--server", default=SERVER)
    a = ap.parse_args()
    if a.scale < 1 or a.work < 1:
        ap.error("--scale and --work are whole numbers, 1 or more")
    if a.seed is None:
        a.seed = random.randrange(2 ** 32)

    native = Image.open(a.native).convert("RGBA")
    w, h = native.size
    k, m = a.scale, a.work
    big = guide(native.convert("RGB"), k * m, a.guide)
    pw = (big.width + 7) // 8 * 8
    ph = (big.height + 7) // 8 * 8
    canvas = pad_to(big, pw, ph)

    stem = Path(a.native).stem
    name = upload(a.server, canvas, f"hd_{stem}_{uuid.uuid4().hex[:8]}.png")
    made = run(a.server, workflow(a, name, f"hd/{stem}"))
    if made.size != (pw, ph):
        raise SystemExit(f"ComfyUI gave {made.size}, not {(pw, ph)}")

    hd = box_reduce(made.crop((0, 0, w * k * m, h * k * m)), m).convert("RGBA")
    hd.putalpha(edge(native.getchannel("A"), k, a.edge))
    assert hd.size == (w * k, h * k), hd.size

    out = Path(a.out_dir)
    out.mkdir(parents=True, exist_ok=True)
    hd.save(out / f"{stem}.png")
    report = hd_check.measure(native, hd, k)
    report.update({"native": str(a.native), "scale": k, "work": m,
                   "seed": a.seed, "prompt": a.prompt, "lora": a.lora,
                   "lora_strength": a.lora_strength, "denoise": a.denoise,
                   "style": a.style, "avoid": a.avoid,
                   "control": a.control, "control_end": a.control_end,
                   "guide": a.guide, "edge": a.edge, "steps": a.steps,
                   "cfg": a.cfg,
                   "checkpoint": a.checkpoint})
    (out / f"{stem}.check.json").write_text(json.dumps(report, indent=2)
                                            + "\n")
    hd_check.preview(native, hd, k).save(out / f"{stem}.preview.png")
    print(f"{out / (stem + '.png')}: {hd.width}x{hd.height} ({k}x of "
          f"{w}x{h}), seed {a.seed}; {hd_check.summary(report)}")


if __name__ == "__main__":
    main()
