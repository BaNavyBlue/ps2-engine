#!/usr/bin/env python3
"""Bring the port into this engine from the private tree it is made in.

  python3 tools/sync_private.py PRIVATE_REPO

PRIVATE_REPO is a clone of the private repository (the one with ps2/ and
PSII_Disasm/ side by side; only ps2/ comes here, since the extractors read
the cartridge image through ps2/tools/rom_map.json). Its ps2/ must have
nothing uncommitted: the engine takes a commit, not a working tree.

What it does, into this repository's ps2/ (emptied first):

  * copies every file the private repository tracks under ps2/, except
    those EXCLUDE names: the game's data (assets/ but for KEEP_ASSETS, the
    route and proof outputs, which are pictures and recordings of the game)
    and the private working notes;
  * puts the private tree's author name, as its git config gives it, as
    "the owner" ("The owner" where a sentence starts) in every text file,
    and refuses the author's address outright -- the name itself is never
    written into this repository, here or anywhere;
  * lays overlay/ps2/ over the result: the engine's own versions of files
    that the private tree makes from the game (the icon);
  * checks what came across: no author name or address left, no address of
    a machine, no home folder's path; exit 1 if any is found.

Then, separately: tools/rom_leak_check.py over the tree, tools/setup.py and
the proofs (NOTES.md says how), and tools/make_expected.py when the assets
changed.
"""
import fnmatch
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

ENGINE = Path(__file__).resolve().parent.parent
OUT = ENGINE / "ps2"
OVERLAY = ENGINE / "overlay" / "ps2"
MARKER = ".synced-from-private"        # OUT is ours to empty when it has it

# Paths under ps2/ that stay behind.
EXCLUDE = [
    "assets/*",             # the game's data, made by the setup from a ROM
    "route_out/*",          # route and proof outputs: frames, games, logs
    "CLAUDE.md",            # the private tree's working rules and machines
    "NOTES_next_context.md",
    "docs/machines.md",
    "docs/old/*",
    "saves/*",
    "*.bin", "*.md.zip", "*.zip", "*.7z", "*.smd", "*.gen", "*.iso",
    # pictures and sounds anywhere (docs' screenshots too): of the game,
    # unless KEEP_ASSETS or the overlay says otherwise
    "*.png", "*.jpg", "*.jpeg", "*.gif", "*.bmp", "*.webp",
    "*.wav", "*.flac", "*.ogg", "*.mp3", "*.mp4", "*.webm",
]
# ...but these files under assets/ are the port's own, or free to pass on.
KEEP_ASSETS = [
    "assets/lang/languages.json",       # the language menu (the port's)
    "assets/lang/retranslation/*",      # Excalibur_Z's, MIT, with his notice
    "assets/music/q_patches.json",      # the re-voiced synth's patches
]

# Folders under ps2/ that are a player's, not the sync's: kept across it.
KEEP_LOCAL = ["saves", "mods"]

OWNER = "the owner"
_SENTENCE = re.compile(r"(^[\s#*/;>|-]*|[.!?:][\s\"']*|[(\"'\[]\s*)$")
IPV4 = re.compile(r"\b(?!127\.0\.0\.1\b)(?!0\.0\.0\.0\b)"
                  r"(?:25[0-5]|2[0-4]\d|1?\d?\d)(?:\.(?:25[0-5]|2[0-4]\d|1?\d?\d)){3}\b")
HOME = re.compile(r"[A-Za-z]:[/\\]Users[/\\][^/\\\s'\"%$]+|/home/[a-z_][a-z0-9_-]*"
                  r"|/Users/[A-Za-z][A-Za-z0-9_-]*", re.IGNORECASE)


def git(repo, *args):
    return subprocess.run(["git", "-C", str(repo), *args], check=True,
                          capture_output=True, text=True).stdout


def matches(rel, patterns):
    return any(fnmatch.fnmatch(rel, p) for p in patterns)


def scrub(text, name):
    """The author's name as OWNER in any of its forms (NAME's, NAME:)."""
    count = 0

    def put(m):
        nonlocal count
        count += 1
        before = text[max(0, m.start() - 40):m.start()].split("\n")[-1]
        return OWNER[0].upper() + OWNER[1:] if _SENTENCE.search(before) \
            else OWNER
    return re.sub(r"\b%s\b" % re.escape(name), put, text,
                  flags=re.IGNORECASE), count


def main():
    if len(sys.argv) != 2:
        print(__doc__)
        return 2
    private = Path(sys.argv[1]).resolve()
    name = git(private, "config", "user.name").strip()
    email = git(private, "config", "user.email").strip()
    if not name:
        print("the private repository has no user.name: cannot scrub")
        return 1
    if git(private, "status", "--porcelain", "--", "ps2").strip():
        print("ps2/ in the private repository has changes not committed")
        return 1
    kept = ENGINE / ".sync-kept"
    if OUT.exists():
        if not (OUT / MARKER).exists():
            print("%s exists but was not made by this script; not emptied"
                  % OUT)
            return 1
        if kept.exists():
            print("%s is left from a sync that stopped: put its folders "
                  "back in ps2/ first" % kept)
            return 1
        # A player's saves and mods (KEEP_LOCAL) outlive the sync; the
        # game's files (assets/) are made again by tools/setup.py.
        for folder in KEEP_LOCAL:
            if (OUT / folder).exists():
                kept.mkdir(exist_ok=True)
                shutil.move(str(OUT / folder), str(kept / folder))
        shutil.rmtree(OUT)
    OUT.mkdir()
    if kept.exists():
        for p in kept.iterdir():
            shutil.move(str(p), str(OUT / p.name))
        kept.rmdir()

    tracked = git(private, "ls-files", "-z", "--", "ps2").split("\0")
    copied = left = names = 0
    for path in filter(None, tracked):
        rel = path[len("ps2/"):]
        if matches(rel, EXCLUDE) and not matches(rel, KEEP_ASSETS):
            left += 1
            continue
        src = private / path
        dest = OUT / rel
        dest.parent.mkdir(parents=True, exist_ok=True)
        data = src.read_bytes()
        try:
            text = data.decode("utf-8")
        except UnicodeDecodeError:
            dest.write_bytes(data)
        else:
            if email and email.lower() in text.lower():
                print("the author's address is in %s: remove it in the "
                      "private tree first" % rel)
                return 1
            text, n = scrub(text, name)
            names += n
            dest.write_bytes(text.encode("utf-8"))
        shutil.copymode(src, dest)
        copied += 1

    overlaid = 0
    if OVERLAY.exists():
        for src in OVERLAY.rglob("*"):
            if src.is_file():
                dest = OUT / src.relative_to(OVERLAY)
                dest.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(src, dest)
                overlaid += 1

    subject = git(private, "log", "-1", "--format=%s", "--", "ps2").strip()
    subject, _ = scrub(subject, name)
    (OUT / MARKER).write_text(
        "ps2/ is the private port's, as of: %s\n"
        "Made by tools/sync_private.py; it empties this folder each time.\n"
        % subject, encoding="utf-8", newline="\n")

    found = []
    for p in OUT.rglob("*"):
        if not p.is_file():
            continue
        try:
            text = p.read_text(encoding="utf-8")
        except UnicodeDecodeError:
            continue
        rel = p.relative_to(OUT).as_posix()
        if re.search(r"\b%s\b" % re.escape(name), text, re.IGNORECASE):
            found.append("%s: the author's name" % rel)
        for m in IPV4.finditer(text):
            # a version number (1.9.2609.5, 3.4.18.0.1) is not an address:
            # skip a quad that runs on into more dotted parts
            s, e = m.start(), m.end()
            if (s > 0 and text[s - 1] in ".0123456789") or \
                    re.match(r"\.\d", text[e:e + 2]):
                continue
            found.append("%s: an address, %s" % (rel, m.group(0)))
        for m in HOME.finditer(text):
            found.append("%s: a home folder, %s" % (rel, m.group(0)))
    print("ps2/ from \"%s\": %d files copied, %d left behind, %d overlaid; "
          "the author's name put as \"%s\" %d times"
          % (subject, copied, left, overlaid, OWNER, names))
    for f in found[:40]:
        print("  FOUND", f)
    if found:
        print("%d things to fix in the private tree (or EXCLUDE) before this "
              "engine is committed" % len(found))
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
