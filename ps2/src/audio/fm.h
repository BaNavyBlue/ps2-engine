#ifndef PS2_FM_H
#define PS2_FM_H

#include "../core/ps2_types.h"

/* YM2612 backend around Nuked-OPN2.
 *
 * This build of Nuked exposes only OPN2_Clock, so a sample is produced by
 * clocking 24 times and summing -- the chip emits one channel's output per
 * clock and cycles through all six. That gives the native rate,
 * master / 144, which is 53267 Hz on NTSC and has to be resampled.
 *
 * Register writes cannot be issued back to back. Nuked holds one pending
 * write and commits it on the next clock, so an address write followed
 * immediately by its data write is destroyed before it is seen. Newer
 * builds have OPN2_WriteBuffered for this; this one does not, so the
 * address and the data go out on successive samples -- 24 clocks apart,
 * far more than the chip needs.
 *
 * The raw output also carries a large DC offset. Real hardware
 * AC-couples it away at the output stage, so a one-pole high-pass does
 * the same here, on each side.
 *
 * The output is stereo, as the chip's is: register $B4 of each channel
 * has a left bit (7) and a right bit (6), set by every instrument upload
 * and by the driver's cfFF_Pan and cfE3_PanAnim, and Nuked honours them.
 * FM6 carries the DAC drums, so they pan with it.
 *
 * The write queue is sized for a song start, which is around 200
 * writes in one tick, arriving several ticks at a time when the main
 * loop catches up. Each takes two native samples to go out. */

#define FM_MASTER_CLOCK 7670442          /* NTSC Mega Drive */
#define FM_NATIVE_RATE  (FM_MASTER_CLOCK / 144)
#define FM_WRITE_QUEUE  2048
#define FM_INS_BYTES    26

/* The DAC drums. The Z80 program in ps2_pcm_drums.asm is the whole of
 * what the Z80 does: on a drum byte from the 68k it picks a sample, a
 * length and a delay count, then feeds register $2A one byte at a time
 * with a `djnz` spin between them. The rate is therefore instruction
 * timing, 240 + 13 * delay T-states per byte at the Z80's 3.58 MHz,
 * and the byte goes out halved (`srl a`).
 *
 * The image is the 8 KB of Z80 RAM, because one drum reads past the
 * end of its sample into RAM nothing initialised. */
#define FM_Z80_CLOCK    3579545
#define FM_Z80_RAM      0x2000
#define FM_DAC_DRUMS    8

typedef struct {
    u8  byte;          /* the drum byte the sequencer sends */
    u16 delay;         /* djnz count */
    u32 addr;          /* into the Z80 RAM image */
    u32 length;
} FmDrum;

typedef struct {
    u8 port;
    u8 reg;
    u8 value;
} FmWrite;

typedef struct {
    void *chip;                  /* ym3438_t, heap-allocated */

    FmWrite queue[FM_WRITE_QUEUE];
    int     q_head, q_tail;
    bool    q_addr_sent;   /* address is out; data goes next sample */

    /* DC blocker state, [0] left and [1] right. */
    float dc_x1[2], dc_y1[2];

    /* Fractional resample from FM_NATIVE_RATE to the output rate, per
     * side. */
    double step;
    double phase;
    float  prev[2], next[2];

    /* The game's instrument bank and the register order it uploads in. */
    u8  reg_order[FM_INS_BYTES];
    u8 *instruments;
    int instrument_count;

    /* DAC drums. */
    u8     *z80_ram;
    FmDrum  drums[FM_DAC_DRUMS];
    int     drum_count;

    bool    dac_active;
    bool    dac_due;         /* a byte is owed; waiting for a free slot */
    bool    dac_addr_sent;   /* $2A is latched; data goes at clock 12 */
    u8      dac_value;
    u32     dac_pos;         /* next byte to send */
    u32     dac_end;
    double  dac_step;        /* DAC bytes per native sample */
    double  dac_phase;

    /* (build 111) The EFFECTS volume: which channels an effect holds (from
     * the hooks' key-ons, through the write queue as marks), and while the
     * effects' gain differs from the music's, each channel's output
     * weighted by it clock by clock -- Nuked puts out one channel a group
     * of four clocks. Equal gains skip it all: the output is the same to
     * the bit. */
    bool    effect_ch[6];
    bool    weighted;
    float   weight[2];       /* [0] the music's channels, [1] the effects' */
    int     lock_ch;         /* the channel on the output this clock */

    /* (build 184) Output frames rendered since the chip last made a sound or
     * had a write waiting (fm_idle), and how many make it idle. */
    int     quiet_frames;
    int     idle_frames;
} Fm;

bool fm_init(Fm *f, int sample_rate);
void fm_shutdown(Fm *f);

/* (build 110) The chip reset, the queue emptied, the DAC stopped. The DC
 * blockers and the resampler run on (the chip's DC does not change), and
 * the instrument bank and the drums stay loaded. The synth table's reset (synth.h): what a
 * backend switched to starts from, before the register shadow is replayed. */
void fm_reset(Fm *f);

/* (build 136d) The instrument bank and the register order SetFMIns uploads
 * it in, from assets/music/fm_instruments.json (instruments.h's
 * instruments_load; the "PS2B" register order and "PS2I" bank containers
 * until then), through the reader fm_set_reader gave, else the disk: the
 * tools' bank -- fm_demo's and q_demo's. (build 137a) The game's are the
 * same file's, built into the driver's image with the song text
 * (songtext.c); SoundData.bin's image until then. */
bool fm_load_instruments(Fm *f, const char *path);

/* Uploads one instrument to a channel, exactly as SetFMIns does. */
void fm_set_instrument(Fm *f, int channel, int instrument);

/* How many writes are still queued; each takes two samples to go out. */
int  fm_pending_writes(const Fm *f);

void fm_note_on(Fm *f, int channel, double hz);
void fm_note_off(Fm *f, int channel);

/* Queues a raw register write; part selects the register bank (0 for
 * channels 1-3, 1 for channels 4-6). */
void fm_write(Fm *f, int part, u8 reg, u8 value);

/* (build 136b) The drums from assets/music/drums.json and its WAVs
 * (drums.h's drums_load; the "PS2D" container until then). */
bool fm_load_drums(Fm *f, const char *path);

/* (build 125) The game's pack for fm_load_drums and fm_load_instruments
 * (ps2_types.h's Ps2Reader); NULL, the default, reads the disk. */
void fm_set_reader(Ps2Reader reader);

/* Starts a drum, exactly as the Z80 would on seeing the byte in $1FFF:
 * whatever was playing stops at once. Audio thread. */
void fm_dac_play(Fm *f, u8 drum_byte);

/* ADDS into out: `frames` stereo frames, interleaved left then right,
 * -1..1. */
void fm_render(Fm *f, float *out, int frames, float gain);

/* (build 111) Marks a channel (0-5) as an effect's or the music's, in the
 * write queue's order, so the mark lands with the key-on it came with. */
void fm_mark_channel(Fm *f, int channel, bool effect);

/* (build 111) fm_render with the music's channels at music_gain and the
 * effects' at effects_gain. With the two equal it is fm_render. */
void fm_render_split(Fm *f, float *out, int frames, float music_gain,
                     float effects_gain);

/* (build 184) A chord chip's (synth.c). fm_settle: a chip just made, its DC
 * blocker set to the reset chip's standing DC, so it is silent from its
 * first sample -- where the effects' chip runs 2 s unheard for that.
 * fm_idle: nothing queued, no drum, and every operator at full attenuation
 * for half a second rendered, its blocker's tail spent: rendering it now
 * adds nothing, and a chip left unrendered takes up where it stopped. */
void fm_settle(Fm *f);
bool fm_idle(const Fm *f);

#endif /* PS2_FM_H */
