#ifndef PS2_DRUMS_H
#define PS2_DRUMS_H

#include "../core/ps2_types.h"

/* (build 130) The DAC drums by name: the byte a song's drum track sends
 * (the sequencer's drum routine, UpdateDrumTrack at ps2.asm:91012) and the
 * Z80 plays from its sample table (tools/extract_drums.py, fm.c's
 * fm_dac_play).
 *
 * The toms' names are the disassembly's own: the drum routine's comments
 * call the byte it pans left "High Tom" and the one it pans right "Low
 * Tom" (ps2.asm:91039-91041). The three toms are one sample at three rates,
 * so the byte between them is the middle tom. The rest are named for what
 * the samples measure as, not by the ROM (docs/audio.md, "Drums"): a body
 * near 200 Hz under noise at 2-6 kHz, the snare; 33 ms with most of its
 * energy below 200 Hz, the kick; noise centred near 3.2 kHz, 125 and 42 ms
 * long, the open and the closed hi-hat. The eighth plays 513 bytes of an
 * 18-byte sample -- past it, into Z80 RAM nothing wrote -- and no song
 * sends it. */
enum {
    DRUM_HIGH_TOM   = 0x80,   /* panned left */
    DRUM_MID_TOM    = 0x81,
    DRUM_LOW_TOM    = 0x82,   /* panned right */
    DRUM_SNARE      = 0x84,
    DRUM_KICK       = 0x88,
    DRUM_UNUSED     = 0x90,
    DRUM_OPEN_HAT   = 0xA0,
    DRUM_CLOSED_HAT = 0xC0
};

/* A drum byte's name for logs and reports ("snare"); NULL for a byte that
 * is none of the eight. */
static inline const char *drum_name(unsigned char byte)
{
    switch (byte) {
    case DRUM_HIGH_TOM:
        return "high tom";
    case DRUM_MID_TOM:
        return "mid tom";
    case DRUM_LOW_TOM:
        return "low tom";
    case DRUM_SNARE:
        return "snare";
    case DRUM_KICK:
        return "kick";
    case DRUM_UNUSED:
        return "unused";
    case DRUM_OPEN_HAT:
        return "open hat";
    case DRUM_CLOSED_HAT:
        return "closed hat";
    default:
        return 0;
    }
}

#ifdef __cplusplus
extern "C" {
#endif

/* (build 136b) One drum as the backends play it (fm.c's FmDrum, Q's
 * SampleDrum carry the same), and the Z80 RAM the samples sit in. */
#define DRUM_Z80_RAM  0x2000
#define DRUM_MAX      8
typedef struct {
    u8  byte;          /* the drum byte the sequencer sends */
    u16 delay;         /* djnz count */
    u32 addr;          /* into the Z80 RAM image */
    u32 length;        /* bytes played */
} DrumEntry;

/* (build 136b) The DAC drums from assets/music/drums.json and the WAVs it
 * names (tools/extract_drums.py), into the form every backend plays from:
 * `ram`, DRUM_Z80_RAM bytes, zeroed and then each sample's WAV bytes (8-bit
 * unsigned mono PCM, the ROM's bytes; the Z80 halves them as it plays) at
 * its "z80_address"; `table`, each drum's byte, delay (the Z80's djnz
 * count), its sample's address and the bytes it plays ("length", which may
 * run past its sample, as $90's does in the ROM). Files through `reader`
 * first (the game's pack), else the disk; a WAV's path is the JSON's
 * folder plus its "file". Refuses, naming it: a file that does not read, a
 * WAV that is not 8-bit mono PCM, a sample over the Z80 program's own bytes
 * (below $118), past Z80 RAM or over another sample, a drum byte outside
 * $80-$FF or given twice, a drum naming no sample, a delay outside 1-255,
 * and a drum playing past Z80 RAM. *count is 0 when it refuses. */
bool drums_load(const char *path, Ps2Reader reader, DrumEntry *table,
                int *count, u8 *ram);

#ifdef __cplusplus
}
#endif

#endif
