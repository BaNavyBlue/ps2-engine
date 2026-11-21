#!/usr/bin/env python3
"""(build 160) The controls through the headless game (main.c, its
platform and settings.json): the Escape menu's CONTROLS and BUTTONS pages
worked by raw presses (the headless platform's PS2_RAW, made what the
SDL3 platform makes of them), a rebind reaching the game, and
settings.json written and read back.

  make headless controls_check
  python3 tools/controls_check.py [--game ./ps2_headless] [--keep DIR]

Every run is the headless game in a scratch folder of its own (assets/
linked in, PS2_SAVES and PS2_MODS there): nothing under saves/ or
route_out/ is touched. A run's last frame (PS2_OUT) says what the game
did: two runs whose presses the game should take alike end on the same
frame, byte for byte.

1. Keys: Escape, the arrows and Enter (the menu's own keys) to the
   CONTROLS page, START's KEY 1 to Q; then on the title Enter, the old
   Start, does nothing (the frame of no press) and Q is Start (the frame
   of PS2_PAD's start). settings.json: "start": ["Q", "-"], no
   "controls". Read back: the same presses with that file and no menu.
2. Gamepad inputs, a gamepad connected (PS2_GAMEPADS=1, PS2_PAD_STYLE=ps3):
   Back, the d-pad and the left stick (the menu's own) and C (East) to the
   BUTTONS page, START's 1ST to R2; then the gamepad's Start does nothing
   and R2 is Start; "start": ["R2", "-"] under "pad". With no gamepad
   connected R2 reaches nothing. Read back as for the keys.
3. The labels: the BUTTONS page's frame with PS2_PAD_STYLE=ps3 differs
   from its frame with none, and from xboxone's and switch's; ps3 with no
   gamepad connected is none's (PAD 1's style, and there is no PAD 1).
4. A file from before build 160, "controls": "wasd": no message, the
   controls the defaults (both key sets), and written again without
   "controls" (FULLSCREEN changed, to make the menu write it). (build
   167) One from builds 159-166, "scanlines": "interlaced": written again
   as true (INTERLACED drew scanlines), with no "flicker".
5. Names nothing has ("NOSUCH", "=", a gamepad's "BOGUS"): a message each,
   those slots their defaults, the rest of the file read.
6. A PS2_RAW or PS2_PAD_STYLE the platform cannot read: a message and no
   game.
7. Frames worth no tick or two (PS2_FRAME_HZ 120 and 30: main.c's pacing
   running free, as on a 120 Hz display or a slow machine): UP's KEY 2 set
   to Q by raw keys, Enter listening; a key pressed on a frame with no tick
   still binds, and the key that started the listening is not bound by the
   frame's second tick.

Exit 1 on any failure."""

import argparse
import hashlib
import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

TREE = Path(__file__).resolve().parent.parent
MENU_AT = 600       # the title is up and running on under the menu
STEP = 6            # frames between presses

failures = []


def check(name, ok, detail=""):
    print("%-56s %s %s" % (name, "ok" if ok else "FAILED", detail))
    if not ok:
        failures.append(name)


def presses(*tokens, start=MENU_AT):
    """PS2_PAD and PS2_RAW from tokens, STEP frames apart: 'pad:b' a game
    button (PS2_PAD), 'raw:Q' or 'raw:PAD:R2' a raw press (PS2_RAW),
    'at:N' the next at frame N. The frame after the last."""
    pad, raw, f = [], [], start
    for t in tokens:
        kind, _, what = t.partition(":")
        if kind == "at":
            f = int(what)
            continue
        (pad if kind == "pad" else raw).append("%d:%s" % (f, what))
        f += STEP
    return ",".join(pad), ",".join(raw), f


class Headless:
    def __init__(self, game, scratch):
        self.game = game
        self.base = scratch / "headless"
        self.base.mkdir(parents=True, exist_ok=True)

        def link(src, to):
            try:
                os.link(src, to)
            except OSError:
                shutil.copy2(src, to)
        shutil.copytree(TREE / "assets", self.base / "assets",
                        copy_function=link)
        self.n = 0

    def run(self, frames, pad="", raw="", pads=0, style=None,
            settings=None, hz=None, mods=None):
        """The game in its own saves and mods folders: (the last frame's
        hash, settings.json as left, stderr, exit status). (build 163)
        `mods`: {path under the mods folder: text} written there first."""
        self.n += 1
        run = self.base / ("run%d" % self.n)
        (run / "saves").mkdir(parents=True)
        (run / "mods").mkdir()
        for path, text in (mods or {}).items():
            (run / "mods" / path).parent.mkdir(parents=True, exist_ok=True)
            (run / "mods" / path).write_text(text)
        if settings is not None:
            (run / "saves" / "settings.json").write_text(settings)
        out = run / "last.ppm"
        env = dict(os.environ, PS2_FRAMES=str(frames), PS2_GAMEPADS=str(pads),
                   PS2_OUT=str(out), PS2_SAVES=str(run / "saves"),
                   PS2_MODS=str(run / "mods"), PS2_NO_AUDIO="1")
        for k in ("PS2_PAD", "PS2_RAW", "PS2_PAD_STYLE", "PS2_BATTLE",
                  "PS2_LEVEL", "PS2_BUILDING", "PS2_START", "PS2_TOUCH",
                  "PS2_WAV", "PS2_RUMBLE", "PS2_FRAME_HZ"):
            env.pop(k, None)
        if pad:
            env["PS2_PAD"] = pad
        if raw:
            env["PS2_RAW"] = raw
        if style is not None:
            env["PS2_PAD_STYLE"] = style
        if hz is not None:
            env["PS2_FRAME_HZ"] = str(hz)
        p = subprocess.run([self.game], cwd=self.base, env=env,
                           capture_output=True, text=True, timeout=900)
        frame = (hashlib.sha256(out.read_bytes()).hexdigest()[:16]
                 if out.exists() else None)
        sfile = run / "saves" / "settings.json"
        text = sfile.read_text() if sfile.exists() else ""
        return frame, text, p.stderr, p.returncode


def section(text, name):
    """settings.json's "keys" or "pad" object, as written."""
    m = re.search(r'"%s": \{(.*?)\}' % name, text, re.S)
    return m.group(1) if m else None


def menu_lines(stderr):
    return [l for l in stderr.splitlines() if l.startswith("sysmenu:")]


# ---- 1. keys ----

KEYS_MENU = ["raw:ESCAPE", "raw:DOWN", "raw:RETURN"] + ["raw:DOWN"] * 7 + [
    "raw:RETURN", "raw:Q", "pad:b", "pad:b"]


def part_keys(h):
    tail = 700
    _, _, end = presses(*KEYS_MENU)
    assert end < tail
    runs = {}
    for name, last in (("none", []), ("return", ["raw:RETURN"]),
                       ("q", ["raw:Q"]), ("start", ["pad:start"])):
        pad, raw, _ = presses(*(KEYS_MENU + ["at:%d" % tail] + last))
        runs[name] = h.run(tail + 60, pad, raw)
    frame = {k: v[0] for k, v in runs.items()}
    check("keys: Enter, Start no more, does nothing",
          frame["return"] == frame["none"],
          "%s %s" % (frame["return"], frame["none"]))
    check("keys: Q is Start now",
          frame["q"] == frame["start"] and frame["q"] != frame["none"],
          "%s %s" % (frame["q"], frame["start"]))
    written = runs["q"][1]
    keys = section(written, "keys") or ""
    check("keys: settings.json has START's Q",
          '"start": ["Q", "-"]' in keys and '"controls"' not in written
          and '"up": ["UP", "W"]' in keys,
          keys.strip().replace("\n", " ")[:120])

    # Read back: the file alone, no menu.
    runs = {}
    for name, settings, last in (
            ("q", written, "raw:Q"), ("return", written, "raw:RETURN"),
            ("start", None, "pad:start"), ("none", None, None)):
        tokens = ["at:%d" % tail] + ([last] if last else [])
        pad, raw, _ = presses(*tokens)
        runs[name] = h.run(tail + 60, pad, raw, settings=settings)
    frame = {k: v[0] for k, v in runs.items()}
    check("keys: read back, Q is Start and Enter nothing",
          frame["q"] == frame["start"] and frame["return"] == frame["none"]
          and frame["start"] != frame["none"]
          and not menu_lines(runs["q"][2]),
          "%s %s %s %s" % (frame["q"], frame["start"], frame["return"],
                           frame["none"]))


# ---- 2. gamepad inputs ----

PAD_MENU = (["raw:PAD:BACK"] + ["raw:PAD:DDOWN"] * 8 + ["raw:PAD:EAST"]
            + ["raw:PAD:LSDOWN"] * 3 + ["raw:PAD:EAST"]
            + ["raw:PAD:DDOWN"] * 7
            + ["raw:PAD:EAST", "raw:PAD:R2", "raw:PAD:BACK", "raw:PAD:BACK",
               "raw:PAD:BACK"])


def part_pad(h):
    tail = 800
    _, _, end = presses(*PAD_MENU)
    assert end < tail
    runs = {}
    for name, last in (("none", []), ("old", ["raw:PAD:START"]),
                       ("r2", ["raw:PAD:R2"]), ("start", ["pad:start"])):
        pad, raw, _ = presses(*(PAD_MENU + ["at:%d" % tail] + last))
        runs[name] = h.run(tail + 60, pad, raw, pads=1, style="ps3")
    frame = {k: v[0] for k, v in runs.items()}
    check("pad: the gamepad's Start, Start no more, does nothing",
          frame["old"] == frame["none"],
          "%s %s" % (frame["old"], frame["none"]))
    check("pad: R2 is Start now",
          frame["r2"] == frame["start"] and frame["r2"] != frame["none"],
          "%s %s" % (frame["r2"], frame["start"]))
    written = runs["r2"][1]
    pads = section(written, "pad") or ""
    check("pad: settings.json has START's R2",
          '"start": ["R2", "-"]' in pads and '"up": ["DUP", "LSUP"]' in pads
          and '"start": ["RETURN", "-"]' in (section(written, "keys") or ""),
          pads.strip().replace("\n", " ")[:120])

    runs = {}
    for name, settings, last, pads_on in (
            ("r2", written, "raw:PAD:R2", 1),
            ("old", written, "raw:PAD:START", 1),
            ("unplugged", written, "raw:PAD:R2", 0),
            ("start", None, "pad:start", 1), ("none", None, None, 1)):
        tokens = ["at:%d" % tail] + ([last] if last else [])
        pad, raw, _ = presses(*tokens)
        runs[name] = h.run(tail + 60, pad, raw, pads=pads_on,
                           settings=settings)
    frame = {k: v[0] for k, v in runs.items()}
    check("pad: read back, R2 is Start and Start nothing",
          frame["r2"] == frame["start"] and frame["old"] == frame["none"]
          and frame["start"] != frame["none"]
          and not menu_lines(runs["r2"][2]),
          "%s %s %s %s" % (frame["r2"], frame["start"], frame["old"],
                           frame["none"]))
    check("pad: no gamepad connected, R2 reaches nothing",
          frame["unplugged"] == frame["none"], frame["unplugged"])


# ---- 3. the labels ----

def part_labels(h):
    # Escape, down to GAMEPAD, its page, down to BUTTONS, its page; the run
    # ends on it.
    tokens = (["pad:esc"] + ["pad:down"] * 8 + ["pad:c"] + ["pad:down"] * 3
              + ["pad:c"])
    pad, _, end = presses(*tokens)
    frames = {}
    for name, style, pads in (("none", None, 1), ("ps3", "ps3", 1),
                              ("xboxone", "xboxone", 1),
                              ("switch", "switch", 1),
                              ("ps3_unplugged", "ps3", 0),
                              ("none_unplugged", None, 0)):
        frames[name] = h.run(end + 20, pad, pads=pads, style=style)[0]
    styles = [frames[k] for k in ("none", "ps3", "xboxone", "switch")]
    check("labels: none, ps3, xboxone, switch drawn apart",
          len(set(styles)) == 4 and None not in styles, " ".join(styles))
    check("labels: ps3 with no gamepad is none's",
          frames["ps3_unplugged"] == frames["none_unplugged"],
          "%s %s" % (frames["ps3_unplugged"], frames["none_unplugged"]))


# ---- 4 and 5. settings.json from elsewhere ----

# FULLSCREEN changed and back, to make the menu write the file.
SAVE_MENU = ["pad:esc", "pad:down", "pad:down", "pad:down", "pad:c", "pad:c",
             "pad:esc"]


def part_files(h):
    pad, _, end = presses(*SAVE_MENU)
    _, default, err, _ = h.run(end + 10, pad)
    _, old, err_old, _ = h.run(end + 10, pad,
                               settings='{"controls": "wasd", '
                                        '"rumble": "low"}')
    check("old file: \"controls\": \"wasd\" reads as the defaults",
          section(old, "keys") == section(default, "keys") is not None
          and section(old, "pad") == section(default, "pad")
          and '"rumble": "low"' in old and '"controls"' not in old
          and not menu_lines(err_old),
          "; ".join(menu_lines(err_old)) or "no message")
    # (build 167) INTERLACED and FLICKER are gone.
    _, il, err_il, _ = h.run(end + 10, pad,
                             settings='{"crt": {"scanlines": "interlaced", '
                                      '"flicker": "soft"}}')
    check("old file: \"scanlines\": \"interlaced\" reads as ON",
          '"scanlines": true' in il and '"flicker"' not in il
          and not menu_lines(err_il),
          "; ".join(menu_lines(err_il))
          or (re.search(r'"crt": \{[^}]*\}', il) or [il[:80]])[0])

    bad = ('{"keys": {"up": ["NOSUCH", "Q"], "b": ["X", "="], '
           '"start": ["-", "SPACE"]}, "pad": {"a": ["BOGUS", "L2"], '
           '"c": ["EAST", "JOY40"]}}')
    _, got, err_bad, _ = h.run(end + 10, pad, settings=bad)
    lines = menu_lines(err_bad)
    want = ["sysmenu: no key NOSUCH; up keeps its own",
            "sysmenu: no key =; b keeps its own",
            "sysmenu: no gamepad input BOGUS; a keeps its own",
            "sysmenu: no gamepad input JOY40; c keeps its own"]
    keys, pads = section(got, "keys") or "", section(got, "pad") or ""
    check("bad names: a message each, those slots their defaults",
          lines == want and '"up": ["UP", "Q"]' in keys
          and '"b": ["X", ";"]' in keys
          and '"start": ["-", "SPACE"]' in keys
          and '"a": ["WEST", "L2"]' in pads
          and '"c": ["EAST", "-"]' in pads,
          "; ".join(lines))


# ---- 6. scripts the platform cannot read ----

def part_scripts(h):
    _, _, err, status = h.run(10, raw="5:NOSUCHKEY")
    check("PS2_RAW with no such key: a message, no game",
          status != 0 and "PS2_RAW: no key or gamepad input NOSUCHKEY" in err,
          "exit %d" % status)
    _, _, err, status = h.run(10, style="dualshock")
    check("PS2_PAD_STYLE with no such style: a message, no game",
          status != 0 and "PS2_PAD_STYLE: no style dualshock" in err,
          "exit %d" % status)


# ---- 7. frames worth no tick, or two ----

def part_frames(h):
    tokens = ["raw:ESCAPE", "raw:DOWN", "raw:RETURN", "raw:RIGHT",
              "raw:RETURN", "raw:Q", "raw:ESCAPE", "raw:ESCAPE"]
    for hz, start in ((30, 600), (120, 1300), (120, 1301)):
        _, raw, end = presses(*tokens, start=start)
        _, text, err, _ = h.run(end + 20, raw=raw, hz=hz)
        keys = section(text, "keys") or ""
        check("frames at %d Hz (from %d): Q bound, Enter not" % (hz, start),
              '"up": ["UP", "Q"]' in keys
              and '"start": ["RETURN", "-"]' in keys,
              keys.strip().replace("\n", " ")[:80])


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--game", default="./ps2_headless")
    ap.add_argument("--keep", default=None,
                    help="keep the scratch folders under DIR")
    a = ap.parse_args()
    game = str(Path(a.game).resolve())
    if a.keep:
        Path(a.keep).mkdir(parents=True, exist_ok=True)
    scratch = Path(tempfile.mkdtemp(prefix="controls_", dir=a.keep))
    try:
        h = Headless(game, scratch)
        part_keys(h)
        part_pad(h)
        part_labels(h)
        part_files(h)
        part_scripts(h)
        part_frames(h)
    finally:
        if not a.keep:
            shutil.rmtree(scratch, ignore_errors=True)
    if failures:
        print("controls: FAIL (%d): %s" % (len(failures),
                                           ", ".join(failures)))
        return 1
    print("controls: PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
