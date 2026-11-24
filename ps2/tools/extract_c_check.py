#!/usr/bin/env python3
"""(build 187) The C extractors (src/extract/, ./ps2_extract) against the
Python ones they port (docs/first_start.md, "In C").

    make ps2_extract
    python3 tools/extract_c_check.py ROM [--scratch DIR] [--keep]

Two whole extractions from the image, each in a scratch folder of its own,
every step the Makefile names (EXTRACTORS) in its order, as `make extract`
runs them: one with every step the Python's; one with each step that has a
C twin (`ps2_extract --list`) run in C, the rest in Python. Around each
step the folder is looked at, so what a step wrote (or took away) is
known. Then:

  * each C step wrote and took away the same files as its Python twin;
  * the two folders end the same: the same files, each the same bytes --
    a PNG the same picture (tools/make_assets.py's file_digest: the
    pixels, the palette, the transparency; the C writes its own zlib
    stream);
  * tools/gen_asset_names.py --check: the C's asset names are
    tools/asset_names.py's.

Exit 1 on any difference. The scratch folders go unless --keep.
"""
import argparse
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
GAME = HERE.parent
sys.path.insert(0, str(HERE))
import make_assets  # noqa: E402


def snapshot(root):
    """{path under root: (size, mtime_ns)} of every file."""
    out = {}
    for dirpath, _, files in os.walk(root):
        for f in files:
            p = Path(dirpath) / f
            st = p.stat()
            out[p.relative_to(root).as_posix()] = (st.st_size, st.st_mtime_ns)
    return out


def changes(before, after):
    written = {p for p, v in after.items() if before.get(p) != v}
    gone = set(before) - set(after)
    return written, gone


def run(argv, cwd, log):
    proc = subprocess.run(argv, cwd=cwd, stdout=subprocess.PIPE,
                          stderr=subprocess.STDOUT, text=True,
                          encoding="utf-8", errors="replace")
    log.write("$ %s\n%s\n" % (" ".join(str(a) for a in argv), proc.stdout))
    return proc.returncode


def python_argv(script, argv):
    return [sys.executable, "-X", "utf8", "-c", make_assets.BOOT,
            str(GAME / script), *argv]


def extraction(root, rom, c_steps, c_tool, log):
    """Every step into root/assets; {step: (written, gone)}."""
    root.mkdir(parents=True)
    runs, _ = make_assets.makefile_runs(GAME / "Makefile", {"ROM": str(rom)})
    # The steps import their neighbours from GAME/tools, found by their
    # own path; only what they write is under root.
    seen = {}
    for name, script, argv in runs:
        before = snapshot(root)
        if name in c_steps:
            cmd = [str(c_tool), name, *argv,
                   "--map", str(GAME / "tools" / "rom_map.json")]
        else:
            cmd = python_argv(script, argv)
        status = run(cmd, root, log)
        if status != 0:
            raise SystemExit("extract_c: %s (%s) failed in %s: see %s"
                             % (name, "C" if name in c_steps else "Python",
                                root.name, log.name))
        seen[name] = changes(before, snapshot(root))
    return seen


def same_file(a, b):
    if a.read_bytes() == b.read_bytes():
        return "bytes"
    if a.suffix.lower() == ".png" \
            and make_assets.file_digest(a) == make_assets.file_digest(b):
        return "picture"
    return None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("rom", type=Path, help="the cartridge image (raw)")
    ap.add_argument("--scratch", type=Path, default=None)
    ap.add_argument("--keep", action="store_true")
    ap.add_argument("--c", dest="c_tool", type=Path, default=None,
                    help="the C tool (./ps2_extract)")
    a = ap.parse_args()

    failures = []
    if subprocess.run([sys.executable, str(HERE / "gen_asset_names.py"),
                       "--check"]).returncode != 0:
        failures.append("asset names")

    c_tool = a.c_tool
    if c_tool is None:
        exe = GAME / "ps2_extract.exe"
        c_tool = exe if exe.is_file() else GAME / "ps2_extract"
    c_tool = c_tool.resolve()
    listed = subprocess.run([str(c_tool), "--list"], stdout=subprocess.PIPE,
                            text=True, check=True).stdout.split()
    rom = a.rom.resolve()

    scratch = Path(tempfile.mkdtemp(prefix="extract_c_", dir=a.scratch))
    try:
        with open(scratch / "log.txt", "w", encoding="utf-8") as log:
            py = extraction(scratch / "py", rom, set(), c_tool, log)
            mix = extraction(scratch / "c", rom, set(listed), c_tool, log)

        files = 0
        pictures = 0
        for name in listed:
            if name not in py:
                failures.append("%s: not a Makefile step" % name)
                continue
            (pw, pg), (cw, cg) = py[name], mix[name]
            if pw != cw or pg != cg:
                only_py = sorted((pw - cw) | (pg - cg))
                only_c = sorted((cw - pw) | (cg - pg))
                failures.append("%s: other files (Python's alone: %s; C's "
                                "alone: %s)" % (name, only_py[:5], only_c[:5]))
            for rel in sorted(cw):
                files += 1
                how = same_file(scratch / "py" / rel, scratch / "c" / rel)
                if how is None:
                    failures.append("%s: %s differs" % (name, rel))
                elif how == "picture":
                    pictures += 1

        all_py = snapshot(scratch / "py")
        all_c = snapshot(scratch / "c")
        if set(all_py) != set(all_c):
            failures.append("the trees hold other files: %s"
                            % sorted(set(all_py) ^ set(all_c))[:5])
        differ = [rel for rel in sorted(set(all_py) & set(all_c))
                  if same_file(scratch / "py" / rel, scratch / "c" / rel)
                  is None]
        if differ:
            failures.append("%d files differ in the end: %s"
                            % (len(differ), differ[:5]))

        print("extract_c: %d steps in C (%s): their %d files the Python's "
              "(%d PNGs the same picture, %d the same bytes); the whole "
              "extraction %d files, %d differ"
              % (len(listed), ", ".join(listed), files, pictures,
                 files - pictures, len(all_py), len(differ)))
    finally:
        if a.keep:
            print("kept: %s" % scratch)
        else:
            shutil.rmtree(scratch, ignore_errors=True)

    if failures:
        print("extract_c: FAIL (%d)" % len(failures))
        for f in failures:
            print("  " + f)
        return 1
    print("extract_c: PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
