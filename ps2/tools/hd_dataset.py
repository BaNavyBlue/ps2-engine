#!/usr/bin/env python3
"""(build 158) The training set for the classic Phantasy Star style (a
LoRA on an SDXL model, trained with OneTrainer; docs/hd_art.md).

  python3 tools/hd_dataset.py OUT [--dump GAME:DIR ...]
      [--images GAME:KIND:DIR ...] [--max-side 1024] [--level-pieces 6]
      [--base CHECKPOINT] [--name psclassic_v1] [--epochs 4]

GAME is ps1, ps2, ps3, ps4, gaiden or adventure (the caption's game tag);
any other word is taken as the tag itself.

--dump GAME:DIR   an art dump in this tree's layout (`make art
                  ART_OUT=DIR`): portraits, cutscenes, monsters,
                  party_battle, field_sprites, effects, windows, levels.
--images GAME:KIND:DIR   a folder of images, its sub-folders' names and
                  file names the caption's words (a .txt beside an image
                  is added to it). KIND:
                    ingame    the game's own pictures and sprites (rips,
                              exports): cropped to what they draw,
                              enlarged by a whole number, masked
                    map       big maps: cut into screen-sized pieces
                              (--level-pieces of them), then as ingame
                    official  illustrations, concept art, box and manual
                              art, art books: as they are, fitted

OUT gets OUT/<game>/<ingame|official>/... -- each image once (an
animation's frames apart, a frame seen before left out), the in-game ones
cropped to what they draw, enlarged by the largest whole number that keeps
the long side within --max-side (nearest neighbour), on grey, with
<name>-masklabel.png (what the image draws, white: OneTrainer's masked
training does not learn the grey) and <name>.txt -- and OneTrainer's
OUT/concepts.json and OUT/train.json:

    venv\\Scripts\\python.exe scripts\\train.py --config-path OUT\\train.json
    (from OneTrainer's folder)

(158d) Only clean pictures: an in-game image already enlarged is first
brought back to its own pixels, and left out if it has more than
--ingame-colours colours (a video's frame, a resized screenshot: blur
the model would learn as blocks) or is still bigger than --max-side; an
"illustration" of 64 colours or fewer is taken as in-game (a wiki's
screenshot filed as art); an illustration whose short side is under
--official-min is left out (stretched to train, it teaches blur). What
was left out, and why, is printed at the end.

Captions are tags, the base model's own way: `psclassic` (the trigger)
and the game (`phantasy star ii` ...) on everything, `pixel art, sprite`
on the games' own images and `official art, illustration` on the rest --
so asking for the second and against the first draws in the illustrators'
hand -- then what the image is and who (rolf, 1boy, blue hair ...).
"""
import argparse
import hashlib
import json
import re
import shutil
import sys
from pathlib import Path

from PIL import Image, ImageChops

TRIGGER = "psclassic"
GAMES = {"ps1": "phantasy star i", "ps2": "phantasy star ii",
         "ps3": "phantasy star iii", "ps4": "phantasy star iv",
         "gaiden": "phantasy star gaiden",
         "adventure": "phantasy star adventure"}
INGAME = "pixel art, sprite"
OFFICIAL = "official art, illustration"
GREY = (128, 128, 128)
SCREEN = {"ps1": (256, 192), "gaiden": (160, 144), "adventure": (160, 144)}
DEFAULT_SCREEN = (320, 224)
PICTURES = (".png", ".jpg", ".jpeg", ".webp", ".bmp", ".gif")
SAMPLES = (("phantasy star ii", "rolf, 1boy, blue hair, upper body"),
           ("phantasy star ii", "monster, no humans, full body"),
           ("phantasy star iv", "chaz, 1boy, blonde hair, upper body"))
SAMPLE_AVOID = ("pixel art, sprite, lowres, text, logo, watermark, "
                "signature, copyright name")
# (158d) An "illustration" with this many colours or fewer is the game's
# own picture (a wiki's enlarged screenshot filed as art): taken as one.
OFFICIAL_AS_SPRITE = 64

# The party of each game, by the names folders give them: name, who, and
# what their art plainly shows.
PARTY = {
    "rolf": "rolf, 1boy, blue hair",
    "nei": "nei, 1girl, purple hair, pointy ears",
    "rudo": "rudo, 1boy",
    "amy": "amy, 1girl",
    "hugh": "hugh, 1boy",
    "anna": "anna, 1girl",
    "kain": "kain, 1boy",
    "shir": "shir, 1girl",
    "alis": "alis, 1girl",
    "myau": "myau, cat",
    "odin": "odin, 1boy",
    "noah": "noah, 1boy",
    "lutz": "lutz, 1boy",
}
# (158c) The later games' casts, by name alone: the tagger's tags (or a
# dump's own captions) say what each picture shows.
CAST = ("rhys", "lyle", "ryan", "mieu", "wren", "ayn", "sari", "thea",
        "kara", "nial", "laya", "alair", "adan", "aron", "gwyn", "sean",
        "crys", "maia", "lena", "lune", "chaz", "alys", "hahn", "rune",
        "gryz", "rika", "demi", "raja", "kyra", "seth")
FACING = {"up": "from behind", "down": "from front", "left": "from side",
          "right": "from side"}


def words(name):
    """'001_fire_ant' -> 'fire ant'; '0_rolf' -> 'rolf'."""
    return re.sub(r"^\d+_", "", name).replace("_", " ").replace("-", " ") \
        .strip().lower()


def who(parts):
    """Everyone a path names, by whole words (158c: 'Alys and hahn 100m'
    -> 'alys, hahn'; until 158c only a part that was a name and nothing
    else): one of PARTY's with what their art shows, more by name alone."""
    names = []
    for p in parts:
        for w in re.findall(r"[a-z]+", words(p)):
            if (w in PARTY or w in CAST) and w not in names:
                names.append(w)
    if len(names) == 1 and names[0] in PARTY:
        return PARTY[names[0]]
    return ", ".join(names)


def dump_caption(kind, rel):
    """What an image of an art dump is, from its folder and file names."""
    parts = list(rel.parts[:-1])
    stem = rel.stem
    person = who(parts + [stem])
    if kind == "portraits":
        return ["portrait", person or words(stem)]
    if kind == "cutscenes":
        return ["cutscene", words(parts[-1] if parts else stem)]
    if kind == "monsters":
        return ["monster", "enemy", words(parts[-1] if parts else stem),
                words(stem)]
    if kind == "party_battle":
        return ["battle", person] + [words(p) for p in parts[1:]] \
            + [words(stem)]
    if kind == "field_sprites":
        return ["chibi", "full body", person] \
            + [words(p) for p in parts if not who([p])] \
            + [FACING.get(stem.split("_")[0], "")]
    if kind == "effects":
        return ["no humans", "effect"] + [words(p) for p in parts] \
            + [words(stem)]
    if kind == "windows":
        return ["user interface", words(stem)]
    if kind == "levels":
        return ["scenery", "no humans", "top-down", "tilemap", words(stem)]
    return [words(p) for p in parts] + [words(stem)]


def folder_caption(rel):
    parts = list(rel.parts[:-1]) + [rel.stem]
    person = who(parts)
    return ([person] if person else []) + [words(p) for p in parts]


def frames(path, most=0):
    """The frames of an image (an APNG's or a GIF's one by one), RGBA:
    every one, or `most` of them spread over the animation (the first,
    the last and between: a monster's eight near-alike frames would
    otherwise outweigh everything else the set holds)."""
    im = Image.open(path)
    n = getattr(im, "n_frames", 1)
    pick = range(n)
    if most and n > most:
        pick = sorted({round(i * (n - 1) / (most - 1)) for i in range(most)})             if most > 1 else [0]
    out = []
    for i in pick:
        im.seek(i)
        out.append(im.convert("RGBA"))
    return out


def pieces(im, screen, n):
    """Up to n screen-sized pieces spread over a big picture."""
    w, h = im.size
    sw, sh = screen
    if w <= sw and h <= sh:
        return [im]
    xs = list(range(0, max(w - sw, 0) + 1, sw)) or [0]
    ys = list(range(0, max(h - sh, 0) + 1, sh)) or [0]
    cells = [(x, y) for y in ys for x in xs]
    step = max(1, len(cells) // n)
    return [im.crop((x, y, min(x + sw, w), min(y + sh, h)))
            for x, y in cells[::step][:n]]


def native(im):
    """(158d) A picture already enlarged -- each pixel a k x k block, a
    wiki's 3x screenshot -- brought back to its own pixels (the largest k
    up to 8 that redraws it exactly); else as it is."""
    w, h = im.size
    for k in range(8, 1, -1):
        if w % k or h % k:
            continue
        small = im.resize((w // k, h // k), Image.NEAREST)
        back = small.resize((w, h), Image.NEAREST)
        # Every band compared: an RGBA image's getbbox() reads only its
        # alpha, which would pass any opaque picture as enlarged.
        diff = ImageChops.difference(im, back).getextrema()
        if all(top == 0 for _, top in diff):
            return small
    return im


def colours(im, most):
    """The picture's colours, or most + 1 if there are more."""
    found = im.getcolors(most)
    return len(found) if found is not None else most + 1


def write_ingame(im, dest, text, max_side):
    bbox = im.getchannel("A").getbbox()
    if bbox is None:
        return False
    im = im.crop(bbox)
    if im.width < 8 or im.height < 8:
        return False
    k = max(1, max_side // max(im.size))
    big = im.resize((im.width * k, im.height * k), Image.NEAREST)
    flat = Image.new("RGB", big.size, GREY)
    flat.paste(big, (0, 0), big)
    mask = big.getchannel("A").point(lambda a: 255 if a else 0)
    dest.parent.mkdir(parents=True, exist_ok=True)
    flat.save(dest.with_suffix(".png"))
    mask.save(dest.with_name(dest.stem + "-masklabel.png"))
    dest.with_suffix(".txt").write_text(text + "\n")
    return True


def write_official(im, dest, text, max_side):
    im = im.convert("RGB")
    cap = max_side * 3 // 2
    if max(im.size) > cap:
        im.thumbnail((cap, cap), Image.LANCZOS)
    dest.parent.mkdir(parents=True, exist_ok=True)
    im.save(dest.with_suffix(".png"))
    dest.with_suffix(".txt").write_text(text + "\n")
    return True


class Builder:
    def __init__(self, out, a):
        self.out, self.a = out, a
        self.seen = set()
        self.counts = {}
        self.left_out = {}

    def leave_out(self, why):
        self.left_out[why] = self.left_out.get(why, 0) + 1

    def new(self, im):
        digest = hashlib.sha1(im.tobytes() + repr(im.size).encode()).digest()
        if digest in self.seen:
            return False
        self.seen.add(digest)
        return True

    def tags(self, game, official, more):
        head = [TRIGGER, GAMES.get(game, game),
                OFFICIAL if official else INGAME]
        return ", ".join(t for t in head + more if t)

    def add(self, game, official, rel, im, more, frame=None):
        # (158d) Only clean pictures: v1's first epoch learnt the blur of
        # video frames and resized screenshots drawn in blocks, and
        # stretched small illustrations (docs/hd_art.md, "v1 stopped").
        if official and colours(im, OFFICIAL_AS_SPRITE) <= OFFICIAL_AS_SPRITE:
            official = False          # a screenshot filed as art
        if not official:
            im = native(im)
        if not self.new(im):
            return
        if official and min(im.size) < self.a.official_min:
            self.leave_out(f"illustration under {self.a.official_min} px")
            return
        if not official:
            if colours(im, self.a.ingame_colours) > self.a.ingame_colours:
                self.leave_out(f"in-game, over {self.a.ingame_colours} "
                               "colours (blurred or compressed)")
                return
            if max(im.size) > self.a.max_side:
                self.leave_out("in-game, too big at its own pixels")
                return
        name = rel.with_suffix("")
        if frame is not None:
            name = name.with_name(f"{name.name}_{frame:02d}")
        dest = self.out / game / ("official" if official else "ingame") / name
        write = write_official if official else write_ingame
        if write(im, dest, self.tags(game, official, more), self.a.max_side):
            key = (game, "official" if official else "ingame")
            self.counts[key] = self.counts.get(key, 0) + 1

    def dump(self, game, root):
        kinds = ("portraits", "cutscenes", "monsters", "party_battle",
                 "field_sprites", "effects", "windows", "levels")
        for kind in kinds:
            base = root / kind
            if not base.is_dir():
                continue
            for p in sorted(base.rglob("*.png")):
                if p.name.endswith("_slots.png"):
                    continue                 # a picture's slot map
                rel = p.relative_to(base)
                images = frames(p, self.a.max_frames)
                if kind == "levels":
                    images = [q.convert("RGBA") for im in images
                              for q in pieces(im, SCREEN.get(game, DEFAULT_SCREEN),
                                              self.a.level_pieces)]
                more = dump_caption(kind, rel)
                for i, im in enumerate(images):
                    self.add(game, False, Path(kind) / rel, im, more,
                             i if len(images) > 1 else None)

    def images(self, game, kind, root):
        for p in sorted(root.rglob("*")):
            if p.suffix.lower() not in PICTURES or "-masklabel" in p.stem:
                continue
            rel = p.relative_to(root)
            own = p.with_suffix(".txt")
            if own.exists():
                # (158b) The tagger's tags say what it is; of the path,
                # only a character's name (a site's folder and file names
                # -- "players guide 202603 ... 0000" -- would be noise).
                person = who(list(rel.parts[:-1]) + [rel.stem])
                more = ([person] if person else [])                     + [own.read_text(encoding="utf-8").strip()]
            else:
                more = folder_caption(rel)
            try:
                images = frames(p, self.a.max_frames)
            except OSError as e:
                print(f"skipped {p}: {e}", file=sys.stderr)
                continue
            if kind == "map":
                images = [q for im in images
                          for q in pieces(im, SCREEN.get(game, DEFAULT_SCREEN),
                                          self.a.level_pieces)]
                more = ["scenery", "no humans", "top-down", "map"] + more
            for i, im in enumerate(images):
                self.add(game, kind == "official", Path(kind) / rel, im, more,
                         i if len(images) > 1 else None)


def trainer_files(out, a, counts):
    concepts = []
    ingame = sum(n for (g, k), n in counts.items() if k == "ingame")
    official = sum(n for (g, k), n in counts.items() if k == "official")
    for (game, kind), n in sorted(counts.items()):
        c = {"name": f"{game} {kind}",
             "path": str((out / game / kind).resolve()),
             "include_subdirectories": True, "enabled": True,
             "text": {"prompt_source": "sample"},
             "image": {"enable_crop_jitter": kind == "official"}}
        if kind == "official" and official:
            # The illustrations are fewer than the sprites, and they are
            # the style: each shown --official-repeats times an epoch.
            c["balancing"] = a.official_repeats
        elif kind == "ingame" and n > a.ingame_cap:
            # (158b) A game's own pictures, at most --ingame-cap of them
            # an epoch (drawn afresh each epoch): the long-plays' map
            # screens would otherwise be most of the training.
            c["balancing"] = a.ingame_cap
            c["balancing_strategy"] = "SAMPLES"
        concepts.append(c)
    (out / "concepts.json").write_text(json.dumps(concepts, indent=2) + "\n")
    work = Path(a.work).resolve()
    train = {
        "__version": 10,
        "model_type": "STABLE_DIFFUSION_XL_10_BASE",
        "training_method": "LORA",
        "base_model_name": str(Path(a.base).resolve()),
        "output_model_destination": str(
            (Path(a.loras) / f"{a.name}.safetensors").resolve()),
        "output_model_format": "SAFETENSORS",
        "output_dtype": "BFLOAT_16",
        "workspace_dir": str(work / "workspace"),
        "cache_dir": str(work / "cache"),
        "concept_file_name": str((out / "concepts.json").resolve()),
        "resolution": str(a.max_side),
        "aspect_ratio_bucketing": True,
        "latent_caching": True,
        "epochs": a.epochs,
        "batch_size": 2,
        "learning_rate": 0.0001,
        "learning_rate_scheduler": "COSINE",
        "learning_rate_warmup_steps": 100,
        "optimizer": {"optimizer": "ADAMW_8BIT", "weight_decay": 0.01},
        "lora_rank": 32,
        "lora_alpha": 16.0,
        "train_dtype": "BFLOAT_16",
        "gradient_checkpointing": "ON",
        "masked_training": True,
        "unmasked_probability": 0.1,
        "unmasked_weight": 0.1,
        "unet": {"train": True, "weight_dtype": "BFLOAT_16"},
        "text_encoder": {"train": False, "weight_dtype": "BFLOAT_16"},
        "text_encoder_2": {"train": False, "weight_dtype": "BFLOAT_16"},
        "vae": {"weight_dtype": "FLOAT_32"},
        "save_every": 1,
        "save_every_unit": "EPOCH",
        "sample_after": 1,
        "sample_after_unit": "EPOCH",
        # (158c) Three pictures after each epoch, to watch who and what it
        # learns: a party member, a monster, a later game's lead. Not
        # training data: the model drawing them (C:\AI\train\...\samples).
        "samples": [{"prompt": f"{TRIGGER}, {game}, {OFFICIAL}, {what}",
                     "negative_prompt": SAMPLE_AVOID,
                     "width": 1024, "height": 1024, "seed": 1,
                     "diffusion_steps": 28, "cfg_scale": 6.0,
                     "enabled": True}
                    for game, what in SAMPLES],
        "tensorboard": False,
    }
    (out / "train.json").write_text(json.dumps(train, indent=2) + "\n")


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("out")
    ap.add_argument("--dump", action="append", default=[],
                    help="GAME:DIR, an art dump")
    ap.add_argument("--images", action="append", default=[],
                    help="GAME:KIND:DIR, KIND ingame, map or official")
    ap.add_argument("--max-side", type=int, default=1024)
    ap.add_argument("--level-pieces", type=int, default=6)
    ap.add_argument("--ingame-cap", type=int, default=600,
                    help="a game's in-game images used an epoch, at most "
                         "(900 until 158d)")
    ap.add_argument("--ingame-colours", type=int, default=256,
                    help="an in-game image with more colours is left out "
                         "(blurred, resized or compressed)")
    ap.add_argument("--official-min", type=int, default=512,
                    help="an illustration whose short side is less is "
                         "left out (it would be stretched to train)")
    ap.add_argument("--official-repeats", type=float, default=3.0,
                    help="times each illustration is shown an epoch "
                         "(2 until 158d)")
    ap.add_argument("--max-frames", type=int, default=3,
                    help="frames kept of an animation, 0 for all")
    ap.add_argument("--base",
                    default="C:/AI/models/checkpoints/Illustrious-XL-v0.1.safetensors")
    ap.add_argument("--loras", default="C:/AI/models/loras")
    ap.add_argument("--work", default="C:/AI/train/psclassic")
    ap.add_argument("--name", default="psclassic_v1")
    ap.add_argument("--epochs", type=int, default=4)
    a = ap.parse_args()

    out = Path(a.out)
    for old in [p for p in out.iterdir() if p.is_dir()] if out.exists() else []:
        shutil.rmtree(old)
    b = Builder(out, a)
    for spec in a.dump:
        game, _, d = spec.partition(":")
        b.dump(game, Path(d))
    for spec in a.images:
        game, kind, d = spec.split(":", 2)
        if kind not in ("ingame", "map", "official"):
            ap.error(f"{spec}: KIND is ingame, map or official")
        b.images(game, kind, Path(d))
    trainer_files(out, a, b.counts)
    for (game, kind), n in sorted(b.counts.items()):
        print(f"{game:10} {kind:9} {n}")
    print(f"{'all':20} {sum(b.counts.values())}")
    for why, n in sorted(b.left_out.items()):
        print(f"left out: {n} {why}")
    print(f"OneTrainer: {out / 'train.json'}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
