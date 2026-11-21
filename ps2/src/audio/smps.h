#ifndef PS2_SMPS_H
#define PS2_SMPS_H

#include "../core/ps2_types.h"

/* SMPS68k: the Phantasy Star II sound driver (ps2.asm:89234).
 *
 * It is 68000 code running on the main CPU, writing the YM2612 and the
 * PSG directly. The Z80 does DAC sample playback and nothing else, so
 * nothing here needs a Z80.
 *
 * There are no PSG tracks. `UpdateTrack` tests the channel byte and
 * dispatches to exactly two places -- DAC when it is $80, FM otherwise
 * -- and every track header in the ROM names $80 or 0-6. `cfF3` and
 * `cfF5` are the PSG coordination flags SMPS defines and PS2 kept, but
 * `cfF5` only stores a byte nothing reads, and `cfF3` pokes the PSG
 * noise register without a track behind it. So the six music voices are
 * one DAC and five FM, and `SilencePSG` exists to quiet a chip the
 * music never uses.
 *
 * All of the driver's data is addressed as 16-bit offsets from
 * `SoundDriverBase`, so SmpsData is one flat image with those offsets
 * intact rather than a set of relocated per-song blobs. */

#define SMPS_TRACK_SIZE    0x30
#define SMPS_MUSIC_TRACKS  6
#define SMPS_SFX_TRACKS    4    /* three, plus the "special" one after them */
#define SMPS_TRACKS        (SMPS_MUSIC_TRACKS + SMPS_SFX_TRACKS)

/* Track RAM is one contiguous block at $FFD030 in the original, and the
 * driver relies on that: `PlaySFX` reaches its SFX track as
 * `bgm_track + $90`, `RestoreBGMChn` reaches back with `suba #$90`, and
 * `cfF8_GoSub` on a track whose stack byte was never initialised indexes
 * $FC bytes past the block it was handed. Keeping one array means those
 * land where they landed on hardware instead of being undefined. */
#define SMPS_TRACK_RAM     (SMPS_TRACKS * SMPS_TRACK_SIZE)

/* (build 184) Chords past the chip. A chord (cfE5) of N notes plays its
 * lead on its own track and N-1 parts on other track blocks. The ROM's
 * driver takes the blocks after the lead's, whatever is there: the game's
 * chords, three notes on FM3, take FM4's and FM5's tracks. A mod's longer
 * chord ran on into the effects' tracks and past the ten, out of the
 * driver's RAM. The port gives a part, in order: a music track after the
 * lead that the song's header leaves out and no other chord has taken
 * (an effect's chord has none), FM1-FM5 by its place -- FM4 and FM5 for
 * the game's chords, as before -- then a chord voice: a track block of the
 * port's own, numbered from SMPS_TRACKS, on a channel of a chip of its own
 * (a chord chip: synth.c, Q's voices; the Mega Drive's one chip never
 * gets its writes, the hooks alone carry them). A lead keeps the parts it
 * took until its song starts again (an effect's track, until PlaySFX
 * clears it). A chord is at most SMPS_CHORD_MAX notes (the song text's
 * limit, a tracker cell's notes); smps_load counts the voices the music
 * can need at once (SmpsData.chord_music, chord_effects), and a part
 * past them, or past SMPS_CHORD_MAX, is left out -- its note bytes still
 * read. docs/audio.md, "Build 184". */
#define SMPS_CHORD_MAX        16
#define SMPS_CHORD_PARTS      (SMPS_CHORD_MAX - 1)
#define SMPS_CHORD_VOICES_MAX (SMPS_TRACKS * SMPS_CHORD_PARTS)
#define SMPS_CHORD_NONE       0xFF

/* The per-channel block, reconstructed from every site that touches it.
 * Word fields are kept as high/low byte pairs because the 68k reads them
 * big-endian and because two of them are aliased by other uses: the
 * `cfF7` loop counters start at $22 (counter N at $22+N) and the `cfF8`
 * return stack grows down from $30, four bytes a gosub -- $2C, $28, $24 --
 * so the first gosub's return address lies over counters 10-13, a second's
 * over 6-9 and a third's over 2-5. (build 139b) Until 139b this said a
 * third nested gosub wrote over counter 1. */
typedef union {
    u8 raw[SMPS_TRACK_SIZE];
    struct {
        u8 flags;            /* 00  see SMPS_F_* below */
        u8 channel;          /* 01  $80 DAC, 0-2 FM1-3, 4-6 FM4-6 */
        u8 tick_multiplier;  /* 02  duration bytes are multiplied by this */
        u8 ptr_lo;           /* 03  stream position, offset from the base */
        u8 ptr_hi;           /* 04 */
        u8 transpose;        /* 05  signed, added to the note index */
        u8 mod_envelope;     /* 06  1-4, or 0 for none */
        u8 instrument;       /* 07 */
        u8 volume;           /* 08  attenuation added to carrier TLs */
        u8 stack;            /* 09  gosub stack pointer, starts at $30 */
        u8 timeout_hi;       /* 0A  ticks until the next note */
        u8 timeout_lo;       /* 0B */
        u8 freq_hi;          /* 0C  FM frequency; on a DAC track, the drum */
        u8 freq_lo;          /* 0D */
        u8 length_hi;        /* 0E  duration x tick multiplier */
        u8 length_lo;        /* 0F */
        u8 slide_speed;      /* 10  signed, per tick */
        u8 mod_env_index;    /* 11 */
        u8 pad12;            /* 12 */
        u8 mod_env_mult;     /* 13 */
        u8 pad14[2];         /* 14 */
        u8 mod_env_state;    /* 16  $80 once the envelope has stopped */
        u8 pan_ams_fms;      /* 17  the value of register $B4 */
        u8 pan_anim_type;    /* 18  0-6, indexes two jump tables */
        u8 pan_anim_reset;   /* 19 */
        u8 pan_anim_timeout; /* 1A */
        u8 pan_anim_index;   /* 1B */
        u8 tl_op1;           /* 1C  register $40 */
        u8 pad1D;            /* 1D */
        u8 tl_op3;           /* 1E  register $44 */
        u8 tl_op4;           /* 1F  register $4C */
        u8 algorithm;        /* 20  register $B0; decides which ops are
                              *     carriers, so which TLs a volume
                              *     change has to touch */
        u8 chord_count;      /* 21  0 off, else this many consecutive
                              *     tracks play the same part */
        u8 loop_count[0xE];  /* 22  cfF7 counters; cfF8 stack overlaps */
    } f;
} SmpsTrack;

/* flags, byte 0 */
#define SMPS_F_FM3_SPECIAL 0x01
#define SMPS_F_HOLD        0x02   /* set by cfE6/cfEE, and while at rest */
#define SMPS_F_OVERRIDDEN  0x04   /* an SFX has taken this channel */
#define SMPS_F_RAW_FREQ    0x08
#define SMPS_F_PITCH_SLIDE 0x20
#define SMPS_F_IN_USE      0x80

/* The extracted image: "PS2S" + eight u32 + the bytes. Offset 0 is
 * SoundDriverBase, so every pointer in the data is an index into it. */
typedef struct {
    u8  *image;
    u32  size;
    u32  spc_sfx_ptrs;
    int  music_count;
    int  sfx_count;
    int  spc_count;      /* (build 184) the special effects, $D8 on */

    u16 *freqs;          /* FMFreqs, the equal-tempered table */
    int  freq_count;

    /* (build 184) The chord voices (above) the music and the effects can
     * need at once, from smps_load's walk of every stream as the sequencer
     * reads it: the most any one song needs, past the music tracks its
     * header leaves out; the effects', each effect track's most, summed.
     * The music's are voices 0 to chord_music - 1, the effects' after
     * them. 0 and 0 for the game's own music. */
    int  chord_music;
    int  chord_effects;
} SmpsData;

/* Everything the driver does to the outside world goes through here:
 * two YM2612 ports, one byte to the Z80's DAC, one PSG register. A
 * trace sink prints them; the real sink hands them to Nuked-OPN2. */
typedef struct {
    void (*fm_write)(void *ctx, int part, u8 reg, u8 value);
    void (*dac_write)(void *ctx, u8 drum);
    void (*psg_write)(void *ctx, u8 value);
    void *ctx;

    /* (build 111) The hooks: every YM2612 write again, by the track that
     * made it, for a backend that voices each track on its own (the Q
     * backend's effects on voices of their own, the EFFECTS volume;
     * docs/audio.md, "The hooks"). Called just before the chip write, and
     * also for the writes the chip never gets -- a music track runs on
     * silently while an effect holds its channel, and its writes come here
     * with to_chip false. `track` is 0-9, or SMPS_TRACK_DRIVER for the
     * driver's own (SilenceFM, the DAC switch), or (build 184) SMPS_TRACKS
     * + v for chord voice v, whose writes come here alone -- part and reg
     * on its chord chip, to_chip true; `channel` the track's own
     * channel id ($28's: 0-2, 4-6; $80 the drums; $FF the driver's) -- a
     * write can land on another channel, as the pan flag's always lands on
     * FM1; part and reg are the chip's (the channel's offset added,
     * channels 4-6 on part 1). A reg of
     * SMPS_HOOK_CLAIM is no chip register: the track (an effect starting)
     * takes the channel whose $28-style id is `value`. NULL: never called,
     * and the chip's writes are the same either way. */
    void (*track_write)(void *ctx, int track, u8 channel, int part, u8 reg,
                        u8 value, bool to_chip);
} SmpsSink;

#define SMPS_TRACK_DRIVER  (-1)
#define SMPS_HOOK_CLAIM    0x00
/* (build 112) No chip register either: the track is about to upload the
 * instrument whose bank index (cfEF's byte, FMInsPtrs) is `value` -- SetFMIns's
 * 26 writes follow on the same track. RestoreBGMChn's upload says so too. A
 * backend that voices instruments by number (Q's patch file) takes it; the
 * chip ignores it. */
#define SMPS_HOOK_INSTRUMENT 0x01
/* (build 143) No chip register: RestoreBGMChn's upload, bracketed -- `value`
 * 1 before it, 0 after, on the music track taking its channel back from an
 * effect. On the one chip that upload lands on a channel the effect has
 * been sounding, so its instrument levels before the track's volume
 * (SetFMIns, then RefreshVolume) go unheard. A backend whose music track
 * kept a voice of its own under the effect (VOICES EXTRA) already holds
 * that instrument there, and the upload's few writes at the instrument's
 * own levels, up to 27 dB over the track's, are a click on a sounding
 * note: such a backend leaves that voice alone in between (the effect's
 * releasing note still takes the upload, as the one chip's does). */
#define SMPS_HOOK_RESTORE  0x02
/* (build 112) The bank's size: 105 instruments (tools/extract_tables.py,
 * build 111: every one equal to the sequencer's; (build 136d) now
 * music/fm_instruments.json, tools/extract_music.py). */
#define SMPS_INSTRUMENTS   105

typedef struct {
    SmpsData data;
    SmpsSink sink;

    /* The driver's RAM, $FFD000 upward. */
    u8  coord_flag_e0;
    u8  tempo_timeout;
    u8  init_tempo_value;
    u8  play_sound_id;
    u8  sound_queue[4];
    u8  current_sfx_priority;
    u8  track_timer;
    u16 special_fm3_notes[4];
    u8  paused_mode;
    /* (build 139f) The port's own, a diagnostic, no ROM byte: the music
     * PlaySoundID last started ($81-$9F; 0 before any) -- what is playing,
     * where audio_play_sound's latch says only what was asked for. */
    u8  music_started;
    /* (build 161) The port's own, diagnostics, no ROM byte: the effect
     * PlaySoundID last started ($A0-$D7; 0 before any) and how many have
     * started (wrapping) -- the rumble's word for when an asked effect
     * began, at the driver's next frame, if at all (the priorities, the
     * queue's one slot), and so when the one before it was cut: PlaySFX
     * stops every effect track first. Nothing in the driver reads them. */
    u8  sfx_started;
    u8  sfx_starts;
    /* (build 132c) The port's own, a diagnostic, no ROM byte: the tracks
     * the pause's walk turned down (cfE8_MusPause, d3 = $FF - volume) that
     * nothing has turned back up -- the unpause's walk, an instrument
     * upload, a volume change, each at the track's own volume, while it
     * holds its channel. A bit a track. See smps_stuck_tracks. */
    u16 quiet;
    u8  communication_byte;
    u8  fade_out_remain;
    u8  fade_timeout;
    u8  pal_divider;          /* $FFD01C, the PAL catch-up counter */
    u8  pal_tick_mult;        /* $FFD01D */
    u8  music_or_sfx;

    bool pal;                 /* the region bit at $FFFFF8 */

    SmpsTrack tracks[SMPS_TRACKS];

    /* $FFD210-$FFD30F: RAM after the tracks that nothing in the ROM
     * names. It is not unused, quite: PlaySFX clears a track and copies
     * nine header bytes, so an SFX track's gosub stack pointer (+$09)
     * is 0, and cfF8 then keeps its return address $FC past the track --
     * beyond the block for the SFX tracks. The pause jingle gosubs, and
     * its return has to come back from here. */
    u8 ram_after_tracks[0x100];

    /* (build 184) The port's own, no ROM bytes: chords' parts (above).
     * chord_part[lead][p] is the track of the lead's part p + 1 -- a track
     * 0-9, a chord voice's block SMPS_TRACKS + v, or SMPS_CHORD_NONE when
     * there was none to give; chord_parts[lead] how many it has taken. */
    u8  music_tracks;         /* the song's header's tracks, at most six */
    u8  chord_part[SMPS_TRACKS][SMPS_CHORD_PARTS];
    u8  chord_parts[SMPS_TRACKS];
    bool part_taken[SMPS_MUSIC_TRACKS];
    bool voice_taken[SMPS_CHORD_VOICES_MAX];
    SmpsTrack voices[SMPS_CHORD_VOICES_MAX];
} Smps;

/* (build 184) Where chord voice v sounds, given the music's count
 * (SmpsData.chord_music): six voices a chord chip, the music's on chips 0
 * up, the effects' on the chips after them -- so a chip is the music's or
 * the effects' alone, and takes one volume. smps_voice_channel is the
 * voice's channel id there, 0-2 then 4-6 as $28 names the six;
 * smps_voice_chip its chip; smps_voice_chips how many the two counts need.
 * Inline: the synths (Q's C++ among them) read them without the driver. */
#define SMPS_VOICES_PER_CHIP 6

static inline int smps_voice_slot(int music, int voice)
{
    return (voice < music) ? voice : voice - music;
}

static inline u8 smps_voice_channel(int music, int voice)
{
    int k = smps_voice_slot(music, voice) % SMPS_VOICES_PER_CHIP;
    return (u8)((k < 3) ? k : k + 1);
}

static inline int smps_voice_chips(int music, int effects)
{
    return (music + SMPS_VOICES_PER_CHIP - 1) / SMPS_VOICES_PER_CHIP
         + (effects + SMPS_VOICES_PER_CHIP - 1) / SMPS_VOICES_PER_CHIP;
}

static inline int smps_voice_chip(int music, int voice)
{
    int chip = smps_voice_slot(music, voice) / SMPS_VOICES_PER_CHIP;
    return (voice < music) ? chip : smps_voice_chips(music, 0) + chip;
}

/* (build 136c) GetFMFreq reads entries 0-63 of the frequency table; the
 * ROM's has 85. smps_load reads it from music/note_frequencies.json and
 * refuses fewer than SMPS_FREQS_MIN or more than SMPS_FREQS_MAX entries.
 * (build 137a) It takes the music directory (assets/music): the frequencies
 * from note_frequencies.json, then the image built from the song text
 * (songtext.h) -- SoundData.bin until then. */
#define SMPS_FREQS_MIN  64
#define SMPS_FREQS_MAX  128

bool smps_load(SmpsData *d, const char *music_dir);

/* (build 125) The game's pack for smps_load (ps2_types.h's Ps2Reader);
 * NULL, the default, reads the disk. */
void smps_set_reader(Ps2Reader reader);
void smps_free(SmpsData *d);

void smps_init(Smps *s, const SmpsData *data, const SmpsSink *sink);

/* Queues a sound id: $81-$9F music, $A0-$D7 SFX, $D8-$DF special SFX,
 * $E0 fade out, $E1 stop SFX, anything else stops everything. */
void smps_play(Smps *s, u8 sound_id);

/* (build 139f) The queue taken now, as a field's DoSoundQueue and
 * PlaySoundID take it -- the sound started -- with no tempo, fade or track
 * step: what a vertical blank in the middle of a long load does to a sound
 * queued before it (game.c, the level's music). Nothing while paused. */
void smps_take_queue(Smps *s);

/* One field. Call at the tick rate; everything else is driven from it. */
void smps_tick(Smps *s);

/* Where a song's track headers start, for tools that want to look. */
u32 smps_music_offset(const Smps *s, int index);

/* (build 129d; lost from the tree at 130, restored in 132a) The music tracks
 * lent to an effect (SMPS_F_OVERRIDDEN) while no effect track that lends
 * them is in use, a bit a track: a part the driver has left silent on the
 * one chip until the next song reloads the tracks -- the shape of the lost
 * music part the owner hears on the Mega Drive. PlaySFX's effect track $90 past
 * a music track lends it (tracks 6-8 lend 3-5); PlaySpcSFX's special track
 * lends 3. 0: none stuck. */
u16 smps_stuck_tracks(const Smps *s);

/* (build 161) The tracks effect `id` ($A0-$D7) takes when PlaySFX plays
 * it, a bit a track (6-9): its header walked as PlaySFX walks it, nothing
 * written. 0 for any other id. For the rumble. */
u16 smps_sfx_tracks(const Smps *s, u8 id);
/* (build 132c) The high byte: music tracks a pause's walk turned down that
 * nothing has turned back up (Smps.quiet), the driver running again. */

/* (build 112) Instrument `index`'s 26 bytes as SetFMIns uploads them (its
 * register order is smps_ins_regs), read from the sequencer's image through
 * FMInsPtrs; false for an index outside the bank. */
extern const u8 smps_ins_regs[26];
bool smps_instrument(const SmpsData *d, int index, u8 out[26]);

#endif /* PS2_SMPS_H */
