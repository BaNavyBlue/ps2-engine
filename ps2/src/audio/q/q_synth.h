#ifndef PS2_Q_SYNTH_H
#define PS2_Q_SYNTH_H

/* (build 110) The Q backend's C interface: the sequencer's chip writes
 * re-voiced with Cycfi's Q (q_synth.cpp, C++20; docs/audio.md, "The Q
 * backend"). Built only with `make Q=1`, `make q_demo` or `make
 * audio_check_q`; C reaches it through these calls and the synth table's
 * entry (q_synth_backend, synth.h), all extern "C".
 *
 * The bank decodes register writes as the chip would take them: it keeps a
 * shadow of both ports, a key-on strikes a voice with the patch the shadow
 * holds, $A4/$A0 retune it, the carriers' total levels set its loudness, $B4
 * pans it, a drum byte strikes the drum voice with FM6's pan. */

#include "../../core/ps2_types.h"
#include "../synth.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct QBank QBank;

/* How the drums sound (docs/audio.md, "The Q backend", for the owner's choice):
 * synthesised by what the ROM's six samples measure as, or the samples
 * themselves at the Z80's rates through Q's Hermite interpolation and a
 * low-pass. PS2_Q_DRUMS=synth|samples picks one at create; synthesised
 * unless it says otherwise. */
typedef enum {
    Q_DRUMS_SYNTH = 0,
    Q_DRUMS_SAMPLES
} QDrumMode;

/* (build 111) How the FM voices are made (docs/audio.md, "The PM voicing"):
 * four operators phase-modulating each other in float, the chip's
 * algorithms, ratios, detune, feedback and envelopes (the default); or build
 * 110's subtractive approximation, a band-limited waveform per carrier
 * through a filter. PS2_Q_VOICE=pm|subtractive picks one at create. */
typedef enum {
    Q_VOICE_PM = 0,
    Q_VOICE_SUBTRACTIVE
} QVoicing;

QBank *q_bank_create(int sample_rate);
void   q_bank_destroy(QBank *b);

/* (build 136b) The drums, music/drums.json and its WAVs (drums.h's
 * drums_load), for Q_DRUMS_SAMPLES (the synthesised drums need nothing
 * loaded). */
bool   q_bank_load_drums(QBank *b, const char *path);

/* (build 125) The game's pack for q_bank_load_drums (ps2_types.h's
 * Ps2Reader); NULL, the default, reads the disk. */
void   q_synth_set_reader(Ps2Reader reader);

/* Every voice silent, the shadow at the chip's reset values. */
void   q_bank_reset(QBank *b);

/* fm_write's shape: a register write to port 0 or 1. */
void   q_bank_write(QBank *b, int part, u8 reg, u8 value);

/* (111) The sequencer's hooks (smps.h's track_write): a write by the track
 * that made it. From the first one the bank voices tracks, not chip
 * channels, and `write` only keeps the chip's registers. */
void   q_bank_track_write(QBank *b, int track, u8 channel, int part, u8 reg,
                          u8 value, bool to_chip);

/* fm_dac_play's shape: a drum byte. */
void   q_bank_drum(QBank *b, u8 drum_byte);

/* fm_render's shape: ADDS `frames` stereo frames, left then right, scaled
 * by gain and by the bank's trim. */
void   q_bank_render(QBank *b, float *out, int frames, float gain);

/* (build 111) The voicing, and the trim that goes with it. */
void   q_bank_set_voicing(QBank *b, QVoicing voicing);

/* (111) The music at music_gain, the effects' voices at effects_gain. */
void   q_bank_render_split(QBank *b, float *out, int frames, float music,
                           float effects);

/* (111) The PM voicing's running rate: 1, 2 or 4 x the output rate
 * (PS2_Q_OVERSAMPLE; 4 unless it says otherwise). */
void   q_bank_set_oversample(QBank *b, int n);

/* (111) Parallel voices on or off (PS2_Q_PARALLEL=0: off). */
void   q_bank_set_parallel(QBank *b, bool on);

/* (build 112) The AUDIO page's VOICES, TAILS and PATCHES (synth.h's
 * SynthOption; PS2_Q_EFFECTS=take, PS2_Q_TAILS=ring, PS2_Q_PATCHES=0 at
 * create for the tools), and the patch file's instruments (a copy is kept). */
void   q_bank_set_option(QBank *b, int option, int value);
void   q_bank_set_patches(QBank *b, const SynthPatch *patches, int count);

/* The drum mode, and the trim that goes with it. */
void   q_bank_set_drum_mode(QBank *b, QDrumMode mode);

/* The bank's level against the chip's (kQTrim in q_synth.cpp, one a drum
 * mode, set by `q_demo --trim-pass`); the pass sets 1 to measure. */
void   q_bank_set_trim(QBank *b, float trim);
float  q_bank_trim(const QBank *b);

/* Diagnostics for q_demo --bench: retune the filters every n samples; FM
 * voices x samples rendered so far. */
void      q_bank_set_cutoff_every(QBank *b, int n);
long long q_bank_voice_samples(const QBank *b);

/* (111b) Writes still waiting for their slot (the chip's pace: one every
 * two native samples), as fm_pending_writes. */
int       q_bank_pending_writes(const QBank *b);

#ifdef __cplusplus
}
#endif

#endif /* PS2_Q_SYNTH_H */
