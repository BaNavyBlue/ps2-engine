"""Assembling ps2.asm's data tables to bytes, for the JSON round-trip.

ps2asm.Asm reads `$hex` tokens, which is all the pixel and map formats
need. The data tables are written with symbols -- `dc.b ItemID_Knife`,
`nametxt "KNIFE"`, `dc.l $03000000|loc_28664`, `dc.w loc_1234-Table` --
so their bytes need the constants evaluated the way the assembler does.
This module is that evaluation for data, and no more:

  * the constants file, read lazily: `NAME = expr`, with `id(ptr)` worked
    out from the table's own layout -- the byte distance from the table's
    `offset` label to `ptr`, over `ptrsize`, plus `idstart`, with the
    `:=` values in force on the constant's line -- rather than from the
    `; 3` comment or the order of the lines (the extractors use the order);
  * dc.b / dc.w / dc.l, dcb, nametxt (with `length` as set above it in
    ps2.asm), cursorbox, border, `even`, and `charset` as the assembler
    applies it to strings and character constants;
  * an address that is not a constant stays symbolic: `Ptr(label, addend)`,
    one cell per byte of its field, so a pointer compares by the label it
    names. A difference of two labels is worked out from the layout.

Region.cells is the table as a list: an int per byte, or a PtrCell for a
byte of a pointer field. Anything the reader does not understand (an
instruction inside a table, a macro it does not know) raises, so a
table it cannot read is reported, never guessed.
"""

import re
from dataclasses import dataclass
from pathlib import Path

from ps2asm import Asm

LABEL_RE = re.compile(r"^([A-Za-z_][A-Za-z0-9_]*)(?::|\s*$|\s+(?=[A-Za-z]))")
ASSIGN_RE = re.compile(r"^([A-Za-z_][A-Za-z0-9_]*)\s*(:=|=|\bequ\b|\bset\b)\s*(.+)$",
                       re.IGNORECASE)
DIRECTIVE_RE = re.compile(
    r"^(?:([A-Za-z_][A-Za-z0-9_]*):)?\s+([A-Za-z_.][A-Za-z0-9_.]*)\s*(.*)$")


class AsmDataError(Exception):
    pass


class NotData(AsmDataError):
    """The line is an instruction or a directive the reader does not
    assemble: where a region read with until_code ends."""


@dataclass(frozen=True)
class Sym:
    """An address the reader cannot place: a label plus a constant."""
    label: str
    addend: int = 0


@dataclass(frozen=True)
class PtrCell:
    """Byte `k` (0 = most significant) of a `width`-byte field that holds
    the address `sym`."""
    sym: Sym
    width: int
    k: int

    def __str__(self):
        add = f"+${self.sym.addend:X}" if self.sym.addend else ""
        return f"<{self.sym.label}{add}>[{self.k}/{self.width}]"


def strip_comment(text):
    """Drop a `;` comment, leaving `;` inside quotes alone."""
    quote = None
    for i, ch in enumerate(text):
        if quote:
            if ch == "\\":
                continue
            if ch == quote:
                quote = None
        elif ch in "\"'":
            quote = ch
        elif ch == ";":
            return text[:i]
    return text


def split_operands(text):
    out, depth, quote, cur = [], 0, None, ""
    i = 0
    while i < len(text):
        ch = text[i]
        if quote:
            cur += ch
            if ch == "\\" and i + 1 < len(text):
                cur += text[i + 1]
                i += 1
            elif ch == quote:
                quote = None
        elif ch in "\"'":
            quote = ch
            cur += ch
        elif ch == "(":
            depth += 1
            cur += ch
        elif ch == ")":
            depth -= 1
            cur += ch
        elif ch == "," and depth == 0:
            out.append(cur.strip())
            cur = ""
        else:
            cur += ch
        i += 1
    if cur.strip():
        out.append(cur.strip())
    return out


STRING_ESCAPES = {"I": ord('"')}     # AS's quote escape (build 129a)


def unescape(body):
    """An AS string body: `\\11` is the character 11 (decimal), `\\I` the
    double quote."""
    out, i = [], 0
    while i < len(body):
        ch = body[i]
        if ch == "\\" and i + 1 < len(body):
            m = re.match(r"\d+", body[i + 1:])
            if m:
                out.append(int(m.group(0)))
                i += 1 + len(m.group(0))
                continue
            out.append(STRING_ESCAPES.get(body[i + 1], ord(body[i + 1])))
            i += 2
            continue
        out.append(ord(ch))
        i += 1
    return out


TOKEN_RE = re.compile(r"""
    \s*(?:
      (?P<hex>\$[0-9A-Fa-f]+)
    | (?P<bin>%[01]+)
    | (?P<dec>\d+)
    | (?P<chr>'(?:\\.|[^'])')
    | (?P<name>[A-Za-z_][A-Za-z0-9_]*)
    | (?P<op><<|>>|[-+*/|&^~!()])
    )""", re.VERBOSE)


class Rom:
    """ps2.asm and the constants, readable as data."""

    def __init__(self, asm_path, const_path, art_dir=None):
        self.asm = asm_path if isinstance(asm_path, Asm) else Asm(Path(asm_path))
        self.lines = self.asm.lines
        self.art_dir = Path(art_dir) if art_dir else None
        self._const_defs = {}
        self._const_cache = {}
        self._read_constants(Path(const_path))
        self._charset_events = []
        self._assign_events = []
        for i, line in enumerate(self.lines):
            body = strip_comment(line).strip()
            if re.match(r"charset\b", body):
                self._charset_events.append((i, body[7:].strip()))
            m = ASSIGN_RE.match(strip_comment(line).rstrip())
            if m and not line[:1].isspace():
                self._assign_events.append((i, m.group(1), m.group(2),
                                            m.group(3).strip()))
                if m.group(2) != ":=":
                    self._const_defs.setdefault(
                        m.group(1), (m.group(3).strip(), {}, "asm"))
        self._charset_cache = {}
        # AS matches symbols without regard to case unless told otherwise:
        # ItemID_Crystanish is id(Item_crystanish), the label
        # Item_Crystanish. An exact match first, then this.
        self._label_ci = {}
        for name in self.asm.label_line:
            self._label_ci.setdefault(name.lower(), name)

    def label(self, name):
        """The label as written in ps2.asm for a symbol, any case."""
        if name in self.asm.label_line:
            return name
        return self._label_ci.get(name.lower())

    # -- constants ------------------------------------------------------

    def _read_constants(self, path):
        ctx = {}
        for line in path.read_text(errors="replace").splitlines():
            body = strip_comment(line).rstrip()
            m = ASSIGN_RE.match(body)
            if not m:
                continue
            name, op, expr = m.group(1), m.group(2).lower(), m.group(3).strip()
            if op in (":=", "set"):
                ctx = dict(ctx)
                ctx[name] = expr
            else:
                self._const_defs.setdefault(name, (expr, ctx, "constants"))

    def const(self, name):
        if name in self._const_cache:
            return self._const_cache[name]
        if name not in self._const_defs:
            raise KeyError(name)
        expr, ctx, _ = self._const_defs[name]
        self._const_cache[name] = None          # a cycle reads as missing
        value = self.eval(expr, ctx)
        self._const_cache[name] = value
        return value

    def has_const(self, name):
        return name in self._const_defs

    # -- expressions ----------------------------------------------------

    def eval(self, expr, ctx=None, charset=None, locals_=None):
        """An int, or a Sym for an address the reader cannot place."""
        return _Parser(self, expr, ctx or {}, charset, locals_ or {}).run()

    def _id(self, label, ctx):
        base = self.eval(ctx["offset"], ctx) if "offset" in ctx else None
        size = self.eval(ctx["ptrsize"], ctx) if "ptrsize" in ctx else None
        start = self.eval(ctx["idstart"], ctx) if "idstart" in ctx else 0
        if not isinstance(base, Sym) or not isinstance(size, int):
            raise AsmDataError(f"id({label}) outside a table")
        if self.label(label) is None:
            raise AsmDataError(f"id({label}): no such label")
        dist = self.distance(base.label, self.label(label)) - base.addend
        if dist % size:
            raise AsmDataError(f"id({label}) is not on an entry boundary")
        return dist // size + start

    # -- charset and := state at a line ---------------------------------

    def charset_at(self, line_no):
        """The character map in force on a line (AS `charset`)."""
        key = max((i for i, _ in self._charset_events if i < line_no),
                  default=-1)
        if key in self._charset_cache:
            return self._charset_cache[key]
        table = {}
        for i, args in self._charset_events:
            if i > key:
                break
            if not args:
                table = {}
                continue
            parts = split_operands(args)
            src = parts[0]
            src_code = unescape(src[1:-1])[0] if src.startswith("'") \
                else self.eval(src)
            dst = parts[1]
            if dst.startswith('"'):
                for k, code in enumerate(unescape(dst[1:-1])):
                    table[src_code + k] = code
            else:
                table[src_code] = self.eval(dst)
        self._charset_cache[key] = table
        return table

    def assigns_at(self, line_no):
        """The `name := expr` values set in ps2.asm above a line."""
        out = {}
        for i, name, op, expr in self._assign_events:
            if i >= line_no:
                break
            if op == ":=":
                out[name] = expr
        return out

    # -- layout ---------------------------------------------------------

    def line_cells(self, line_no, offset, ctx, size_only=False):
        """The cells one line assembles to, `offset` bytes into its region
        (for `even`). None for a line that holds no data (blank, comment,
        label alone, an assignment); raises on anything else."""
        raw = strip_comment(self.lines[line_no])
        if not raw.strip():
            return None
        if not raw[:1].isspace() and ASSIGN_RE.match(raw.rstrip()):
            return None
        m = LABEL_RE.match(raw)
        rest = raw
        if m and not raw[:1].isspace():
            rest = " " + raw[m.end():].lstrip(":")
            if not rest.strip():
                return None
            if ASSIGN_RE.match(raw.rstrip()):
                return None
        body = rest.strip()
        word = body.split(None, 1)
        op = word[0].lower()
        args = word[1] if len(word) > 1 else ""
        charset = self.charset_at(line_no)
        if op in ("dc.b", "dc.w", "dc.l"):
            width = {"b": 1, "w": 2, "l": 4}[op[-1]]
            out = []
            for tok in split_operands(args):
                if tok.startswith('"'):
                    if width != 1:
                        raise AsmDataError("a string in dc.w/dc.l")
                    out += [charset.get(c, c) for c in unescape(tok[1:-1])]
                    continue
                if size_only:
                    out += [0] * width      # a size needs no value
                    continue
                out += self._field(tok, width, ctx, charset)
            return out
        if op in ("dcb.b", "dcb.w", "dcb.l"):
            width = {"b": 1, "w": 2, "l": 4}[op[-1]]
            count_tok, value_tok = split_operands(args)
            count = self.eval(count_tok, ctx)
            return self._field(value_tok, width, ctx, charset) * count
        if op == "nametxt":
            text = args.strip()
            codes = [charset.get(c, c) for c in unescape(text[1:-1])]
            length = self.eval("length", ctx)
            if len(codes) < length:
                codes += [0xC4] + [0] * (length - len(codes) - 1)
            return codes
        if op == "cursorbox":
            text = args.strip()
            return [0xB4, 0xB5] + [charset.get(c, c)
                                   for c in unescape(text[1:-1])]
        if op == "border":
            count_tok, value_tok = split_operands(args)
            return self._field(value_tok, 1, ctx, charset) \
                * self.eval(count_tok, ctx)
        if op == "even":
            return [0] if offset & 1 else []
        if op in ("charset", "outradix", "radix"):
            return None
        if size_only and op in ("bra.w", "bsr.w"):
            return [0] * 4      # ObjectTable's entries: id() needs their size
        if size_only and op in ("bra.s", "bsr.s"):
            return [0] * 2
        if op == "binclude" and self.art_dir is not None:
            name = split_operands(args)[0].strip('"').replace("\\", "/")
            return list((self.art_dir.parent / name).read_bytes())
        raise NotData(f"line {line_no + 1}: not data: {body[:60]!r}")

    def _field(self, tok, width, ctx, charset):
        value = self.eval(tok, ctx, charset)
        if isinstance(value, Sym):
            return [PtrCell(value, width, k) for k in range(width)]
        value &= (1 << (8 * width)) - 1
        return list(value.to_bytes(width, "big"))

    def distance(self, a, b):
        """Bytes from label a to label b over the data between them."""
        la, lb = self.asm.label_line[a], self.asm.label_line[b]
        if lb < la:
            return -self.distance(b, a)
        ctx = self.assigns_at(la)
        total = 0
        for i in range(la, lb):
            if i == la:
                raw = strip_comment(self.lines[i])
                m = LABEL_RE.match(raw)
                if m and not raw[m.end():].lstrip(":").strip():
                    continue
            ctx = self._track_assign(i, ctx)
            cells = self.line_cells(i, total, ctx, size_only=True)
            if cells:
                total += len(cells)
        return total

    def _track_assign(self, i, ctx):
        m = ASSIGN_RE.match(strip_comment(self.lines[i]).rstrip())
        if m and m.group(2) == ":=" and not self.lines[i][:1].isspace():
            ctx = dict(ctx)
            ctx[m.group(1)] = m.group(3).strip()
        return ctx

    def cells_from(self, label, count):
        """The first `count` cells from a label, straight through any
        labels in the way."""
        return self.region(label, limit=count).cells[:count]

    def region(self, label, end=None, until_code=True, limit=None):
        """The cells from a label to `end` (a label, exclusive) or, with
        until_code, to the first line that is not data. Labels inside are
        recorded with their offsets in Region.labels."""
        start = self.asm.label_line[label]
        stop = self.asm.label_line[end] if end else len(self.lines)
        ctx = self.assigns_at(start)
        cells, labels, pads = [], {label: 0}, []
        for i in range(start, stop):
            if limit is not None and len(cells) >= limit:
                break
            raw = strip_comment(self.lines[i])
            m = LABEL_RE.match(raw)
            if m and not raw[:1].isspace() and i != start:
                labels.setdefault(m.group(1), len(cells))
            ctx = self._track_assign(i, ctx)
            try:
                got = self.line_cells(i, len(cells), ctx)
            except NotData:
                if until_code and end is None:
                    break
                raise
            if got:
                if strip_comment(self.lines[i]).strip().lower() == "even":
                    pads.append(len(cells))
                cells += got
        return Region(label, cells, labels, start, i, pads)


@dataclass
class Region:
    label: str
    cells: list
    labels: dict
    first_line: int
    last_line: int
    pads: list = None           # offsets of the bytes `even` put in

    def at(self, label):
        return self.labels[label]

    @property
    def table(self):
        """The cells less an `even` pad at the end: alignment, not an
        entry."""
        cells = self.cells
        while cells and self.pads and len(cells) - 1 in self.pads:
            cells = cells[:-1]
        return cells


class _Parser:
    """One expression, recursive descent, in AS's order, not C's: the bit
    operators bind tighter than arithmetic. Loosest first: `+ -`, `* /`,
    `|` and `!` (xor), `&`, the shifts, unary. So `(ObjID_X-$14)<<2+1` is
    the shifted id plus one, as LevelSpriteData means it (build 131a)."""

    def __init__(self, rom, expr, ctx, charset, locals_):
        self.rom, self.ctx, self.charset, self.locals = \
            rom, ctx, charset, locals_
        self.expr = expr.strip()
        self.toks, pos = [], 0
        while pos < len(self.expr):
            m = TOKEN_RE.match(self.expr, pos)
            if not m or m.end() == pos:
                if self.expr[pos:].strip() == "":
                    break
                raise AsmDataError(f"cannot read {self.expr!r} at "
                                   f"{self.expr[pos:]!r}")
            pos = m.end()
            self.toks.append((m.lastgroup, m.group(m.lastgroup)))
        self.i = 0

    def run(self):
        value = self.binary(0)
        if self.i != len(self.toks):
            raise AsmDataError(f"cannot read {self.expr!r}")
        return value

    LEVELS = (("+", "-"), ("*", "/"), ("|", "!"), ("&",), ("<<", ">>"))

    def peek(self):
        return self.toks[self.i] if self.i < len(self.toks) else (None, None)

    def take(self):
        if self.i >= len(self.toks):
            raise AsmDataError(f"{self.expr!r} ends early")
        tok = self.toks[self.i]
        self.i += 1
        return tok

    def binary(self, level):
        if level == len(self.LEVELS):
            return self.unary()
        left = self.binary(level + 1)
        while self.peek()[0] == "op" and self.peek()[1] in self.LEVELS[level]:
            op = self.take()[1]
            left = apply_op(self.rom, op, left, self.binary(level + 1))
        return left

    def unary(self):
        kind, text = self.peek()
        if kind == "op" and text in ("-", "~", "+"):
            self.take()
            value = self.unary()
            if isinstance(value, Sym):
                raise AsmDataError("unary operator on an address")
            return {"-": -value, "~": ~value, "+": value}[text]
        return self.atom()

    def atom(self):
        kind, text = self.take()
        if kind == "hex":
            return int(text[1:], 16)
        if kind == "bin":
            return int(text[1:], 2)
        if kind == "dec":
            return int(text)
        if kind == "chr":
            code = unescape(text[1:-1])[0]
            return self.charset.get(code, code) if self.charset else code
        if kind == "op" and text == "(":
            value = self.binary(0)
            if self.take() != ("op", ")"):
                raise AsmDataError("unbalanced parentheses")
            return value
        if kind == "name":
            if text == "id" and self.peek() == ("op", "("):
                self.take()
                arg = self.take()
                if arg[0] != "name" or self.take() != ("op", ")"):
                    raise AsmDataError("id() takes one label")
                return self.rom._id(arg[1], self.ctx)
            if text in self.locals:
                return self.locals[text]
            if text in self.ctx:
                return self.rom.eval(self.ctx[text], self.ctx, self.charset)
            if self.rom.has_const(text):
                value = self.rom.const(text)
                if value is None:
                    raise AsmDataError(f"{text} refers to itself")
                return value
            label = self.rom.label(text)
            if label is not None:
                return Sym(label, 0)
            raise AsmDataError(f"unknown symbol {text}")
        raise AsmDataError(f"unexpected {text!r} in {self.expr!r}")


def apply_op(rom, op, a, b):
    if isinstance(a, Sym) or isinstance(b, Sym):
        if op == "-" and isinstance(a, Sym) and isinstance(b, Sym):
            return rom.distance(b.label, a.label) + a.addend - b.addend
        if op in ("+", "|") and isinstance(a, Sym) and isinstance(b, int):
            return Sym(a.label, a.addend + b)
        if op in ("+", "|") and isinstance(b, Sym) and isinstance(a, int):
            return Sym(b.label, b.addend + a)
        if op == "-" and isinstance(a, Sym) and isinstance(b, int):
            return Sym(a.label, a.addend - b)
        raise AsmDataError(f"cannot apply {op} to an address")
    if op == "|":
        return a | b
    if op == "!":
        return a ^ b
    if op == "&":
        return a & b
    if op == "<<":
        return a << b
    if op == ">>":
        return a >> b
    if op == "+":
        return a + b
    if op == "-":
        return a - b
    if op == "*":
        return a * b
    if op == "/":
        return int(a / b)
    raise AsmDataError(op)
