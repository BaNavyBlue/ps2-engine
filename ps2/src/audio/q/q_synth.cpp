/* (build 110) The Q backend: the sequencer's chip writes re-voiced with
 * Cycfi's Q (docs/audio.md, "The Q backend"). Moved from build 109's
 * tools/q_demo.cpp (the preliminary test), where it was measured against
 * Nuked-OPN2; what changed in the move: the drums from the ROM's samples as
 * a second mode, a reset, the bank's trim, and the synth table's entry.
 *
 * C++20, header-only Q (q_lib and its infra submodule, -isystem). Built
 * only by `make Q=1` (the game), `make audio_check_q` and `make q_demo`;
 * `make`, `make headless` and `make route_probe` never see it. C reaches it
 * only through extern "C": q_synth.h's calls and q_synth_backend.
 *
 * (110) The notes came from the register writes, decoded (docs/audio.md's
 * first place for a note sink). (111) In the game they come from the
 * sequencer's hooks, by track: an effect plays on a voice of its own and the
 * music it would have displaced plays on; the MUSIC and EFFECTS gains apply
 * to each. The decoder remains for the tools that hand the bank register
 * writes alone (q_demo, drum_hits).
 *
 * (111) Two voicings: the chip's phase modulation in float (the default,
 * "The PM voicing" below) and 110's subtractive approximation
 * (PS2_Q_VOICE=subtractive). */

extern "C" {
#include "q_synth.h"
#include "../synth.h"
#include "../fm.h"
#include "../drums.h"
#include "../smps.h"
}
#include <q/support/literals.hpp>
#include <q/support/phase.hpp>
#include <q/synth/sin_osc.hpp>
/* (build 139d) Q 1.5 (October 2026) moved the generators into q/synth/gen/
 * and the oscillators into q/synth/va/; Q before it has them all in
 * q/synth/. Either is taken: the names inside are the same. */
#if __has_include(<q/synth/gen/envelope_gen.hpp>)
#include <q/synth/va/saw_osc.hpp>
#include <q/synth/va/square_osc.hpp>
#include <q/synth/va/pulse_osc.hpp>
#include <q/synth/gen/envelope_gen.hpp>
#include <q/synth/gen/exponential_gen.hpp>
#else
#include <q/synth/saw_osc.hpp>
#include <q/synth/square_osc.hpp>
#include <q/synth/pulse_osc.hpp>
#include <q/synth/envelope_gen.hpp>
#include <q/synth/exponential_gen.hpp>
#endif
#include <q/fx/svf.hpp>
#include <q/utility/interpolation_primitives.hpp>
#include <q/fx/clip.hpp>
#include <q/fx/dc_block.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <numbers>   /* (build 113) std::numbers::pi: MinGW has no M_PI */
#include <string>
#include <vector>


namespace q = cycfi::q;
using namespace q::literals;

namespace {

constexpr double kNativeRate = FM_MASTER_CLOCK / 144.0;   /* 53267 Hz */
constexpr double kForever    = 3600.0;                    /* seconds */

/* The Q bank's own levels, matched to the OPN2 path by loudness (the RMS of
 * the title's theme and of each drum; docs/audio.md, "The Q test"). */
constexpr float kFmVoiceGain = 0.110f;
constexpr float kDrumGain    = 0.091f;

/* (110) The ROM's drums against the chip's, by RMS (--solo drums), and the
 * bank's trim by drum mode: its peaks against the chip's over every song,
 * the median of the 23 songs' differences, set by `q_demo --trim-pass`
 * (docs/audio.md, "The Q backend"). The synthesised drums are the peakier
 * (their kick goes through a clip), so their mode's trim is the lower. */
constexpr float kSampleDrumGain = 1.10f;   /* +0.8 dB: title, My Home */
constexpr float kQTrim[2] = {
    0.798f,    /* Q_DRUMS_SYNTH:   -2.0 dB (110b's drums; 110a's 0.760) */
    0.929f,    /* Q_DRUMS_SAMPLES: -0.6 dB */
};

/* ------------------------------------------------------------------------
 * The YM2612's envelope, as times.
 *
 * Attenuation is 10 bits, 0.09375 dB a step (a TL step, 0.75 dB, is 8 of
 * them; an SL step, 3 dB, is 32). The generator moves once every three
 * native samples. At an effective rate r (0-63) it steps every 2^(11 - r/4)
 * clocks below 48, by an increment whose average over its 8-entry row is
 * 0.5, 0.625, 0.75 or 0.875 by r & 3; from 48 up every clock, by 1-1.75,
 * 2-3.5, 4-7 and 8. Decay is linear in steps; attack multiplies the
 * attenuation by (1 - inc/16) each step, and is instant from 62. These give
 * the time of each phase, which is all a Q envelope takes. */

constexpr double kEgClock   = 3.0 / kNativeRate;
constexpr double kStepDb    = 0.09375;
constexpr double kSteps40Db = 40.0 / kStepDb;

double eg_steps_per_clock(int rate)
{
    static const double row[4] = { 0.5, 0.625, 0.75, 0.875 };

    if (rate < 2) {
        return 0.0;
    }
    if (rate > 63) {
        rate = 63;
    }
    int group = rate >> 2;
    int sub   = rate & 3;
    if (group <= 11) {
        return row[sub] / double(1 << (11 - group));
    }
    if (group == 15) {
        return 8.0;
    }
    return row[sub] * 2.0 * double(1 << (group - 12));
}

double eg_decay_seconds(int rate, double steps)
{
    double s = eg_steps_per_clock(rate);

    if (s <= 0.0) {
        return kForever;
    }
    return steps / s * kEgClock;
}

double eg_attack_seconds(int rate)
{
    if (rate >= 62) {
        return 0.0;
    }
    double s = eg_steps_per_clock(rate);
    if (s <= 0.0) {
        return kForever;
    }
    /* 0.85: Nuked's attacks, timed to -1 dB (one operator, rates 10-46),
     * come in 6-25% under this model's, 15% at most rates. */
    return 0.85 * std::log(1024.0) / -std::log(1.0 - s / 16.0) * kEgClock;
}

/* The key code the rate scaling and the detune read: the block, and two
 * bits from the F-number's top four (Nuked-OPN2's fn_note). */
int key_code(int block, int fnum)
{
    static const int fn_note[16] = {
        0, 0, 0, 0, 0, 0, 0, 1, 2, 3, 3, 3, 3, 3, 3, 3
    };

    return (block << 2) | fn_note[(fnum >> 7) & 15];
}

/* The detune, as Nuked-OPN2 works it out (OPN2_PhaseCalcIncrement): steps
 * of the phase increment by key code, before the multiple. A step is
 * 53267 / 2^20 Hz, about 0.05 Hz -- measured, not derived: the title's bass
 * instrument held on Nuked beats at 0.258 Hz at block 2 and 0.400 Hz at
 * block 3, which are its detunes' 5 and 8 steps apart at that size. */
float detune_hz(int dt, int kc)
{
    static const int pg_detune[8] = { 16, 17, 19, 20, 22, 24, 27, 29 };
    int dt_l = dt & 3;

    if (dt_l == 0) {
        return 0.0f;
    }
    int k      = (kc > 0x1C) ? 0x1C : kc;
    int block  = k >> 2;
    int note   = k & 3;
    int sum    = block + 9 + ((dt_l == 3) | (dt_l & 2));
    int steps  = pg_detune[((sum & 1) << 2) | note] >> (9 - (sum >> 1));
    float hz   = float(steps * kNativeRate / 1048576.0);

    return (dt & 4) ? -hz : hz;
}

int effective_rate(int r5, int kc, int ks)
{
    if (r5 == 0) {
        return 0;
    }
    int r = 2 * r5 + (kc >> (3 - ks));
    return (r > 63) ? 63 : r;
}

int release_rate(int rr4, int kc, int ks)
{
    int r = 4 * rr4 + 2 + (kc >> (3 - ks));
    return (r > 63) ? 63 : r;
}

float db_steps_to_lin(double steps)
{
    return float(std::pow(10.0, -steps * kStepDb / 20.0));
}

/* A TL of 0.75 dB a step, as amplitude. */
float tl_to_lin(int tl)
{
    return float(std::pow(10.0, -0.0375 * double(tl & 0x7F)));
}

/* Nuked-OPN2 adds a modulator's 14-bit output, halved, to a 10-bit phase:
 * at full level that is +-4 cycles, an FM index of 8 pi. Feedback adds the
 * last two outputs of op 1 shifted right by 10 - FB. */
float modulator_index(int tl)
{
    return float(8.0 * q::pi) * tl_to_lin(tl);
}

float feedback_index(int fb)
{
    return (fb == 0) ? 0.0f : float(q::pi * std::pow(2.0, fb - 5));
}

/* Operators 1-4 by their register offset ($30 + ...): 0, 8, 4, $C. The
 * instrument bank stores them in operator order and SetFMIns writes them
 * to $x0, $x8, $x4, $xC (docs/audio.md). */
constexpr int kOpOffset[4] = { 0x0, 0x8, 0x4, 0xC };

/* Per algorithm: which operators are carriers, and which operators
 * modulate each operator directly (bit n = operator n + 1). */
constexpr u8 kCarriers[8] = { 0x8, 0x8, 0x8, 0x8, 0xA, 0xE, 0xE, 0xF };
constexpr u8 kModulators[8][4] = {
    { 0x0, 0x1, 0x2, 0x4 },   /* 0: 1 > 2 > 3 > 4 */
    { 0x0, 0x0, 0x3, 0x4 },   /* 1: (1 + 2) > 3 > 4 */
    { 0x0, 0x0, 0x2, 0x5 },   /* 2: (1 + (2 > 3)) > 4 */
    { 0x0, 0x1, 0x0, 0x6 },   /* 3: ((1 > 2) + 3) > 4 */
    { 0x0, 0x1, 0x0, 0x4 },   /* 4: (1 > 2) + (3 > 4) */
    { 0x0, 0x1, 0x1, 0x1 },   /* 5: 1 > (2, 3, 4) */
    { 0x0, 0x1, 0x0, 0x0 },   /* 6: (1 > 2) + 3 + 4 */
    { 0x0, 0x0, 0x0, 0x0 },   /* 7: 1 + 2 + 3 + 4 */
};

enum class Wave {
    Sine,
    Saw,
    Square,
    Pulse,
};

/* An operator's envelope: attack to full, decay to the sustain level,
 * sustain falling to silence (the second decay rate), release. Q's own
 * adsr_envelope_gen makes its sustain fall linearly in amplitude; these are
 * all exponential, nearer the chip's decay, which is linear in dB. */
q::envelope_gen make_op_envelope(float sps)
{
    return q::envelope_gen{
        q::make_envelope_segment<q::exp_upward_ramp_gen>(1_ms, 1.0f, sps),
        q::make_envelope_segment<q::exp_downward_ramp_gen>(1_ms, 0.5f, sps),
        q::make_envelope_segment<q::exp_downward_ramp_gen>(1_s, 0.0f, sps),
        q::make_envelope_segment<q::exp_downward_ramp_gen>(100_ms, 0.0f, sps)
    };
}

/* A segment of zero width returns one sample of garbage in Q (a linear
 * ramp divides by it; an exponential one overshoots), so none is shorter
 * than half a millisecond. */
q::duration at_least(double seconds)
{
    return q::duration{(seconds < 0.0005) ? 0.0005 : seconds};
}

/* ------------------------------------------------------------------------
 * One FM channel, re-voiced.
 *
 * Each carrier becomes a Q oscillator through an svf low-pass. What the
 * chip's modulators would do to it decides the rest: the frequency ratio of
 * its strongest modulator picks the waveform (1: all harmonics, a saw; 2:
 * odd ones, a square; k: every k-th missing, a pulse of width 1/k; weak
 * modulation: a sine), and the modulation index -- the modulators' total
 * levels through their own envelopes -- opens the filter as it would widen
 * the chip's spectrum. The carriers' envelopes and levels are the chip's. */
struct Voice
{
    explicit Voice(float sps);
    Voice(Voice const&) = delete;
    Voice& operator=(Voice const&) = delete;
    Voice(Voice&&) = default;
    Voice& operator=(Voice&&) = default;

    std::array<q::envelope_gen, 4>   env;
    std::array<q::phase_iterator, 4> phase;
    std::array<q::svf, 4>            filter;
    std::array<q::pulse_osc, 4>      pulse;
    std::array<q::phase, 4>          pulse_offset{};
    std::array<Wave, 4>              wave{};
    std::array<float, 4>             mul{};
    std::array<float, 4>             detune_hz{};
    std::array<float, 4>             beta_peak{};   /* as a modulator */
    std::array<float, 4>             ratio{};       /* strongest mod / this */
    std::array<float, 4>             level{};       /* carrier, smoothed */
    std::array<float, 4>             level_target{};
    float sps;
    int   algorithm = 0;
    float feedback_beta = 0.0f;
    float base_hz = 0.0f;
    float pan_l = 1.0f;
    float pan_r = 1.0f;
    float pan_l_target = 1.0f;
    float pan_r_target = 1.0f;
    int   cutoff_every = 1;
    int   cutoff_count = 0;

    bool  active() const;
    float render_one(float& left, float& right);
};

Voice::Voice(float sps_)
 : filter{{ q::svf{1_kHz, sps_, 0.9}, q::svf{1_kHz, sps_, 0.9},
            q::svf{1_kHz, sps_, 0.9}, q::svf{1_kHz, sps_, 0.9} }}
 , sps{sps_}
{
    for (auto& e : env) {
        e = make_op_envelope(sps_);
    }
}

bool Voice::active() const
{
    for (int i = 0; i < 4; i++) {
        if ((kCarriers[algorithm] >> i) & 1) {
            if (!env[i].in_idle_phase()) {
                return true;
            }
        }
    }
    return false;
}

float Voice::render_one(float& left, float& right)
{
    float e[4];
    float beta_mod[4];
    float sum = 0.0f;
    bool  retune = (--cutoff_count <= 0);

    if (retune) {
        cutoff_count = cutoff_every;
    }
    for (int i = 0; i < 4; i++) {
        e[i] = env[i]();
    }
    for (int i = 0; i < 4; i++) {
        float up = 0.0f;
        u8 mods = kModulators[algorithm][i];

        for (int m = 0; m < i; m++) {
            if ((mods >> m) & 1) {
                up += beta_mod[m];
            }
        }
        if (i == 0) {
            up += feedback_beta;
        }
        beta_mod[i] = beta_peak[i] * e[i] * (1.0f + 0.5f * up);

        if (!((kCarriers[algorithm] >> i) & 1)) {
            continue;
        }
        level[i] += (level_target[i] - level[i]) * 0.01f;

        /* Every waveform's fundamental in phase with the sine's, as the
         * chip's carriers are: two carriers at one pitch then add, as they
         * do on the chip, where a rising saw against a square would cancel.
         * So the saw falls, and the pulse is read a quarter cycle less half
         * its width on. */
        float x = 0.0f;
        switch (wave[i]) {
        case Wave::Sine:
            x = q::sin(phase[i]++);
            break;
        case Wave::Saw:
            x = -q::saw(phase[i]++);
            break;
        case Wave::Square:
            x = q::square(phase[i]++);
            break;
        case Wave::Pulse:
            x = pulse[i](phase[i]._phase + pulse_offset[i], phase[i]._step);
            phase[i]++;
            break;
        }
        if (wave[i] != Wave::Sine) {
            if (retune) {
                float f  = base_hz * mul[i] + detune_hz[i];
                float r  = (ratio[i] > 1.0f) ? ratio[i] : 1.0f;
                float fc = f * (1.0f + 0.6f * up * r);
                float hi = 0.45f * sps;
                fc = (fc < f) ? f : fc;
                fc = (fc > hi) ? hi : fc;
                filter[i].cutoff(q::frequency{double(fc)}, sps);
            }
            x = filter[i](x);
        }
        sum += x * level[i] * e[i];
    }
    pan_l += (pan_l_target - pan_l) * 0.02f;
    pan_r += (pan_r_target - pan_r) * 0.02f;
    left  += sum * pan_l;
    right += sum * pan_r;
    return sum;
}

/* ------------------------------------------------------------------------
 * (build 111) The PM voicing: the chip's own FM, in float.
 *
 * Four operators, each a sine (Q's quarter-wave table, `q::sin`, on Q's
 * 32-bit `q::phase`) whose phase the others push, wired by the chip's eight
 * algorithms exactly as Nuked-OPN2 wires them (its fm_algorithm table, read
 * in full): which operator feeds which, and when -- the chip works its
 * operators in the order 1, 3, 2, 4, so where operator 2 feeds operator 3
 * (algorithms 0-2) operator 3 hears operator 2's output from the sample
 * before. Operator 1 feeds itself from its last two outputs, as the chip
 * does, those taken a native sample and two apart (19 and 38 us) whatever
 * the rate here, so the feedback's character is the chip's. The depths are
 * the chip's: a modulator at full level moves its carrier four cycles
 * (Nuked adds a 14-bit output, halved, to a 10-bit phase), feedback
 * (o1 + o2) x 2^(FB - 7) cycles.
 *
 * The pitch is the chip's to the bit: the phase increment worked out as
 * OPN2_PhaseCalcIncrement does -- the F-number and block, the detune from
 * the key code, the multiple, the 17- and 20-bit wraps -- and then taken as
 * a frequency. The envelope is the chip's generator run as the chip runs
 * it: one step every three native samples on a 12-bit timer, each rate's
 * increments from its row of the step table, the attack's exponential
 * (level -= (level + 1) x 2^inc / 32), the linear decays, the sustain level
 * and key scaling as Nuked has them; the attenuation it keeps (10 bits,
 * 0.094 dB a step, TL added x 8) becomes a gain, 2^(-att/64), and is
 * interpolated between steps, so the chip's timing without its stairs.
 *
 * Kept from the chip on purpose: each channel's sum of its carriers is
 * clamped as the chip's 9-bit accumulator clamps it (after each carrier, in
 * the order 1, 3, 2, 4), which is part of how algorithms 4-7 sound at full
 * level. Left out: the 14-bit operator output, the 10-bit phase into a
 * 256-entry log-sine, the 9-bit DAC and its ladder crossover, and the
 * stairs of the envelope -- the chip's grain -- and the LFO and SSG-EG,
 * which nothing in the game switches on.
 *
 * Aliasing: phase modulation at the chip's depths throws partials far past
 * any output rate (a modulator at full level spreads its carrier over some
 * 25 times its own frequency). The operators run oversampled -- 4 x 48 kHz
 * by default, the AUDIO page's OVERSAMPLE (111a) or PS2_Q_OVERSAMPLE=1|2|4
 * -- and the voices' sum comes down
 * through a Kaiser-windowed low-pass (flat to 19 kHz, 80 dB down from 26
 * kHz) before every fourth sample is kept, so what folds back lands above
 * 22 kHz; the chip itself runs at 53 kHz and folds its partials straight
 * into the audible band. docs/audio.md, "The PM voicing", has the
 * measurements. */

constexpr int kPgDetune[8] = { 16, 17, 19, 20, 22, 24, 27, 29 };
constexpr int kFnNote[16]  = { 0, 0, 0, 0, 0, 0, 0, 1, 2, 3, 3, 3, 3, 3, 3, 3 };
constexpr u8  kEgStepHi[4][4] = {
    { 0, 0, 0, 0 },
    { 1, 0, 0, 0 },
    { 1, 0, 1, 0 },
    { 1, 1, 1, 0 }
};

/* The chip's slot order: operator 1, 3, 2, 4 (register offsets 0, 4, 8, C). */
constexpr int kChipOrder[4] = { 0, 2, 1, 3 };

enum class EgState : u8 {
    Attack,
    Decay,
    Sustain,
    Release
};

struct PmOp
{
    /* the registers, as the chip holds them */
    int multi = 1;          /* the multiple x 2, 1 for a multiple of 0 */
    int dt    = 0;
    int tl    = 0x7F;
    int ks    = 0;
    int ar    = 0;
    int dr    = 0;
    int sr    = 0;
    int sl    = 0;          /* 5 bits: 15 is 31, as Nuked keeps it */
    int rr    = 0;

    q::phase phase{};
    q::phase step{};

    EgState state = EgState::Release;
    int     level = 0x3FF;  /* 10-bit attenuation */
    float   amp_prev = 0.0f;
    float   amp_next = 0.0f;

    int attenuation() const
    {
        int a = level + (tl << 3);
        return (a > 0x3FF) ? 0x3FF : a;
    }
};

float att_to_gain(int att)
{
    return (att >= 0x3FF) ? 0.0f : std::exp2(-float(att) / 64.0f);
}

/* The chip's phase increment (OPN2_PhaseCalcIncrement, the LFO off): 20-bit
 * phase units a native sample. */
u32 pg_increment(int fnum, int block, int kcode, int dt, int multi)
{
    u32 f = u32(fnum) << 1;
    u32 base = (f << block) >> 2;
    int dt_l = dt & 3;
    u32 detune = 0;

    if (dt_l != 0) {
        int kc   = (kcode > 0x1C) ? 0x1C : kcode;
        int blk  = kc >> 2;
        int note = kc & 3;
        int sum  = blk + 9 + ((dt_l == 3) | (dt_l & 2));
        detune = u32(kPgDetune[((sum & 1) << 2) | note] >> (9 - (sum >> 1)));
    }
    if (dt & 4) {
        base -= detune;
    } else {
        base += detune;
    }
    base &= 0x1FFFF;
    return ((base * u32(multi)) >> 1) & 0xFFFFF;
}

/* One of the chip's envelope steps for one operator (OPN2_EnvelopePrepare and
 * OPN2_EnvelopeADSR, SSG-EG and the AM LFO off): the rate by state, key
 * scaling, the increment this step of the timer gives it, then the state
 * machine. `shift` is the step's timer shift (its lowest set bit + 1, 0 for
 * none), `low` its low two bits. */
void eg_step(PmOp& o, int kcode, int shift, int low)
{
    int rate5 = 0;
    switch (o.state) {
    case EgState::Attack:
        rate5 = o.ar;
        break;
    case EgState::Decay:
        rate5 = o.dr;
        break;
    case EgState::Sustain:
        rate5 = o.sr;
        break;
    case EgState::Release:
        rate5 = (o.rr << 1) | 1;
        break;
    }
    int ksv  = kcode >> (o.ks ^ 3);
    int rate = rate5 * 2 + ksv;
    rate = (rate > 63) ? 63 : rate;
    bool ratemax = (rate >> 1) == 31;
    int inc = 0;

    if (rate5 != 0) {
        if (rate < 48) {
            int sum = ((rate >> 2) + shift) & 15;
            if (sum == 12) {
                inc = 1;
            } else if (sum == 13) {
                inc = (rate >> 1) & 1;
            } else if (sum == 14) {
                inc = rate & 1;
            }
        } else {
            inc = kEgStepHi[rate & 3][low] + (rate >> 2) - 11;
            inc = (inc > 4) ? 4 : inc;
        }
    }

    int level = o.level;
    bool off = (level & 0x3F0) == 0x3F0;
    int add = 0;

    switch (o.state) {
    case EgState::Attack:
        if (level == 0) {
            o.state = EgState::Decay;
        } else if (inc != 0 && !ratemax) {
            add = (~level * (1 << inc)) >> 5;
        }
        break;
    case EgState::Decay:
        if ((level >> 4) == (o.sl << 1)) {
            o.state = EgState::Sustain;
        } else if (!off && inc != 0) {
            add = 1 << (inc - 1);
        }
        break;
    case EgState::Sustain:
        if (!off && inc != 0) {
            add = 1 << (inc - 1);
        }
        break;
    case EgState::Release:
        if (!off && inc != 0) {
            add = 1 << (inc - 1);
        }
        break;
    }
    if (o.state != EgState::Attack && off) {
        o.state = EgState::Release;
        level = 0x3FF;
    }
    o.level = (level + add) & 0x3FF;
}

/* The operators each algorithm feeds, from Nuked's fm_algorithm: into
 * operator 2 (from 1), into operator 3 (from 1 now and 2 a sample ago), into
 * operator 4 (from 1, 2 and 3 now), and the carriers. Bit n = operator n+1. */
constexpr u8 kInto2[8] = { 0x1, 0x0, 0x0, 0x1, 0x1, 0x1, 0x1, 0x0 };
constexpr u8 kInto3[8] = { 0x2, 0x3, 0x2, 0x0, 0x0, 0x1, 0x0, 0x0 };
constexpr u8 kInto4[8] = { 0x4, 0x4, 0x5, 0x6, 0x4, 0x1, 0x0, 0x0 };

struct PmVoice
{
    std::array<PmOp, 4> op{};
    int   algorithm = 0;
    int   feedback  = 0;
    float fb_scale  = 0.0f;
    int   fnum = 0;
    int   block = 0;
    int   kcode = 0;
    float pan_l = 1.0f;
    float pan_r = 1.0f;
    float pan_l_target = 1.0f;
    float pan_r_target = 1.0f;
    bool  effect = false;   /* an effect track's: the EFFECTS gain */
    int   owner = -1;       /* the virtual channel playing it, -1 a tail */
    bool  struck = false;
    /* (111b) a tail's fade: running samples left of it, and its length */
    int   fade_left = 0;
    int   fade_len  = 0;
    /* (build 112) from the patch file, at the strike: each operator's pitch
     * moved (a ratio), the instrument's level, and how its tail ends
     * (SYNTH_TAIL_ROW: as the TAILS row) */
    std::array<float, 4> fine_mul{{ 1.0f, 1.0f, 1.0f, 1.0f }};
    float patch_gain = 1.0f;
    int   tail_mode  = SYNTH_TAIL_ROW;

    /* operator 1's past outputs, a ring at the running rate, for the
     * feedback's two taps a native sample and two back */
    std::array<float, 16> o1_hist{};
    int   o1_at = 0;
    float o2_prev = 0.0f;

    bool active() const
    {
        if (!struck) {
            return false;
        }
        for (auto const& o : op) {
            if (o.level < 0x3F0) {
                return true;
            }
        }
        return false;
    }
    /* audible: a carrier within 60 dB of full level */
    bool audible() const
    {
        if (!struck) {
            return false;
        }
        for (int i = 0; i < 4; i++) {
            if (((kCarriers[algorithm] >> i) & 1) && op[size_t(i)].attenuation() < 640) {
                return true;
            }
        }
        return false;
    }
    /* the quietest carrier's attenuation, for stealing the quietest tail */
    int loudest_att() const
    {
        int best = 0x3FF;
        for (int i = 0; i < 4; i++) {
            if ((kCarriers[algorithm] >> i) & 1) {
                best = std::min(best, op[size_t(i)].attenuation());
            }
        }
        return best;
    }
};

/* ------------------------------------------------------------------------
 * The drums, re-voiced: a tone that falls in pitch and a filtered noise,
 * each with its own decay. The six samples are the ROM's; these stand in
 * for them by what the samples measure as (docs/audio.md, "Drums"): the
 * toms' bodies at 214, 195 and 165 Hz, the snare's at 214 Hz with noise
 * centred near 2.3 kHz, the short loud 60 Hz one a kick, the two noises
 * hats. Like the DAC, one drum at a time: a new hit replaces the last. */
struct DrumPatch
{
    u8    byte;
    float tone_from_hz, tone_to_hz, glide_s, tone_level, tone_decay_s;
    float noise_level, noise_decay_s, noise_hz, noise_q;
    bool  noise_highpass;
    float click_level, click_ms, click_hz;   /* (110b) gated noise, attack */
    bool  clip;
    /* (111) The toms: the pitch keeps falling after the glide (Hz a
     * second). (139e) And the membrane (kTomModes, below) in place of 111's
     * four overtones, with the room (TomRoom) behind it. */
    float fall_hz_s;
    bool  membrane;
};

/* (110b) Re-voiced against each ROM drum struck alone through the chip,
 * band by band (<200 Hz, 200 Hz-2 kHz, 2-6 kHz, 6-12 kHz) and by the time it
 * takes to fall 20 dB (docs/audio.md, "The drums re-voiced"). The toms and
 * the snare got the chip's decays and noise above their bodies; the hats'
 * noise moved down into 1.5-6 kHz, where the ROM's hats have theirs (played
 * at about 13 kHz, they have little above 6.5); the short hard drum keeps a
 * sub-bass body -- a kick -- with a gated noise burst for its attack.
 *
 * (111) The toms again, against the chip's pitch over time and its
 * partials (tools/fm_probe --drum; docs/audio.md, "The toms"): the ROM's
 * tom starts some 25% above where it rings and never stops falling -- the
 * high tom 258, 229, 217, 210, 207, 204 Hz at 20 ms steps -- where 110b's
 * glided for 80 ms and held; and it has membrane overtones near 1.7, 2.4,
 * 2.9 and 4.6 times its pitch, 8-14 dB under it at the strike and some 20
 * dB by 50 ms, where 110b's was a pure sine. So: a higher start, the slow
 * fall after the glide, and four overtones decaying ahead of the body.
 *
 * (139e) The four overtones are the membrane's fourteen modes now
 * (kTomModes), the start the chip's own pitch, and a small room behind the
 * toms (TomRoom): The owner's pick by ear of three re-voicings. */
constexpr DrumPatch kDrums[] = {
    /* byte tone Hz   glide  lvl   decay  noise  decay  filter  Q   hp     click  ms   Hz    clip   fall    membrane */
    { DRUM_HIGH_TOM, 262, 221, 0.090f, 0.52f, 0.24f, 1.00f, 0.06f, 3000, 0.5f, false, 0.30f, 4.0f, 2500, false, 100.0f, true },
    { DRUM_MID_TOM, 238, 200, 0.090f, 0.52f, 0.26f, 1.00f, 0.06f, 3000, 0.5f, false, 0.30f, 4.0f, 2500, false, 90.0f, true },
    { DRUM_LOW_TOM, 202, 173, 0.090f, 0.52f, 0.30f, 1.00f, 0.06f, 3000, 0.5f, false, 0.30f, 4.0f, 2500, false, 70.0f, true },
    { DRUM_SNARE, 230, 185, 0.030f, 0.70f, 0.12f, 1.70f, 0.38f, 2000, 0.5f, false, 0.06f, 3.0f, 3000, false, 0.0f, false },
    { DRUM_KICK, 160,  52, 0.035f, 0.56f, 0.09f, 0.00f, 0.01f, 3000, 0.7f, false, 1.20f, 8.0f, 1000, true,  0.0f, false },
    { DRUM_UNUSED,   0,   0, 0.001f, 0.0f, 0.01f, 0.30f, 0.005f, 3000, 0.7f, true, 0.00f, 0.0f, 1000, false, 0.0f, false },
    { DRUM_OPEN_HAT,   0,   0, 0.001f, 0.0f, 0.01f, 1.60f, 0.30f, 2200, 0.3f, false, 0.00f, 0.0f, 1000, false, 0.0f, false },
    { DRUM_CLOSED_HAT,   0,   0, 0.001f, 0.0f, 0.01f, 3.00f, 0.045f, 1800, 0.3f, false, 0.00f, 0.0f, 1000, false, 0.0f, false },
};

/* (build 139e) The toms' membrane, from the ROM's own tom (the one sample
 * the three toms play at three rates, assets/music/drums/tom.wav, measured
 * at the high tom's 13.46 kHz; docs/audio.md, "Build 139e"): fourteen
 * modes at inharmonic ratios to the gliding pitch, each at its level under
 * the fundamental at the strike and dying at its own rate beyond the
 * body's -- the one at 4.24 (about 1 kHz on the high tom) nearly as loud as
 * the fundamental and gone within 30 ms, the one at 3.01 the longest. 111's
 * four overtones at near one level, dying together, read as a chord; these
 * read as one pitch with a knock and a wash. The owner's pick of three
 * versions by ear ("7"), with the strike's pitch at the chip's (262 / 238
 * / 202 Hz, 111's was 320 / 290 / 250) and the room. */
struct TomMode
{
    float ratio;        /* to the fundamental as it glides */
    float level_db;     /* under the fundamental at the strike */
    float decay_db_s;   /* beyond the body's own decay */
};

constexpr TomMode kTomModes[] = {
    { 1.70f,  -5.0f, 230.0f }, { 2.20f,  -9.0f, 205.0f },
    { 2.45f, -11.0f, 115.0f }, { 2.63f,  -7.0f, 220.0f },
    { 3.01f, -14.0f,  65.0f }, { 3.46f, -11.0f, 250.0f },
    { 3.78f, -12.0f, 180.0f }, { 4.24f,  -1.0f, 385.0f },
    { 4.85f, -11.0f, 220.0f }, { 5.20f, -12.0f, 195.0f },
    { 5.92f, -10.0f, 195.0f }, { 6.56f, -11.0f, 195.0f },
    { 7.30f, -15.0f, 140.0f }, { 8.63f, -13.0f, 180.0f },
};
constexpr int kTomModeCount = int(sizeof kTomModes / sizeof kTomModes[0]);

/* (build 139e) A small room behind the toms, the owner's "a tiny reverb": a
 * Schroeder reverberator -- 4 ms before it, four damped combs of 13-20 ms
 * a side (the right's half a millisecond longer) for a 0.4 s decay, two
 * allpasses -- fed the toms alone (mono) and added under them at 0.3 of its
 * output, about 20 dB under the dry tom. It runs from a tom's first sample
 * to a second after the last, then empties itself (no denormals, nothing
 * to pay while no tom plays); a reset empties it. */
class TomRoom
{
public:
    void  init(float sps);
    void  clear();
    void  feed(float x);                  /* a tom's sample, this tick */
    void  render(float& left, float& right);

private:
    static constexpr int kCombs = 4;
    static constexpr int kAllpasses = 2;
    std::vector<float> comb[2][kCombs];
    int   comb_at[2][kCombs] = {};
    float comb_g[2][kCombs] = {};
    float damp[2][kCombs] = {};
    std::vector<float> ap[2][kAllpasses];
    int   ap_at[2][kAllpasses] = {};
    std::vector<float> pre;
    int   pre_at = 0;
    float send = 0.0f;
    long  left_on = 0;                    /* samples until it empties */
    long  hold = 0;                       /* a second, in samples */
};

void TomRoom::init(float sps)
{
    const float comb_ms[kCombs] = { 13.1f, 15.7f, 17.9f, 19.6f };
    const float ap_ms[kAllpasses] = { 4.9f, 1.7f };
    const float rt60 = 0.40f;

    for (int c = 0; c < 2; c++) {
        for (int i = 0; i < kCombs; i++) {
            float d = (comb_ms[i] + 0.53f * float(c)) * 0.001f;
            comb[c][i].assign(size_t(d * sps) + 1, 0.0f);
            comb_g[c][i] = std::pow(10.0f, -3.0f * d / rt60);
        }
        for (int i = 0; i < kAllpasses; i++) {
            ap[c][i].assign(size_t((ap_ms[i] + 0.31f * float(c)) * 0.001f * sps) + 1,
                            0.0f);
        }
    }
    pre.assign(size_t(0.004f * sps) + 1, 0.0f);
    hold = long(sps);
    clear();
}

void TomRoom::clear()
{
    for (int c = 0; c < 2; c++) {
        for (int i = 0; i < kCombs; i++) {
            std::fill(comb[c][i].begin(), comb[c][i].end(), 0.0f);
            comb_at[c][i] = 0;
            damp[c][i] = 0.0f;
        }
        for (int i = 0; i < kAllpasses; i++) {
            std::fill(ap[c][i].begin(), ap[c][i].end(), 0.0f);
            ap_at[c][i] = 0;
        }
    }
    std::fill(pre.begin(), pre.end(), 0.0f);
    pre_at = 0;
    send = 0.0f;
    left_on = 0;
}

void TomRoom::feed(float x)
{
    send += x;
    left_on = hold;
}

void TomRoom::render(float& left, float& right)
{
    if (left_on <= 0) {
        return;
    }
    left_on--;
    if (left_on == 0) {
        clear();
        return;
    }
    float x = pre[size_t(pre_at)];
    pre[size_t(pre_at)] = send;
    pre_at = (pre_at + 1) % int(pre.size());
    send = 0.0f;

    float out[2] = { 0.0f, 0.0f };
    for (int c = 0; c < 2; c++) {
        float sum = 0.0f;
        for (int i = 0; i < kCombs; i++) {
            std::vector<float>& b = comb[c][i];
            int& at = comb_at[c][i];
            float y = b[size_t(at)];
            damp[c][i] = y * 0.65f + damp[c][i] * 0.35f;
            b[size_t(at)] = x + damp[c][i] * comb_g[c][i];
            at = (at + 1) % int(b.size());
            sum += y;
        }
        float y = sum * 0.25f;
        for (int i = 0; i < kAllpasses; i++) {
            std::vector<float>& b = ap[c][i];
            int& at = ap_at[c][i];
            float d = b[size_t(at)];
            float v = y + 0.6f * d;
            b[size_t(at)] = v;
            y = d - 0.6f * v;
            at = (at + 1) % int(b.size());
        }
        out[c] = y;
    }
    left  += out[0] * 0.30f;
    right += out[1] * 0.30f;
}

/* (build 139d) The drums' noise, the port's own: Q's white_noise_gen as it
 * was before Q 1.5, where these drums were voiced by ear (builds 110b-111).
 * That generator read its 32 bits as unsigned, so its "white noise" ran from
 * 0 to 2, not -1 to 1 -- white noise plus a constant 1, which the drums'
 * band-pass and high-pass filters mostly take out again. Q 1.5 reads them
 * signed (-1 to 1); built with it, every drum moved by up to a decibel in a
 * band (drum_hits) and the songs' peaks by up to 0.6 dB (q_demo's trim
 * pass). Kept here, the drums are the same with any Q. The same musicdsp.org
 * generator, the same seeds, the same arithmetic. */
struct DrumNoise
{
    float operator()()
    {
        x1 ^= x2;
        s = float(x2) * scale;
        x2 += x1;
        return s;
    }

    std::uint32_t x1 = 0x67452301;
    std::uint32_t x2 = 0xefcdab89;
    static constexpr float scale = 2.0f / 0xffffffff;
    float s = 0.0f;
};

struct DrumVoice
{
    explicit DrumVoice(float sps);

    q::phase_iterator         phase;
    q::exp_downward_ramp_gen  glide;
    q::envelope_gen           tone_env;
    q::envelope_gen           noise_env;
    DrumNoise                 noise;           /* (139d) */
    q::svf                    filter;
    q::svf                    click_filter;    /* (110b) */
    q::cubic_clip             clip;
    DrumPatch                 patch{};
    float                     sps;
    int                       glide_left = 0;
    int                       click_left = 0;  /* (110b) samples of gate */
    /* (139e) the membrane's modes: phase, level, and its fall a sample */
    std::array<q::phase_iterator, kTomModeCount> mode_phase{};
    std::array<float, kTomModeCount> mode_amp{};
    std::array<float, kTomModeCount> mode_mul{};
    float                     room_send = 0.0f; /* (139e) this sample's */
    bool                      room_fed = false; /* for the bank's room */
    float                     fall_hz = 0.0f;        /* (111) so far */
    float                     pan_l = 1.0f;
    float                     pan_r = 1.0f;

    void  strike(DrumPatch const& p, bool left, bool right);
    bool  active() const;
    void  render_one(float& left, float& right);
};

q::envelope_gen make_drum_envelope(float sps)
{
    return q::envelope_gen{
        q::make_envelope_segment<q::exp_upward_ramp_gen>(0.5_ms, 1.0f, sps),
        q::make_envelope_segment<q::exp_downward_ramp_gen>(100_ms, 0.0f, sps)
    };
}

DrumVoice::DrumVoice(float sps_)
 : glide{10_ms, sps_}
 , tone_env{make_drum_envelope(sps_)}
 , noise_env{make_drum_envelope(sps_)}
 , filter{1_kHz, sps_, 0.7}
 , click_filter{1_kHz, sps_, 0.7}
 , sps{sps_}
{
}

void DrumVoice::strike(DrumPatch const& p, bool left, bool right)
{
    patch = p;
    pan_l = left ? 1.0f : 0.0f;
    pan_r = right ? 1.0f : 0.0f;
    glide.config(at_least(p.glide_s), sps);
    glide.reset();
    glide_left = int(p.glide_s * sps);
    tone_env[1].config(at_least(p.tone_decay_s), sps);
    noise_env[1].config(at_least(p.noise_decay_s), sps);
    filter.config(q::frequency{double(p.noise_hz)}, sps, double(p.noise_q));
    click_filter.config(q::frequency{double(p.click_hz)}, sps, 0.7);
    click_left = int(p.click_ms * 0.001f * sps);
    phase.set(q::frequency{double(p.tone_from_hz)}, sps);
    /* (111) the slow fall, (139e) the membrane's modes, from the strike */
    if (p.membrane) {
        for (int i = 0; i < kTomModeCount; i++) {
            mode_phase[size_t(i)]._phase = q::phase{};
            mode_amp[size_t(i)] = std::pow(10.0f, kTomModes[i].level_db / 20.0f);
            mode_mul[size_t(i)] = std::pow(10.0f,
                                           -kTomModes[i].decay_db_s / 20.0f / sps);
        }
    }
    fall_hz  = 0.0f;
    tone_env.reset();
    noise_env.reset();
    tone_env.attack();
    noise_env.attack();
}

bool DrumVoice::active() const
{
    return !tone_env.in_idle_phase() || !noise_env.in_idle_phase()
        || click_left > 0;
}

void DrumVoice::render_one(float& left, float& right)
{
    float x = 0.0f;

    if (patch.tone_level > 0.0f) {
        float g = (glide_left > 0) ? glide() : 0.0f;
        float f = patch.tone_to_hz + (patch.tone_from_hz - patch.tone_to_hz) * g;

        /* (111) the toms' fall after the glide, down to half the body */
        f -= fall_hz;
        fall_hz += patch.fall_hz_s / sps;
        f = (f < 0.5f * patch.tone_to_hz) ? 0.5f * patch.tone_to_hz : f;
        if (glide_left > 0) {
            glide_left--;
        }
        if (glide_left > 0 || patch.fall_hz_s > 0.0f) {
            phase.set(q::frequency{double(f)}, sps);
        }
        float env = tone_env();
        float tone = q::sin(phase++);
        if (patch.membrane) {
            float modes = 0.0f;
            for (int i = 0; i < kTomModeCount; i++) {
                mode_phase[size_t(i)].set(q::frequency{double(f * kTomModes[i].ratio)}, sps);
                modes += q::sin(mode_phase[size_t(i)]++) * mode_amp[size_t(i)];
                mode_amp[size_t(i)] *= mode_mul[size_t(i)];
            }
            tone += modes;
        }
        x += tone * env * patch.tone_level;
    }
    if (patch.noise_level > 0.0f) {
        float n = filter(noise());

        if (patch.noise_highpass) {
            n = filter.highpass();
        } else {
            n = filter.bandpass();
        }
        x += n * noise_env() * patch.noise_level;
    }
    /* (110b) The attack: band-passed noise held at its level for click_ms,
     * then cut over the last millisecond -- a gate, not a decay. */
    if (click_left > 0 && patch.click_level > 0.0f) {
        float fade = 0.001f * sps;
        float gate = (float(click_left) < fade) ? float(click_left) / fade : 1.0f;
        click_filter(noise());
        x += click_filter.bandpass() * gate * patch.click_level;
    }
    if (click_left > 0) {
        click_left--;
    }
    if (patch.clip) {
        x = clip(x * 1.5f);
    }
    /* (139e) a tom for the room, as one channel (QBank::render feeds it) */
    if (patch.membrane) {
        room_send = x * 0.5f * (pan_l + pan_r);
        room_fed  = true;
    }
    left  += x * pan_l;
    right += x * pan_r;
}

/* ------------------------------------------------------------------------
 * The drums from the ROM's own samples (build 110): the six the Z80 plays,
 * each at the rate its loop gives it -- 240 + 13 x delay T-states a byte at
 * 3.58 MHz, 10.4 to 13.5 kHz -- read through Q's Hermite interpolation and a
 * low-pass at 0.45 of that rate. The chip holds each byte for about four
 * native samples, so its images ring on above the sample's own band; these
 * are smooth to it and dark above it. Each byte is halved, as the Z80's
 * `srl a` halves it, and taken about the halved sample's middle (the chip's
 * DC is its output stage's to block). Like the DAC, one at a time: a new hit
 * replaces the last, whatever was playing. */
struct SampleDrum
{
    u8  byte;
    u16 delay;
    u32 addr;
    u32 length;
};

struct SampleDrumVoice
{
    explicit SampleDrumVoice(float sps);

    std::vector<u8>                       ram;     /* the Z80 RAM, 8 KB */
    std::array<SampleDrum, FM_DAC_DRUMS>  table{};
    int                                   count = 0;
    q::svf                                filter;
    float                                 sps;
    bool                                  playing = false;
    u32                                   start = 0;
    u32                                   length = 0;
    double                                pos = 0.0;    /* bytes from start */
    double                                step = 0.0;   /* bytes a sample */
    float                                 pan_l = 1.0f;
    float                                 pan_r = 1.0f;

    float byte_at(long i) const;
    void  strike(u8 want, bool left, bool right);
    bool  active() const;
    void  render_one(float& left, float& right);
};

SampleDrumVoice::SampleDrumVoice(float sps_)
 : filter{5_kHz, sps_, 0.707}
 , sps{sps_}
{
}

/* The byte the DAC would hold at index i of the sample playing: before the
 * first, the first; after the last, the last (the DAC keeps its value). */
float SampleDrumVoice::byte_at(long i) const
{
    if (i < 0) {
        i = 0;
    }
    if (i >= long(length)) {
        i = long(length) - 1;
    }
    u8 b = ram[(start + u32(i)) & (FM_Z80_RAM - 1)];
    return (float(b >> 1) - 64.0f) / 128.0f;
}

void SampleDrumVoice::strike(u8 want, bool left, bool right)
{
    if (ram.empty()) {
        return;
    }
    for (int i = 0; i < count; i++) {
        SampleDrum const& d = table[size_t(i)];

        if (d.byte != want || d.length == 0) {
            continue;
        }
        double rate = double(FM_Z80_CLOCK) / (240.0 + 13.0 * double(d.delay));
        double fc   = 0.45 * rate;

        start   = d.addr;
        length  = d.length;
        pos     = 0.0;
        step    = rate / double(sps);
        pan_l   = left ? 1.0f : 0.0f;
        pan_r   = right ? 1.0f : 0.0f;
        playing = true;
        filter.cutoff(q::frequency{fc}, sps);
        return;
    }
}

bool SampleDrumVoice::active() const
{
    return playing;
}

void SampleDrumVoice::render_one(float& left, float& right)
{
    long  i  = long(pos);
    float mu = float(pos - double(i));
    float x  = q::hermite_interpolate(byte_at(i - 1), byte_at(i),
                                      byte_at(i + 1), byte_at(i + 2), mu);

    x = filter(x);
    left  += x * pan_l;
    right += x * pan_r;
    pos += step;
    if (pos >= double(length)) {
        playing = false;
    }
}


/* ------------------------------------------------------------------------
 * The bank: the virtual channels the notes are decoded onto, the voices, the
 * drums, and a shadow of the chip's registers. Its C interface has fm.c's
 * shape.
 *
 * (111) A virtual channel is where one stream of writes is decoded into
 * notes. Without the hooks (q_demo, drum_hits: `write` alone) there are six,
 * one a chip channel, as build 110 had it. With them (the game: the synth
 * table's track_write) there is one a sequencer track -- the drums, FM1-FM5
 * and the four effect tracks -- so an effect plays on a channel of its own
 * and the music track it displaces runs on under it, as it does in the
 * sequencer, where the chip only stops hearing it. Writes the driver makes
 * itself (silencing) go to every virtual channel on that chip channel; a
 * track's write to a chip channel not its own (the pan flag's, which always
 * lands on FM1) goes to whichever virtual channel last struck there; an
 * effect claiming a channel starts from the chip's registers for it, so what
 * it never writes -- the pitch, for the effects that set FM3's special mode
 * on FM4 or FM5 -- is what the chip would have.
 *
 * (111) Parallel voices (the PM voicing): a strike on a virtual channel
 * whose voice still sounds (a carrier within 60 dB of full) leaves that
 * voice to finish its release as a tail and starts the note on a fresh
 * voice from silence, its phases at zero -- where the chip starts the new
 * note's attack from the old one's level and zeroes the phases under the
 * sounding tail, a click. PS2_Q_PARALLEL=0 plays it the chip's way. */

struct VChan
{
    u8   reg[256]{};        /* per-channel registers, at slot 0's addresses */
    int  chip_ch = -1;      /* the chip channel its writes address */
    bool effect  = false;   /* an effect track's */
    int  voice   = -1;      /* PM: the voice it plays; subtractive: itself */
    /* (build 112) the instrument its track last uploaded (SMPS_HOOK_
     * INSTRUMENT), its patch in the file (-1 none), and the song's volume
     * on the ROM instrument's carriers, in TL steps (what RefreshVolume
     * added to them), so a patch's carriers get it too */
    int  ins     = -1;
    int  patch   = -1;
    int  vol     = 0;
    /* (build 143) inside RestoreBGMChn's upload (smps.h, SMPS_HOOK_RESTORE) */
    bool restoring = false;
};

constexpr int kVChans      = SMPS_TRACKS;   /* 10: enough for either use */
constexpr double kTailFadeS = 0.015;        /* (111b) a parallel tail's fade */
/* (build 112) TAILS RING: at most this many tails ring their release at once;
 * the next fades the quietest of them (15 ms), so a fresh voice is never
 * taken from under a sounding one. The virtual channels' own ten and these
 * twelve leave the pool of 24 two for fades. */
constexpr int kRingMax     = 12;
constexpr int kPmVoices    = 24;
constexpr float kPmVoiceGain = 0.1070f;     /* a carrier as loud as the chip's: 768/8192 by the chip's scale, +1.15 dB measured (eight instruments held, fm_probe --note: -1.1 to -1.3 dB at 768/8192) */

/* The trims, by voicing and drum mode: the bank's peaks against the chip's
 * over every song (q_demo --trim-pass; docs/audio.md). */
constexpr float kQTrimPm[2] = {
    0.804f,    /* Q_DRUMS_SYNTH:   -1.90 dB (111b: the chip's pace; 111 0.865) */
    0.947f,    /* Q_DRUMS_SAMPLES: -0.48 dB (111 1.031) */
};

/* The Kaiser-windowed low-pass the PM voices come down through. */
std::vector<float> design_lowpass(int taps, double cutoff, double rate, double beta)
{
    auto bessel_i0 = [](double x) {
        double sum = 1.0;
        double term = 1.0;
        for (int k = 1; k < 40; k++) {
            term *= (x / (2.0 * k)) * (x / (2.0 * k));
            sum += term;
        }
        return sum;
    };
    std::vector<float> h(static_cast<size_t>(taps));
    double m = double(taps - 1) / 2.0;
    double fc = cutoff / rate;
    double sum = 0.0;
    for (int n = 0; n < taps; n++) {
        double x = double(n) - m;
        double sinc = (x == 0.0) ? 2.0 * fc : std::sin(2.0 * std::numbers::pi * fc * x) / (std::numbers::pi * x);
        double r = x / m;
        double w = bessel_i0(beta * std::sqrt(std::max(0.0, 1.0 - r * r))) / bessel_i0(beta);
        h[size_t(n)] = float(sinc * w);
        sum += sinc * w;
    }
    for (auto& v : h) {
        v = float(double(v) / sum);
    }
    return h;
}

}   /* namespace */

struct QBank
{
    explicit QBank(float sps);

    u8                 reg[2][256]{};     /* the chip's registers, both ports */

    /* (111b) The chip's pace. Nuked takes a register write every two native
     * samples (fm.c's queue: the address, then the data), so a tick's writes
     * land spread out -- a song's first tick over some 7 ms, and the two
     * tracks of an effect in unison (the level-up's FM4 and FM5) struck some
     * 150 us apart, where their copies partly cancel. Q took a tick's writes
     * all at once: every strike in the same instant, the unisons 3-6 dB over
     * the chip. Now each write waits for its slot: the chip's writes take
     * one each, a hook's write lands with the chip write it comes before. */
    struct Pending
    {
        bool   hook;
        bool   to_chip;
        int    track;
        u8     channel;
        int    part;
        u8     reg;
        u8     value;
        double at;              /* output samples */
    };
    std::vector<Pending> pending;
    size_t             pending_at = 0;
    double             clock = 0.0;       /* output samples rendered */
    double             q_next = 0.0;      /* the next slot's start */
    double             slot = 0.0;        /* two native samples, as output */
    /* (build 184) a vector: kVChans, the tracks', then a mod's chord voices
     * (set_chord_voices), virtual channel SMPS_TRACKS + v for voice v */
    std::vector<VChan> vchan = std::vector<VChan>(size_t(kVChans));
    std::array<int, 6> chip_owner{};      /* the virtual channel last struck */
    int                chord_music = 0;   /* (build 184) the music's voices */
    bool               hooked = false;    /* (111) voices from track_write */
    QVoicing           voicing = Q_VOICE_PM;
    bool               parallel = true;
    /* (111b) PS2_Q_EFFECTS=take: effects take the music's channel as on the
     * chip (the music's note there stops), still on voices of their own for
     * the EFFECTS volume; the default leaves the music playing under them */
    bool               take_channels = false;
    /* (build 112) TAILS RING: a parallel tail rings its release (111a's) */
    bool               ring_tails = false;
    /* (build 112) PS2_Q_GRAIN=1, a diagnostic: the chip's output grain on
     * Q's channels -- each carrier truncated to the 9-bit accumulator's step,
     * and the YM2612 DAC's jump at zero (Nuked's OPN2_ChOutput: +4 on the
     * positive side, -3 on the negative, of those steps; the +4 of silence
     * taken off, so a silent channel is 0 and the jump is 7 below zero). What
     * instrument 74's tail is made of on the chip (docs/audio.md, "Build
     * 112"). (build 116) The AUDIO page's GRAIN; the variable still sets it
     * for the tools. */
    bool               grain = false;
    /* (build 112) the patch file (synth.h's SynthPatch), and PATCHES */
    std::vector<SynthPatch> patches;
    bool               patches_on = true;
    std::vector<Voice> voices;            /* subtractive: one a virtual channel */
    std::vector<PmVoice> pm;              /* PM: the pool */
    DrumVoice          drums;
    TomRoom            tom_room;          /* (139e) the room behind the toms */
    SampleDrumVoice    samples;           /* (110) the ROM's drums */
    QDrumMode          drum_mode = Q_DRUMS_SYNTH;
    q::dc_block        dc_l;
    q::dc_block        dc_r;
    float              sps;
    float              trim = kQTrimPm[Q_DRUMS_SYNTH];
    long long          voice_samples = 0; /* FM voices x samples rendered */

    /* (111) the PM voices' running rate and the way down from it */
    int                os = 4;
    double             os_rate = 0.0;
    double             eg_per_sample = 0.0;   /* EG steps a running sample */
    double             eg_pos = 0.0;
    int                eg_timer = 0;
    int                fb_lag1 = 1;
    int                fb_lag2 = 2;
    std::vector<float> fir;
    std::vector<float> hist_l;
    std::vector<float> hist_r;
    int                hist_at = 0;

    void reset();
    bool load_drums(const char* path);
    void set_oversample(int n);
    void set_voicing(QVoicing v);
    void write(int part, u8 r, u8 value);
    void track_write(int track, u8 channel, int part, u8 r, u8 value,
                     bool to_chip);
    void write_now(int part, u8 r, u8 value);
    void track_write_now(int track, u8 channel, int part, u8 r, u8 value,
                         bool to_chip);
    void end_effect_note(int ch);
    void chord_write_now(int vc, int part, u8 r, u8 value);
    void set_chord_voices(int music, int effects);
    void set_option(int option, int value);
    int  patch_for(int ins) const;
    void apply_own(int vc, u8 base, u8 value);
    void patch_levels(int vc);
    int  ringing_tails(int* quietest) const;
    void apply_due(double t);
    void apply(int vc, u8 base, u8 value);
    void key(int vc, bool on);
    void drum(u8 byte);
    void render(float* out, int frames, float music, float effects);

    /* the subtractive voicing (110's) */
    u8   vreg(int vc, int base) const { return vchan[size_t(vc)].reg[base]; }
    void sub_strike(int vc);
    void sub_retune(int vc);
    void sub_relevel(int vc);
    void sub_repan(int vc);

    /* the PM voicing */
    int  pm_allocate();
    void pm_load(PmVoice& v, int vc);
    void pm_retune(PmVoice& v, int vc);
    void pm_steps(PmVoice& v);
    void pm_strike(int vc);
    void pm_eg_step();
    void pm_render_one(PmVoice& v, float gain, float& l, float& r);
};

QBank::QBank(float sps_)
 : drums{sps_}
 , samples{sps_}
 , dc_l{10_Hz, sps_}
 , dc_r{10_Hz, sps_}
 , sps{sps_}
{
    tom_room.init(sps_);
    voices.reserve(kVChans);
    for (int i = 0; i < kVChans; i++) {
        voices.emplace_back(sps_);
    }
    pm.resize(kPmVoices);
    pending.reserve(1024);
    slot = 2.0 * double(sps_) / kNativeRate;
    chip_owner.fill(-1);
    for (int p = 0; p < 2; p++) {
        for (int s = 0; s < 3; s++) {
            reg[p][0xB4 + s] = 0xC0;
        }
    }
    for (auto& c : vchan) {
        c.reg[0xB4] = 0xC0;
    }
    set_oversample(4);
}

void QBank::set_oversample(int n)
{
    int want = (n == 1 || n == 2) ? n : 4;
    if (want == os && !fir.empty() == (os > 1)) {
        return;
    }
    os = want;
    os_rate = double(sps) * os;
    eg_per_sample = (kNativeRate / 3.0) / os_rate;
    fb_lag1 = std::max(1, int(std::lround(os_rate / kNativeRate)));
    fb_lag2 = std::max(fb_lag1 + 1, int(std::lround(2.0 * os_rate / kNativeRate)));
    if (os > 1) {
        fir = design_lowpass(32 * os + 1, 22500.0, os_rate, 8.0);
    } else {
        fir.clear();
    }
    hist_l.assign(fir.size(), 0.0f);
    hist_r.assign(fir.size(), 0.0f);
    hist_at = 0;
    /* (111a) From the AUDIO page while notes sound: every voice's phase
     * steps are per running sample, so each is worked out again at the new
     * rate from the pitch it holds (its tail's too); the low-pass starts
     * empty, a few samples' dip. */
    for (auto& v : pm) {
        if (v.struck) {
            pm_steps(v);
        }
    }
}

void QBank::set_voicing(QVoicing v)
{
    voicing = v;
}

/* (110) As the chip after a reset: the shadow cleared bar $B4's two
 * speakers, every voice idle, the drums stopped, the DC blockers settled.
 * What a switch to Q starts from before audio.c replays its shadow. */
void QBank::reset()
{
    std::memset(reg, 0, sizeof reg);
    for (int p = 0; p < 2; p++) {
        for (int s = 0; s < 3; s++) {
            reg[p][0xB4 + s] = 0xC0;
        }
    }
    for (auto& c : vchan) {
        c = VChan{};
        c.reg[0xB4] = 0xC0;
    }
    chip_owner.fill(-1);
    for (auto& v : voices) {
        for (auto& e : v.env) {
            e.reset();
        }
        v.level.fill(0.0f);
        v.level_target.fill(0.0f);
        v.pan_l = 1.0f;
        v.pan_r = 1.0f;
        v.pan_l_target = 1.0f;
        v.pan_r_target = 1.0f;
    }
    for (auto& v : pm) {
        v = PmVoice{};
    }
    pending.clear();
    pending_at = 0;
    q_next = clock;
    std::fill(hist_l.begin(), hist_l.end(), 0.0f);
    std::fill(hist_r.begin(), hist_r.end(), 0.0f);
    drums.tone_env.reset();
    drums.click_left = 0;
    drums.noise_env.reset();
    drums.room_fed = false;
    tom_room.clear();
    samples.playing = false;
    dc_l = 0.0f;
    dc_l.x = 0.0f;
    dc_r = 0.0f;
    dc_r.x = 0.0f;
}

/* (build 125) q_synth_set_reader's: the game's pack, or none. */
static Ps2Reader g_reader = nullptr;

extern "C" void q_synth_set_reader(Ps2Reader reader)
{
    g_reader = reader;
}

/* (build 125) The drum container from the reader or the disk, read as
 * fread reads, so the parser below is unchanged. */
struct QSource
{
    FILE*     fh = nullptr;
    const u8* p = nullptr;
    size_t    left = 0;

    bool open(const char* path)
    {
        size_t size = 0;
        p = (g_reader != nullptr) ? g_reader(path, &size) : nullptr;
        left = size;
        if (p != nullptr) {
            return true;
        }
        fh = std::fopen(path, "rb");
        return fh != nullptr;
    }

    size_t read(void* dst, size_t unit, size_t n)
    {
        if (fh != nullptr) {
            return std::fread(dst, unit, n, fh);
        }
        size_t k = std::min(unit * n, left);
        std::memcpy(dst, p, k);
        p += k;
        left -= k;
        return unit != 0 ? k / unit : 0;
    }

    void close()
    {
        if (fh != nullptr) {
            std::fclose(fh);
            fh = nullptr;
        }
    }
};

/* (110) The drums as fm_load_drums reads them: (build 136b) drums.json and
 * its WAVs through drums_load (drums.h), with Q's own reader. */
bool QBank::load_drums(const char* path)
{
    DrumEntry table[DRUM_MAX];
    int count = 0;
    std::vector<u8> ram(DRUM_Z80_RAM);

    if (!drums_load(path, g_reader, table, &count, ram.data())) {
        std::fprintf(stderr, "q: no drums from %s\n", path);
        return false;
    }
    for (int i = 0; i < count; i++) {
        samples.table[size_t(i)] = SampleDrum{
            table[i].byte, table[i].delay, table[i].addr, table[i].length
        };
    }
    samples.ram   = std::move(ram);
    samples.count = count;
    return true;
}

/* ------------------------------------------------------------------------
 * Decoding: writes onto virtual channels. */

/* A $28 id (0-2, 4-6) as a channel 0-5, -1 for the two that are none. */
int key_channel(u8 value)
{
    int id = value & 7;
    if (id == 3 || id == 7) {
        return -1;
    }
    return (id < 3) ? id : id - 1;
}

/* (111b) A write queued for its slot, as the chip's queue takes it. */
void QBank::write(int part, u8 r, u8 value)
{
    double start = std::max(q_next, clock);
    double at = start + slot;
    q_next = at;
    pending.push_back(Pending{ false, true, 0, 0, part, r, value, at });
}

/* (111b) A hook's write: with the chip write that follows it, in its slot --
 * or, for one the chip never gets, where that slot would have been. */
void QBank::track_write(int track, u8 channel, int part, u8 r, u8 value,
                        bool to_chip)
{
    hooked = true;
    if (r == SMPS_HOOK_RESTORE) {
        /* (build 143) no write the chip takes, so no slot of its own: in
         * order with the writes it brackets, their times as before */
        pending.push_back(Pending{ true, to_chip, track, channel, part, r,
                                   value, std::max(q_next, clock) });
        return;
    }
    double at = std::max(q_next, clock) + slot;
    pending.push_back(Pending{ true, to_chip, track, channel, part, r, value,
                               at });
}

/* (111b) Every write due by output sample t, in the order they came. */
void QBank::apply_due(double t)
{
    while (pending_at < pending.size() && pending[pending_at].at <= t) {
        Pending const& w = pending[pending_at];
        if (w.hook) {
            /* (111b) taking channels as the chip does: a music track's
             * writes while an effect holds its channel are not voiced */
            if (!(take_channels && !w.to_chip && w.track >= 0
                  && w.track < SMPS_MUSIC_TRACKS)) {
                track_write_now(w.track, w.channel, w.part, w.reg, w.value,
                                w.to_chip);
            }
        } else {
            write_now(w.part, w.reg, w.value);
        }
        pending_at++;
    }
    if (pending_at == pending.size()) {
        pending.clear();
        pending_at = 0;
    }
}

/* The chip's write: the shadow, and without the hooks the notes. */
void QBank::write_now(int part, u8 r, u8 value)
{
    int p = part ? 1 : 0;

    reg[p][r] = value;
    if (hooked) {
        return;
    }
    if (p == 0 && r == 0x28) {
        int ch = key_channel(value);
        if (ch < 0) {
            return;
        }
        /* FM6 is the drums' DAC while $2B has the DAC on. */
        if (ch == 5 && (reg[0][0x2B] & 0x80)) {
            return;
        }
        vchan[size_t(ch)].chip_ch = ch;
        key(ch, (value & 0xF0) != 0);
        return;
    }
    if (r < 0x30 || (r & 3) == 3 || (r >= 0xA8 && r < 0xB0)) {
        return;
    }
    int ch = p * 3 + (r & 3);
    vchan[size_t(ch)].chip_ch = ch;
    apply(ch, u8(r - (r & 3)), value);
}

/* (111) The hooks' write (smps.h): onto the track's own virtual channel. */
void QBank::track_write_now(int track, u8 channel, int part, u8 r, u8 value,
                            bool to_chip)
{
    int p = part ? 1 : 0;

    hooked = true;
    if (track == SMPS_TRACK_DRIVER) {
        /* Silencing: every virtual channel on that chip channel. */
        if (p == 0 && r == 0x28) {
            int ch = key_channel(value);
            for (int vc = 0; vc < kVChans; vc++) {
                if (ch >= 0 && vchan[size_t(vc)].chip_ch == ch) {
                    key(vc, (value & 0xF0) != 0);
                }
            }
            return;
        }
        if (r < 0x30 || (r & 3) == 3 || (r >= 0xA8 && r < 0xB0)) {
            return;
        }
        int ch = p * 3 + (r & 3);
        for (int vc = 0; vc < kVChans; vc++) {
            if (vchan[size_t(vc)].chip_ch == ch) {
                apply(vc, u8(r - (r & 3)), value);
            }
        }
        return;
    }
    if (track >= SMPS_TRACKS) {
        chord_write_now(track, p, r, value);    /* (build 184) */
        return;
    }
    if (track < 0 || track >= kVChans) {
        return;
    }
    VChan& c = vchan[size_t(track)];
    c.effect = track >= SMPS_MUSIC_TRACKS;
    /* its own channel, from its block: a write that lands elsewhere is the
     * chip's channel's there (below) */
    if (!(channel & 0x80) && key_channel(channel) >= 0) {
        c.chip_ch = key_channel(channel);
    }

    if (r == SMPS_HOOK_RESTORE) {
        /* (build 143) RestoreBGMChn's upload, bracketed. Under EXTRA the
         * music track's own voice played on under the effect and holds its
         * instrument already: the upload leaves it alone (its levels before
         * the track's volume were a click on the sounding note) and still
         * reaches the effect's releasing voice below. LIMITED took the
         * channel, and takes the upload as before. */
        c.restoring = value != 0;
        return;
    }
    bool keep_own = c.restoring && !take_channels;
    if (r == SMPS_HOOK_INSTRUMENT) {
        if (keep_own) {
            return;
        }
        /* (build 112) the instrument its upload is, by number: the patch
         * file's, if it has one; the song's volume starts from none, as
         * the upload's own levels are the instrument's */
        c.ins   = value;
        c.patch = patch_for(value);
        c.vol   = 0;
        return;
    }
    if (r == SMPS_HOOK_CLAIM) {
        /* An effect taking a channel: it starts from the chip's registers
         * there, as the chip's channel would sound it. */
        int ch = key_channel(value);
        if (ch < 0) {
            return;
        }
        c.chip_ch = ch;
        if (take_channels) {
            /* (111b) the music's note on that channel stops, as the chip's
             * does when the effect takes it: faded as a tail */
            for (int vc = 0; vc < SMPS_MUSIC_TRACKS; vc++) {
                VChan& m = vchan[size_t(vc)];
                if (m.chip_ch == ch && m.voice >= 0
                    && pm[size_t(m.voice)].owner == vc) {
                    PmVoice& t = pm[size_t(m.voice)];
                    t.owner = -1;
                    t.fade_len = std::max(1, int(kTailFadeS * os_rate));
                    t.fade_left = t.fade_len;
                    m.voice = -1;
                }
                if (m.chip_ch == ch && voicing == Q_VOICE_SUBTRACTIVE) {
                    for (auto& e : voices[size_t(vc)].env) {
                        e.release();
                    }
                }
            }
        }
        int cp = ch / 3;
        int cs = ch % 3;
        for (int base = 0x30; base < 0xB8; base += 4) {
            if (base >= 0xA8 && base < 0xB0) {
                continue;
            }
            c.reg[base] = reg[cp][base + cs];
        }
        return;
    }
    if (p == 0 && r == 0x28) {
        int ch = key_channel(value);
        if (ch < 0 || (ch == 5 && (reg[0][0x2B] & 0x80))) {
            return;
        }
        c.chip_ch = ch;
        key(track, (value & 0xF0) != 0);
        return;
    }
    if (r < 0x30 || (r & 3) == 3 || (r >= 0xA8 && r < 0xB0)) {
        return;
    }
    int ch = p * 3 + (r & 3);
    if (c.chip_ch < 0) {
        c.chip_ch = ch;
    }
    if (ch == c.chip_ch) {
        if (!keep_own) {
            apply_own(track, u8(r - (r & 3)), value);
        }
        /* (111c) The music's write that reaches the chip on a channel whose
         * sound is still an effect's -- the instrument the music gets back
         * when the effect ends (RestoreBGMChn): on the chip it lands under
         * the effect's releasing note, which takes the music's levels and
         * rates from then on; so does the effect's voice here. Without it
         * the level-up's tail rang on some 2 s over the victory music where
         * the chip's is gone at once. */
        int owner = chip_owner[size_t(ch)];
        if (to_chip && track < SMPS_MUSIC_TRACKS && owner >= SMPS_MUSIC_TRACKS
            && owner != track) {
            apply(owner, u8(r - (r & 3)), value);
        }
    } else if (chip_owner[size_t(ch)] >= 0) {
        /* A track's write to a channel not its own (the pan flag's goes to
         * FM1 whatever the track): the chip's channel, so whoever plays on
         * it now. */
        apply(chip_owner[size_t(ch)], u8(r - (r & 3)), value);
    }
}

/* (build 184) A chord voice's write (smps.h): onto its own virtual channel,
 * SMPS_TRACKS + v. It is no channel of the chip's -- its chord chip's channel
 * is the voice's alone -- so it takes no channel and ends no note there; the
 * effects' voices (chord_music on) at the effects' volume. */
void QBank::chord_write_now(int vc, int part, u8 r, u8 value)
{
    if (vc < SMPS_TRACKS || vc >= int(vchan.size())) {
        return;
    }
    VChan& c = vchan[size_t(vc)];
    c.effect  = (vc - SMPS_TRACKS) >= chord_music;
    c.chip_ch = -1;
    if (r == SMPS_HOOK_INSTRUMENT) {
        c.ins   = value;
        c.patch = patch_for(value);
        c.vol   = 0;
        return;
    }
    if (part == 0 && r == 0x28) {
        key(vc, (value & 0xF0) != 0);
        return;
    }
    if (r < 0x30 || (r & 3) == 3 || (r >= 0xA8 && r < 0xB0)) {
        return;
    }
    apply_own(vc, u8(r - (r & 3)), value);
}

/* (build 184) Room for a mod's chord voices (synth.h): a virtual channel
 * each, a subtractive voice each, and the PM pool grown by as many, so a
 * chord's notes never take each other's voices or the tracks'. At start-up,
 * before anything sounds: a voice past a smaller pool would be lost. */
void QBank::set_chord_voices(int music, int effects)
{
    music   = std::clamp(music, 0, SMPS_CHORD_VOICES_MAX);
    effects = std::clamp(effects, 0, SMPS_CHORD_VOICES_MAX - music);
    size_t n = size_t(kVChans + music + effects);
    size_t had = vchan.size();

    chord_music = music;
    vchan.resize(n);
    for (size_t i = had; i < n; i++) {
        vchan[i].reg[0xB4] = 0xC0;
    }
    while (voices.size() < n) {
        voices.emplace_back(sps);
    }
    if (voices.size() > n) {
        voices.erase(voices.begin() + std::ptrdiff_t(n), voices.end());
    }
    pm.resize(size_t(kPmVoices + music + effects));
}

/* (111c) The effect's note sounding on chip channel ch ends: its voice
 * faded as a tail (the subtractive voicing: released). */
void QBank::end_effect_note(int ch)
{
    int vc = chip_owner[size_t(ch)];
    if (vc < 0) {
        return;
    }
    VChan& e = vchan[size_t(vc)];
    if (voicing == Q_VOICE_SUBTRACTIVE) {
        for (auto& env : voices[size_t(vc)].env) {
            env.release();
        }
        return;
    }
    if (e.voice >= 0 && pm[size_t(e.voice)].owner == vc) {
        PmVoice& t = pm[size_t(e.voice)];
        t.owner = -1;
        t.fade_len = std::max(1, int(kTailFadeS * os_rate));
        t.fade_left = t.fade_len;
        e.voice = -1;
    }
}

/* (build 112) The AUDIO page's switches (synth.h's SynthOption). */
void QBank::set_option(int option, int value)
{
    switch (option) {
    case SYNTH_OPT_VOICES:
        take_channels = value != 0;
        break;
    case SYNTH_OPT_TAILS:
        ring_tails = value != 0;
        break;
    case SYNTH_OPT_PATCHES:
        patches_on = value != 0;
        break;
    case SYNTH_OPT_GRAIN:
        grain = value != 0;
        break;
    default:
        break;
    }
}

int QBank::patch_for(int ins) const
{
    for (size_t i = 0; i < patches.size(); i++) {
        if (patches[i].instrument == ins) {
            return int(i);
        }
    }
    return -1;
}

/* (build 112) A write by a track to its own channel, through its
 * instrument's patch when PATCHES is on and the file has one. The rules take
 * no count of where in SetFMIns's upload a write falls, so a switch's replay
 * (the registers in address order) comes out the same:
 *   - an instrument register (the operators' $30/$50/$60/$70/$80, and $B0)
 *     takes the patch's value, whatever was written -- but SetMaxRelRate's
 *     $FF into $80 (an effect silencing the channel it takes) stands;
 *   - a level ($40-$4C): a write to one of the ROM instrument's carriers is
 *     RefreshVolume's instrument level plus the song's volume, so the volume
 *     is what it wrote less the ROM's level; every level is then the patch's,
 *     with that volume added on the patch's own carriers (patch_levels);
 *   - the rest (pan, pitch, keys) as written. */
void QBank::apply_own(int vc, u8 base, u8 value)
{
    VChan& c = vchan[size_t(vc)];

    if (!patches_on || c.patch < 0) {
        apply(vc, base, value);
        return;
    }
    SynthPatch const& p = patches[size_t(c.patch)];
    if (base >= 0x40 && base <= 0x4C) {
        static const int kOpOf[4] = { 0, 2, 1, 3 };   /* $40 $44 $48 $4C */
        int op = kOpOf[(base - 0x40) >> 2];
        if ((kCarriers[p.rom[0xB0] & 7] >> op) & 1) {
            c.vol = int(value & 0x7F) - int(p.rom[base] & 0x7F);
        }
        patch_levels(vc);
        return;
    }
    bool ins_reg = base == 0xB0
                || (base >= 0x30 && base <= 0x3C)
                || (base >= 0x50 && base <= 0x7C)
                || (base >= 0x80 && base <= 0x8C);
    if (ins_reg && !(base >= 0x80 && value == 0xFF)) {
        apply(vc, base, p.reg[base]);
        return;
    }
    apply(vc, base, value);
}

/* (build 112) The patch's four levels on the virtual channel, the song's
 * volume on its carriers, then the voice's patch again. */
void QBank::patch_levels(int vc)
{
    VChan& c = vchan[size_t(vc)];
    SynthPatch const& p = patches[size_t(c.patch)];
    int alg = p.reg[0xB0] & 7;

    for (int op = 0; op < 4; op++) {
        int base = 0x40 + kOpOffset[op];
        int tl = p.reg[base] & 0x7F;
        if ((kCarriers[alg] >> op) & 1) {
            tl += c.vol;
        }
        tl = (tl < 0) ? 0 : tl;
        c.reg[base] = u8((tl > 0x7F) ? 0x7F : tl);
    }
    apply(vc, 0x40, c.reg[0x40]);
}

/* (build 112) How many tails ring their release unfaded, and the quietest
 * of them (its index into the pool, -1 for none). */
int QBank::ringing_tails(int* quietest) const
{
    int n = 0;
    int worst = -1;
    *quietest = -1;
    for (int i = 0; i < int(pm.size()); i++) {
        PmVoice const& v = pm[size_t(i)];
        if (v.owner < 0 && v.active() && v.fade_len == 0) {
            n++;
            if (v.loudest_att() > worst) {
                worst = v.loudest_att();
                *quietest = i;
            }
        }
    }
    return n;
}

/* A register on a virtual channel, and what it changes in the voice it
 * plays. */
void QBank::apply(int vc, u8 base, u8 value)
{
    VChan& c = vchan[size_t(vc)];
    c.reg[base] = value;

    if (voicing == Q_VOICE_SUBTRACTIVE) {
        if (base == 0xA0) {
            sub_retune(vc);
        } else if (base >= 0x40 && base <= 0x4C) {
            sub_relevel(vc);
        } else if (base == 0xB4) {
            sub_repan(vc);
        }
        return;
    }
    if (c.voice < 0) {
        return;
    }
    PmVoice& v = pm[size_t(c.voice)];
    if (base == 0xA0) {
        pm_retune(v, vc);
    } else if (base == 0xA4) {
        /* the high byte waits for the low, as the chip latches it */
    } else if (base == 0xB4) {
        v.pan_l_target = (value & 0x80) ? 1.0f : 0.0f;
        v.pan_r_target = (value & 0x40) ? 1.0f : 0.0f;
    } else {
        /* the operators' registers act at once, as on the chip; the
         * algorithm, feedback and pitch with them */
        pm_load(v, vc);
    }
}

void QBank::key(int vc, bool on)
{
    VChan& c = vchan[size_t(vc)];

    if (on) {
        if (c.chip_ch >= 0) {
            /* (111c) A music strike on a channel whose sound is an effect's
             * ends that note, as the chip's one note a channel does. */
            int prev = chip_owner[size_t(c.chip_ch)];
            if (prev >= SMPS_MUSIC_TRACKS && prev != vc
                && vc < SMPS_MUSIC_TRACKS) {
                end_effect_note(c.chip_ch);
            }
            chip_owner[size_t(c.chip_ch)] = vc;
        }
        if (voicing == Q_VOICE_SUBTRACTIVE) {
            sub_strike(vc);
        } else {
            pm_strike(vc);
        }
        return;
    }
    if (voicing == Q_VOICE_SUBTRACTIVE) {
        for (auto& e : voices[size_t(vc)].env) {
            e.release();
        }
        return;
    }
    if (c.voice >= 0) {
        for (auto& o : pm[size_t(c.voice)].op) {
            o.state = EgState::Release;
        }
    }
}

/* ------------------------------------------------------------------------
 * The subtractive voicing (build 110's, unchanged but for reading a virtual
 * channel's registers). */

void QBank::sub_retune(int vc)
{
    Voice& v = voices[size_t(vc)];
    int fnum  = ((vreg(vc, 0xA4) & 7) << 8) | vreg(vc, 0xA0);
    int block = (vreg(vc, 0xA4) >> 3) & 7;

    v.base_hz = float(fnum * std::ldexp(1.0, block - 1) * kNativeRate
                      / 1048576.0);
    for (int i = 0; i < 4; i++) {
        double f = double(v.base_hz * v.mul[size_t(i)] + v.detune_hz[size_t(i)]);
        v.phase[size_t(i)].set(q::frequency{(f > 1.0) ? f : 1.0}, sps);
    }
}

void QBank::sub_relevel(int vc)
{
    Voice& v = voices[size_t(vc)];

    for (int i = 0; i < 4; i++) {
        v.level_target[size_t(i)] = tl_to_lin(vreg(vc, 0x40 + kOpOffset[i]));
    }
}

void QBank::sub_repan(int vc)
{
    Voice& v = voices[size_t(vc)];
    u8 pan = vreg(vc, 0xB4);

    v.pan_l_target = (pan & 0x80) ? 1.0f : 0.0f;
    v.pan_r_target = (pan & 0x40) ? 1.0f : 0.0f;
}

/* A key-on: the patch is whatever the channel's registers hold now. */
void QBank::sub_strike(int vc)
{
    Voice& v  = voices[size_t(vc)];
    u8 b0     = vreg(vc, 0xB0);
    int fnum  = ((vreg(vc, 0xA4) & 7) << 8) | vreg(vc, 0xA0);
    int block = (vreg(vc, 0xA4) >> 3) & 7;
    int kc    = key_code(block, fnum);

    v.algorithm     = b0 & 7;
    v.feedback_beta = feedback_index((b0 >> 3) & 7);

    for (int i = 0; i < 4; i++) {
        int off  = kOpOffset[i];
        u8 dtmul = vreg(vc, 0x30 + off);
        u8 ksar  = vreg(vc, 0x50 + off);
        u8 d1r   = vreg(vc, 0x60 + off) & 31;
        u8 d2r   = vreg(vc, 0x70 + off) & 31;
        u8 slrr  = vreg(vc, 0x80 + off);
        int ks   = ksar >> 6;
        int m    = dtmul & 15;
        int dt   = (dtmul >> 4) & 7;
        int sl   = slrr >> 4;
        double sl_steps = (sl == 15) ? 992.0 : double(sl * 32);

        v.mul[size_t(i)] = (m == 0) ? 0.5f : float(m);
        v.detune_hz[size_t(i)] = detune_hz(dt, kc) * v.mul[size_t(i)];
        v.beta_peak[size_t(i)] = modulator_index(vreg(vc, 0x40 + off));

        double attack  = eg_attack_seconds(effective_rate(ksar & 31, kc, ks));
        int    d1_rate = effective_rate(d1r, kc, ks);
        int    d2_rate = effective_rate(d2r, kc, ks);
        double decay   = eg_decay_seconds(d1_rate, sl_steps);
        float  sustain = db_steps_to_lin(sl_steps);
        double fall    = eg_decay_seconds(d2_rate, kSteps40Db);
        double release = eg_decay_seconds(release_rate(slrr & 15, kc, ks),
                                          kSteps40Db);
        q::envelope_gen& e = v.env[size_t(i)];

        if (d1_rate == 0) {
            /* No first decay: the level holds at full. */
            e[1].config(1.0f, at_least(0.0), sps);
            e[2].config(1.0f, q::duration{kForever}, sps);
        } else if (d2_rate == 0) {
            e[1].config(sustain, at_least(decay), sps);
            e[2].config(sustain, q::duration{kForever}, sps);
        } else {
            e[1].config(sustain, at_least(decay), sps);
            e[2].config(0.0f, at_least(fall), sps);
        }
        e[0].config(1.0f, at_least(attack), sps);
        e[3].config(0.0f, at_least(release), sps);
    }

    /* What each carrier's modulators make of it. */
    for (int i = 0; i < 4; i++) {
        u8 mods = kModulators[v.algorithm][i];
        float best = 0.0f;
        float total = 0.0f;
        float ratio = 1.0f;

        for (int m = 0; m < 4; m++) {
            if ((mods >> m) & 1) {
                total += v.beta_peak[size_t(m)];
                if (v.beta_peak[size_t(m)] > best) {
                    best  = v.beta_peak[size_t(m)];
                    ratio = v.mul[size_t(m)] / v.mul[size_t(i)];
                }
            }
        }
        if (i == 0 && v.feedback_beta > total) {
            total = v.feedback_beta;
            ratio = 1.0f;
        }
        v.ratio[size_t(i)] = ratio;

        int k = int(std::lround(ratio));
        if (total < 0.3f) {
            v.wave[size_t(i)] = Wave::Sine;
        } else if (k < 1 || std::fabs(ratio - float(k)) > 0.15f || k == 1) {
            v.wave[size_t(i)] = Wave::Saw;
        } else if (k == 2) {
            v.wave[size_t(i)] = Wave::Square;
        } else {
            float w = (k > 8) ? 0.125f : 1.0f / float(k);
            v.wave[size_t(i)] = Wave::Pulse;
            v.pulse[size_t(i)].width(w);
            v.pulse_offset[size_t(i)] = q::frac_to_phase(0.75f + w / 2.0f);
        }
    }

    sub_retune(vc);
    sub_relevel(vc);
    sub_repan(vc);
    /* The chip zeroes every operator's phase at a key-on, so partials that
     * beat against each other start each note the same way; so do these. */
    for (auto& ph : v.phase) {
        ph._phase = q::phase{};
    }
    for (auto& e : v.env) {
        e.attack();
    }
}

/* ------------------------------------------------------------------------
 * The PM voicing. */

int QBank::pm_allocate()
{
    int quietest = -1;
    int quietest_att = -1;

    for (int i = 0; i < int(pm.size()); i++) {
        PmVoice const& v = pm[size_t(i)];
        if (v.owner < 0 && !v.active()) {
            return i;
        }
        if (v.owner < 0 && v.loudest_att() > quietest_att) {
            quietest_att = v.loudest_att();
            quietest = i;
        }
    }
    return (quietest >= 0) ? quietest : 0;
}

/* The patch from the channel's registers into the voice, as the chip reads
 * them: the operators, the algorithm and feedback, the pitch. The envelope's
 * state and level carry on. */
void QBank::pm_load(PmVoice& v, int vc)
{
    u8 b0 = vreg(vc, 0xB0);

    v.algorithm = b0 & 7;
    v.feedback  = (b0 >> 3) & 7;
    v.fb_scale  = (v.feedback == 0) ? 0.0f : std::ldexp(1.0f, v.feedback - 7);
    for (int i = 0; i < 4; i++) {
        int off = kOpOffset[i];
        PmOp& o = v.op[size_t(i)];
        u8 dtmul = vreg(vc, 0x30 + off);
        u8 ksar  = vreg(vc, 0x50 + off);
        u8 slrr  = vreg(vc, 0x80 + off);
        int mul  = dtmul & 15;

        o.multi = (mul == 0) ? 1 : mul * 2;
        o.dt    = (dtmul >> 4) & 7;
        o.tl    = vreg(vc, 0x40 + off) & 0x7F;
        o.ks    = ksar >> 6;
        o.ar    = ksar & 31;
        o.dr    = vreg(vc, 0x60 + off) & 31;
        o.sr    = vreg(vc, 0x70 + off) & 31;
        o.sl    = slrr >> 4;
        o.sl   |= (o.sl + 1) & 0x10;
        o.rr    = slrr & 15;
    }
    pm_retune(v, vc);
}

void QBank::pm_retune(PmVoice& v, int vc)
{
    v.fnum  = ((vreg(vc, 0xA4) & 7) << 8) | vreg(vc, 0xA0);
    v.block = (vreg(vc, 0xA4) >> 3) & 7;
    v.kcode = (v.block << 2) | kFnNote[(v.fnum >> 7) & 15];
    pm_steps(v);
}

/* The operators' phase steps at the running rate, from the pitch the voice
 * holds. */
void QBank::pm_steps(PmVoice& v)
{
    for (int i = 0; i < 4; i++) {
        PmOp& o = v.op[size_t(i)];
        u32 inc = pg_increment(v.fnum, v.block, v.kcode, o.dt, o.multi);
        /* (build 112) a patch's fine_cents, a ratio on the chip's step */
        double cycles = double(inc) / 1048576.0 * kNativeRate / os_rate
                      * double(v.fine_mul[size_t(i)]);
        o.step = q::phase{u32(std::llround(cycles * 4294967296.0) & 0xFFFFFFFFll)};
    }
}

void QBank::pm_strike(int vc)
{
    VChan& c = vchan[size_t(vc)];
    int vi = c.voice;

    if (vi >= 0 && pm[size_t(vi)].owner != vc) {
        vi = -1;
    }
    if (parallel && vi >= 0 && pm[size_t(vi)].audible()) {
        /* its tail goes on, released, on the voice it has -- and fades out
         * over kTailFadeS (111b), the time the chip gives it: none, the
         * strike ends it there. So the new note begins from silence under a
         * tail that fades rather than jumps (no click), and notes do not pile
         * up where the chip has one a channel (111's tails rang their whole
         * release: the level-up's six notes and a release of 8 dB a second
         * stacked 10 dB over the chip). */
        PmVoice& tail = pm[size_t(vi)];
        for (auto& o : tail.op) {
            o.state = EgState::Release;
        }
        tail.owner = -1;
        /* (build 112) TAILS RING, or the patch's own "ring": the tail
         * finishes its release, as 111a had it -- at most kRingMax at once,
         * the quietest faded to make room */
        bool ring = (tail.tail_mode == SYNTH_TAIL_ROW)
                  ? ring_tails : (tail.tail_mode == SYNTH_TAIL_RING);
        if (ring) {
            int quietest = -1;
            if (ringing_tails(&quietest) > kRingMax && quietest >= 0) {
                PmVoice& q = pm[size_t(quietest)];
                q.fade_len = std::max(1, int(kTailFadeS * os_rate));
                q.fade_left = q.fade_len;
            }
        } else {
            tail.fade_len = std::max(1, int(kTailFadeS * os_rate));
            tail.fade_left = tail.fade_len;
        }
        vi = -1;
    }
    if (vi < 0) {
        vi = pm_allocate();
        PmVoice& fresh = pm[size_t(vi)];
        fresh = PmVoice{};
        fresh.owner = vc;
        c.voice = vi;
    }
    PmVoice& v = pm[size_t(vi)];
    v.effect = c.effect;
    /* (build 112) what only Q does, from the patch (none: as the ROM) */
    v.fine_mul.fill(1.0f);
    v.patch_gain = 1.0f;
    v.tail_mode = SYNTH_TAIL_ROW;
    if (patches_on && c.patch >= 0) {
        SynthPatch const& p = patches[size_t(c.patch)];
        for (int i = 0; i < 4; i++) {
            v.fine_mul[size_t(i)] = std::exp2(p.fine_cents[i] / 1200.0f);
        }
        v.patch_gain = std::pow(10.0f, p.volume_db / 20.0f);
        v.tail_mode = p.tail;
    }
    pm_load(v, vc);
    u8 pan = vreg(vc, 0xB4);
    v.pan_l_target = (pan & 0x80) ? 1.0f : 0.0f;
    v.pan_r_target = (pan & 0x40) ? 1.0f : 0.0f;
    if (!v.struck) {
        v.pan_l = v.pan_l_target;
        v.pan_r = v.pan_r_target;
    }
    v.struck = true;
    /* the key-on: phases to zero, the attack from the level the operator
     * has (from silence on a fresh voice), at once at the top rates */
    for (auto& o : v.op) {
        int ksv = v.kcode >> (o.ks ^ 3);
        int rate = std::min(63, o.ar * 2 + ksv);
        o.phase = q::phase{};
        o.state = EgState::Attack;
        if ((rate >> 1) == 31) {
            o.level = 0;
        }
        o.amp_next = att_to_gain(o.attenuation());
        o.amp_prev = o.amp_next;
    }
    v.o1_hist.fill(0.0f);
    v.o2_prev = 0.0f;
}

/* One step of the chip's envelope generator, every voice: the 12-bit timer,
 * its shift and low bits, each operator stepped, its gain for the next
 * interval. */
void QBank::pm_eg_step()
{
    eg_timer = (eg_timer + 1) & 0xFFF;
    int shift = 0;
    if (eg_timer != 0) {
        int t = eg_timer;
        while ((t & 1) == 0) {
            t >>= 1;
            shift++;
        }
        shift++;
    }
    int low = eg_timer & 3;
    for (auto& v : pm) {
        if (!v.struck) {
            continue;
        }
        for (auto& o : v.op) {
            o.amp_prev = o.amp_next;
            eg_step(o, v.kcode, shift, low);
            o.amp_next = att_to_gain(o.attenuation());
        }
    }
}

/* One running sample of one voice: the operators in the chip's order, the
 * carriers into the channel's clamped sum, panned, into l and r. */
void QBank::pm_render_one(PmVoice& v, float gain, float& l, float& r)
{
    float t = float(eg_pos);
    float amp[4];
    for (int i = 0; i < 4; i++) {
        PmOp const& o = v.op[size_t(i)];
        amp[i] = o.amp_prev + (o.amp_next - o.amp_prev) * t;
    }
    auto run = [](PmOp& o, float gain_now, float mod_cycles) {
        double m = double(mod_cycles);
        u32 off = u32(std::int64_t(m * 4294967296.0));
        q::phase ph = o.phase;
        ph += q::phase{off};
        return q::sin(ph) * gain_now;
    };
    int alg = v.algorithm;
    const int n1 = int(v.o1_hist.size());
    float fb = 0.0f;
    if (v.feedback != 0) {
        float a = v.o1_hist[size_t((v.o1_at - (fb_lag1 - 1) + n1) % n1)];
        float b = v.o1_hist[size_t((v.o1_at - (fb_lag2 - 1) + n1) % n1)];
        fb = (a + b) * v.fb_scale;
    }
    float o1 = run(v.op[0], amp[0], fb);
    float in3 = 0.0f;
    if (kInto3[alg] & 1) {
        in3 += o1;
    }
    if (kInto3[alg] & 2) {
        in3 += v.o2_prev;
    }
    float o3 = run(v.op[2], amp[2], 4.0f * in3);
    float in2 = (kInto2[alg] & 1) ? o1 : 0.0f;
    float o2 = run(v.op[1], amp[1], 4.0f * in2);
    float in4 = 0.0f;
    if (kInto4[alg] & 1) {
        in4 += o1;
    }
    if (kInto4[alg] & 2) {
        in4 += o2;
    }
    if (kInto4[alg] & 4) {
        in4 += o3;
    }
    float o4 = run(v.op[3], amp[3], 4.0f * in4);

    v.o1_at = (v.o1_at + 1) % n1;
    v.o1_hist[size_t(v.o1_at)] = o1;
    v.o2_prev = o2;
    for (auto& o : v.op) {
        o.phase += o.step;
    }

    /* the chip's accumulator: each carrier added in its slot order, the
     * running sum clamped to 9 bits */
    const float outs[4] = { o1, o2, o3, o4 };
    float acc = 0.0f;
    for (int k = 0; k < 4; k++) {
        int i = kChipOrder[k];
        if ((kCarriers[alg] >> i) & 1) {
            /* (112) PS2_Q_GRAIN: the operator's output >> 5, a floor */
            acc += grain ? std::floor(outs[i] * 256.0f) / 256.0f : outs[i];
            acc = (acc > 255.0f / 256.0f) ? 255.0f / 256.0f : acc;
            acc = (acc < -1.0f) ? -1.0f : acc;
        }
    }
    if (grain && acc < 0.0f) {
        acc -= 7.0f / 256.0f;
    }
    float coef = 0.02f / float(os);
    v.pan_l += (v.pan_l_target - v.pan_l) * coef;
    v.pan_r += (v.pan_r_target - v.pan_r) * coef;
    float s = acc * kPmVoiceGain * gain * v.patch_gain;
    if (v.fade_len > 0) {
        /* (111b) a tail's raised-cosine fade to nothing, then free */
        float x = float(v.fade_left) / float(v.fade_len);
        s *= 0.5f - 0.5f * std::cos(float(q::pi) * x);
        v.fade_left--;
        if (v.fade_left <= 0) {
            v.struck = false;
            v.fade_len = 0;
        }
    }
    l += s * v.pan_l;
    r += s * v.pan_r;
}

void QBank::drum(u8 byte)
{
    u8 want = 0x80;

    /* (111b) The drum's pan ($B6) is written just before its byte: the
     * DAC's byte takes no slot, so FM6's pan is taken as the queue will
     * leave it. */
    for (size_t i = pending_at; i < pending.size(); i++) {
        if (!pending[i].hook && pending[i].part == 1 && pending[i].reg == 0xB6) {
            reg[1][0xB6] = pending[i].value;
        }
    }

    /* The Z80's scan: the first of bits 0-6 set, a bare $80 its own. */
    for (int bit = 0; bit < 7; bit++) {
        if (byte & (1 << bit)) {
            want = u8(0x80 | (1 << bit));
            break;
        }
    }
    u8 pan = reg[1][0xB6];
    bool left  = (pan & 0x80) != 0;
    bool right = (pan & 0x40) != 0;

    if (drum_mode == Q_DRUMS_SAMPLES) {
        samples.strike(want, left, right);
        return;
    }
    for (auto const& d : kDrums) {
        if (d.byte == want) {
            drums.strike(d, left, right);
            return;
        }
    }
}

void QBank::render(float* out, int frames, float music, float effects)
{
    for (int n = 0; n < frames; n++) {
        apply_due(clock + double(n));
        float fm_l = 0.0f;
        float fm_r = 0.0f;
        float dr_l = 0.0f;
        float dr_r = 0.0f;

        if (voicing == Q_VOICE_SUBTRACTIVE) {
            for (int vc = 0; vc < int(vchan.size()); vc++) {
                Voice& v = voices[size_t(vc)];
                if (v.active()) {
                    float g = vchan[size_t(vc)].effect ? effects : music;
                    float l = 0.0f;
                    float r = 0.0f;
                    v.render_one(l, r);
                    fm_l += l * g;
                    fm_r += r * g;
                    voice_samples++;
                }
            }
            fm_l *= kFmVoiceGain;
            fm_r *= kFmVoiceGain;
        } else {
            /* the running rate: os samples of every voice, each into the
             * low-pass's history; every os-th, one sample out */
            for (int k = 0; k < os; k++) {
                float l = 0.0f;
                float r = 0.0f;
                for (auto& v : pm) {
                    if (v.active()) {
                        pm_render_one(v, v.effect ? effects : music, l, r);
                    }
                }
                eg_pos += eg_per_sample;
                while (eg_pos >= 1.0) {
                    eg_pos -= 1.0;
                    pm_eg_step();
                }
                if (os == 1) {
                    fm_l = l;
                    fm_r = r;
                } else {
                    hist_l[size_t(hist_at)] = l;
                    hist_r[size_t(hist_at)] = r;
                    hist_at = (hist_at + 1) % int(fir.size());
                }
            }
            for (auto const& v : pm) {
                voice_samples += v.active() ? 1 : 0;
            }
            if (os > 1) {
                int m = int(fir.size());
                int at = hist_at;
                for (int i = 0; i < m; i++) {
                    at = (at == 0) ? m - 1 : at - 1;
                    fm_l += fir[size_t(i)] * hist_l[size_t(at)];
                    fm_r += fir[size_t(i)] * hist_r[size_t(at)];
                }
            }
        }
        if (drums.active()) {
            drums.render_one(dr_l, dr_r);
        }
        /* (139e) the room behind the toms, under them */
        if (drums.room_fed) {
            tom_room.feed(drums.room_send);
            drums.room_fed = false;
        }
        tom_room.render(dr_l, dr_r);
        if (samples.active()) {
            float sl = 0.0f;
            float sr = 0.0f;
            samples.render_one(sl, sr);
            dr_l += sl * kSampleDrumGain;
            dr_r += sr * kSampleDrumGain;
        }
        float l = fm_l + dr_l * kDrumGain * music;
        float r = fm_r + dr_r * kDrumGain * music;
        out[2 * n]     += dc_l(l) * trim;
        out[2 * n + 1] += dc_r(r) * trim;
    }
    clock += double(frames);
}

/* ------------------------------------------------------------------------
 * The C interface (q_synth.h) and the synth table's entry (synth.h). */

extern "C" {

QBank* q_bank_create(int sample_rate)
{
    QBank* b = new QBank(float(sample_rate));
    const char* mode  = std::getenv("PS2_Q_DRUMS");
    const char* voice = std::getenv("PS2_Q_VOICE");
    const char* os    = std::getenv("PS2_Q_OVERSAMPLE");
    const char* par   = std::getenv("PS2_Q_PARALLEL");

    if (voice != nullptr && std::strcmp(voice, "subtractive") == 0) {
        q_bank_set_voicing(b, Q_VOICE_SUBTRACTIVE);
    }
    if (mode != nullptr && std::strcmp(mode, "samples") == 0) {
        q_bank_set_drum_mode(b, Q_DRUMS_SAMPLES);
    }
    if (os != nullptr) {
        b->set_oversample(std::atoi(os));
    }
    if (par != nullptr && std::strcmp(par, "0") == 0) {
        b->parallel = false;
    }
    const char* take = std::getenv("PS2_Q_EFFECTS");
    if (take != nullptr && std::strcmp(take, "take") == 0) {
        b->take_channels = true;
    }
    /* (build 112) for the tools (q_demo, fm_probe); in the game the AUDIO
     * page's rows (main.c reads these into them) */
    const char* tails = std::getenv("PS2_Q_TAILS");
    if (tails != nullptr && std::strcmp(tails, "ring") == 0) {
        b->ring_tails = true;
    }
    const char* grain = std::getenv("PS2_Q_GRAIN");
    if (grain != nullptr && std::strcmp(grain, "1") == 0) {
        b->grain = true;
    }
    const char* pat = std::getenv("PS2_Q_PATCHES");
    if (pat != nullptr && std::strcmp(pat, "0") == 0) {
        b->patches_on = false;
    }
    return b;
}

void q_bank_destroy(QBank* b)
{
    delete b;
}

bool q_bank_load_drums(QBank* b, const char* path)
{
    return b->load_drums(path);
}

void q_bank_reset(QBank* b)
{
    b->reset();
}

void q_bank_write(QBank* b, int part, u8 r, u8 value)
{
    b->write(part, r, value);
}

void q_bank_track_write(QBank* b, int track, u8 channel, int part, u8 r,
                        u8 value, bool to_chip)
{
    b->track_write(track, channel, part, r, value, to_chip);
}

void q_bank_drum(QBank* b, u8 drum_byte)
{
    b->drum(drum_byte);
}

void q_bank_render(QBank* b, float* out, int frames, float gain)
{
    b->render(out, frames, gain, gain);
}

void q_bank_render_split(QBank* b, float* out, int frames, float music,
                         float effects)
{
    b->render(out, frames, music, effects);
}

/* The trim goes with the voicing and the drum mode; a q_bank_set_trim after
 * them overrides it. */
static void q_bank_retrim(QBank* b)
{
    int d = (b->drum_mode == Q_DRUMS_SAMPLES) ? 1 : 0;
    b->trim = (b->voicing == Q_VOICE_SUBTRACTIVE) ? kQTrim[d] : kQTrimPm[d];
}

void q_bank_set_voicing(QBank* b, QVoicing voicing)
{
    b->set_voicing(voicing);
    q_bank_retrim(b);
}

void q_bank_set_drum_mode(QBank* b, QDrumMode mode)
{
    b->drum_mode = mode;
    q_bank_retrim(b);
}

void q_bank_set_oversample(QBank* b, int n)
{
    b->set_oversample(n);
}

void q_bank_set_parallel(QBank* b, bool on)
{
    b->parallel = on;
}

void q_bank_set_option(QBank* b, int option, int value)
{
    b->set_option(option, value);
}

void q_bank_set_patches(QBank* b, const SynthPatch* patches, int count)
{
    b->patches.assign(patches, patches + ((count > 0) ? count : 0));
    for (auto& c : b->vchan) {
        c.patch = (c.ins >= 0) ? b->patch_for(c.ins) : -1;
    }
}

void q_bank_set_trim(QBank* b, float trim)
{
    b->trim = trim;
}

float q_bank_trim(const QBank* b)
{
    return b->trim;
}

void q_bank_set_cutoff_every(QBank* b, int n)
{
    for (auto& v : b->voices) {
        v.cutoff_every = (n < 1) ? 1 : n;
    }
}

long long q_bank_voice_samples(const QBank* b)
{
    return b->voice_samples;
}

int q_bank_pending_writes(const QBank* b)
{
    return int(b->pending.size() - b->pending_at);
}

static void* q_backend_create(int sample_rate)
{
    QBank* b = q_bank_create(sample_rate);
    b->hooked = true;    /* the game hands it the hooks' writes */
    return b;
}

static bool q_backend_load_drums(void* self, const char* path)
{
    return q_bank_load_drums(static_cast<QBank*>(self), path);
}

static void q_backend_reset(void* self)
{
    QBank* b = static_cast<QBank*>(self);
    q_bank_reset(b);
    b->hooked = true;
}

static void q_backend_write(void* self, int part, u8 reg, u8 value)
{
    q_bank_write(static_cast<QBank*>(self), part, reg, value);
}

static void q_backend_drum(void* self, u8 drum_byte)
{
    q_bank_drum(static_cast<QBank*>(self), drum_byte);
}

static void q_backend_track_write(void* self, int track, u8 channel, int part,
                                  u8 reg, u8 value, bool to_chip)
{
    q_bank_track_write(static_cast<QBank*>(self), track, channel, part, reg,
                       value, to_chip);
}

static void q_backend_render(void* self, float* out, int frames,
                             float music_gain, float effects_gain)
{
    q_bank_render_split(static_cast<QBank*>(self), out, frames, music_gain,
                        effects_gain);
}

static void q_backend_shutdown(void* self)
{
    q_bank_destroy(static_cast<QBank*>(self));
}

/* (111a) The AUDIO page's OVERSAMPLE. */
static void q_backend_set_oversample(void* self, int n)
{
    q_bank_set_oversample(static_cast<QBank*>(self), n);
}

/* (build 112) VOICES, TAILS, PATCHES; and the patch file. */
static void q_backend_set_option(void* self, int option, int value)
{
    q_bank_set_option(static_cast<QBank*>(self), option, value);
}

static void q_backend_set_patches(void* self, const SynthPatch* patches,
                                  int count)
{
    q_bank_set_patches(static_cast<QBank*>(self), patches, count);
}

/* (build 184) A mod's chord voices. */
static void q_backend_set_chord_voices(void* self, int music, int effects)
{
    static_cast<QBank*>(self)->set_chord_voices(music, effects);
}

extern const SynthBackend q_synth_backend;
const SynthBackend q_synth_backend = {
    "q", "Q",
    q_backend_create, q_backend_load_drums, q_backend_reset, q_backend_write,
    q_backend_drum, q_backend_track_write, q_backend_render,
    q_backend_shutdown, true, q_backend_set_oversample,
    q_backend_set_option, q_backend_set_patches, q_backend_set_chord_voices
};

}   /* extern "C" */
