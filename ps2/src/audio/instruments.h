#ifndef PS2_INSTRUMENTS_H
#define PS2_INSTRUMENTS_H

#include "../core/ps2_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* (build 136d) The FM instruments from assets/music/fm_instruments.json
 * (tools/extract_music.py), in the form the upload routine sends them
 * (SetFMIns, ps2.asm after loc_BA818): 26 register writes to the track's
 * channel, the register numbers from one table and the values from the
 * instrument, byte i to register i. fm.h's FM_INS_BYTES is the same 26. */
#define INSTRUMENT_REGISTERS  26
#define INSTRUMENTS_MAX       256    /* the instrument command's operand is a byte */

/* `order`: the INSTRUMENT_REGISTERS registers, channel 1's numbers ($B0,
 * $B4, $30-$8C), in the file's "upload_order". `*bank`: malloc'd, *count
 * instruments of INSTRUMENT_REGISTERS bytes, instrument n's byte i the
 * value for register order[i], composed from the named fields and the
 * instrument's "ignored_bits" -- the ROM's bytes for the extracted file.
 * Files through `reader` first (the game's pack), else the disk. Refuses,
 * naming it: a file that does not read or is not JSON; an "upload_order"
 * that is not the 26 register names, each once; "instruments" holding none
 * or more than INSTRUMENTS_MAX; an instrument whose "number" is not its
 * place; a field missing, not a whole number (or, for "lfo_am", true or
 * false; "pan" center, left, right or none), or out of its range; an
 * operator field that is not a row of four; and an ignored bit naming no
 * register, past bit 7 or one the chip reads. *count is 0 and *bank NULL
 * when it refuses. */
bool instruments_load(const char *path, Ps2Reader reader,
                      u8 order[INSTRUMENT_REGISTERS], u8 **bank, int *count);

/* (build 137a) The same, and each instrument's "name" (what the song text
 * calls it) into *names, an array of `count` names the caller frees. Both
 * refuse, naming it, a name that is not one word -- letters, digits, _, at
 * most INSTRUMENT_NAME_MAX - 1 -- a name given twice, and names on some
 * instruments but not all. */
#define INSTRUMENT_NAME_MAX   32
bool instruments_load_named(const char *path, Ps2Reader reader,
                            u8 order[INSTRUMENT_REGISTERS], u8 **bank,
                            int *count, char (**names)[INSTRUMENT_NAME_MAX]);

#ifdef __cplusplus
}
#endif

#endif
