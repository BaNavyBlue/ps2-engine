#!/usr/bin/env python3
"""(build 165) The Japanese cartridge -- Phantasy Star II: Kaerazaru Toki no
Owari ni -- as one of the port's languages (docs/languages.md, Japanese).

    python3 tools/import_japanese.py JP_ROM RETRANSLATION [-o DIR]

JP_ROM is the Japanese cartridge's image, or a zip holding it (a 1 MB
overdump is fine: its first 768 KB must be CRC32 BEC8EB5A, the image
Excalibur_Z's tools were checked against). It stays outside the tree; what
is written from it is the face and the character map. RETRANSLATION is a
checkout of his ps2-retranslation repository, whose work/dialogue.json and
work/script.json carry the Japanese of every message and table (`jp`,
decoded from that cartridge and matched to the US script ids); read as
data, nothing of his runs. Written into DIR (assets/lang/ja):

    script/Script_*.txt   every US script id's Japanese, in the port's
                          notation, the kana as UTF-8
    script/script.json    the banks, as assets/script/script.json
    names.json            the names and labels drawn, in Japanese
    font.png              the face: a code's lower tile -- the letter --
                          from the cartridge's FontsIconsArt, a tile a code
    font_upper.png        and its upper tile: the dakuten or handakuten
    lang.json             the menu's name, the face (eight pixels a glyph,
                          two rows), the charset: each character's code
    NOTICE.txt            where all of it comes from

The cartridge's codes are the language's: VDPCharacterMaps ($129C8 in the
JP image; $12BC8 in the US) gives each code's two tiles, and the tiles'
letters (his tools/ps2text.py's table, read off the JP font sheet) give
its character. FontsIconsArt is at $26800 in the JP image (the five
`lea (FontsIconsArt).l, a0` the US has at $73CC, $75BE, $853C, $87E6 and
$893E are at $723A, $742C, $83AA, $8654 and $87AC there).
"""

import argparse
import io
import json
import re
import sys
import zipfile
import zlib
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
import ps2png  # noqa: E402
from ps2art import decompress, tile_to_indices  # noqa: E402
from script_ends import end_where_the_us_ends, read_entries  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
STOCK_SCRIPT = ROOT / "assets" / "script"

JP_SIZE = 0xC0000
JP_CRC = 0xBEC8EB5A
JP_FONT = 0x26800           # FontsIconsArt
JP_CHARMAP = 0x129C8        # VDPCharacterMaps: a code's (upper, lower) tiles
# WindowArtLayoutPtrs ($13768 in the US), a window id's 8 bytes at id - 1:
# position, art pointer, width + 1, lines - 1 (his tools/jptables.py); the
# name board is window $24, its art in the image itself.
JP_WINDOWS = 0x13568
WIN_NAME_INPUT = 0x24
CODES = 0xD0                # the map's entries
DAKUTEN, HANDAKUTEN, BLANK = 0x5B, 0x5C, 0x26

# The JP font sheet's letters by tile (Excalibur_Z's tools/ps2text.py,
# _JP_TILES, "read off the JP font sheet"), and the voiced and half-voiced
# forms its dakuten and handakuten make.
TILE_ROWS = [
    (0x10, "BC"),
    (0x26, " あいうえおかきくけ"),
    (0x30, "こさしすせそたちつてとなにぬねの"),
    (0x40, "はひふへほまみむめもやゆよらりる"),
    (0x50, "れろ!?わをんっゃゅょ__、。ア"),
    (0x60, "イウエオカキクケコサシスセソタチ"),
    (0x70, "ツテトナニヌネノハヒフ・ホマミム"),
    (0x80, "メモヤユヨラリルレロワヲンァィ_"),
    (0x90, "ェォッャュョー012345678"),
    (0xA0, "9ADEHLMNOPSTVWXY"),
    (0xB0, "「」/▼"),
]
VOICED = dict(zip("かきくけこさしすせそたちつてとはひふへほ"
                  "カキクケコサシスセソタチツテトハヒフヘホウ",
                  "がぎぐげござじずぜぞだぢづでどばびぶべぼ"
                  "ガギグゲゴザジズゼゾダヂヅデドバビブベボヴ"))
HALF_VOICED = dict(zip("はひふへほハヒフヘホ", "ぱぴぷぺぽパピプペポ"))

# His notation to the port's, as import_retranslation.py's.
TOKENS = {
    "BR": "\n", "CLR": "{cont}", "PAGE": "{pause}",
    "END": "{end:C4}", "C5": "{end:C5}", "C6": "{end:C6}", "C7": "{end:C7}",
    "NAME": "{char}", "NAME2": "{char2}", "ENEMY": "{enemy}",
    "TECH": "{tech}", "ITEM": "{item}", "MESETA": "{meseta}",
}

# 0D05 (the Central Tower's report on Neifirst): both cartridges have no end
# code there, and run on into the Room bank's table bytes and its greeting;
# his `jp` keeps them. The port's ends it, as the bank's last entry, and as
# his English does: the Japanese up to the garbage page, ended.
GARBAGE_0D05 = "えはけうねまみせぬふねとのあやあ"


def die(msg):
    sys.exit(f"import_japanese: {msg}")


def read_rom(path):
    data = path.read_bytes()
    if zipfile.is_zipfile(io.BytesIO(data)):
        with zipfile.ZipFile(io.BytesIO(data)) as z:
            bins = [n for n in z.namelist()
                    if n.lower().endswith((".bin", ".md", ".gen", ".smd"))]
            if len(bins) != 1:
                die(f"{path}: not one ROM image in the zip ({bins})")
            data = z.read(bins[0])
    rom = data[:JP_SIZE]
    crc = zlib.crc32(rom)
    if len(rom) < JP_SIZE or crc != JP_CRC:
        die(f"{path}: its first 768 KB are CRC32 {crc:08X}, not the Japanese "
            f"cartridge's {JP_CRC:08X}")
    return rom


def charmap(rom):
    """Each code's (upper tile, lower tile), and the characters: code -> ch
    for every code whose tiles are a letter."""
    tiles = {}
    for start, chars in TILE_ROWS:
        for i, c in enumerate(chars):
            if c != "_":
                tiles[start + i] = c
    pairs, decode = {}, {}
    for code in range(CODES):
        top, bot = rom[JP_CHARMAP + 2 * code], rom[JP_CHARMAP + 2 * code + 1]
        pairs[code] = (top, bot)
        ch = tiles.get(bot)
        if ch is None:
            continue
        if top == DAKUTEN:
            ch = VOICED.get(ch)
        elif top == HANDAKUTEN:
            ch = HALF_VOICED.get(ch)
        elif top != BLANK:
            ch = None
        if ch is not None and ch not in decode.values():
            decode[code] = ch
    return pairs, decode


def write_face(rom, pairs, out):
    """font.png and font_upper.png: a tile a code, the cartridge's lower and
    upper tiles, ink (its colour 1) as 1 and everything else 0."""
    sheet, _ = decompress(rom, JP_FONT)
    if len(sheet) != 192:
        die(f"FontsIconsArt at {JP_FONT:#x} is {len(sheet)} tiles, not 192")

    def glyph(t):
        if t == BLANK or t >= len(sheet):
            return [0] * 64
        return [1 if v == 1 else 0 for v in tile_to_indices(sheet[t])]

    lower = [glyph(pairs[c][1]) if c in pairs else [0] * 64
             for c in range(256)]
    upper = [glyph(pairs[c][0]) if c in pairs else [0] * 64
             for c in range(256)]
    rgb = [(0, 0, 0), (238, 238, 238)]
    ps2png.write_tile_sheet(out / "font.png", lower, rgb, packed=False)
    ps2png.write_tile_sheet(out / "font_upper.png", upper, rgb, packed=False)


def name_board(rom, pairs, decode):
    """The name board's six rows of kana as its art has them (rows 5-15,
    two apart, the marks above each), 19 cells each, a space where a
    cell holds no letter: the letters' places, which the cursor stops on.
    ススム, モドル and オワル are the last row's words (advance, back, end)."""
    e = rom[JP_WINDOWS + 8 * (WIN_NAME_INPUT - 1):
            JP_WINDOWS + 8 * WIN_NAME_INPUT]
    ptr = int.from_bytes(e[2:6], "big")
    width, lines = e[6] - 1, e[7] + 1
    art = [rom[ptr + k * width:ptr + (k + 1) * width] for k in range(lines)]
    by_tiles = {pair: code for code, pair in pairs.items() if code in decode}
    rows = []
    for k in range(5, 16, 2):
        row = ""
        for c in range(width):
            code = by_tiles.get((art[k - 1][c], art[k][c]))
            row += decode[code] if code is not None else " "
        rows.append(row)
    return rows


def to_port(text, charset, where):
    """His notation to the port's, the characters kept (UTF-8): each must
    be one the cartridge has a code for."""
    out = []
    i = 0
    while i < len(text):
        ch = text[i]
        if ch == "{":
            j = text.index("}", i)
            tok = text[i + 1:j]
            if tok in TOKENS:
                out.append(TOKENS[tok])
            elif re.fullmatch(r"[0-9A-F]{2}", tok):
                out.append("{%s}" % tok)
            else:
                die(f"{where}: no token {{{tok}}}")
            i = j + 1
            continue
        if ch not in charset:
            die(f"{where}: {ch!r} has no code on the cartridge")
        out.append(ch)
        i += 1
    return "".join(out)


def entry_jp(e, by_id):
    """The Japanese the port's id shows: its own `jp`; "" for the second
    half of a pair the US split (`jp_cont`: the head's runs on into it, and
    on into the next, as his notes say the chain still works); 0D05 ended
    where the garbage starts."""
    if e.get("jp_cont"):
        return ""
    jp = e.get("jp", "")
    if e["id"] == "loc_1B6EC" and GARBAGE_0D05 in jp:
        jp = jp[:jp.index(GARBAGE_0D05)].rstrip()
        if jp.endswith("{PAGE}"):
            jp = jp[:-len("{PAGE}")]
        jp += "{END}"
    return jp


def write_scripts(repo, out, charset):
    d = json.loads((repo / "work" / "dialogue.json").read_text("utf-8"))
    by_id = {sid: e for e in d["entries"] for sid in e["ids"]}
    manifest = json.loads((STOCK_SCRIPT / "script.json").read_text())
    (out / "script").mkdir(parents=True, exist_ok=True)
    total = 0
    for name, b in sorted(manifest.items(), key=lambda kv: kv[1]["bank"]):
        bank, count = b["bank"], b["count"]
        parts = [f"# {name}, bank {bank:#04x}; script_id = {bank:#04x}NN,"
                 f" NN 1-based\n# The Japanese cartridge's "
                 f"(tools/import_japanese.py; docs/languages.md)\n"]
        texts = {}
        for n in range(1, count + 1):
            sid = "%02X%02X" % (bank, n)
            e = by_id.get(sid)
            if e is None or e.get("added"):
                die(f"{sid}: not in dialogue.json")
            texts[n] = to_port(entry_jp(e, by_id), charset, sid)
        # (build 168f) Each message ends where the US one does: 1101 and
        # 190B ran on into the next message, which the event code then
        # shows again.
        for k in end_where_the_us_ends(
                texts, read_entries(STOCK_SCRIPT / b["file"]), bank):
            print(f"{bank:02X}{k:02X}: ended where the US message ends")
        for n in range(1, count + 1):
            parts.append(f"\n[{n}]\n{texts[n]}\n")
            total += 1
        (out / "script" / b["file"]).write_text("".join(parts), "utf-8",
                                                newline="\n")
    (out / "script" / "script.json").write_text(
        json.dumps(manifest, indent=2) + "\n", newline="\n")
    return total


def clean_label(text):
    """A window row's label: its number placeholder ("たいりょく     0")
    gone -- the game writes the number there itself."""
    return re.sub(r"\s+\d+$", "", text).rstrip()


def name_text(text, charset, where):
    for ch in text:
        if ch not in charset:
            die(f"{where}: {ch!r} has no code on the cartridge")
    return text


def write_names(repo, out, charset):
    script = json.loads((repo / "work" / "script.json").read_text("utf-8"))
    items = json.loads((ROOT / "assets/data/items.json").read_text())
    techs = json.loads((ROOT / "assets/data/techniques.json").read_text())
    battle = json.loads((ROOT / "assets/data/battle.json").read_text())
    item_key = {it["id"]: it["key"] for it in items}
    tech_key = {t["id"]: t["key"] for t in techs["techniques"]}
    enemy_label = {e["id"]: e["label"] for e in battle["enemies"]}

    def runs(seg):
        return script["segments"][seg]["runs"]

    def num(r):
        return int(r["id"].split("#")[1])

    def jp(r):
        return (r.get("jp") or "").replace("{C2}", "ヴ")

    names = {"source": "work/script.json (jp)"}
    names["characters"] = [name_text(jp(r), charset, r["id"])
                           for r in runs("charnames")]
    names["items"] = {item_key[num(r)]: name_text(jp(r), charset, r["id"])
                      for r in runs("items") if num(r) in item_key
                      and num(r) != 0 and jp(r)}
    names["techniques"] = {tech_key[num(r)]: name_text(jp(r), charset,
                                                       r["id"])
                           for r in runs("techs") if num(r) in tech_key
                           and num(r) != 0 and jp(r)}
    names["enemies"] = {enemy_label[num(r)]: name_text(jp(r), charset,
                                                       r["id"])
                        for r in runs("enemies") if num(r) in enemy_label
                        and jp(r)}
    for key, seg in (("songs", "soundtracks"), ("towns", "teleport"),
                     ("jobs", "jobs"), ("prompts", "prompts"),
                     ("labels", "labels")):
        names[key] = [name_text(jp(r), charset, r["id"]) for r in runs(seg)]
    names["windows"] = {}
    for r in runs("windows"):
        rows = [clean_label(t) for t in jp(r).split("{BR}")]
        # The library's list has a title row the US window has not got.
        if r["label"] == "WinArt_LibraryOptions" and rows[0] == "リスト":
            rows = rows[1:]
        names["windows"][r["label"]] = [name_text(t, charset, r["id"])
                                        for t in rows]
    (out / "names.json").write_text(
        json.dumps(names, indent=2, ensure_ascii=False) + "\n", "utf-8",
        newline="\n")
    return names


def commit_of(repo):
    head = repo / ".git" / "HEAD"
    if not head.exists():
        return ""
    ref = head.read_text().strip()
    if not ref.startswith("ref: "):
        return ref
    path = repo / ".git" / ref[5:]
    if path.exists():
        return path.read_text().strip()
    packed = repo / ".git" / "packed-refs"
    for line in packed.read_text().splitlines() if packed.exists() else []:
        if line.endswith(" " + ref[5:]):
            return line.split()[0]
    return ""


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("rom", type=Path, help="the Japanese cartridge, or a zip")
    ap.add_argument("repo", type=Path, help="a ps2-retranslation checkout")
    ap.add_argument("-o", "--out", type=Path,
                    default=ROOT / "assets" / "lang" / "ja")
    args = ap.parse_args()

    rom = read_rom(args.rom)
    pairs, decode = charmap(rom)
    charset = {ch: code for code, ch in decode.items()}
    args.out.mkdir(parents=True, exist_ok=True)
    write_face(rom, pairs, args.out)
    total = write_scripts(args.repo, args.out, charset)
    names = write_names(args.repo, args.out, charset)
    names["board"] = name_board(rom, pairs, decode)
    (args.out / "names.json").write_text(
        json.dumps(names, indent=2, ensure_ascii=False) + "\n", "utf-8",
        newline="\n")

    lang = {
        "name": "JAPANESE",
        "about": "the Japanese cartridge's text in its own font: the "
                 "script and tables from Excalibur_Z's retranslation files' "
                 "`jp`, the font and character map from the cartridge "
                 "(docs/languages.md)",
        "source": "ps2-retranslation " + commit_of(args.repo)
                  + "; the Japanese cartridge, CRC32 BEC8EB5A",
        "proportional": True,
        "tall": True,
        "font": "font.png",
        "font_upper": "font_upper.png",
        "advance": [8] * 256,
        # Every character, ASCII too, is the cartridge's code.
        "charset_full": True,
        "charset": {ch: code for ch, code in sorted(charset.items(),
                                                    key=lambda kv: kv[1])},
    }
    (args.out / "lang.json").write_text(
        json.dumps(lang, indent=1, ensure_ascii=False) + "\n", "utf-8",
        newline="\n")
    (args.out / "NOTICE.txt").write_text(
        "The Japanese here is SEGA's, from Phantasy Star II: Kaerazaru Toki\n"
        "no Owari ni (1989; the cartridge CRC32 BEC8EB5A): the font and the\n"
        "character map read from the cartridge by the port's\n"
        "tools/import_japanese.py, and the text as Excalibur_Z's Phantasy\n"
        "Star II: English Retranslation (github.com/ExcalZ/ps2-retranslation)\n"
        "carries it -- decoded from that cartridge and matched to the US\n"
        "script ids by his tools (work/dialogue.json, work/script.json, the\n"
        "`jp` fields; his tools and his work are MIT, see\n"
        "../retranslation/LICENSE.txt). Where the port changes it: the\n"
        "Central Tower's 0D05 ended before the bytes both cartridges run on\n"
        "into, and the second halves of the messages the US split empty, the\n"
        "first half's text running on through them (docs/languages.md).\n",
        "utf-8", newline="\n")
    print(f"{total} script ids; {len(charset)} characters with codes; "
          f"names: {len(names['items'])} items, {len(names['techniques'])} "
          f"techniques, {len(names['enemies'])} enemies, "
          f"{len(names['windows'])} windows")


if __name__ == "__main__":
    main()
