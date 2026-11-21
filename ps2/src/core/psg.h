#ifndef PS2_PSG_H
#define PS2_PSG_H

#include "ps2_types.h"

/* SN76489 (the Mega Drive's PSG): three square-wave tone channels and one
 * noise channel, each with 4-bit attenuation. Fully specified and small,
 * so it is written out here rather than emulated approximately.
 *
 * The FM side is deliberately NOT here. YM2612 timbre comes from the
 * chip's specific envelope rates, sine quantisation and DAC ladder, and an
 * approximation sounds wrong in a way that is obvious and unfixable. That
 * belongs behind SynthBackend as a real core (Nuked-OPN2 or ymfm). */

#define PSG_CHANNELS  4    /* 3 tone + 1 noise */
#define PSG_CLOCK     3579545
#define PSG_DIVIDER   16   /* internal clock divide */

typedef struct {
    u16 tone[3];       /* 10-bit period; 0 or 1 means DC */
    u8  atten[4];      /* 0 = loudest, 15 = silent */
    u8  noise_ctrl;    /* bit 2 = white/periodic, bits 0-1 = shift rate */

    /* Generator state. */
    s32 counter[4];
    s8  output[3];
    u16 lfsr;
    s8  noise_out;

    /* Fractional resampling from the chip rate to the output rate. */
    double step;
    double phase;
} Psg;

void psg_init(Psg *p, int sample_rate);

/* Register write, in the chip's own latch/data format. */
void psg_write(Psg *p, u8 value);

/* Convenience for the UI sounds, bypassing the latch protocol. */
void psg_set_tone(Psg *p, int channel, u16 period, u8 atten);
void psg_set_noise(Psg *p, u8 ctrl, u8 atten);

/* Renders `frames` samples, ADDING into out (mono, -1..1). */
void psg_render(Psg *p, float *out, int frames, float gain);

/* Period for a frequency, for callers that think in Hz. */
u16 psg_period_for_hz(double hz);

#endif /* PS2_PSG_H */
