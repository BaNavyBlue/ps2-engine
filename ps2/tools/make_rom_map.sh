#!/usr/bin/env bash
# (build 185) tools/rom_map.json from the disassembly, proved against the ROM.
#
#   tools/make_rom_map.sh DISASM_DIR ROM WORK_DIR
#
# Run it when the disassembly changes (a label renamed or added, a constant
# changed): the extractors find everything through the map.
#
# 1. Assembles a copy of the disassembly in WORK_DIR with `cpu 68020` in
#    place of `cpu 68000`, for its listing. Today's AS (1.42 Bld 306) treats
#    a 68000's addresses as 24-bit and so codes `$FFxxxx` as a short
#    address; the original assembler did not, and the sound driver's input
#    code came out 160 bytes short. On a 68020 nothing is shortened, and the
#    change moves no line, so the listing lines up with the real ps2.asm.
# 2. Links it with the disassembly's own AS/ps2p2bin.c (the stock p2bin
#    places the Z80 driver wrongly) and checks the result is the ROM, byte
#    for byte: otherwise the listing's addresses are not the ROM's.
# 3. tools/make_rom_map.py reads the listing into tools/rom_map.json.
#
# Needs AS (asl), gcc, cmp, and Python 3 ($PYTHON, default python3).
set -euo pipefail
[ $# -eq 3 ] || { sed -n '2,4p' "$0"; exit 2; }
disasm=$1 rom=$2 work=$3
here=$(cd "$(dirname "$0")" && pwd)
py=${PYTHON:-python3}

mkdir -p "$work"
cp -r "$disasm/." "$work/"
n=$(grep -c -E '^[[:space:]]+cpu[[:space:]]+68000[[:space:]]*$' "$work/ps2.asm" || true)
[ "$n" = 1 ] || { echo "expected one 'cpu 68000' line, found $n" >&2; exit 1; }
sed -i -E 's/^([[:space:]]+cpu[[:space:]]+)68000([[:space:]]*)$/\168020\2/' "$work/ps2.asm"

(cd "$work" &&
 asl -xx -c -A -L -olist ps2.lst ps2.asm > asl.out 2>&1 || { cat asl.out; exit 1; }
 gcc -O2 -o ps2p2bin AS/ps2p2bin.c &&
 ./ps2p2bin ps2.p ps2built.bin ps2.h > /dev/null)
cmp "$work/ps2built.bin" "$rom"
echo "assembled: the ROM, byte for byte"

"$py" "$here/make_rom_map.py" "$disasm/ps2.asm" "$work/ps2.lst" "$rom"
