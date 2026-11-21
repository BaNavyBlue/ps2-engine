# Notices

## The game

*Phantasy Star II* is copyright SEGA. This repository contains none of the
game's data: no ROM, and no art, music, maps or text taken from one. To play,
you need a cartridge image of your own (see the README for which ones work).
The setup step makes the game's files from that image on your machine; they
are never part of this repository or its releases.

This project is not affiliated with, endorsed by or connected to SEGA.

## What the MIT licence covers

`LICENSE` (MIT) covers this project's own work: the engine's source, its
tools, its tests and its documentation. It does not cover the parts below,
which keep their own terms.

## Other people's work

| Part | Where | Author | Terms |
|---|---|---|---|
| The disassembly's names | `ps2/tools/rom_map.json` (and the port's code and docs, which use the same names) | lory90 (*Phantasy Star II Disassembly for Mega Drive/Genesis*, distributed by Phantasy Star Cave) | No licence stated. The map lists the disassembly's label and constant names with the ROM addresses they stand for; the extractors find the game's data by them. It holds no code, comments or data of the disassembly, but the names are lory90's: credited here, and not covered by this project's MIT licence. |
| Nuked-OPN2 (the YM2612 chip) | `ps2/src/audio/ym3438.c`, `ps2/src/audio/ym3438.h` | Alexey Khokholov (Nuke.YKT) | LGPL-2.1; `ps2/src/audio/LICENSE.ym3438` |
| Cycfi Q (the re-voiced synth) | fetched at build time, not in this repository | Joel de Guzman | MIT |
| SDL3 | linked, not in this repository | Sam Lantinga and contributors | zlib |
| *Phantasy Star II: English Retranslation* (the RETRANSLATED language's script, names and face) | `ps2/assets/lang/retranslation/` | Excalibur_Z | MIT; `ps2/assets/lang/retranslation/LICENSE.txt` |

Released builds carry these notices in their `licenses/` folder, with those
of what the build links in (on Windows also the GCC runtime, winpthreads and
libiconv, and the bundled Python and Pillow). Because the game links
Nuked-OPN2 (LGPL-2.1) statically, each release package is also the complete
source it was built from, so it can be rebuilt with a changed copy of that
library.
