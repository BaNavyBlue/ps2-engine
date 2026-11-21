#ifndef PS2_PATCHES_H
#define PS2_PATCHES_H

/* (build 112) The patch file: hand-voiced instruments for Q in place of the
 * ROM's (docs/audio.md, "The patch file"). assets/music/q_patches.json, read
 * once at start-up by audio_load_music; the chip, the reference, always plays
 * the ROM's.
 *
 *   {
 *     "patches": [
 *       { "instrument": 70, "name": "...", "enabled": true,
 *         "algorithm": 7, "feedback": 0, "volume_db": 0.0,
 *         "tail": "row" | "fade" | "ring",
 *         "operators": [ { "multiple": 2, "detune": 0, "level": 0,
 *                          "key_scale": 0, "attack": 5, "decay": 3,
 *                          "sustain_rate": 15, "sustain_level": 2,
 *                          "release": 7, "fine_cents": 0.0 },
 *                        {...}, {...}, {...} ] } ] }
 *
 * Every field but "instrument" may be left out, and then the ROM's value
 * stands: a patch says only what it changes. The operators are the chip's
 * operators 1-4 in that order (the bank's $30, $38, $34, $3C). The numbers
 * are the chip's own and have its ranges -- multiple 0-15 (0 is a half),
 * detune -3..+3, level 0-127 (0.75 dB a step, 0 the loudest; on a carrier
 * the song's volume is still added), key_scale 0-3, attack, decay and
 * sustain_rate 0-31, sustain_level and release 0-15; a value out of range is
 * brought into it, with a line on stderr. What only Q can do: fine_cents (an
 * operator's pitch moved in cents), volume_db (the instrument's level), tail
 * ("row": as the AUDIO page's TAILS; "fade" or "ring" for this instrument
 * whatever the row says). "enabled": false keeps a patch in the file and out
 * of the game. tools/q_patch_template.py writes an instrument's ROM values in
 * this shape, to start from. */

#include "smps.h"
#include "synth.h"

/* Reads `path` (an assets/ path comes from the preloaded store) into `out`,
 * each patch resolved against the ROM's instrument in `data`; returns how
 * many are enabled (at most `max`, one an instrument -- a later one for the
 * same instrument replaces the earlier). 0 when there is no file. */
int patches_load(const char *path, const SmpsData *data, SynthPatch *out,
                 int max);

#endif /* PS2_PATCHES_H */
