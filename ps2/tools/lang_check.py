#!/usr/bin/env python3
"""(build 163) LANGUAGE through the headless game (main.c, its platform
and settings.json; docs/languages.md): what the routes cannot reach, the
probe having no main.c to write the file.

  make headless
  python3 tools/lang_check.py [--game ./ps2_headless] [--keep DIR]

Each run is controls_check.py's: the headless game in a scratch folder of
its own, its last frame (PS2_OUT) what it showed -- here the attract
crawl's second message, at frame END.

1. The menu: Escape, down nine, Enter on the title (which runs on under
   the menu) and Escape: settings.json has "language": "retranslation",
   and the crawl is his -- not the stock crawl's frame.
2. Read back: that file and no menu, the same frame.
3. A key no language has: "sysmenu: no language ...", the stock frame.
4. A mod of his names.json that does not parse: set aside (the mods
   folder's rule), his own read, the menu run's frame.
5. (build 165) "language": "ja": the Japanese crawl, neither the stock
   frame nor his.

Exit 1 on any failure."""

import argparse
import shutil
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from controls_check import Headless, check, failures, presses  # noqa: E402

END = 3500          # the crawl's second message is up (lang_crawl.route)
MENU = ["raw:ESCAPE"] + ["raw:DOWN"] * 9 + ["raw:RETURN", "raw:ESCAPE"]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--game", default="./ps2_headless")
    ap.add_argument("--keep", default=None,
                    help="keep the scratch folders under DIR")
    a = ap.parse_args()
    game = str(Path(a.game).resolve())
    if a.keep:
        Path(a.keep).mkdir(parents=True, exist_ok=True)
    scratch = Path(tempfile.mkdtemp(prefix="lang_", dir=a.keep))
    try:
        h = Headless(game, scratch)

        _, raw, _ = presses(*MENU)
        menu_frame, written, err, _ = h.run(END, raw=raw)
        stock_frame, _, _, _ = h.run(END)
        check("menu: settings.json has the retranslation",
              '"language": "retranslation"' in written,
              written.replace("\n", " ")[:100])
        check("menu: the crawl is his, not the stock one",
              menu_frame is not None and menu_frame != stock_frame,
              "%s %s" % (menu_frame, stock_frame))

        frame, _, err, _ = h.run(END, settings='{"language": "retranslation"}')
        check("read back: the same crawl", frame == menu_frame,
              "%s %s" % (frame, menu_frame))

        frame, _, err, _ = h.run(END, settings='{"language": "klingon"}')
        check("no such language: said, and the stock crawl",
              frame == stock_frame and "no language klingon" in err,
              "%s %s" % (frame, stock_frame))

        frame, _, _, _ = h.run(END, settings='{"language": "ja"}')
        check("the Japanese cartridge: its own crawl",
              frame is not None and frame not in (stock_frame, menu_frame),
              "%s %s %s" % (frame, stock_frame, menu_frame))

        frame, _, err, _ = h.run(
            END, settings='{"language": "retranslation"}',
            mods={"lang/retranslation/names.json": "{ not json"})
        check("a broken mod of his names: set aside, his own read",
              frame == menu_frame and "set aside" in err,
              "%s %s" % (frame, menu_frame))
    finally:
        if not a.keep:
            shutil.rmtree(scratch, ignore_errors=True)
    if failures:
        print("lang: FAIL (%d): %s" % (len(failures), ", ".join(failures)))
        return 1
    print("lang: PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
