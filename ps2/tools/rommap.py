"""Reading the cartridge image by the disassembly's names (build 185).

The extractors read the ROM itself, a player's own image, through
tools/rom_map.json (tools/make_rom_map.py makes it from the disassembly):
every label of ps2.asm with its address, in the order of ps2.asm's lines,
the drum samples' labels, the binclude'd art files and the constants. They
need no disassembly.

    rom = RomMap(path_to_image)
    rom.blob("Pal_Sega")          # bytes from the label to the next label
    rom.addr("GameScriptPtrs")    # its address
    rom.u32(rom.addr("Foo") + 4)  # a big-endian long
    rom.const("ItemID_Knife")     # a constant's value
    rom.file("art/foo.bin")       # a binclude'd file's bytes

`blob` is ps2asm.Asm.span's block as the ROM has it: from a label to the
next label in ps2.asm's line order, whatever lines lie between. Where
ps2asm.Asm.blob read only the `$hex` tokens of the dc lines, this has every
byte: symbols, strings and `even`'s pad byte included.

A name is matched exactly, then without regard to case, as AS matches
symbols.
"""
import atexit
import hashlib
import json
import os
import re
from pathlib import Path

DEFAULT_MAP = Path(__file__).resolve().with_name("rom_map.json")


class RomMapError(Exception):
    pass


class RomMap:
    def __init__(self, rom_path, map_path=DEFAULT_MAP):
        doc = json.loads(Path(map_path).read_text(encoding="utf-8"))
        self.rom = Path(rom_path).read_bytes()
        want = doc["rom"]
        if len(self.rom) != want["size"] or \
                hashlib.sha1(self.rom).hexdigest() != want["sha1"]:
            raise RomMapError(
                "%s is not the cartridge image tools/rom_map.json is of "
                "(%d bytes, SHA-1 %s): Phantasy Star II (USA, Europe) Rev 02, "
                "raw, with no copier header"
                % (rom_path, want["size"], want["sha1"]))
        self.labels = [(n, a) for n, a in doc["labels"]]
        self.drums = [(n, a) for n, a in doc["drums"]]
        self.files = {n: (a, s) for n, (a, s) in doc["files"].items()}
        self.consts = dict(doc["constants"])
        self._index = {}
        for i, (n, _) in enumerate(self.labels):
            self._index.setdefault(n, i)
        self._ci = {}
        for n in list(self._index) + [n for n, _ in self.drums]:
            self._ci.setdefault(n.lower(), n)
        self._drum = {}
        for n, a in self.drums:
            self._drum.setdefault(n, a)
        self._at = {}
        for n, a in self.labels:
            self._at.setdefault(a, []).append(n)
        self._const_ci = {n.lower(): n for n in self.consts}
        self._used = _USED

    # -- names ----------------------------------------------------------

    def name(self, name):
        """The label as ps2.asm writes it, any case; None if there is none."""
        if name in self._index or name in self._drum:
            return name
        return self._ci.get(name.lower())

    def has(self, name):
        return self.name(name) is not None

    def addr(self, name):
        """A label's ROM address (a drum label's too)."""
        found = self.name(name)
        if found is None:
            raise KeyError(name)
        self._used.add(found)
        if found in self._index:
            return self.labels[self._index[found]][1]
        return self._drum[found]

    def index(self, name):
        """A label's place among ps2.asm's labels, in line order."""
        found = self.name(name)
        if found is None or found not in self._index:
            raise KeyError(name)
        self._used.add(found)
        return self._index[found]

    def next_addr(self, name):
        """Where the label after it (in ps2.asm's line order) starts: the
        end of `name`'s block."""
        i = self.index(name)
        if i + 1 < len(self.labels):
            self._used.add(self.labels[i + 1][0])
            return self.labels[i + 1][1]
        return len(self.rom)

    def names_at(self, address):
        """Every label at an address, in ps2.asm's line order."""
        names = self._at.get(address, [])
        self._used.update(names)
        return list(names)

    def matching(self, pattern):
        """(name, address) of each label whose name matches a regex, in
        line order."""
        rx = re.compile(pattern)
        out = [(n, a) for n, a in self.labels if rx.search(n)]
        self._used.update(n for n, _ in out)
        return out

    # -- bytes ----------------------------------------------------------

    def read(self, address, length):
        if address < 0 or address + length > len(self.rom):
            raise RomMapError("read of %d at %X is outside the ROM"
                              % (length, address))
        return self.rom[address:address + length]

    def blob(self, name):
        """The bytes from a label to the next label."""
        start = self.addr(name)
        return self.rom[start:self.next_addr(name)]

    def run(self, name, length):
        """`length` bytes from a label on, through any labels in the way."""
        return self.read(self.addr(name), length)

    def u8(self, address):
        return self.rom[address]

    def u16(self, address):
        return int.from_bytes(self.read(address, 2), "big")

    def u32(self, address):
        return int.from_bytes(self.read(address, 4), "big")

    def s8(self, address):
        return int.from_bytes(self.read(address, 1), "big", signed=True)

    def s16(self, address):
        return int.from_bytes(self.read(address, 2), "big", signed=True)

    def file(self, name):
        """A binclude'd file's bytes ("art/foo.bin", either slash)."""
        key = name.replace("\\", "/")
        if key not in self.files:
            raise KeyError(name)
        self._used.add("file:" + key)
        address, size = self.files[key]
        return self.read(address, size)

    # -- constants ------------------------------------------------------

    def has_const(self, name):
        return name in self.consts or name.lower() in self._const_ci

    def const(self, name):
        key = name if name in self.consts else self._const_ci.get(name.lower())
        if key is None:
            raise KeyError(name)
        self._used.add("const:" + key)
        return self.consts[key]


# PS2_ROMMAP_USED=FILE: every name an extractor looked up, appended to FILE
# (tools/make_rom_map.py --keep-used reads it, to leave out the rest).
_USED = set()


def _write_used():
    path = os.environ.get("PS2_ROMMAP_USED")
    if path and _USED:
        with open(path, "a", encoding="utf-8") as fh:
            fh.write("".join(n + "\n" for n in sorted(_USED)))


atexit.register(_write_used)
