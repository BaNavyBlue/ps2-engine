#include "psg.h"

#include <string.h>

/* Attenuation is 2 dB per step, 15 being off. */
static const float PSG_VOLUME[16] = {
    1.0000f, 0.7943f, 0.6310f, 0.5012f,
    0.3981f, 0.3162f, 0.2512f, 0.1995f,
    0.1585f, 0.1259f, 0.1000f, 0.0794f,
    0.0631f, 0.0501f, 0.0398f, 0.0000f
};

void psg_init(Psg *p, int sample_rate)
{
    memset(p, 0, sizeof *p);

    for (int i = 0; i < PSG_CHANNELS; i++) {
        p->atten[i] = 15;      /* silent until written */
    }
    for (int i = 0; i < 3; i++) {
        p->output[i] = 1;
    }

    p->lfsr      = 0x8000;
    p->noise_out = 1;
    p->step      = (double)(PSG_CLOCK / PSG_DIVIDER) / (double)sample_rate;
    p->phase     = 0.0;
}

u16 psg_period_for_hz(double hz)
{
    if (hz <= 0.0) {
        return 0;
    }
    double period = (double)(PSG_CLOCK / PSG_DIVIDER) / (2.0 * hz);
    if (period < 1.0) {
        period = 1.0;
    }
    if (period > 1023.0) {
        period = 1023.0;
    }
    return (u16)(period + 0.5);
}

void psg_set_tone(Psg *p, int channel, u16 period, u8 atten)
{
    if (channel < 0 || channel > 2) {
        return;
    }
    p->tone[channel]  = period & 0x03FF;
    p->atten[channel] = atten & 0x0F;
}

void psg_set_noise(Psg *p, u8 ctrl, u8 atten)
{
    p->noise_ctrl = ctrl & 0x07;
    p->atten[3]   = atten & 0x0F;
}

void psg_write(Psg *p, u8 value)
{
    static u8 latched_reg = 0;

    if (value & 0x80) {
        latched_reg = (value >> 4) & 0x07;
        int chan = latched_reg >> 1;

        if (latched_reg & 1) {
            p->atten[chan] = value & 0x0F;
        } else if (chan == 3) {
            p->noise_ctrl = value & 0x07;
            p->lfsr = 0x8000;
        } else {
            p->tone[chan] = (u16)((p->tone[chan] & 0x03F0) | (value & 0x0F));
        }
        return;
    }

    /* Data byte: upper six bits of a tone period. */
    int chan = latched_reg >> 1;
    if (latched_reg & 1) {
        p->atten[chan] = value & 0x0F;
    } else if (chan == 3) {
        p->noise_ctrl = value & 0x07;
    } else {
        p->tone[chan] = (u16)((p->tone[chan] & 0x000F)
                              | ((value & 0x3F) << 4));
    }
}

static void clock_chip(Psg *p)
{
    for (int i = 0; i < 3; i++) {
        if (p->tone[i] < 2) {
            /* A period below 2 holds the output high rather than
             * producing an inaudible squeal. */
            p->output[i] = 1;
            continue;
        }
        if (--p->counter[i] <= 0) {
            p->counter[i] = p->tone[i];
            p->output[i]  = (s8)-p->output[i];
        }
    }

    /* Noise shift rate: 0x10, 0x20, 0x40, or tone channel 2's period. */
    int rate = p->noise_ctrl & 3;
    s32 reload = (rate == 3) ? (s32)p->tone[2] : (0x10 << rate);
    if (reload < 1) {
        reload = 1;
    }

    if (--p->counter[3] <= 0) {
        p->counter[3] = reload;

        u16 feedback;
        if (p->noise_ctrl & 4) {
            /* White noise: taps 0 and 3. */
            feedback = (u16)(((p->lfsr) ^ (p->lfsr >> 3)) & 1);
        } else {
            feedback = (u16)(p->lfsr & 1);
        }
        p->lfsr = (u16)((p->lfsr >> 1) | (feedback << 15));
        p->noise_out = (s8)((p->lfsr & 1) ? 1 : -1);
    }
}

void psg_render(Psg *p, float *out, int frames, float gain)
{
    for (int i = 0; i < frames; i++) {
        p->phase += p->step;
        while (p->phase >= 1.0) {
            clock_chip(p);
            p->phase -= 1.0;
        }

        float sample = 0.0f;
        for (int c = 0; c < 3; c++) {
            sample += (float)p->output[c] * PSG_VOLUME[p->atten[c]];
        }
        sample += (float)p->noise_out * PSG_VOLUME[p->atten[3]];

        /* Four channels at full tilt would clip; scale to headroom. */
        out[i] += sample * gain * 0.25f;
    }
}
