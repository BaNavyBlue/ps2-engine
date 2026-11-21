#ifndef PS2_AUDIO_H
#define PS2_AUDIO_H

#include "psg.h"
#include "../audio/smps.h"
#include "../audio/synth.h"

#include <stdatomic.h>

/* Mixer with separate music and SFX buses, so the two can be gain-staged
 * independently. That matters once real voices exist: the original's mix
 * assumed at most six FM channels summing, and adding SFX voices and
 * release tails pushes the peak up enough to clip.
 *
 * The sequencer lives here too, but it runs on the game thread: it is
 * game logic, ticked once per field like everything else, and its
 * output is a stream of chip writes. Those cross to the audio thread
 * through the same ring the placeholder beeps use. */

#define AUDIO_RATE     48000
#define AUDIO_MAX_LEN  4096

/* The mix is stereo, interleaved left then right: the YM2612 pans each
 * channel with $B4, and the SN76489, which is mono, sits in the middle
 * of both sides as on the Mega Drive. */
#define AUDIO_CHANNELS 2

/* The game thread and SDL's audio thread touch this struct at the same
 * time, so register writes cross as commands through a single-producer,
 * single-consumer ring rather than being applied in place. Without this
 * the beeps are a data race: they mostly work and occasionally glitch or
 * tear a period value.
 *
 * Starting a song is a burst: SilenceAll, then 26 registers per
 * instrument for every channel, then key-ons -- around 200 writes in one
 * tick, and a catch-up frame can issue several ticks back to back. */
#define AUDIO_QUEUE 4096

typedef enum {
    CMD_PSG_TONE = 0,    /* channel, period, atten */
    CMD_PSG_NOISE,       /* period (as ctrl), atten */
    CMD_PSG_RAW,         /* value: a byte in the chip's latch/data format */
    CMD_FM,              /* part, reg, value */
    CMD_DAC,             /* value: the drum byte, no player yet */
    CMD_SYNTH,           /* (build 110) value: the backend to switch to */
    CMD_TRACK,           /* (build 111) the hooks: a write by its track --
                          * channel the port, reg, value, period the track
                          * + 1 (0 the driver) in its low byte and the
                          * track's own channel id in its high, atten
                          * whether the chip got it */
    CMD_OVERSAMPLE,      /* (111a) value: 1, 2 or 4 */
    CMD_SYNTH_OPTION,    /* (build 112) reg: a SynthOption, value its value */
    CMD_RESYNC           /* (build 120) the chips to Audio.handoff */
} AudioCmdKind;

typedef struct {
    u8  kind;
    u8  channel;     /* PSG: 0-2 tone, 3 noise. FM: the port, 0 or 1 */
    u8  reg;
    u8  value;
    u16 period;
    u8  atten;
} AudioCmd;

/* (build 110-111, gathered in build 120) What the chips have been told:
 * every FM register as last written, both ports, and each channel's key
 * bits ($28's top four); the same by track, from the hooks -- each track's
 * registers as last written (the chip's part and register), which of them
 * it has written, its key-on as last written (0 released), the channel an
 * effect track last claimed (+1, 0 none), its own channel id, its
 * instrument (+1, 0 none yet) and whether its last write reached the chip;
 * and (build 120) the PSG's registers, with the one its latch points at.
 * What a backend switched to is given, so a held note keeps sounding --
 * and what the chips are set back to when FAST's held writes end.
 * (build 184) And each chord voice's (smps.h), kept small: the registers of
 * its one channel on its chord chip -- the operators' $30-$9C, $B0, $B4, the
 * pitch's $A4 and $A0, at AUDIO_VOICE_REG's places -- which of them it has
 * written, its channel id, its key and its instrument (+1, 0 none yet). */
#define AUDIO_VOICE_REGS 32

typedef struct {
    u8   reg[AUDIO_VOICE_REGS];
    u32  set;                   /* a bit a place in reg */
    u8   chan;
    u8   key;
    u8   ins;
} AudioVoiceShadow;

typedef struct {
    u8   shadow[2][256];
    u8   keyed[6];
    u8   track_reg[SMPS_TRACKS][2][256];
    u8   track_set[SMPS_TRACKS][2][32];     /* a bit a register */
    u8   track_key[SMPS_TRACKS];
    u8   track_claim[SMPS_TRACKS];
    u8   track_chan[SMPS_TRACKS];
    u8   track_ins[SMPS_TRACKS];
    bool track_to_chip[SMPS_TRACKS];
    AudioVoiceShadow voice[SMPS_CHORD_VOICES_MAX];    /* (build 184) */
    u16  psg_tone[3];       /* (build 120) not kept in `live`: the Psg is */
    u8   psg_atten[4];
    u8   psg_noise;
    u8   psg_latched;
} AudioShadow;

typedef struct {
    Psg   psg;

    /* (build 110) The synth backends (src/audio/synth.h): each one this
     * build has, created at start-up (synth_inst[id], NULL where it could
     * not be); `synth` is the one written and rendered, `synth_id` its id.
     * Before build 110 this was an Fm, fm.c called directly. */
    void               *synth_inst[SYNTH_COUNT];
    const SynthBackend *synth;
    int                 synth_id;
    bool                synth_ready;   /* false without Nuked-OPN2 */

    /* (build 110-111) Audio thread: what it has played (AudioShadow) --
     * what a switch replays into a backend, by channel and, for a backend
     * that voices tracks (Q), by track; the chip's marks of the effects'
     * channels. */
    AudioShadow         live;
    int                 synth_want;    /* game thread: the one asked for */

    /* (build 120) Game thread: everything the sequencer has written, queued
     * or not (mirror). While muted the sequencer's writes are held, not
     * queued -- FAST's millions would only fill the queue and be dropped,
     * leaving the chips out of step with the music -- and when the sound
     * comes back the mirror goes over in one CMD_RESYNC (handoff, the audio
     * thread's until it clears resync_busy), the chips set to it the way a
     * switch sets a backend, before any write after it. */
    AudioShadow         mirror;
    AudioShadow         handoff;
    bool                holding;
    bool                resync_pending;
    atomic_bool         resync_busy;

    /* (build 124) Audio thread: VOICES as last set (1 LIMITED, the chip's
     * own until told): a change with the chip playing replays it. */
    int                 voices_now;

    /* (build 110) The AUDIO page's MASTER and MUSIC in whole dB, 0 the
     * default, AUDIO_DB_OFF silent; read by the audio thread at each
     * render. */
    atomic_int          master_db;
    atomic_int          music_db;
    atomic_int          effects_db;    /* (build 111) the effects' */

    /* (build 110) Game thread: every write the sequencer sent through the
     * sink since audio_init, counted and hashed (FNV-1a over kind, port,
     * register, value) -- the route probe's `expect sound-writes`, which
     * shows a switch of backend leaves the sequencer alone. */
    u32                 write_count;
    u32                 write_hash;

    /* (build 160) Game thread: every id the game has asked for since
     * audio_take_asked last took them, a bit an id -- whether or not the
     * sequencer could play it (no music data, UpdateSoundQueue's latch).
     * The rumble's word for what the game just did (src/game/rumble.h);
     * nothing in the sound reads it. */
    u32                 asked[8];

    /* (build 112) The patch file (src/audio/patches.c), read by
     * audio_load_music and handed to every backend that takes it. */
    SynthPatch *patches;
    int         patch_count;

    Smps     smps;
    SmpsData smps_data;
    bool     smps_ready;

    float music_gain;
    float effects_gain;   /* (build 111) the FM channels an effect holds */
    float sfx_gain;
    float master_gain;

    /* Audio thread: the PSG rendered mono, before it is put on both
     * sides. */
    float psg_mono[AUDIO_MAX_LEN];

    AudioCmd        queue[AUDIO_QUEUE];
    atomic_uint     head;   /* written by the game thread */
    atomic_uint     tail;   /* written by the audio thread */
    atomic_bool     muted;  /* (build 100b) rendered, then silenced */
} Audio;

void audio_init(Audio *a, int sample_rate);

/* Builds the sound driver's image from `asset_dir`/music's song text and
 * note frequencies (build 137a; SoundData.bin until then) and brings
 * up the FM core. Returns false if the music data is missing; a missing
 * FM core is reported but is not a failure, the music just has nothing
 * to sound through. */
bool audio_load_music(Audio *a, const char *asset_dir);
void audio_shutdown(Audio *a);

/* Fills `out` with `frames` stereo frames -- AUDIO_CHANNELS floats each,
 * left then right -- in -1..1. Audio thread. */
void audio_render(Audio *a, float *out, int frames);

/* (build 100b) Silence the output: COMPUTER PLAYS's fast speed. (build 120)
 * Game thread. The sequencer runs on; its writes are held rather than
 * queued (the queue could not take them), and when the sound comes back
 * the chips are set to where the music is (Audio.mirror). */
void audio_set_muted(Audio *a, bool muted);

/* (build 110) Game thread: switch the synth backend (synth.h) between
 * ticks. The audio thread resets the one asked for and replays the register
 * shadow into it -- every register as last written, then a key-on for each
 * channel keyed -- so a note held across the switch keeps sounding. Asking
 * for the one playing resets and replays it all the same. A backend this
 * build has not got, or could not create, falls back to the chip. Returns
 * the one that will play. */
int  audio_set_synth(Audio *a, int id);

/* Whether this build has the backend and could create it. */
bool audio_synth_available(const Audio *a, int id);

/* (build 110) The AUDIO page's volumes, in whole dB on the mix's own gains
 * (mix.h); AUDIO_DB_OFF is silence. Any thread. (build 111) EFFECTS: the
 * sound effects apart from the music -- on the chip the channels an effect
 * holds, on Q the effects' own voices; MUSIC is then the music alone. */
#define AUDIO_DB_OFF (-100)
void audio_set_volume(Audio *a, int master_db, int music_db, int effects_db);

/* (build 111a) The AUDIO page's OVERSAMPLE: every backend that oversamples
 * (Q's PM voicing) runs its voices at 1, 2 or 4 x the output rate, from the
 * next render; the chip has no such thing. Game thread. */
void audio_set_oversample(Audio *a, int factor);

/* (build 112) The AUDIO page's VOICES, TAILS and PATCHES (synth.h's
 * SynthOption) to every backend that has them, playing or not, from the next
 * render. Game thread. */
void audio_set_synth_option(Audio *a, int option, int value);

/* (build 110) The sequencer's writes so far: their count and hash (above). */
u32  audio_write_hash(const Audio *a, u32 *count);
/* (build 183) Game thread: a YM2612 register as the sequencer last wrote
 * it -- `part` 0 (FM1-FM3 and the chip's own registers) or 1 (FM4-FM6),
 * from the mirror above, held or queued, rendered or not; 0 before any
 * write to it. The route probe's `expect sound-register`. */
u8   audio_register_written(const Audio *a, int part, u8 reg);
/* (build 129d, restored in 132a) smps_stuck_tracks of the game's sequencer;
 * 0 before it is loaded. */
u16  audio_stuck_tracks(const Audio *a);

/* Game thread, once per tick: SoundDriverInput. */
void audio_music_tick(Audio *a);

/* Game thread: UpdateSoundQueue. $81-$9F music, $A0-$D7 SFX, $E0 fade
 * out, $E1 stop SFX. The original keeps the last id queued and skips a
 * repeat, so this does too. (build 139f) True when it queued the id (it
 * was not the latch's). */
bool audio_play_sound(Audio *a, u8 id);

/* (build 139f) Game thread: the sequencer takes its queue now
 * (smps_take_queue) -- a vertical blank in the middle of a load the port
 * does in one tick. */
void audio_take_queue(Audio *a);

/* (build 139f) Game thread: the music the sequencer last started, $81-$9F
 * (0 before any): what plays, not what was asked for. */
u8 audio_music_started(const Audio *a);

/* Game thread: `move.b #id, (sound_queue).w`. Sound effects are written
 * straight to the queue and never touch UpdateSoundQueue's latch, so the
 * same effect can play twice running. */
void audio_play_sfx(Audio *a, u8 id);

/* Game thread: `move.b #id, ($FFFFF640).w` -- the latch written directly,
 * with nothing queued. loc_7E02 does this around the Neifirst scene so the
 * level's own music matches the latch on the next load and Power, queued
 * as an effect, plays on. */
void audio_set_last_sound(Audio *a, u8 id);

/* (build 160) Game thread: the ids asked for (audio_play_sound,
 * audio_play_sfx) since the last call, bit id & 31 of word id >> 5, into
 * `out`, and the record cleared. */
void audio_take_asked(Audio *a, u32 out[8]);

/* Diagnostics: called with every id that reaches the sequencer (after
 * UpdateSoundQueue's latch, for audio_play_sound), on the calling thread.
 * route_probe logs the music by it. NULL turns it off. */
void audio_debug_on_sound(void (*fn)(void *ctx, u8 id), void *ctx);

/* Game thread: `move.b #0, (paused_mode).w`. The pause sound stops the
 * sequencer with cfE8, and a stopped sequencer never reads its queue, so
 * unpausing has to clear the flag itself -- CheckGamePause does. */
void audio_music_resume(Audio *a);

/* The sequencer's track_timer, as the game loop reads it in the few
 * places it waits a sound out in place (`cmpi.b #$80, (track_timer).w /
 * bne.s -`): play_sfx clears it and a track's end (cfF2) sets $80. True
 * once it is $80 -- and always without the music data, when there is no
 * sequencer to wait on. */
bool audio_track_ended(const Audio *a);

/* `move.b #0, (track_timer).w`, from the game thread. */
void audio_track_timer_clear(Audio *a);

/* Called from the game thread. Queues a register change; the audio
 * thread applies it at the start of its next render. */
void audio_beep(Audio *a, double hz, u8 atten);
void audio_silence_beep(Audio *a);

#endif /* PS2_AUDIO_H */
