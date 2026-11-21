#include "fm.h"
#include "drums.h"
#include "instruments.h"

/* Nuked-OPN2 is not in the tree -- copy ym3438.c and ym3438.h into
 * src/audio/ and the Makefile's wildcard picks them up. Without it this
 * file still builds, so that `make headless` keeps working, and fm_init
 * reports the core as absent rather than failing the link. */
#if defined(__has_include)
#  if __has_include("ym3438.h")
#    define FM_HAVE_NUKED 1
#  endif
#endif

#ifdef FM_HAVE_NUKED
#include "ym3438.h"
#endif

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Nuked's chip-type argument is a bitmask; bit 0 selects YM2612 mode,
 * which is what a Mega Drive has -- it enables the 9-bit DAC ladder that
 * gives the chip its characteristic crunch. If your ym3438.h names the
 * constant, prefer it over this literal. */
#define FM_CHIP_TYPE 0x01

bool fm_init(Fm *f, int sample_rate)
{
    memset(f, 0, sizeof *f);

#ifdef FM_HAVE_NUKED
    f->chip = calloc(1, sizeof(ym3438_t));
    if (f->chip == NULL) {
        return false;
    }

    OPN2_SetChipType(FM_CHIP_TYPE);
    OPN2_Reset((ym3438_t *)f->chip);

    f->step  = (double)FM_NATIVE_RATE / (double)sample_rate;
    f->phase = 1.0;   /* prime the resampler so the first frame is real */
    f->idle_frames = sample_rate / 2;     /* (build 184) fm_idle's half second */
    return true;
#else
    (void)sample_rate;
    return false;
#endif
}

void fm_shutdown(Fm *f)
{
    free(f->chip);
    f->chip = NULL;
    free(f->instruments);
    f->instruments = NULL;
    free(f->z80_ram);
    f->z80_ram = NULL;
}

void fm_reset(Fm *f)
{
    if (f->chip == NULL) {
        return;
    }
#ifdef FM_HAVE_NUKED
    OPN2_Reset((ym3438_t *)f->chip);
#endif
    f->q_head      = 0;
    f->q_tail      = 0;
    f->q_addr_sent = false;
    /* The DC blockers and the resampler keep their state: the chip's
     * standing DC is the same after a reset as before, so zeroing the
     * blockers would put it back as a step -- a thump (-34 dBFS, measured)
     * on every switch to the chip. */
    f->dac_active    = false;
    f->dac_due       = false;
    f->dac_addr_sent = false;
    f->dac_value     = 0;
    f->dac_pos       = 0;
    f->dac_end       = 0;
    f->dac_step      = 0.0;
    f->dac_phase     = 0.0;
    /* (build 111) Every channel the music's until an effect strikes one:
     * the switch's replay marks them again. */
    memset(f->effect_ch, 0, sizeof f->effect_ch);
}

/* A mark in the write queue: no chip write, taken the moment it reaches
 * the head (fm_native_sample), so it costs the queue no time. */
#define FM_MARK_PORT 0xFF

void fm_mark_channel(Fm *f, int channel, bool effect)
{
    int next = (f->q_head + 1) % FM_WRITE_QUEUE;
    if (next == f->q_tail || channel < 0 || channel >= 6) {
        return;
    }
    f->queue[f->q_head].port  = FM_MARK_PORT;
    f->queue[f->q_head].reg   = (u8)channel;
    f->queue[f->q_head].value = effect ? 1 : 0;
    f->q_head = next;
}

void fm_write(Fm *f, int part, u8 reg, u8 value)
{
    int next = (f->q_head + 1) % FM_WRITE_QUEUE;
    if (next == f->q_tail) {
        return;   /* full; dropping beats stalling */
    }

    f->queue[f->q_head].port  = (u8)(part ? 2 : 0);
    f->queue[f->q_head].reg   = reg;
    f->queue[f->q_head].value = value;
    f->q_head = next;
}

/* (build 125) fm_set_reader's bytes, else the disk's: (build 136b, 136d)
 * drums.c and instruments.c read through it. */
static Ps2Reader g_reader;

void fm_set_reader(Ps2Reader reader)
{
    g_reader = reader;
}

/* (build 136d) instruments.c builds the bytes SetFMIns sends from the named
 * fields; the bank's layout is the one the containers had. */
_Static_assert(FM_INS_BYTES == INSTRUMENT_REGISTERS,
               "an instrument is the 26 writes SetFMIns makes");

bool fm_load_instruments(Fm *f, const char *path)
{
    u8  order[INSTRUMENT_REGISTERS];
    u8 *bank  = NULL;
    int count = 0;
    if (!instruments_load(path, g_reader, order, &bank, &count)) {
        return false;
    }
    memcpy(f->reg_order, order, sizeof order);
    free(f->instruments);
    f->instruments      = bank;
    f->instrument_count = count;
    return true;
}

bool fm_load_drums(Fm *f, const char *path)
{
    /* (build 136b) drums.json and its WAVs (drums.c), through the reader
     * fm_set_reader gave, else the disk. */
    DrumEntry table[DRUM_MAX];
    u8 *ram = malloc(DRUM_Z80_RAM);
    int count = 0;

    if (ram == NULL || !drums_load(path, g_reader, table, &count, ram)) {
        free(ram);
        return false;
    }
    for (int i = 0; i < count && i < FM_DAC_DRUMS; i++) {
        f->drums[i].byte   = table[i].byte;
        f->drums[i].delay  = table[i].delay;
        f->drums[i].addr   = table[i].addr;
        f->drums[i].length = table[i].length;
    }
    f->z80_ram    = ram;
    f->drum_count = (count < FM_DAC_DRUMS) ? count : FM_DAC_DRUMS;
    return true;
}

/* zloc_68: bits 0..6 of the drum byte are tested in that order and the
 * first one set wins; a bare $80 is its own entry. The music only ever
 * sends single-bit bytes, so the table lookup and the bit scan agree,
 * but the scan is what the Z80 does so the scan is what runs. */
void fm_dac_play(Fm *f, u8 drum_byte)
{
    const FmDrum *d = NULL;
    u8 want = 0x80;
    double t_states;

    if (f->z80_ram == NULL || !(drum_byte & 0x80)) {
        return;
    }
    for (int bit = 0; bit < 7; bit++) {
        if (drum_byte & (1 << bit)) {
            want = (u8)(0x80 | (1 << bit));
            break;
        }
    }
    for (int i = 0; i < f->drum_count; i++) {
        if (f->drums[i].byte == want) {
            d = &f->drums[i];
        }
    }
    if (d == NULL) {
        return;
    }

    /* Per byte, from zloc_F8 round to itself: ld b,a 4; djnz 13 per
     * pass but 8 on the last; ex 4; ld a,(de) 7; srl 8; ld c 7; call 17;
     * then zloc_F: push 11, ld a 7, ld (1FFD) 13, ld a,c 4, ld (4000) 13,
     * one pass of the busy check 28, pop 10, nop 4, ld (4001) 13, xor 4,
     * ld (1FFD) 13, ret 10; then inc de 6, dec hl 6, ld a,l 4, or h 4,
     * jr 7, ld a,(1FFF) 13, or 4, jp 10, ex 4, jp 10. That is 253 with
     * one djnz pass, so 240 + 13 * delay. The busy flag is only raised
     * by data writes and clears in ~90 T-states, well inside the loop,
     * so the check never spins. The 68k's bus grabs around its own
     * register writes stall this loop on real hardware and are ignored. */
    t_states = 240.0 + 13.0 * (double)d->delay;

    f->dac_active = true;
    f->dac_due    = false;
    f->dac_pos    = d->addr;
    f->dac_end    = d->addr + d->length;
    f->dac_step   = ((double)FM_Z80_CLOCK / t_states) / (double)FM_NATIVE_RATE;
    f->dac_phase  = 1.0;   /* first byte goes out on the next sample */
}

int fm_pending_writes(const Fm *f)
{
    int n = f->q_head - f->q_tail;
    return (n < 0) ? (n + FM_WRITE_QUEUE) : n;
}

void fm_set_instrument(Fm *f, int channel, int instrument)
{
    if (instrument < 0 || instrument >= f->instrument_count) {
        return;
    }

    int part = (channel >= 3) ? 1 : 0;
    int slot = channel % 3;
    const u8 *ins = &f->instruments[(size_t)instrument * FM_INS_BYTES];

    for (int i = 0; i < FM_INS_BYTES; i++) {
        u8 reg = f->reg_order[i];

        /* $B0 and $B4 are per-channel; the operator registers are
         * per-channel too, offset by the slot within the part. */
        fm_write(f, part, (u8)(reg + slot), ins[i]);
    }
}

/* fnum = f * 144 * 2^(21 - block) / master_clock */
static u16 fm_fnum(double hz, int block)
{
    double fnum = hz * 144.0 * pow(2.0, 21 - block)
                / (double)FM_MASTER_CLOCK;
    if (fnum < 0.0) {
        fnum = 0.0;
    }
    if (fnum > 2047.0) {
        fnum = 2047.0;
    }
    return (u16)(fnum + 0.5);
}

void fm_note_on(Fm *f, int channel, double hz)
{
    int part = (channel >= 3) ? 1 : 0;
    int slot = channel % 3;

    /* Pick the block that lands fnum in the upper half of its range,
     * where the chip's pitch resolution is best. */
    int block = 0;
    while (block < 7 && fm_fnum(hz, block) > 1023) {
        block++;
    }
    u16 fnum = fm_fnum(hz, block);

    fm_write(f, part, (u8)(0xA4 + slot),
             (u8)(((block & 7) << 3) | ((fnum >> 8) & 7)));
    fm_write(f, part, (u8)(0xA0 + slot), (u8)(fnum & 0xFF));

    /* Key-on is always written to part 0, with the channel encoded. */
    fm_write(f, 0, 0x28, (u8)(0xF0 | (slot + (part ? 4 : 0))));
}

void fm_note_off(Fm *f, int channel)
{
    int part = (channel >= 3) ? 1 : 0;
    int slot = channel % 3;

    fm_write(f, 0, 0x28, (u8)(slot + (part ? 4 : 0)));
}

/* One output sample at the chip's native rate: 24 clocks, summed.
 *
 * Every write is issued at one of two points in the sample, and the
 * spacing is not optional. Nuked commits an FM register write when its
 * internal cycle reaches that register's slot, up to 24 clocks after
 * the data byte, and an address write arriving before then cancels the
 * commit. So a data write at the end of sample N owns the whole of
 * sample N+1: the next address write, whoever's it is, waits for the
 * end of N+1. That is the same rule as the queue's one-write-per-sample
 * cadence, and it is why the 68k driver halts the Z80 around every
 * register write on real hardware -- the Z80's DAC writes to $2A
 * would otherwise land between an address and its data.
 *
 * The DAC byte takes the end-of-sample slot for its address, when the
 * queue is not mid-pair, and puts its data out at clock 12 of the next
 * sample. $2A commits immediately (it is a mode register, not a slot
 * register), so nothing has to wait after it. A byte is therefore
 * delayed by at most one sample when it collides with a queue pair;
 * the phase accumulator keeps the average rate exact.
 *
 * Left and right come out separately: Nuked's two outputs, each the
 * channels whose $B4 lets them through to that side. */
static void fm_native_sample(Fm *f, float out[2])
{
#ifndef FM_HAVE_NUKED
    (void)f;
    out[0] = 0.0f;
    out[1] = 0.0f;
#else
    ym3438_t *chip = (ym3438_t *)f->chip;
    Bit16s buf[2];
    Bit32s l = 0;
    Bit32s r = 0;

    if (f->dac_active) {
        f->dac_phase += f->dac_step;
        if (f->dac_phase >= 1.0) {
            f->dac_phase -= 1.0;
            f->dac_due = true;
        }
    }

    if (!f->weighted) {
        for (int i = 0; i < 24; i++) {
            OPN2_Clock(chip, buf);
            l += buf[0];
            r += buf[1];
            if (i == 12 && f->dac_addr_sent) {
                OPN2_Write(chip, 1, f->dac_value);
                f->dac_addr_sent = false;
            }
        }
        /* Six channels of 14-bit output summed per side, scaled to
         * roughly -1..1. The scale is the old mono mix's per side: that
         * was (l + r) / 16384, so a voice on both sides is exactly as loud
         * in each speaker as it was, and one panned hard is twice what the
         * fold-down gave it. */
        out[0] = (float)l / 8192.0f;
        out[1] = (float)r / 8192.0f;
    } else {
        /* (build 111) Each group of four clocks carries one channel: the
         * one Nuked's OPN2_ChOutput latches when the cycle is a multiple
         * of four (the channel counter, plus one before cycle 12). */
        float lw = 0.0f;
        float rw = 0.0f;

        for (int i = 0; i < 24; i++) {
            if ((chip->cycles & 3) == 0) {
                f->lock_ch = (int)((chip->channel + (chip->cycles < 12 ? 1 : 0))
                                   % 6);
            }
            OPN2_Clock(chip, buf);
            float w = f->weight[f->effect_ch[f->lock_ch] ? 1 : 0];
            lw += (float)buf[0] * w;
            rw += (float)buf[1] * w;
            if (i == 12 && f->dac_addr_sent) {
                OPN2_Write(chip, 1, f->dac_value);
                f->dac_addr_sent = false;
            }
        }
        out[0] = lw / 8192.0f;
        out[1] = rw / 8192.0f;
    }

    /* (build 111) Marks at the head are taken now, between writes: no
     * sample slot is spent on them, so the chip's writes keep their time. */
    while (!f->q_addr_sent && f->q_tail != f->q_head
           && f->queue[f->q_tail].port == FM_MARK_PORT) {
        f->effect_ch[f->queue[f->q_tail].reg] = f->queue[f->q_tail].value != 0;
        f->q_tail = (f->q_tail + 1) % FM_WRITE_QUEUE;
    }

    if (f->dac_due && !f->q_addr_sent) {
        OPN2_Write(chip, 0, 0x2A);
        f->dac_addr_sent = true;
        f->dac_due = false;
        f->dac_value = (u8)(f->z80_ram[f->dac_pos & (FM_Z80_RAM - 1)] >> 1);
        f->dac_pos++;
        if (f->dac_pos >= f->dac_end) {
            f->dac_active = false;
        }
    } else if (f->q_tail != f->q_head) {
        const FmWrite *w = &f->queue[f->q_tail];

        if (!f->q_addr_sent) {
            OPN2_Write(chip, w->port, w->reg);
            f->q_addr_sent = true;
        } else {
            OPN2_Write(chip, (Bit32u)(w->port + 1), w->value);
            f->q_addr_sent = false;
            f->q_tail = (f->q_tail + 1) % FM_WRITE_QUEUE;
        }
    }

#endif
}

/* One-pole high-pass, standing in for the AC coupling on the real
 * hardware's output stage. Without it the chip's DC offset dwarfs the
 * audio. */
static float fm_dc_block(Fm *f, int side, float x)
{
    const float r = 0.9995f;
    float y = x - f->dc_x1[side] + r * f->dc_y1[side];

    f->dc_x1[side] = x;
    f->dc_y1[side] = y;
    return y;
}

/* (build 184) Whether the chip sounds nothing now: no write waiting, no
 * drum, every operator's envelope out at full attenuation (Nuked's eg_out,
 * TL and the LFO's AM in it), where an operator puts out nothing and the
 * chip's output is its standing DC alone. */
static bool fm_silent(const Fm *f)
{
#ifdef FM_HAVE_NUKED
    const ym3438_t *chip = (const ym3438_t *)f->chip;

    if (f->q_head != f->q_tail || f->q_addr_sent || f->dac_active
        || f->dac_due || f->dac_addr_sent) {
        return false;
    }
    for (int slot = 0; slot < 24; slot++) {
        if (chip->eg_out[slot] < 0x3ff) {
            return false;
        }
    }
    return true;
#else
    (void)f;
    return true;
#endif
}

void fm_settle(Fm *f)
{
    if (f->chip == NULL) {
        return;
    }
    /* the reset chip's output, steady after its pipeline has run through */
    float s[2] = { 0.0f, 0.0f };
    for (int i = 0; i < 64; i++) {
        fm_native_sample(f, s);
    }
    for (int side = 0; side < 2; side++) {
        f->dc_x1[side] = s[side];
        f->dc_y1[side] = 0.0f;
        f->prev[side]  = 0.0f;
        f->next[side]  = 0.0f;
    }
    f->quiet_frames = f->idle_frames;
}

bool fm_idle(const Fm *f)
{
    if (f->chip == NULL) {
        return true;
    }
    /* a write waiting wakes it: fm_render alone counts quiet frames */
    if (f->q_head != f->q_tail || f->dac_active || f->dac_due) {
        return false;
    }
    return f->quiet_frames >= f->idle_frames;
}

void fm_render(Fm *f, float *out, int frames, float gain)
{
    if (f->chip == NULL) {
        return;
    }
    for (int i = 0; i < frames; i++) {
        while (f->phase >= 1.0) {
            float s[2];

            fm_native_sample(f, s);
            for (int c = 0; c < 2; c++) {
                f->prev[c] = f->next[c];
                f->next[c] = fm_dc_block(f, c, s[c]);
            }
            f->phase -= 1.0;
        }

        float t = (float)f->phase;
        for (int c = 0; c < 2; c++) {
            out[2 * i + c] += (f->prev[c] + (f->next[c] - f->prev[c]) * t)
                            * gain;
        }
        f->phase += f->step;
    }
    /* (build 184) fm_idle's count: bookkeeping only, the output untouched */
    if (!fm_silent(f)) {
        f->quiet_frames = 0;
    } else if (f->quiet_frames < f->idle_frames) {
        f->quiet_frames += frames;
    }
}

/* (build 111) The EFFECTS volume on the chip. The bus takes the larger gain
 * and each channel its share of it, by whether an effect holds it; equal
 * gains are fm_render exactly. */
void fm_render_split(Fm *f, float *out, int frames, float music_gain,
                     float effects_gain)
{
    if (music_gain == effects_gain) {
        f->weighted = false;
        fm_render(f, out, frames, music_gain);
        return;
    }
    float bus = (music_gain > effects_gain) ? music_gain : effects_gain;

    f->weighted  = true;
    f->weight[0] = music_gain / bus;
    f->weight[1] = effects_gain / bus;
    fm_render(f, out, frames, bus);
}
