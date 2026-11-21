#!/usr/bin/env python3
"""(build 125) The game's pack: every file it reads, and what COMPUTER PLAYS
runs, in one file for `make dist` to link into the program
(src/platform/pack.S; src/core/assetfs.c reads it).

    python3 tools/make_pack.py [--out build/pack.bin] [--scrub]

In it, from the tree's top:

  * assets/, whole (bar Tiled's .tsx and .bak files, dot files and (build
    130) assets/icon/, which only the build reads): what the game reads,
    and the maps, data and script text the driver reads;
  * shaders/: the CRT chain's compiled shaders;
  * what COMPUTER PLAYS reads besides assets/: (build 179) the C player
    is in the game (src/player/), and reads routes/saves/ (the story's
    checkpoints, to tell a player's save's stage) and
    src/game/script_ids.h (its story log's script names) from the pack.
    Builds 125-178 packed the Python driver, tools/ HARNESS_TOOLS and
    routes/new_game.route, which the game wrote out the first time it
    played.

The format: "PS2P", the file count, then each file -- its path's length
with a NUL (two bytes), the path and its NUL, its size (four), its bytes and
a NUL -- little-endian, the files in path order. --scrub puts the
maintainer's name as make_dist.py does in every text file (the package's
own rule). The file is written only when its bytes change, so the program
is relinked only then.
"""
import argparse
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from make_manifest import read_by_game  # noqa: E402

SKIP_SUFFIX = {".tsx", ".bak"}


def files():
    out = []
    for p in sorted(Path("assets").rglob("*")):
        if p.is_file() and p.suffix not in SKIP_SUFFIX \
                and read_by_game(p, Path("assets")) \
                and not p.name.startswith("."):
            out.append(p)
    # (build 138c) shaders/ is made on the maintainer's machines (`make
    # shaders`) and travels in no tarball: a tree without it packs a game
    # that draws without the CRT chain (crt_gpu.c finds no shader and the
    # placeholder draws), said here rather than stopping.
    shaders = Path("shaders")
    found = sorted(p for p in shaders.iterdir() if p.is_file()) \
        if shaders.is_dir() else []
    if not found:
        print("make_pack: no shaders/ here -- the packed game will draw "
              "without the CRT chain; run `make shaders` first for it",
              file=sys.stderr)
    out += found
    out += sorted(p for p in Path("routes/saves").rglob("*") if p.is_file())
    out.append(Path("src/game/script_ids.h"))
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default="build/pack.bin")
    ap.add_argument("--scrub", action="store_true",
                    help="the maintainer's name put as make_dist.py does")
    args = ap.parse_args()

    scrub = None
    if args.scrub:
        from make_dist import scrub
    blob = bytearray(b"PS2P")
    paths = files()
    blob += struct.pack("<I", len(paths))
    put = 0
    for p in paths:
        data = p.read_bytes()
        if scrub is not None:
            try:
                text, n = scrub(data.decode("utf-8"))
                if n:
                    data = text.encode("utf-8")
                    put += n
            except UnicodeDecodeError:
                pass
        # Forward slashes whatever the Python: MSYS2's gave shaders/'s
        # entries as_posix() left with backslashes, which the game's
        # lookups (shaders/<stem>.dxil) never matched.
        name = p.as_posix().replace("\\", "/").encode("utf-8") + b"\0"
        blob += struct.pack("<H", len(name)) + name
        blob += struct.pack("<I", len(data)) + data + b"\0"
    out = Path(args.out)
    out.parent.mkdir(parents=True, exist_ok=True)
    if not out.exists() or out.read_bytes() != bytes(blob):
        out.write_bytes(bytes(blob))
        print(f"make_pack: {out}, {len(paths)} files, "
              f"{len(blob) / 1e6:.1f} MB" + (f", the name put {put} times"
                                              if scrub else ""))
    return 0


if __name__ == "__main__":
    sys.exit(main())
