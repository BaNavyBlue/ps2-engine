#!/usr/bin/env python3
"""How much of ps2.asm's 68000 code the port has taken over: a static count.

The port cites the ROM's routines by their labels in its comments
("SetCharEquipment", "loc_70D6"), so a routine's label in src/ is the
record that it was read and ported. This tool splits ps2.asm's code into
routines and asks, for each, whether src/ names it.

  instructions  every 68000 instruction line (the `_btst`-style macros
                count, the data macros -- nametxt, border, ... -- do not)
  reachable     the instructions control can reach from the vectors and
                from every code label data or code takes the address of
                (dc.l tables, `lea`, `move.l #`, `jmp Table(pc,d0.w)`);
                a table made only of `bra` lines is entered at every line
  routine       an entry -- a `bsr`/`jsr` target or an address taken --
                up to the next entry in the file, with every branch target
                and table between them; a routine's instructions are what
                it holds, so the counts add up to the whole
  cited         src/ names the routine's entry or one of its inner labels
                (or a "ps2.asm:N" line inside it) on a line that does not
                say "not ported"

What it cannot say: whether a cited routine is ported in full (a comment
can cite one branch of it), or whether an uncited one was ported under
another name or replaced by the port's own design (the VDP, the DMA
queue, the Z80's bus). The uncited list is where reading starts, and
tools/rom_left_review.tsv is that reading (build 181): one line an uncited
routine -- PORTED, REPLACED, DEAD or MISSING, where, and what it does. The
summary counts the classes; a routine uncited and not in the review, or
in the review and cited now, is listed so the file is kept up to date.

    python3 tools/rom_left.py [--asm ../PSII_Disasm/ps2.asm] [--out DIR]

writes DIR/routines.tsv (one line a routine) and DIR/summary.txt; exit 1
when the review is out of step with the count.
"""

import argparse
import re
import sys
from collections import defaultdict
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

# AS: a label in column 0, with or without its colon; `+`, `-` and `/`
# are the nameless ones (`/` answers both a `+` and a `-` reference).
LABEL_RE = re.compile(r"^([A-Za-z_][A-Za-z0-9_]*)(?::|(?=\s)|$)")
NAMELESS_RE = re.compile(r"^([+\-/]+)(?:\s|$)")
IDENT_RE = re.compile(r"(?<![\w$.@])([A-Za-z_][A-Za-z0-9_]*)")

CONDITIONS = ["cc", "cs", "eq", "ge", "gt", "hi", "le", "ls", "lt", "mi",
              "ne", "pl", "vc", "vs", "hs", "lo"]
COND_BRANCHES = {"b" + c for c in CONDITIONS}
DB_BRANCHES = {"db" + c for c in CONDITIONS} | {"dbf", "dbt", "dbra"}
SETS = {"s" + c for c in CONDITIONS} | {"sf", "st"}

MNEMONICS = {
    "abcd", "add", "adda", "addi", "addq", "addx", "and", "andi", "asl",
    "asr", "bchg", "bclr", "bra", "bset", "bsr", "btst", "chk", "clr",
    "cmp", "cmpa", "cmpi", "cmpm", "divs", "divu", "eor", "eori", "exg",
    "ext", "illegal", "jmp", "jsr", "lea", "link", "lsl", "lsr", "move",
    "movea", "movem", "movep", "moveq", "muls", "mulu", "nbcd", "neg",
    "negx", "nop", "not", "or", "ori", "pea", "reset", "rol", "ror",
    "roxl", "roxr", "rte", "rtr", "rts", "sbcd", "stop", "sub", "suba",
    "subi", "subq", "subx", "swap", "tas", "trap", "trapv", "tst", "unlk",
} | COND_BRANCHES | DB_BRANCHES | SETS

# ps2.macros.asm / macrosetup.asm: these expand to one instruction.
INSN_MACROS = {"_btst", "_bset", "_bclr", "_bchg", "_move", "_andi", "_ori",
               "_cmpi", "_tst"}

DATA_OPS = {"dc", "dcb", "ds", "binclude", "even", "align", "cnop",
            "nametxt", "border", "cursorbox", "charset", "soundtracktxt"}

TERMINATORS = {"rts", "rte", "rtr", "bra", "jmp", "illegal"}


def strip_comment(text):
    """The line up to its first `;` outside quotes."""
    quote = None
    for i, ch in enumerate(text):
        if quote:
            if ch == quote:
                quote = None
        elif ch in "\"'":
            quote = ch
        elif ch == ";":
            return text[:i]
    return text


def split_operands(text):
    """Operands split on commas outside parentheses."""
    out, depth, cur = [], 0, []
    for ch in text:
        if ch == "(":
            depth += 1
        elif ch == ")":
            depth -= 1
        if ch == "," and depth == 0:
            out.append("".join(cur).strip())
            cur = []
        else:
            cur.append(ch)
    if cur:
        out.append("".join(cur).strip())
    return out


class Line:
    __slots__ = ("no", "labels", "nameless", "op", "operands", "kind")

    def __init__(self, no):
        self.no = no            # 0-based line index
        self.labels = []        # named labels defined on this line
        self.nameless = None    # "+", "-", "/", "--", ...
        self.op = None          # mnemonic, lower case, no size
        self.operands = ""      # operand text
        self.kind = "none"      # insn, data, other, none


def parse(asm_path):
    raw = asm_path.read_text(errors="replace").splitlines()
    lines = []
    unknown = defaultdict(int)
    in_macro = 0
    for no, text in enumerate(raw):
        ln = Line(no)
        body = strip_comment(text).rstrip()
        m = NAMELESS_RE.match(body)
        if m:
            ln.nameless = m.group(1)
            body = body[m.end():]
        else:
            m = LABEL_RE.match(body)
            if m:
                ln.labels.append(m.group(1))
                body = body[m.end():]
        body = body.strip()
        if body:
            parts = body.split(None, 1)
            op = parts[0].lower()
            # `name macro args` / `name function ...` in column 0 were
            # taken as labels above: the second word is the directive.
            if op in ("macro", "function", "equ", "=", "set"):
                if op == "macro":
                    in_macro += 1
                ln.labels = []
                ln.kind = "other"
                lines.append(ln)
                continue
            if op == "endm" and in_macro:
                in_macro -= 1
                ln.kind = "other"
                lines.append(ln)
                continue
            base = op.split(".")[0]
            ln.op = base
            ln.operands = parts[1].strip() if len(parts) > 1 else ""
            if in_macro:
                ln.kind = "other"
            elif base in MNEMONICS or base in INSN_MACROS:
                ln.kind = "insn"
            elif base in DATA_OPS:
                ln.kind = "data"
            else:
                ln.kind = "other"
                unknown[base] += 1
        lines.append(ln)
    return raw, lines, unknown


def nameless_target(lines, at, ref):
    """The line a `+`/`-` reference from line `at` lands on: `+` the next
    `+` (or `/`) line, `++` the one after it, `-` and `--` backwards."""
    if ref[0] == "+":
        rng = range(at + 1, len(lines))
    else:
        rng = range(at - 1, -1, -1)
    left = len(ref)
    for i in rng:
        name = lines[i].nameless
        if name == ref[0] or name == "/":
            left -= 1
            if left == 0:
                return i
    return None


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--asm", type=Path,
                    default=ROOT.parent / "PSII_Disasm" / "ps2.asm")
    ap.add_argument("--src", type=Path, default=ROOT / "src")
    ap.add_argument("--out", type=Path, default=ROOT / "route_out" / "rom_left")
    ap.add_argument("--review", type=Path,
                    default=ROOT / "tools" / "rom_left_review.tsv")
    args = ap.parse_args()

    raw, lines, unknown = parse(args.asm)

    label_line = {}
    for ln in lines:
        for name in ln.labels:
            label_line.setdefault(name, ln.no)

    def next_content(i):
        """The first line at or after i that holds an instruction or data."""
        while i < len(lines) and lines[i].kind not in ("insn", "data"):
            i += 1
        return i if i < len(lines) else None

    def first_insn_of(name):
        """The instruction line a code label stands on, else None."""
        i = next_content(label_line[name])
        if i is not None and lines[i].kind == "insn":
            return i
        return None

    code_labels = {n for n in label_line if first_insn_of(n) is not None}

    # Each instruction's static successors, calls and the labels it takes
    # the address of.
    succ = defaultdict(list)
    calls = defaultdict(list)
    addr_taken = defaultdict(set)
    computed = defaultdict(set)
    indirect = set()

    def resolve(operand, at):
        """A branch operand's target line (an instruction), or None."""
        o = operand.strip()
        if NAMELESS_RE.match(o + " "):
            t = nameless_target(lines, at, o)
            return next_content(t) if t is not None else None
        m = re.fullmatch(r"\(?([A-Za-z_][A-Za-z0-9_]*)\)?(?:\.[wl])?"
                         r"(?:\(pc\))?", o, re.IGNORECASE)
        if m and m.group(1) in code_labels:
            return first_insn_of(m.group(1))
        return None

    data_refs = set()
    for ln in lines:
        if ln.kind == "data":
            for ident in IDENT_RE.findall(ln.operands):
                if ident in code_labels:
                    data_refs.add(ident)
            continue
        if ln.kind != "insn":
            continue
        ops = split_operands(ln.operands)
        op = ln.op
        nxt = next_content(ln.no + 1)
        if op in COND_BRANCHES or op in DB_BRANCHES:
            t = resolve(ops[-1], ln.no)
            if t is not None:
                succ[ln.no].append(t)
            if nxt is not None:
                succ[ln.no].append(nxt)
            continue
        if op in ("bra", "bsr", "jmp", "jsr"):
            target = ops[0] if ops else ""
            m = re.fullmatch(r"([A-Za-z_][A-Za-z0-9_]*)\s*"
                             r"(?:[-+]\s*\$?[0-9A-Fa-f]+\s*)?"
                             r"\(\s*pc\s*,.*\)", target, re.IGNORECASE)
            if m:
                computed[ln.no].add(m.group(1))
                addr_taken[ln.no].add(m.group(1))
                t = None
            else:
                t = resolve(target, ln.no)
                if t is None:
                    indirect.add(ln.no)
            if op in ("bra", "jmp"):
                if t is not None:
                    succ[ln.no].append(t)
            else:
                if t is not None:
                    calls[ln.no].append(t)
                if nxt is not None:
                    succ[ln.no].append(nxt)
            continue
        for ident in IDENT_RE.findall(ln.operands):
            if ident in code_labels:
                addr_taken[ln.no].add(ident)
        if op not in TERMINATORS and nxt is not None:
            if lines[nxt].kind == "insn":
                succ[ln.no].append(nxt)

    # A table of `bra.w X` or `jmp (X).l` + `nop` lines is entered at
    # every line (jsr Table(pc,d0.w) lands on one of them); collect each
    # label's run of branches.
    def branch_table(name):
        i = first_insn_of(name)
        run = []
        while i is not None and lines[i].kind == "insn" and \
                lines[i].op in ("bra", "jmp", "nop"):
            if lines[i].op != "nop":
                run.append(i)
            i = next_content(i + 1)
        return run if len(run) >= 2 else [first_insn_of(name)]

    # Reachability over instructions.
    roots = set()
    for name in data_refs:
        roots.update(branch_table(name))
    reach = set()
    work = list(roots)
    while work:
        i = work.pop()
        if i in reach:
            continue
        reach.add(i)
        for t in succ[i] + calls[i]:
            if t not in reach:
                work.append(t)
        for name in addr_taken[i]:
            for t in branch_table(name):
                if t not in reach:
                    work.append(t)

    # Routine entries: call targets, taken addresses, and what a branch
    # table's lines branch to (the table itself stays with the routine
    # that dispatches through it).
    entry_lines = set()
    for i in range(len(lines)):
        for t in calls[i]:
            entry_lines.add(t)

    def enter(name):
        run = branch_table(name)
        if len(run) >= 2:
            for t in run:
                entry_lines.update(succ[t])
        else:
            entry_lines.update(run)

    for name in data_refs:
        enter(name)
    for i in list(addr_taken):
        for name in addr_taken[i]:
            enter(name)
    entry_lines.discard(None)

    insn_lines = [ln.no for ln in lines if ln.kind == "insn"]

    # The label a routine is known by: the first named label on or above
    # its entry instruction with nothing but labels and blanks between.
    def labels_above(i):
        names = []
        j = i
        while j >= 0:
            names = lines[j].labels + names
            if j < i and lines[j].kind in ("insn", "data"):
                names = names[len(lines[j].labels):]
                break
            j -= 1
        return names

    routines = []
    cur = None
    for i in insn_lines:
        if i in entry_lines or cur is None:
            names = labels_above(i)
            cur = {"line": i, "name": names[0] if names else
                   "line_%d" % (i + 1), "insns": [], "labels": []}
            routines.append(cur)
        cur["insns"].append(i)
    # A routine's labels: the code labels from just after the content
    # line before it to its last instruction (a table's label inside it is
    # data, and citing the table is not citing the code).
    for r in routines:
        first, last = r["insns"][0], r["insns"][-1]
        r["end"] = last
        j = first - 1
        while j >= 0 and lines[j].kind not in ("insn", "data"):
            j -= 1
        r["labels"] = [n for k in range(j + 1, last + 1)
                       for n in lines[k].labels if n in code_labels]

    # What a tree names: labels, and ps2.asm line numbers ("ps2.asm:91087",
    # "ps2.asm:91087-91248"), which smps.c and others cite instead.
    names_wanted = set(label_line)
    asm_line_re = re.compile(r"ps2\.asm[: ]+(\d+)(?:-(\d+))?")

    def citations(top, suffixes, skip=(), prefix=""):
        names = defaultdict(set)
        spans = []                  # (first, last, file), 0-based lines
        refused = defaultdict(set)
        for path in sorted(top.rglob("*")):
            if path.suffix not in suffixes or "__pycache__" in path.parts:
                continue
            if path.relative_to(top).parts[0] in skip:
                continue
            rel = prefix + path.relative_to(top).as_posix()
            for text in path.read_text(errors="replace").splitlines():
                low = text.lower()
                bad = "not ported" in low or "unported" in low
                for w in re.findall(r"[A-Za-z_][A-Za-z0-9_]*", text):
                    if w in names_wanted:
                        (refused if bad else names)[w].add(rel)
                if not bad:
                    for m in asm_line_re.finditer(text):
                        a = int(m.group(1)) - 1
                        b = int(m.group(2)) - 1 if m.group(2) else a
                        spans.append((a, b, rel))
        return names, spans, refused

    # (build 192) src/extract/ is the extractors in C, the tools' twins that
    # make the game's files from the cartridge (docs/first_start.md, "In
    # C"), not the port of the game's code: what it names counts with the
    # tools' citations, not src/'s (builds 188-190's extractors named five
    # routines the review has as the tools' -- NemDec, DecompressArt2, ...).
    cited, cited_spans, not_ported = citations(
        args.src, (".c", ".h", ".cpp", ".hpp", ".hlsl"), skip=("extract",))
    tools_cited, tools_spans, _ = citations(
        ROOT / "tools", (".py", ".c", ".cpp", ".h"))
    extract_cited, extract_spans, _ = citations(
        args.src / "extract", (".c", ".h"), prefix="../src/extract/")
    for name, files in extract_cited.items():
        tools_cited[name] |= files
    tools_spans += extract_spans

    def cited_files(r, names, spans):
        files = {f for n in r["labels"] for f in names[n]}
        # A line citation counts where it lands inside the routine's lines
        # (from its labels to its last instruction).
        lo = label_line[r["labels"][0]] if r["labels"] else r["line"]
        lo = min(lo, r["line"])
        for a, b, f in spans:
            if a <= r["end"] and b >= lo:
                files.add(f)
        return sorted(files)

    # The lower bound: each code label's own stretch (to the next code
    # label), counted only when src/ names that label or a line in it.
    span_hits = sorted((a, b) for a, b, _ in cited_spans)
    stretch_cited = 0
    stretch_labels = 0
    starts = sorted((first_insn_of(n), n) for n in code_labels)
    for k, (first, name) in enumerate(starts):
        stop = starts[k + 1][0] if k + 1 < len(starts) else len(lines)
        n = sum(1 for i in range(first, stop) if lines[i].kind == "insn")
        hit = bool(cited[name]) or any(
            a < stop and b >= label_line[name] for a, b in span_hits)
        if hit:
            stretch_cited += n
            stretch_labels += 1

    sound_base = label_line.get("SoundDriverBase", len(lines))
    for r in routines:
        r["cited_by"] = cited_files(r, cited, cited_spans)
        r["cited"] = bool(r["cited_by"])
        r["tools_by"] = cited_files(r, tools_cited, tools_spans)
        r["reach"] = sum(1 for i in r["insns"] if i in reach)
        r["np"] = any(n in not_ported and n not in cited for n in r["labels"])
        r["area"] = "sound" if r["line"] >= sound_base else "game"

    args.out.mkdir(parents=True, exist_ok=True)
    with open(args.out / "routines.tsv", "w", newline="\n") as f:
        f.write("line\tend\troutine\tarea\tinstructions\treachable\tcited\t"
                "said_not_ported\tlabels\tcited_by\ttools_cite\n")
        for r in routines:
            f.write("%d\t%d\t%s\t%s\t%d\t%d\t%s\t%s\t%s\t%s\t%s\n" % (
                r["line"] + 1, r["end"] + 1, r["name"], r["area"],
                len(r["insns"]), r["reach"], "yes" if r["cited"] else "no",
                "yes" if r["np"] else "no", ",".join(r["labels"]),
                ",".join(r["cited_by"]), ",".join(r["tools_by"])))

    total = len(insn_lines)
    reach_n = len(reach & set(insn_lines))
    out = []
    out.append("ps2.asm: %d lines, %d instruction lines, %d reachable, "
               "%d not" % (len(lines), total, reach_n, total - reach_n))
    out.append("computed jumps: %d, indirect jumps/calls: %d" % (
        len(computed), len(indirect)))

    def tally(label, rs):
        n_i = sum(len(r["insns"]) for r in rs)
        n_r = sum(r["reach"] for r in rs)
        out.append("  %-34s %5d routines %6d instructions %6d reachable" % (
            label, len(rs), n_i, n_r))

    for area in ("game", "sound", None):
        rs = [r for r in routines if area is None or r["area"] == area]
        out.append("%s:" % (area or "all"))
        tally("routines", rs)
        tally("cited by src/", [r for r in rs if r["cited"]])
        unc = [r for r in rs if not r["cited"]]
        tally("not cited", unc)
        tally("not cited, reachable", [r for r in unc if r["reach"]])
        tally("not cited, reachable, tools cite",
              [r for r in unc if r["reach"] and r["tools_by"]])
    out.append("lower bound: %d of %d code labels named in src/ "
               "themselves; their own stretches %d instructions" % (
                   stretch_labels, len(code_labels), stretch_cited))

    # The reading of the uncited routines, by routine name.
    review = {}
    if args.review.exists():
        for row in args.review.read_text().splitlines()[1:]:
            if row.strip() and not row.startswith("#"):
                cols = row.split("\t")
                review[cols[0]] = cols[1]
    by_name = {r["name"]: r for r in routines}
    unread = [r for r in routines
              if not r["cited"] and r["reach"] and r["name"] not in review]
    stale = sorted(n for n in review
                   if n not in by_name or by_name[n]["cited"])
    if review:
        out.append("the review (%s), uncited reachable routines by class:"
                   % args.review.name)
        for cls in sorted(set(review.values())):
            rs = [by_name[n] for n, c in review.items()
                  if c == cls and n in by_name and not by_name[n]["cited"]]
            tally(cls, rs)
    for r in unread:
        out.append("NOT IN THE REVIEW: %s (ps2.asm:%d, %d instructions)" % (
            r["name"], r["line"] + 1, len(r["insns"])))
    for n in stale:
        out.append("IN THE REVIEW BUT %s: %s" % (
            "CITED NOW" if n in by_name else "NO SUCH ROUTINE", n))
    directives = {"if", "endif", "else", "rept", "endm", "include", "cpu",
                  "end", "fatal", "org", "outradix", "padding", "padrom",
                  "save", "restore", ":="}
    odd = {k: v for k, v in unknown.items() if k not in directives}
    if odd:
        out.append("unknown ops (not counted): " + ", ".join(
            "%s %d" % kv for kv in sorted(odd.items())))
    (args.out / "summary.txt").write_text("\n".join(out) + "\n",
                                          newline="\n")
    print("\n".join(out))
    return 1 if (review and (unread or stale)) else 0


if __name__ == "__main__":
    sys.exit(main())
