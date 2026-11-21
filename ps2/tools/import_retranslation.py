#!/usr/bin/env python3
"""(build 163) Excalibur_Z's English retranslation of Phantasy Star II as
one of the port's languages (docs/languages.md): his script and his
proportional font, on the port's own gameplay.

    python3 tools/import_retranslation.py RETRANSLATION [-o DIR] [--check]

RETRANSLATION is a checkout of his ps2-retranslation repository. What is
read from it is data only -- work/dialogue.json, work/script.json, LICENSE
and the glyph table of tools/vwfmixed.py, which is parsed (ast), never
imported -- so nothing of his runs here. Written into DIR
(assets/lang/retranslation):

    script/Script_*.txt   every script id's `en`, in the port's notation
    script/script.json    the banks, as assets/script/script.json
    names.json            the names and labels the game draws (items,
                          techniques, enemies, the party, places, songs,
                          jobs, prompts, the windows' labels)
    font.png              the proportional face, a tile a script byte
                          (16 x 16 tiles, index 1 ink, 0 paper)
    lang.json             the menu's name, the advances, the commit read
    LICENSE.txt           his MIT notice, which travels with his text

Only his text and font come in (The owner, build 163: "Text and font
only, keep the port's gameplay for now"). His build also changes the
menus, shops, battle commands and balance, and a few of his lines are
written for those; ORIGINAL_GAME below gives those spots the forms his
glossary has for the original game, and the two shop replies his shop flow
rewords get lines for the original flow.

--check converts his `us` fields -- the 1989 text, as his tools read it
from the ROM -- the same way and compares them with assets/script/: every
script id must come out as the port's own file has it, which is what
proves the notation's mapping.
"""

import argparse
import ast
import base64
import json
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
import ps2png  # noqa: E402
from script_ends import end_where_the_us_ends, read_entries  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
STOCK_SCRIPT = ROOT / "assets" / "script"

# His notation (tools/ps2text.py) to the port's (window.c read_token).
# {BR} is a line of the file; {PAGE} is {pause}, which the port's loader
# follows with the line break the ROM's RunScript makes, unless {cont}
# comes next -- as his engine does.
TOKENS = {
    "BR": "\n", "CLR": "{cont}", "PAGE": "{pause}",
    "END": "{end:C4}", "C5": "{end:C5}", "C6": "{end:C6}", "C7": "{end:C7}",
    "NAME": "{char}", "NAME2": "{char2}", "ENEMY": "{enemy}",
    "TECH": "{tech}", "ITEM": "{item}", "MESETA": "{meseta}",
}

# The script charset (ps2.asm:25907) and the bytes he added: one ellipsis
# dot ($47, three of them an ellipsis), the umlauts of the Armel shields
# and the Equip screen's arrow. The port's files write a byte with no
# letter as {XX}.
CHARSET = {" ": 0x00}
for _i, _c in enumerate("0123456789"):
    CHARSET[_c] = 1 + _i
for _i in range(26):
    CHARSET[chr(ord("A") + _i)] = 11 + _i
    CHARSET[chr(ord("a") + _i)] = 37 + _i
CHARSET.update({",": 0x3F, ".": 0x40, ";": 0x41, '"': 0x42, "?": 0x43,
                "!": 0x44, "'": 0x45, "-": 0x46, "…": 0x47,
                "Ä": 0x48, "ä": 0x49, "▶": 0x4A, ":": 0x77})
PLAIN = set("0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz"
            " ,.;\"?!'-:")

# diafont.py's advances: ink + 1 px, space 3, the dot 1 + 2; a byte with no
# glyph 8. And its two glyphs beyond vwfmixed's table.
GAP, SPACE = 1, 3
EXTRA_GLYPHS = {
    "…": (6, ("#",)),
    "▶": (1, ("#..", "##.", "###", "##.", "#..")),
}

# The spots his text ties to his engine, as the original game has them.
# Each is said in docs/languages.md.
ORIGINAL_GAME = {
    # shop_no_reprompt rewords these for his flow, where declining goes back
    # to the list ("Then, how about something else?"). In the original flow
    # declining asks who will carry it again: the JP じゃ、だれが もつんだ?
    # ("Then, who'll carry it?"), in each seller's own voice (0602, 0701).
    "dialogue": {
        "0604": "Then who'll carry it?{C5}",
        "0704": "Then who's gonna carry it?{C5}",
    },
    # improvement_rebalance's three items in old slots; the original game's
    # are his glossary's (work/glossary.md, Items).
    "items": {
        0x44: "Heilsam Boots",
        0x5D: "Whip",
        0x6E: "Bowgun",
    },
    "windows": {
        # party_menu_ps4's order (Items, Techniques, Status, Equip, Macro);
        # the original's is ITEM STATE TECH STRNG EQP, the JP アイテム
        # じょうたい テクニック つよさ そうび, in his words for each:
        # じょうたい opens Status/Order, つよさ is his "View Strength".
        "WinArt_PlayerMenu": ["Items", "Status", "Techniques", "Strength",
                              "Equip"],
        # The rebalance turned the whips into claws ("slicers and
        # knives"); the JP スライサーやムチ is slicers and whips.
        "WinArt_AnnaProfile": ["Age unknown. No one knows",
                               "what she did in the past.",
                               "She fights with slicers and",
                               "whips, leaping at her foes",
                               "to strike them down."],
    },
}

# The added script ids (`added`): his engine's own messages, none of which
# the port's events ever ask for.


def die(msg):
    sys.exit(f"import_retranslation: {msg}")


def to_port(text, where):
    """His notation to the port's: tokens, letters, and {XX} for a byte
    with no letter of its own."""
    out = []
    i = 0
    while i < len(text):
        ch = text[i]
        if ch == "{":
            j = text.index("}", i)
            tok = text[i + 1:j]
            if tok in TOKENS:
                out.append(TOKENS[tok])
            elif re.fullmatch(r"[0-9A-Fa-f]{2}", tok):
                out.append("{%s}" % tok.upper())
            else:
                die(f"{where}: no token {{{tok}}}")
            i = j + 1
            continue
        if ch in PLAIN:
            out.append(ch)
        elif ch in CHARSET:
            out.append("{%02X}" % CHARSET[ch])
        else:
            die(f"{where}: {ch!r} is not in the font")
        i += 1
    return "".join(out)


def name_text(text, where):
    """A name or a label: the letters as they are, a byte past ASCII
    ({47} and the umlauts) left as UTF-8 for lang.c's charset. Tokens have
    no place here."""
    for ch in text:
        if ch not in PLAIN and ch not in CHARSET:
            die(f"{where}: {ch!r} is not in the font")
    if "{" in text:
        die(f"{where}: a token in a name")
    return text


def bank_files():
    """assets/script/script.json: bank number -> (name, count, file)."""
    stock = json.loads((STOCK_SCRIPT / "script.json").read_text())
    banks = {}
    for name, b in stock.items():
        banks[b["bank"]] = (name, b["count"], b["file"])
    return stock, banks


def entry_text(e, field):
    """The port's text for one entry's field. The 1989 text keeps the dead
    bytes after its end code (`tail`), as the port's files do."""
    text = to_port(e[field], e["id"])
    if field == "us" and e.get("tail"):
        text += bytes_to_port(bytes.fromhex(e["tail"]))
    return text


# extract_script.py's control codes, for the tails' bytes.
CONTROL_BYTES = {0xBB: "{char}", 0xBC: "{char2}", 0xBD: "{enemy}",
                 0xBE: "{tech}", 0xBF: "{item}", 0xC0: "{meseta}",
                 0xC1: "\n", 0xC2: "{cont}", 0xC3: "{pause}"}
LETTER = {b: ch for ch, b in CHARSET.items() if ch in PLAIN}


def bytes_to_port(data):
    """Stock script bytes in the port's notation, as extract_script.py
    writes them."""
    out = []
    for b in data:
        if b >= 0xC4:
            out.append("{end:%02X}" % b)
        elif b in CONTROL_BYTES:
            out.append(CONTROL_BYTES[b])
        elif b in LETTER:
            out.append(LETTER[b])
        else:
            out.append("{%02X}" % b)
    return "".join(out)


def load_dialogue(repo):
    d = json.loads((repo / "work" / "dialogue.json").read_text())
    by_id = {}
    for e in d["entries"]:
        for sid in e["ids"]:
            by_id[sid] = e
    return d["entries"], by_id


def bank_text(name, bank, count, by_id, field, header=""):
    """A bank's file, as extract_script.py writes the stock ones."""
    parts = [f"# {name}, bank {bank:#04x}; script_id = {bank:#04x}NN,"
             f" NN 1-based\n{header}"]
    texts = {}
    for n in range(1, count + 1):
        sid = "%02X%02X" % (bank, n)
        e = by_id.get(sid)
        if e is None or e.get("added"):
            die(f"{sid}: not in dialogue.json")
        if field == "en":
            texts[n] = to_port(ORIGINAL_GAME["dialogue"].get(sid, e["en"]),
                               sid)
        else:
            texts[n] = entry_text(e, field)
    if field == "en":
        # (build 168f) Each message ends where the US one does, as the
        # Japanese import's must; his text already does (his 010B's run-on
        # into 010C is script_ends.RUN_ON's).
        file = bank_files()[1][bank][2]
        for k in end_where_the_us_ends(texts,
                                       read_entries(STOCK_SCRIPT / file),
                                       bank):
            print(f"{bank:02X}{k:02X}: ended where the US message ends")
    for n in range(1, count + 1):
        parts.append(f"\n[{n}]\n{texts[n]}\n")
    return "".join(parts)


def check_stock(repo):
    """His `us` fields against assets/script/: every bank's file written
    from them byte for byte the port's own."""
    _, by_id = load_dialogue(repo)
    _, banks = bank_files()
    same = differ = 0
    for bank, (name, count, file) in sorted(banks.items()):
        mine = bank_text(name, bank, count, by_id, "us")
        theirs = (STOCK_SCRIPT / file).read_text()
        if mine == theirs:
            same += 1
            continue
        differ += 1
        a, b = mine.split("\n[")[1:], theirs.split("\n[")[1:]
        for x, y in zip(a, b):
            if x != y:
                print(f"  {name}: his [{x!r}\n    port [{y!r}")
    print(f"--check: {same} of {len(banks)} banks' files byte for byte "
          f"the port's, {differ} not")
    return differ == 0


def write_scripts(repo, out):
    _, by_id = load_dialogue(repo)
    stock_manifest, banks = bank_files()
    (out / "script").mkdir(parents=True, exist_ok=True)
    total = 0
    for bank, (name, count, file) in sorted(banks.items()):
        (out / "script" / file).write_text(
            bank_text(name, bank, count, by_id, "en",
                      "# The retranslation's (tools/import_retranslation.py;"
                      " docs/languages.md)\n"), newline="\n")
        total += count
    (out / "script" / "script.json").write_text(
        json.dumps(stock_manifest, indent=2) + "\n", newline="\n")
    added = sorted(sid for sid, e in by_id.items() if e.get("added"))
    return total, added


def segment(script, name):
    return script["segments"][name]["runs"]


def run_number(run):
    return int(run["id"].split("#")[1])


def write_names(repo, out):
    script = json.loads((repo / "work" / "script.json").read_text())
    items = json.loads((ROOT / "assets/data/items.json").read_text())
    techs = json.loads((ROOT / "assets/data/techniques.json").read_text())
    battle = json.loads((ROOT / "assets/data/battle.json").read_text())
    item_key = {it["id"]: it["key"] for it in items}
    tech_key = {t["id"]: t["key"] for t in techs["techniques"]}
    enemy_label = {e["id"]: e["label"] for e in battle["enemies"]}

    names = {"source": "work/script.json"}
    names["characters"] = [name_text(r["en"], r["id"])
                           for r in segment(script, "charnames")]

    names["items"] = {}
    for r in segment(script, "items"):
        n = run_number(r)
        if n in item_key and n != 0:
            text = ORIGINAL_GAME["items"].get(n, r["en"])
            names["items"][item_key[n]] = name_text(text, r["id"])

    names["techniques"] = {}
    for r in segment(script, "techs"):
        n = run_number(r)
        if n in tech_key and n != 0 and r["en"]:
            names["techniques"][tech_key[n]] = name_text(r["en"], r["id"])

    names["enemies"] = {}
    for r in segment(script, "enemies"):
        n = run_number(r)
        if n in enemy_label:
            names["enemies"][enemy_label[n]] = name_text(r["en"], r["id"])

    for key, seg in (("songs", "soundtracks"), ("towns", "teleport"),
                     ("jobs", "jobs"), ("prompts", "prompts"),
                     ("labels", "labels")):
        names[key] = [name_text(r["en"], r["id"]) for r in segment(script, seg)]

    names["windows"] = {}
    for r in segment(script, "windows"):
        rows = ORIGINAL_GAME["windows"].get(r["label"],
                                            r["en"].split("{BR}"))
        names["windows"][r["label"]] = [name_text(t, r["id"]) for t in rows]

    (out / "names.json").write_text(
        json.dumps(names, indent=2, ensure_ascii=False) + "\n",
        newline="\n", encoding="utf-8")
    return names


def read_glyphs(repo):
    """vwfmixed.py's _g(ch, top, *rows) calls, read as literals."""
    src = (repo / "tools" / "vwfmixed.py").read_text(encoding="utf-8")
    glyphs = {}
    for node in ast.walk(ast.parse(src)):
        if (isinstance(node, ast.Call) and isinstance(node.func, ast.Name)
                and node.func.id == "_g"):
            args = [ast.literal_eval(a) for a in node.args]
            ch, top, rows = args[0], args[1], tuple(args[2:])
            glyphs[ch] = (top, rows)
    glyphs.update(EXTRA_GLYPHS)
    return glyphs


def build_font(repo):
    """256 glyphs by script byte, 8 rows of 1bpp each (bit 7 the left), and
    the advances: diafont.py's build, from the same table."""
    glyphs = read_glyphs(repo)
    rows = [[0] * 8 for _ in range(256)]
    width = [8] * 256
    for ch, b in CHARSET.items():
        if ch == " ":
            width[b] = SPACE
            continue
        if ch not in glyphs:
            continue
        top, art = glyphs[ch]
        for dy, r in enumerate(art):
            for dx, p in enumerate(r):
                if p == "#":
                    rows[b][top + dy] |= 0x80 >> dx
        ink = max(len(r) for r in art)
        width[b] = ink + (2 if ch == "…" else GAP)
    return rows, width


def check_font(repo, rows, width):
    """The build against the copy his proofreader embeds (proofsync.py),
    when it has one: the same face."""
    html = (repo / "tools" / "proofread.html").read_text(encoding="utf-8")
    m1 = re.search(r'DIAFONT_B64="([^"]+)"', html)
    m2 = re.search(r'DIAWIDTH_B64="([^"]+)"', html)
    if not m1 or not m2:
        return "the proofreader has no copy of the face to compare"
    font = base64.b64decode(m1.group(1))
    wid = base64.b64decode(m2.group(1))
    mine = bytes(b for r in rows for b in r)
    if font != mine or wid != bytes(width):
        die("the face built from vwfmixed.py is not the proofreader's")
    return "the same as the proofreader's (DIAFONT_B64, DIAWIDTH_B64)"


def write_font(out, rows):
    tiles = []
    for r in rows:
        px = []
        for y in range(8):
            for x in range(8):
                px.append(1 if r[y] & (0x80 >> x) else 0)
        tiles.append(px)
    # Paper and ink as the windows show them (the window colours' $B and
    # $1 are near enough): the picture is for an editor; the game reads
    # only the indices.
    rgb = [(0, 0, 0), (238, 238, 238)]
    ps2png.write_tile_sheet(out / "font.png", tiles, rgb, packed=False)


def write_license(repo, out):
    text = (repo / "LICENSE").read_text(encoding="utf-8")
    notice = text.split("-" * 20)[0].rstrip() + "\n"
    (out / "LICENSE.txt").write_text(
        "The text in script/ and names.json and the face in font.png are\n"
        "Excalibur_Z's Phantasy Star II: English Retranslation\n"
        "(github.com/ExcalZ/ps2-retranslation: work/dialogue.json,\n"
        "work/script.json, tools/vwfmixed.py), converted by the port's\n"
        "tools/import_retranslation.py. Its credit: \"Translation, hacking\n"
        "and testing by Excalibur_Z, with Claude (Anthropic) for translation\n"
        "and technical work.\" Two lines of script/ are the port's own, for\n"
        "the original game's shop flow: Script_WeaponStore.txt [4] and\n"
        "Script_ArmorStore.txt [4] (the port's docs/languages.md).\n\n"
        "Under his license:\n\n" + notice, newline="\n", encoding="utf-8")


def commit_of(repo):
    head = repo / ".git" / "HEAD"
    if not head.exists():
        return ""
    ref = head.read_text().strip()
    if ref.startswith("ref: "):
        path = repo / ".git" / ref[5:]
        if path.exists():
            return path.read_text().strip()
        packed = repo / ".git" / "packed-refs"
        for line in packed.read_text().splitlines() if packed.exists() else []:
            if line.endswith(" " + ref[5:]):
                return line.split()[0]
        return ""
    return ref


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("repo", type=Path, help="a ps2-retranslation checkout")
    ap.add_argument("-o", "--out", type=Path,
                    default=ROOT / "assets" / "lang" / "retranslation")
    ap.add_argument("--check", action="store_true",
                    help="only compare his `us` text with assets/script/")
    args = ap.parse_args()

    if not (args.repo / "work" / "dialogue.json").exists():
        die(f"{args.repo}: no work/dialogue.json")
    if args.check:
        sys.exit(0 if check_stock(args.repo) else 1)

    args.out.mkdir(parents=True, exist_ok=True)
    total, added = write_scripts(args.repo, args.out)
    names = write_names(args.repo, args.out)
    rows, width = build_font(args.repo)
    font_note = check_font(args.repo, rows, width)
    write_font(args.out, rows)
    write_license(args.repo, args.out)

    lang = {
        "name": "RETRANSLATED",
        "about": "Excalibur_Z's English retranslation from the Japanese "
                 "script, its text and proportional font (docs/languages.md)",
        "source": "ps2-retranslation " + commit_of(args.repo),
        "proportional": True,
        # (build 165) His layout for his longer words (docs/languages.md):
        # the dialogue window's battle box, the technique windows and the
        # main menu a cell wider, six letters a name.
        "layout": ["battle_box", "wide_techs", "wide_menu", "long_names"],
        "font": "font.png",
        # The advance of each script byte, pixels (ink + 1, space 3).
        "advance": width,
        # The letters past ASCII its names write, by script byte.
        "charset": {ch: b for ch, b in CHARSET.items() if ch not in PLAIN},
    }
    (args.out / "lang.json").write_text(
        json.dumps(lang, indent=1, ensure_ascii=False) + "\n",
        newline="\n", encoding="utf-8")

    print(f"{total} script ids in {args.out / 'script'}; his own "
          f"{len(added)} left out ({' '.join(added)})")
    print(f"names: {len(names['items'])} items, {len(names['techniques'])} "
          f"techniques, {len(names['enemies'])} enemies, "
          f"{len(names['windows'])} windows")
    print(f"font: {font_note}")


if __name__ == "__main__":
    main()
