#ifndef PS2_SYNTH_H
#define PS2_SYNTH_H

/* (build 110) The synth backends: what turns the sequencer's chip writes
 * into sound.
 *
 * The sequencer (smps.c) runs on the game thread and knows nothing of
 * this: its register writes, drum bytes and PSG bytes cross to the audio
 * thread through audio.c's ring, and there audio_drain hands each FM write
 * and each drum byte to the backend in use, and audio_render asks it for
 * the FM bus. A backend is this small table of calls, chosen at start-up
 * and switchable between ticks (the AUDIO page on the Escape menu):
 *
 *   CHIP  Nuked-OPN2 (fm.c): the YM2612 cycle by cycle, the reference.
 *         The default, and the only entry in a build without Q.
 *   Q     (`make Q=1`) the same writes re-voiced with Cycfi's Q: a
 *         register shadow decoded into band-limited oscillators, filters
 *         and the chip's envelopes as times (src/audio/q/, C++20 behind
 *         extern "C"; docs/audio.md, "The Q backend").
 *
 * Every backend is created at start-up and loads its data then (nothing
 * reads assets/ after start-up); only the one in use is written and
 * rendered. Switching resets the new one and replays the register shadow
 * audio.c keeps into it, so a note held across the switch keeps sounding.
 *
 * (build 111) The sequencer's hooks (smps.h) give every write a second time,
 * by the track that made it; a backend takes them through track_write. They
 * are how effects get voices of their own on Q, and how both backends weight
 * the effects apart from the music (the AUDIO page's EFFECTS).
 *
 * All the calls but create are the audio thread's once the audio device
 * runs; create, load_drums and shutdown are called before it starts and
 * after it stops. */

#include "../core/ps2_types.h"

/* (build 112) The AUDIO page's switches a backend may have (set_option):
 *   VOICES   0 EXTRA: effects on voices of their own, the music plays on
 *            under them (111's); 1 LIMITED: an effect takes the music's
 *            channel as on the chip, the music's note there stops
 *   TAILS    0 FADE: a note struck again ends the last in 15 ms, as the
 *            chip's one note a channel (111b's); 1 RING: the last finishes
 *            its own release on a voice of its own (111a's)
 *   PATCHES  0 off; 1 the patch file's instruments (below) in place of
 *            the ROM's
 *   GRAIN    (build 116) 0 off; 1 the chip's output grain on Q's channels:
 *            each carrier cut to the 9-bit sum's step, and the YM2612 DAC's
 *            jump at zero (docs/audio.md, "Build 112", Step Up's chord)
 * Q has all four; the chip, the reference, none -- (build 124) but VOICES:
 * EXTRA puts the effects on a second chip (synth.c, docs/second_chip.md). */
typedef enum {
    SYNTH_OPT_VOICES = 0,
    SYNTH_OPT_TAILS,
    SYNTH_OPT_PATCHES,
    SYNTH_OPT_GRAIN,
    SYNTH_OPT_COUNT
} SynthOption;

/* (build 112) One instrument of the patch file (assets/music/q_patches.json,
 * src/audio/patches.c; docs/audio.md, "The patch file"), resolved against
 * the ROM's: its bank index, the ROM's upload and the patched one by chip
 * register (channel 1's addresses: $30 + 4n ... $B4), and what only a
 * re-voiced backend can do -- each operator's pitch moved in cents, the
 * instrument's level, and its tail apart from the TAILS row. */
#define SYNTH_TAIL_ROW  (-1)
#define SYNTH_TAIL_FADE 0
#define SYNTH_TAIL_RING 1

typedef struct {
    int   instrument;
    u8    rom[256];
    u8    reg[256];
    float fine_cents[4];     /* operators 1-4 */
    float volume_db;
    int   tail;              /* SYNTH_TAIL_ROW, _FADE or _RING */
} SynthPatch;

typedef struct {
    const char *name;    /* settings.json's word: "chip", "q" */
    const char *label;   /* the AUDIO page's: "CHIP", "Q" */

    /* A new instance at the output rate, or NULL when it cannot run (the
     * chip without Nuked-OPN2's files in src/audio/). */
    void *(*create)(int sample_rate);

    /* (build 136b) The drums, music/drums.json and its WAVs
     * (drums.h's drums_load; tools/extract_drums.py). */
    bool  (*load_drums)(void *self, const char *path);

    /* Silence and clear: every voice stopped, every register at its reset
     * value, nothing queued -- as the chip after a reset. */
    void  (*reset)(void *self);

    /* One register write, as the sequencer made it: part 0 for $22-$2B and
     * channels 1-3, part 1 for channels 4-6. */
    void  (*write)(void *self, int part, u8 reg, u8 value);

    /* One drum byte, as the Z80 took it from $1FFF: whatever was playing
     * stops. */
    void  (*drum)(void *self, u8 drum_byte);

    /* (build 111) The same writes again from the sequencer's hooks, by the
     * track that made them (smps.h's track_write: track 0-9 or
     * SMPS_TRACK_DRIVER, the track's own channel id, the chip's part and
     * register, SMPS_HOOK_CLAIM for
     * an effect taking a channel, and whether the chip got it). CHIP keeps
     * from them which channels an effect holds, for the EFFECTS volume; Q
     * voices each track on its own from them (takes_tracks). */
    void  (*track_write)(void *self, int track, u8 channel, int part, u8 reg,
                         u8 value, bool to_chip);

    /* ADDS `frames` stereo frames, interleaved left then right: the music
     * scaled by music_gain, the effects by effects_gain (build 111; before
     * it one gain for both). */
    void  (*render)(void *self, float *out, int frames, float music_gain,
                    float effects_gain);

    /* Frees the instance. */
    void  (*shutdown)(void *self);

    /* (build 111) Whether its voices come from track_write rather than
     * write (Q): a switch to it replays audio.c's shadow of each track as
     * well as the chip's. */
    bool  takes_tracks;

    /* (111a) The AUDIO page's OVERSAMPLE: the rate a backend that
     * oversamples runs its voices at, 1, 2 or 4 x the output rate (Q's PM
     * voicing); NULL where it means nothing (the chip). */
    void  (*set_oversample)(void *self, int factor);

    /* (build 112) The AUDIO page's VOICES, TAILS and PATCHES, (116) and
     * GRAIN (SynthOption); NULL where they mean nothing. (build 124) The
     * chip takes VOICES alone. */
    void  (*set_option)(void *self, int option, int value);

    /* (build 112) The patch file, read at start-up: the backend keeps its
     * own copy. NULL where it has none (the chip). */
    void  (*set_patches)(void *self, const SynthPatch *patches, int count);

    /* (build 184) The chord voices a mod's music can need (smps.h,
     * SmpsData.chord_music and chord_effects), read at start-up with the
     * music: the backend makes room to voice them -- the chip a chord chip
     * for every six (synth.c), Q a virtual channel and a voice each. 0 and
     * 0, the game's own music: none, and nothing else changes. Their writes
     * come through track_write as tracks SMPS_TRACKS + v. */
    void  (*set_chord_voices)(void *self, int music, int effects);
} SynthBackend;

typedef enum {
    SYNTH_CHIP = 0,
    SYNTH_Q,
    SYNTH_COUNT
} SynthId;

/* The backend, or NULL when this build has none by that id (Q without
 * `make Q=1`). */
const SynthBackend *synth_backend(SynthId id);

/* By settings.json's word; -1 for a word no backend has. */
int synth_from_name(const char *name);

/* (build 129b) The chip backend's writes still queued, A's and B's
 * together: for a tool that times from an empty queue (fm_probe). `chip`
 * is an instance of synth_backend(SYNTH_CHIP). */
int synth_chip_pending_writes(const void *chip);

/* (build 125) The game's pack for every backend's drums (the chip's
 * fm_set_reader, Q's q_synth_set_reader); NULL reads the disk. */
void synth_set_reader(Ps2Reader reader);

#endif /* PS2_SYNTH_H */
