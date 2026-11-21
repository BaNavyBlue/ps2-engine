#!/usr/bin/env python3
"""Generate src/game/script_ids.h: a SCRIPT_* name for every script id the
game sources use, from the extracted script text (assets/script/*.txt).
Run from the ps2/ directory. The header is checked in; rerun after adding
a script id to the code.

menu.c (build 97b) writes its ids in short form (0x01, 0x102) and passes
them through its own helpers, so there a literal counts as a script id
only inside a call that takes one (MENU_CALLS). `--apply` rewrites those
literals in menu.c to their names (the other files were rewritten in
build 96a)."""
import os, re, sys, tempfile

ROOT = os.getcwd()
FILES = ["src/game/game.c", "src/game/building.c", "src/game/npc.c"]
MENU = "src/game/menu.c"
# menu.c's calls that take a script id, and the hex literals inside them.
MENU_CALLS = ("win_script_set2", "win_script_set", "use_describe", "use_card",
              "use_on_door")
MENU_CALL_RE = re.compile(r"\b(" + "|".join(MENU_CALLS) + r")\s*\(")
HEX_RE = re.compile(r"\b0x[0-9A-Fa-f]+\b")


def menu_call_spans(src):
    """(start, end) of the argument text of every MENU_CALLS call,
    definitions and prototypes excluded (their arguments are types)."""
    spans = []
    for m in MENU_CALL_RE.finditer(src):
        line_start = src.rfind("\n", 0, m.start()) + 1
        if src[line_start:m.start()].strip().startswith(("static", "void", "bool")):
            continue
        depth, i = 1, m.end()
        while depth and i < len(src):
            if src[i] == "(":
                depth += 1
            elif src[i] == ")":
                depth -= 1
            i += 1
        spans.append((m.end(), i - 1))
    return spans


def menu_ids(src, scripts):
    out = []
    for a, b in menu_call_spans(src):
        for h in HEX_RE.finditer(src, a, b):
            v = int(h.group(0), 16)
            if v in scripts:
                out.append((h.start(), h.end(), v))
    return out
HEADER = "src/game/script_ids.h"

BANK_PREFIX = {
    0x00: "ITEM", 0x01: "TECH", 0x02: "EQUIP", 0x03: "MEMORY", 0x04: "CLONE",
    0x05: "HOSPITAL", 0x06: "WEAPONS", 0x07: "ARMOR", 0x08: "ITEMS",
    0x09: "HOME", 0x0A: "USTVESTIA", 0x0B: "INVENTOR", 0x0C: "TELEPORT",
    0x0D: "TOWER", 0x0E: "ROOM", 0x0F: "ROOF", 0x10: "LIBRARY",
    0x11: "GOVERNOR", 0x12: "BATTLE", 0x13: "INTRO", 0x14: "OPENING",
    0x15: "START", 0x16: "PEOPLE", 0x17: "ACTION", 0x18: "EVENT",
    0x19: "MISC",
}

# Names the code already had by hand (game.c, build 92-93): kept.
HAND = {
    0x181E: "LUTZ_REVIVES_THEM", 0x183B: "STOOD_IN_AMAZEMENT",
    0x181F: "BACK_TO_MOTA", 0x1831: "LUTZ_WAIT",
    0x1832: "WHO_ARE_THESE_PEOPLE", 0x1819: "WELCOME_TO_NOAH",
    0x181A: "WE_HATE_YOU", 0x181B: "ALGO_PROSPERED",
    0x181C: "HARDLY_EXPECTED", 0x1833: "WE_ARE_FROM_EARTH",
    0x1834: "WE_WERE_WEAKER", 0x1837: "DIE_ANYWAY",
    0x1838: "WE_WILL_TELL_YOU", 0x183F: "SETTLE_FOR_THIS_STAR",
    0x1821: "SENT_ON_THEIR_WAY",
}

def load_scripts():
    out = {}
    for f in os.listdir("assets/script"):
        if not f.endswith(".txt"):
            continue
        lines = open(os.path.join("assets/script", f), encoding="utf-8").read().split("\n")
        m = re.search(r"bank (0x[0-9A-Fa-f]+)", lines[0])
        if not m:
            continue
        bank = int(m.group(1), 16)
        cur = None
        buf = []
        for ln in lines[1:]:
            mm = re.match(r"^\[(\d+)\]$", ln)
            if mm:
                if cur is not None:
                    out[(bank << 8) | cur] = "\n".join(buf).strip()
                cur = int(mm.group(1))
                buf = []
            elif cur is not None:
                buf.append(ln)
        if cur is not None:
            out[(bank << 8) | cur] = "\n".join(buf).strip()
    return out

STOP = {"the", "a", "an", "to", "of", "is", "are", "in", "and", "it", "you",
        "i", "for", "that", "this", "on", "at", "be", "have", "has", "was",
        "with", "as", "your", "my", "we", "he", "she", "they", "s"}

def words_of(text):
    t = re.sub(r"\{[^}]*\}", " ", text)
    t = t.replace("\\I", " ")           # the quote control code
    t = t.replace("'", "")
    t = re.sub(r"[^A-Za-z0-9 ]", " ", t)
    ws = [w for w in t.split() if w]
    return ws

def make_name(bank, text):
    ws = words_of(text)
    keep = [w for w in ws if w.lower() not in STOP]
    if len(keep) < 2:
        keep = ws
    keep = keep[:4]
    if not keep:
        return None
    return BANK_PREFIX[bank] + "_" + "_".join(w.upper() for w in keep)

def main():
    scripts = load_scripts()
    used = set()
    # The header as it stands: a name already given stays, and a name
    # the sources use by itself still gets its line.
    known = {}
    if os.path.exists(HEADER):
        for ln in open(HEADER, encoding="utf-8"):
            m = re.match(r"#define SCRIPT_([A-Z0-9_]+)\s+0x([0-9A-F]{4})", ln)
            if m and m.group(1) != "NONE":     # written by hand below
                known[m.group(1)] = int(m.group(2), 16)
    pat = re.compile(r"\b0x(0[0-9A-Fa-f]|1[0-9])[0-9A-Fa-f]{2}\b")
    npat = re.compile(r"\bSCRIPT_([A-Z0-9_]+)\b")
    for f in FILES:
        for ln in open(f, encoding="utf-8"):
            if ln.lstrip().startswith("#define"):
                continue
            for m in npat.finditer(ln):
                if m.group(1) in known:
                    used.add(known[m.group(1)])
            for m in pat.finditer(ln):
                v = int(m.group(0), 16)
                # Bank 0's ids are small numbers that also occur as palette
                # words in tables: those count only on a line that names a
                # script.
                if (v >> 8) == 0 and "script" not in ln:
                    continue
                if v in scripts:
                    used.add(v)
    menu_src = open(MENU, encoding="utf-8").read()
    for m in npat.finditer(menu_src):
        if m.group(1) in known:
            used.add(known[m.group(1)])
    for _, _, v in menu_ids(menu_src, scripts):
        used.add(v)
    used |= set(HAND)
    by_id = {v: n for n, v in known.items()}
    names = {}
    taken = set()
    for v in sorted(used):
        bank = v >> 8
        if v in HAND:
            n = HAND[v]
        elif v in by_id:
            n = by_id[v]
        else:
            n = make_name(bank, scripts[v]) or (BANK_PREFIX[bank] + "_%02X" % (v & 0xFF))
        base = n
        k = 2
        while n in taken:
            n = "%s_%d" % (base, k)
            k += 1
        taken.add(n)
        names[v] = n
    # Written aside and moved into place: a failure part way must not
    # leave a cut header, whose names the next run would lose (97c).
    with open(HEADER + ".tmp", "w", encoding="utf-8", newline="\n") as out:
        out.write("#ifndef PS2_SCRIPT_IDS_H\n#define PS2_SCRIPT_IDS_H\n\n")
        out.write("/* Script ids by name: the window text blocks (GameScriptPtrs,\n"
                  " * the .txt files under assets/script), $BBNN: bank BB, entry NN (1-based).\n"
                  " * The name is the bank then the line's first words; the comment\n"
                  " * is the line as the game shows it. Generated by\n"
                  " * tools/gen_script_ids.py from the ids the sources use (build 96);\n"
                  " * rerun it after using a new one. */\n\n")
        out.write("/* No script: script_id cleared (move.w #0, (script_id).w). */\n")
        out.write("#define SCRIPT_%-36s 0x%04X\n" % ("NONE", 0))
        last_bank = None
        for v in sorted(used):
            bank = v >> 8
            if bank != last_bank:
                out.write("\n/* bank $%02X: %s */\n" % (bank, BANK_PREFIX[bank]))
                last_bank = bank
            text = re.sub(r"\s+", " ", re.sub(r"\{[^}]*\}", "", scripts[v])).strip()
            text = text.replace("*/", "* /")
            if len(text) > 60:
                text = text[:57] + "..."
            out.write("#define SCRIPT_%-36s 0x%04X  /* %s */\n" % (names[v], v, text))
        out.write("\n#endif /* PS2_SCRIPT_IDS_H */\n")
    os.replace(HEADER + ".tmp", HEADER)
    # the replacement map for the sources ((build 113) on Windows "/tmp" is
    # C:/tmp, which is not there: the temporary directory instead)
    tmp = tempfile.gettempdir() if os.name == "nt" else "/tmp"
    with open(os.path.join(tmp, "script_map.txt"), "w", newline="\n") as m:
        for v in sorted(used):
            m.write("%04X SCRIPT_%s\n" % (v, names[v]))
    print(len(used), "script ids named")
    if "--apply" in sys.argv:
        edits = menu_ids(menu_src, scripts)
        for a, b, v in reversed(edits):
            menu_src = menu_src[:a] + "SCRIPT_" + names[v] + menu_src[b:]
        open(MENU, "w", encoding="utf-8", newline="\n").write(menu_src)
        print(len(edits), "literals in", MENU, "named")

if __name__ == "__main__":
    main()
