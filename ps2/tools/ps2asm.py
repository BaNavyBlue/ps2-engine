"""Reading labelled data out of ps2.asm.

The disassembly stores most data inline as `dc.b` / `dc.w` / `dc.l` under a
label, and a block runs until the next label. That is enough to pull any
blob out by name without needing a ROM, which is what every extractor here
relies on.

Two wrinkles the naive reader gets wrong:

  * `dc.l` operands are often `$03000000|loc_28664` -- a pointer with flag
    bits in the byte the 68000 ignores. Scraping `$` tokens turns that into
    two numbers. `parse_long_operands` keeps the pair together.
  * Trailing `;0x0 (0x000498E8-0x0004A370, ...)` comments are full of hex.
    Everything after the first `;` is dropped.
"""

import re

# AS accepts a label in column 0 without a colon; five in ps2.asm are
# written that way (loc_810E4, Item_Sandals, ...).
LABEL_RE = re.compile(r"^([A-Za-z_][A-Za-z0-9_]*)(?::|\s*$)")
# A data line may carry its own label: `PtrPal_Sega:  dc.l Pal_Sega`.
# Matching only leading whitespace loses every one of those.
DC_RE = re.compile(
    r"^(?:[A-Za-z_][A-Za-z0-9_]*:)?\s*dc\.([bwl])\s+(.*)$", re.IGNORECASE)
HEX_RE = re.compile(r"\$([0-9A-Fa-f]+)")


class Asm:
    """An indexed ps2.asm: label -> line, and the sorted set of label lines."""

    def __init__(self, path):
        self.lines = path.read_text(errors="replace").splitlines()
        self.label_line = {}
        marks = []
        for i, line in enumerate(self.lines):
            m = LABEL_RE.match(line)
            if m:
                self.label_line.setdefault(m.group(1), i)
                marks.append(i)
        self.marks = sorted(set(marks))

    def span(self, label):
        """(start, end) line numbers of a label's block, end exclusive."""
        start = self.label_line[label]
        end = next((i for i in self.marks if i > start), len(self.lines))
        return start + 1, end

    def blob(self, label):
        """Every dc.b/w/l under a label, as big-endian bytes."""
        start, end = self.span(label)
        out = bytearray()
        for line in self.lines[start:end]:
            m = DC_RE.match(line)
            if not m:
                continue
            width = {"b": 1, "w": 2, "l": 4}[m.group(1).lower()]
            for tok in HEX_RE.findall(m.group(2).split(";")[0]):
                out += (int(tok, 16) & ((1 << (width * 8)) - 1)).to_bytes(
                    width, "big")
        return bytes(out)

    def run(self, label, max_bytes=1 << 20):
        """Bytes from a label onward, straight through any labels in the way.

        A compressed stream is one blob, but the disassembler puts a label
        wherever something points into the middle of it -- several dungeon
        maps are pointed at from two levels at different offsets. Stopping
        at the next label truncates those, which shows up as a map that
        decodes to a handful of bytes.

        So this keeps going until the first line that is neither data, a
        label, a comment nor blank, and lets the decompressor decide where
        the stream actually ends; each one already reports how much it
        consumed, so over-reading costs nothing and under-reading is what
        silently corrupts.
        """
        start = self.label_line[label] + 1
        out = bytearray()
        for line in self.lines[start:]:
            if len(out) >= max_bytes:
                break
            stripped = line.strip()
            if not stripped or stripped.startswith(";"):
                continue
            m = DC_RE.match(line)
            if not m:
                if LABEL_RE.match(line):
                    continue      # a label alone, still inside the blob
                break
            width = {"b": 1, "w": 2, "l": 4}[m.group(1).lower()]
            for tok in HEX_RE.findall(m.group(2).split(";")[0]):
                out += (int(tok, 16) & ((1 << (width * 8)) - 1)).to_bytes(
                    width, "big")
        return bytes(out)

    def operands(self, label, directive=None):
        """The raw operand text of each dc directive under a label."""
        start, end = self.span(label)
        items = []
        for line in self.lines[start:end]:
            m = DC_RE.match(line)
            if not m:
                continue
            if directive and m.group(1).lower() != directive:
                continue
            body = m.group(2).split(";")[0]
            items += [tok.strip() for tok in body.split(",") if tok.strip()]
        return items


def parse_long_operands(items):
    """`$03000000|loc_28664` -> (0x03, 'loc_28664'); `loc_498E8` -> (0, ...).

    Both `|` and `+` appear as the separator in ps2.asm; they mean the same
    thing here, since the flag bits and the address never overlap.

    A plain `$xxxxxxxx` with no symbol comes back as (flags, None) with the
    low 24 bits dropped, which no caller currently needs but is better than
    silently losing the flag byte.
    """
    out = []
    for item in items:
        flags, symbol = 0, None
        for part in re.split(r"[|+]", item):
            part = part.strip()
            if part.startswith("$"):
                flags |= int(part[1:], 16) >> 24
            else:
                symbol = part
        out.append((flags, symbol))
    return out
