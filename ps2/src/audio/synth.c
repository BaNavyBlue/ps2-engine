#include "synth.h"
#include "fm.h"
#include "smps.h"

#include <stdlib.h>
#include <string.h>

/* (build 110) The backend table (synth.h). */

/* ------------------------------------------------------------------------
 * CHIP: Nuked-OPN2 through fm.c, exactly the calls audio.c made before the
 * table existed -- fm_write, fm_dac_play, fm_render -- on an Fm of its own.
 *
 * (build 124) Two of them (docs/second_chip.md). A is the Mega Drive's one
 * chip: under VOICES LIMITED it is all that is heard, written and weighted
 * exactly as before, and an effect takes the music's channel. Under EXTRA,
 * A plays the music alone -- every music track's writes from the hooks, the
 * ones the chip never got while an effect held the channel too -- and B the
 * effects, each on the channel it claimed, so the music loses no note. B
 * runs all the time, heard or not (the page's recommendation): stopping and
 * starting it would move its standing DC through the blocker, a thump. */

#define CHIP_SCRATCH  1024          /* frames of B rendered unheard at once */
#define CHIP_WARM_UP  96000         /* frames B runs at start, 2 s: its DC */

typedef struct {
    Fm    a;                        /* the music's (LIMITED: the one chip) */
    Fm    b;                        /* the effects' (EXTRA) */
    bool  extra;                    /* VOICES EXTRA */
    /* The registers as the plain writes left them (both ports): what the
     * one chip holds under LIMITED, and what B's channel starts from when
     * an effect claims it. */
    u8    shadow[2][256];
    int   claim[6];                 /* the effect track on B's channel, -1 */
    /* (build 143) a music track inside RestoreBGMChn's upload (smps.h,
     * SMPS_HOOK_RESTORE) */
    bool  restoring[SMPS_MUSIC_TRACKS];
    float scratch[CHIP_SCRATCH * 2];
    /* (build 184) The chord chips (smps.h, "Chords past the chip"): six
     * chord voices each, the music's chips first, then the effects', made
     * by chip_set_chord_voices for a mod whose chords need them -- none for
     * the game's music. Each is rendered while it sounds (fm_idle). */
    Fm   *chord;
    int   chord_chips;
    int   chord_music;              /* the music's voices */
    int   chord_music_chips;
    int   sample_rate;
} Chip;

/* $28's channel id (0-2, 4-6) as a channel 0-5; -1 for none. */
static int chip_key_channel(u8 value)
{
    int id = value & 7;
    if (id == 3 || id == 7) {
        return -1;
    }
    return (id < 3) ? id : id - 1;
}

/* The channel (0-5) a channel register of `part` is; -1 for a global one.
 * $A8-$AE are FM3's special-mode pitches, FM3's. */
static int chip_reg_channel(int part, u8 reg)
{
    if (reg < 0x30) {
        return -1;
    }
    if (reg >= 0xA8 && reg <= 0xAE) {
        return part ? -1 : 2;
    }
    if ((reg & 3) == 3) {
        return -1;
    }
    return (part ? 3 : 0) + (reg & 3);
}

/* B's channel ch given the one chip's registers there, as the chip's own
 * channel stands when an effect takes it (docs/second_chip.md, the first of
 * the three): operators, algorithm and pan, then the pitch (the high byte,
 * which the chip latches, then the low), FM3's special-mode pitches with it
 * -- what an effect that never writes a pitch sounds at. */
static void chip_seed_b(Chip *c, int ch)
{
    int p    = (ch >= 3) ? 1 : 0;
    int slot = ch % 3;
    for (int base = 0x30; base <= 0xB4; base += 4) {
        if (base >= 0xA0 && base <= 0xAC) {
            continue;
        }
        fm_write(&c->b, p, (u8)(base + slot), c->shadow[p][base + slot]);
    }
    fm_write(&c->b, p, (u8)(0xA4 + slot), c->shadow[p][0xA4 + slot]);
    fm_write(&c->b, p, (u8)(0xA0 + slot), c->shadow[p][0xA0 + slot]);
    if (ch == 2) {
        for (int i = 0; i < 3; i++) {
            fm_write(&c->b, 0, (u8)(0xAC + i), c->shadow[0][0xAC + i]);
            fm_write(&c->b, 0, (u8)(0xA8 + i), c->shadow[0][0xA8 + i]);
        }
    }
}

/* The effect's note on B's channel ch ends when the music strikes there,
 * as the one chip's single note a channel does (the third of the three):
 * every operator to the fastest release, keyed off. */
static void chip_end_b(Chip *c, int ch)
{
    int p    = (ch >= 3) ? 1 : 0;
    int slot = ch % 3;
    for (int op = 0; op < 4; op++) {
        fm_write(&c->b, p, (u8)(0x80 + 4 * op + slot), 0xFF);
    }
    fm_write(&c->b, 0, 0x28, (u8)(slot + (p ? 4 : 0)));
    c->claim[ch] = -1;
}

static void chip_reset(void *self)
{
    Chip *c = self;

    fm_reset(&c->a);
    fm_reset(&c->b);
    memset(c->shadow, 0, sizeof c->shadow);
    for (int p = 0; p < 2; p++) {
        for (int slot = 0; slot < 3; slot++) {
            c->shadow[p][0xB4 + slot] = 0xC0;
        }
    }
    for (int ch = 0; ch < 6; ch++) {
        c->claim[ch] = -1;
    }
    memset(c->restoring, 0, sizeof c->restoring);
    for (int k = 0; k < c->chord_chips; k++) {
        fm_reset(&c->chord[k]);
    }
}

/* B rendered unheard: kept running, its output let go. */
static void chip_run_b(Chip *c, int frames)
{
    while (frames > 0) {
        int n = (frames > CHIP_SCRATCH) ? CHIP_SCRATCH : frames;
        memset(c->scratch, 0, (size_t)n * 2 * sizeof c->scratch[0]);
        fm_render(&c->b, c->scratch, n, 1.0f);
        frames -= n;
    }
}

static void *chip_create(int sample_rate)
{
    Chip *c = calloc(1, sizeof *c);

    if (c == NULL) {
        return NULL;
    }
    c->sample_rate = sample_rate;
    if (!fm_init(&c->a, sample_rate) || !fm_init(&c->b, sample_rate)) {
        fm_shutdown(&c->a);
        fm_shutdown(&c->b);
        free(c);
        return NULL;
    }
    chip_reset(c);
    /* B's standing DC through its blocker before it is ever heard, then
     * its resampler back in step with A's. */
    chip_run_b(c, CHIP_WARM_UP);
    c->b.phase = c->a.phase;
    for (int side = 0; side < 2; side++) {
        c->b.prev[side] = 0.0f;
        c->b.next[side] = 0.0f;
    }
    return c;
}

static bool chip_load_drums(void *self, const char *path)
{
    Chip *c = self;
    return fm_load_drums(&c->a, path);    /* the drums are the music's */
}

/* The plain write: the one chip's under LIMITED. Under EXTRA the hooks
 * carry every channel's writes (chip_track_write); the globals -- $22, $27,
 * $2B, the mode registers -- go from here to both chips. */
static void chip_write(void *self, int part, u8 reg, u8 value)
{
    Chip *c = self;

    c->shadow[part ? 1 : 0][reg] = value;
    /* (build 184) the LFO is the chord chips' too; the other chip-wide
     * registers are not: $27's FM3 mode would turn a chord voice on the
     * chip's third channel into FM3's special mode, $2B's DAC its sixth
     * into the drums */
    if (part == 0 && reg == 0x22) {
        for (int k = 0; k < c->chord_chips; k++) {
            fm_write(&c->chord[k], 0, reg, value);
        }
    }
    if (!c->extra) {
        fm_write(&c->a, part, reg, value);
        return;
    }
    if (part == 0 && reg < 0x30 && reg != 0x28) {
        fm_write(&c->a, part, reg, value);
        fm_write(&c->b, part, reg, value);
    }
}

static void chip_drum(void *self, u8 drum_byte)
{
    Chip *c = self;
    fm_dac_play(&c->a, drum_byte);
}

/* (build 184) A chord voice's write (smps.h): onto its chord chip, where
 * the driver addressed it -- the same under LIMITED and EXTRA, as the voice
 * is no channel of the Mega Drive's chip either way. The hooks' marks (an
 * instrument's number) and the chip-wide registers are not a voice's. */
static void chip_chord_write(Chip *c, int voice, int part, u8 reg, u8 value)
{
    int k = smps_voice_chip(c->chord_music, voice);

    if (voice < 0 || k < 0 || k >= c->chord_chips) {
        return;
    }
    if (reg < 0x30 && !(part == 0 && reg == 0x28)) {
        return;
    }
    fm_write(&c->chord[k], part, reg, value);
}

/* (build 111) The hooks: an effect's key-on that reaches the chip marks its
 * channel as the effects', a music track's as the music's (fm.c weights each
 * channel by it when EFFECTS and MUSIC differ). The mark goes through the
 * chip's write queue, so it lands with the key-on it came with.
 * (build 124) That is LIMITED's. Under EXTRA the hooks route every channel
 * write: a music track's to A, whether the one chip would have had it or
 * not; an effect's to B on the channel it claimed (to A elsewhere: the pan
 * flag's lands on FM1, the music's, as on the Mega Drive); the driver's
 * silencing to both. Once the effect has ended -- the music's writes reach
 * the chip again, RestoreBGMChn's upload first -- they land under B's
 * releasing note as well, as they do on the one chip, and the music's
 * first strike there ends it.
 * (build 143) But RestoreBGMChn's upload itself (smps.h, SMPS_HOOK_RESTORE)
 * reaches B alone: A has played that track all along, its instrument
 * there already, and the upload's levels before the track's volume were a
 * click on A's sounding note. */
static void chip_track_write(void *self, int track, u8 channel, int part,
                             u8 reg, u8 value, bool to_chip)
{
    Chip *c = self;

    (void)channel;
    if (track >= SMPS_TRACKS) {
        chip_chord_write(c, track - SMPS_TRACKS, part, reg, value);
        return;
    }
    if (reg == SMPS_HOOK_RESTORE) {
        /* kept under LIMITED too, where nothing reads it */
        if (track >= 0 && track < SMPS_MUSIC_TRACKS) {
            c->restoring[track] = (value != 0);
        }
        return;
    }
    if (!c->extra) {
        if (!to_chip || reg != 0x28 || !(value & 0xF0)
            || track == SMPS_TRACK_DRIVER) {
            return;
        }
        int ch = chip_key_channel(value);
        if (ch < 0) {
            return;
        }
        fm_mark_channel(&c->a, ch, track >= SMPS_MUSIC_TRACKS);
        return;
    }

    if (reg == SMPS_HOOK_INSTRUMENT) {
        return;
    }
    if (reg == SMPS_HOOK_CLAIM) {
        int ch = chip_key_channel(value);
        if (ch >= 0 && track >= SMPS_MUSIC_TRACKS) {
            c->claim[ch] = track;
            chip_seed_b(c, ch);
        }
        return;
    }
    if (part == 0 && reg < 0x30 && reg != 0x28) {
        return;                         /* a global: chip_write's */
    }
    if (track == SMPS_TRACK_DRIVER) {
        fm_write(&c->a, part, reg, value);
        fm_write(&c->b, part, reg, value);
        return;
    }
    bool key = (part == 0 && reg == 0x28);
    int  ch  = key ? chip_key_channel(value) : chip_reg_channel(part, reg);
    if (ch < 0) {
        fm_write(&c->a, part, reg, value);
        return;
    }
    if (track >= SMPS_MUSIC_TRACKS) {
        fm_write((c->claim[ch] == track) ? &c->b : &c->a, part, reg, value);
        return;
    }
    if (!c->restoring[track]) {
        fm_write(&c->a, part, reg, value);
    }
    if (to_chip && c->claim[ch] >= 0) {
        if (!key) {
            fm_write(&c->b, part, reg, value);
        } else if (value & 0xF0) {
            chip_end_b(c, ch);
        }
    }
}

static void chip_render(void *self, float *out, int frames, float music_gain,
                        float effects_gain)
{
    Chip *c = self;

    if (!c->extra) {
        fm_render_split(&c->a, out, frames, music_gain, effects_gain);
        chip_run_b(c, frames);
    } else {
        fm_render(&c->a, out, frames, music_gain);
        fm_render(&c->b, out, frames, effects_gain);
    }
    /* (build 184) the chord chips while they sound: a chip at rest adds
     * nothing (fm_idle), and takes up from there at its next write */
    for (int k = 0; k < c->chord_chips; k++) {
        if (!fm_idle(&c->chord[k])) {
            fm_render(&c->chord[k], out, frames,
                      (k < c->chord_music_chips) ? music_gain : effects_gain);
        }
    }
}

static void chip_free_chords(Chip *c)
{
    for (int k = 0; k < c->chord_chips; k++) {
        fm_shutdown(&c->chord[k]);
    }
    free(c->chord);
    c->chord             = NULL;
    c->chord_chips       = 0;
    c->chord_music       = 0;
    c->chord_music_chips = 0;
}

static void chip_shutdown(void *self)
{
    Chip *c = self;

    fm_shutdown(&c->a);
    fm_shutdown(&c->b);
    chip_free_chords(c);
    free(c);
}

/* (build 184) A chord chip for every six chord voices the music can need,
 * the music's and the effects' apart (smps_voice_chip), each settled to
 * silence from its first sample. Before the audio thread runs (synth.h). */
static void chip_set_chord_voices(void *self, int music, int effects)
{
    Chip *c = self;
    int   n = smps_voice_chips(music, effects);

    chip_free_chords(c);
    if (n <= 0) {
        return;
    }
    c->chord = calloc((size_t)n, sizeof *c->chord);
    if (c->chord == NULL) {
        return;
    }
    for (int k = 0; k < n; k++) {
        if (!fm_init(&c->chord[k], c->sample_rate)) {
            for (int j = 0; j <= k; j++) {
                fm_shutdown(&c->chord[j]);
            }
            free(c->chord);
            c->chord = NULL;
            return;
        }
        fm_settle(&c->chord[k]);
    }
    c->chord_chips       = n;
    c->chord_music       = music;
    c->chord_music_chips = smps_voice_chips(music, 0);
}

/* (build 124) VOICES, the chip's one switch: 0 EXTRA (B), 1 LIMITED (the
 * Mega Drive's one chip, the default until the AUDIO page says). audio.c
 * resets and replays the chip when it changes, so A and B take up the song
 * where the other way left it. */
static void chip_set_option(void *self, int option, int value)
{
    Chip *c = self;

    if (option == SYNTH_OPT_VOICES) {
        c->extra = (value == 0);
    }
}

int synth_chip_pending_writes(const void *chip)
{
    const Chip *c = chip;
    int n = fm_pending_writes(&c->a) + fm_pending_writes(&c->b);
    for (int k = 0; k < c->chord_chips; k++) {
        n += fm_pending_writes(&c->chord[k]);
    }
    return n;
}

static const SynthBackend g_chip = {
    "chip", "CHIP",
    chip_create, chip_load_drums, chip_reset, chip_write, chip_drum,
    chip_track_write, chip_render, chip_shutdown, false, NULL,
    chip_set_option, NULL, chip_set_chord_voices
};

/* ------------------------------------------------------------------------
 * Q: src/audio/q/q_synth.cpp, C++20, linked only by `make Q=1`, which
 * defines PS2_HAVE_Q. Its table is defined there, behind extern "C". */

#ifdef PS2_HAVE_Q
extern const SynthBackend q_synth_backend;
void q_synth_set_reader(Ps2Reader reader);      /* q_synth.h */
#endif

void synth_set_reader(Ps2Reader reader)
{
    fm_set_reader(reader);
#ifdef PS2_HAVE_Q
    q_synth_set_reader(reader);
#endif
}

const SynthBackend *synth_backend(SynthId id)
{
    switch (id) {
    case SYNTH_CHIP:
        return &g_chip;
    case SYNTH_Q:
#ifdef PS2_HAVE_Q
        return &q_synth_backend;
#else
        return NULL;
#endif
    default:
        return NULL;
    }
}

int synth_from_name(const char *name)
{
    if (name == NULL) {
        return -1;
    }
    if (strcmp(name, "chip") == 0) {
        return SYNTH_CHIP;
    }
    if (strcmp(name, "q") == 0) {
        return SYNTH_Q;
    }
    return -1;
}
