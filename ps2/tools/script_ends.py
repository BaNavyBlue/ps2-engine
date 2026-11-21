"""(build 168f) A language's message ends where the US message ends.

The game's event code asks for the US script ids: a message is the id it
asks for and the ids after it up to the first that holds an end code
(window.c's expand_script runs on through an entry with none, an empty one
too). Where a language's text has no end code at a point the US message
ends, its message runs on into the next one, and where the event code then
asks for that one too, the next message is shown twice: the Japanese
Governor's mission ($1101 then $1105, building.c's win_script2) and Lutz's
second half ($190B then $190E). Used by import_japanese.py and
import_retranslation.py on the text they write, and by hand once on the
files they wrote before 168f (docs/languages.md).

One run-on is the language's own and stays (RUN_ON): $010B.

    python3 tools/script_ends.py --check    # every language's script files
                                            # as they stand: exit 1 if a
                                            # message runs past a US end
"""

import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

END = re.compile(r"\{end:[0-9A-F]{2}\}")

# Script ids whose run-on past the US end is the language's text, not a
# message shown twice: the event code asks for the id alone.
#   $010B, a healing technique on a poisoned member (TechEffect_Res,
#   ps2.asm:15266, menu.c's tech_res): "touches X's wounds." and nothing
#   after it. The US ends it there and so loses $010C's line, the poison
#   not healed; the Japanese cartridge runs on into it, and the
#   retranslation with it (languages.md, "For the original game").
RUN_ON = {0x010B}


def read_entries(path):
    """A script file in the port's notation: {n: text} -- each entry's
    lines between its `[n]` line and the blank line before the next."""
    out, cur = {}, None
    for line in Path(path).read_text(encoding="utf-8").splitlines():
        if line.startswith("#"):
            continue
        m = re.match(r"\[(\d+)\]$", line)
        if m:
            cur = int(m.group(1))
            out[cur] = []
            continue
        if cur is not None:
            out[cur].append(line)
    return {n: "\n".join(lines).strip("\n") for n, lines in out.items()}


def end_where_the_us_ends(texts, us, bank):
    """`texts` ({n: the language's text} of script bank `bank`) changed in
    place: for each US entry that holds an end code, the language's text
    that covers it -- its own entry, or the last one before it with text,
    inside the same US message -- ends there too, with the US entry's end
    code, put in place of a page break it ends with (RUN_ON's ids left as
    they are). Returns the entries changed."""
    changed = []
    start = 1
    for n in sorted(us):
        m = END.search(us[n])
        if m is None:
            continue
        k = n
        while k > start and not texts.get(k, ""):
            k -= 1
        text = texts.get(k, "")
        if text and not END.search(text) and (bank << 8 | k) not in RUN_ON:
            body = text.rstrip("\n")
            if body.endswith("{pause}"):
                body = body[:-len("{pause}")]
            texts[k] = body + m.group(0)
            changed.append(k)
        start = n + 1
    return changed


def check():
    """Every language's script files against the US ones: each message
    that would change under the rule, said. 0 when none would."""
    bad = 0
    banks = json.loads((ROOT / "assets" / "script" / "script.json")
                       .read_text(encoding="utf-8"))
    langs = sorted(p for p in (ROOT / "assets" / "lang").iterdir()
                   if (p / "script").is_dir())
    for lang in langs:
        files = 0
        for b in banks.values():
            path = lang / "script" / b["file"]
            if not path.exists():
                continue
            files += 1
            texts = read_entries(path)
            us = read_entries(ROOT / "assets" / "script" / b["file"])
            for k in end_where_the_us_ends(texts, us, b["bank"]):
                print(f"{lang.name} {b['file']} [{k}]: runs past the US "
                      f"message's end")
                bad += 1
        print(f"{lang.name}: {files} script files")
    print("script_ends: " + ("PASS" if bad == 0 else f"FAIL, {bad}"))
    return 1 if bad else 0


if __name__ == "__main__":
    if sys.argv[1:] != ["--check"]:
        sys.exit(__doc__)
    sys.exit(check())
