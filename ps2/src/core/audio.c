#include "audio.h"
#include "../audio/drums.h"
#include "../audio/mix.h"
#include "../audio/patches.h"
#include "../audio/songtext.h"
#include "assetfs.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static u8 g_last_sound_id;   /* $FFFFF640, UpdateSoundQueue's latch */
static void (*g_debug_sound_fn)(void *ctx, u8 id);
static void *g_debug_sound_ctx;

void audio_debug_on_sound(void (*fn)(void *ctx, u8 id), void *ctx)
{
    g_debug_sound_fn  = fn;
    g_debug_sound_ctx = ctx;
}

/* PS2_SNDLOG=1 prints every chip write the sequencer emits on stderr,
 * with the tick it was emitted on -- the same lines tools/pause_probe
 * prints, but from the real build with the real pad. */
static long g_tick;
static bool g_sndlog;

/* (build 110) FNV-1a, a byte at a time, for the sequencer's writes. */
static void write_hash(Audio *a, u8 kind, u8 port, u8 reg, u8 value)
{
    const u8 bytes[4] = { kind, port, reg, value };
    u32 h = a->write_hash;

    for (int i = 0; i < 4; i++) {
        h ^= bytes[i];
        h *= 16777619u;
    }
    a->write_hash = h;
    a->write_count++;
}

static bool audio_push(Audio *a, const AudioCmd *cmd)
{
    unsigned head = atomic_load_explicit(&a->head, memory_order_relaxed);
    unsigned tail = atomic_load_explicit(&a->tail, memory_order_acquire);

    if (head - tail >= AUDIO_QUEUE) {
        return false;   /* full: drop rather than stall the game thread */
    }

    a->queue[head % AUDIO_QUEUE] = *cmd;
    atomic_store_explicit(&a->head, head + 1, memory_order_release);
    return true;
}

/* (build 110-111; build 120 for any AudioShadow) A chip write into a
 * shadow: the register, and a key-on's or key-off's bits. */
static void shadow_fm(AudioShadow *s, int part, u8 reg, u8 value)
{
    int p = part ? 1 : 0;

    s->shadow[p][reg] = value;
    if (p == 0 && reg == 0x28) {
        int id = value & 7;
        if (id != 3 && id != 7) {
            s->keyed[(id < 3) ? id : id - 1] = (u8)(value & 0xF0);
        }
    }
}

/* (build 184) A chord voice's register's place in its record
 * (AudioVoiceShadow), -1 for one it does not keep. */
static int voice_reg_place(u8 reg)
{
    if (reg >= 0x30 && reg < 0xA0) {
        return (reg - 0x30) >> 2;               /* 0-27, the operators' */
    }
    switch (reg & ~3) {
    case 0xB0:
        return 28;
    case 0xB4:
        return 29;
    case 0xA4:
        return 30;
    case 0xA0:
        return 31;
    default:
        return -1;
    }
}

/* The register at a place, on a voice whose channel id is `chan`. */
static u8 voice_place_reg(int place, u8 chan)
{
    static const u8 tail[4] = { 0xB0, 0xB4, 0xA4, 0xA0 };
    u8 base = (place < 28) ? (u8)(0x30 + 4 * place) : tail[place - 28];
    return (u8)(base + (chan & 3));
}

/* (build 184) A chord voice's write into its record. Its keys are its own:
 * no track and no other voice plays on its chord chip's channel. */
static void shadow_voice(AudioShadow *s, int voice, u8 channel, int part,
                         u8 reg, u8 value)
{
    if (voice < 0 || voice >= SMPS_CHORD_VOICES_MAX) {
        return;
    }
    AudioVoiceShadow *v = &s->voice[voice];
    v->chan = channel;
    if (reg == SMPS_HOOK_INSTRUMENT) {
        v->ins = (u8)(value + 1);
    } else if (part == 0 && reg == 0x28) {
        v->key = value;
    } else {
        int place = voice_reg_place(reg);
        if (place >= 0) {
            v->reg[place] = value;
            v->set |= 1u << place;
        }
    }
}

/* ... and a track's write, from the hooks. */
static void shadow_track(AudioShadow *s, int track, u8 channel, int part,
                         u8 reg, u8 value, bool to_chip)
{
    int p = part ? 1 : 0;

    if (track >= SMPS_TRACKS) {
        shadow_voice(s, track - SMPS_TRACKS, channel, part, reg, value);
        return;
    }

    /* (build 169) A key write ends every other track's keys down on its
     * channel among the writes that reach the same chip -- the music's and
     * the driver's on the music's (VOICES EXTRA's A), the effects' and the
     * driver's on the effects' (B) -- as the chip's channel has one set of
     * keys. Up to 168 a record kept its keys down until its own track
     * wrote again, and a replay (the resync after FAST, a switch) struck
     * notes long ended: SilenceFM's key-offs at a new song are the
     * driver's, no track's, so the last song's held notes stayed down
     * (COMPUTER PLAYS back from FAST at the knock in Rolf's house: the
     * street's FM4 note rang on under the house's music, and a door
     * effect's on B); and the drum track's own key-off is $28 = $80
     * (cfF2_TrkEnd's FMNoteOff on the DAC channel, as the ROM writes it),
     * FM1's operator 4 keyed, which FM1's next key write ends on the chip
     * and never did here. LIMITED's replay keys from the chip's own
     * shadow (shadow_fm), right all along. */
    if (p == 0 && reg == 0x28 && (value & 3) != 3) {
        bool driver = track == SMPS_TRACK_DRIVER;
        bool effect = track >= SMPS_MUSIC_TRACKS;
        for (int u = 0; u < SMPS_TRACKS; u++) {
            bool same_chip = driver || (u >= SMPS_MUSIC_TRACKS) == effect;
            if (u != track && same_chip && (s->track_key[u] & 0xF0)
                && (s->track_key[u] & 7) == (value & 7)) {
                s->track_key[u] = (u8)(value & 7);
            }
        }
    }

    /* (build 143) RestoreBGMChn's bracket is no register, and a replay
     * after a switch starts nothing half-way: nothing to record */
    if (track < 0 || track >= SMPS_TRACKS || reg == SMPS_HOOK_RESTORE) {
        return;
    }
    /* (build 124a) A track on another channel than it was -- a new song
     * lays its tracks out afresh -- starts its record again. Kept, the
     * last song's registers under the track stayed set, and a switch's
     * replay put them back on the channel they had then: the crawl's
     * track 2 was the title theme's FM1, and its old levels went onto the
     * crawl's melody there, on the chip and on Q alike (The owner: "higher
     * pitched and louder, less flute like"). */
    if (s->track_chan[track] != channel) {
        memset(s->track_set[track], 0, sizeof s->track_set[track]);
        s->track_key[track]   = 0;
        s->track_claim[track] = 0;
        s->track_ins[track]   = 0;
    }
    s->track_chan[track] = channel;
    if (reg == SMPS_HOOK_CLAIM) {
        s->track_claim[track] = (u8)(value + 1);
    } else if (reg == SMPS_HOOK_INSTRUMENT) {
        s->track_ins[track] = (u8)(value + 1);
    } else if (p == 0 && reg == 0x28) {
        s->track_key[track] = value;
    } else {
        s->track_reg[track][p][reg] = value;
        s->track_set[track][p][reg >> 3] |= (u8)(1 << (reg & 7));
    }
    s->track_to_chip[track] = to_chip;
}

/* (build 120) A PSG byte into a shadow, by psg_write's latch/data rules
 * (psg.c), with the shadow's own latch. */
static void shadow_psg(AudioShadow *s, u8 value)
{
    if (value & 0x80) {
        s->psg_latched = (u8)((value >> 4) & 0x07);
    }
    int reg  = s->psg_latched;
    int chan = reg >> 1;
    if (reg & 1) {
        s->psg_atten[chan] = value & 0x0F;
    } else if (chan == 3) {
        s->psg_noise = value & 0x07;
    } else if (value & 0x80) {
        s->psg_tone[chan] = (u16)((s->psg_tone[chan] & 0x03F0)
                                  | (value & 0x0F));
    } else {
        s->psg_tone[chan] = (u16)((s->psg_tone[chan] & 0x000F)
                                  | ((value & 0x3F) << 4));
    }
}

/* (build 120) With the sound back after FAST, the mirror to the audio
 * thread in one command, once the queue has room and the audio thread has
 * let go of the last one. True when nothing is left pending: then the
 * sequencer's writes are queued again, after it. */
static bool resync_sent(Audio *a)
{
    if (!a->resync_pending) {
        return true;
    }
    if (atomic_load(&a->resync_busy)) {
        return false;
    }
    AudioCmd cmd = { CMD_RESYNC, 0, 0, 0, 0, 0 };
    memcpy(&a->handoff, &a->mirror, sizeof a->handoff);
    atomic_store(&a->resync_busy, true);
    if (!audio_push(a, &cmd)) {
        atomic_store(&a->resync_busy, false);
        return false;
    }
    a->resync_pending = false;
    return true;
}

/* (build 120) The sequencer's command to the queue -- unless its writes
 * are held (muted), or the resync after them is not queued yet. The mirror
 * has it either way. */
static void sink_push(Audio *a, const AudioCmd *cmd)
{
    if (a->holding || !resync_sent(a)) {
        return;
    }
    audio_push(a, cmd);
}

/* The sequencer's sink. Runs on the game thread inside smps_tick. */
static void sink_fm(void *ctx, int part, u8 reg, u8 value)
{
    AudioCmd cmd = { CMD_FM, (u8)part, reg, value, 0, 0 };
    Audio   *a   = ctx;

    write_hash(ctx, CMD_FM, (u8)part, reg, value);
    shadow_fm(&a->mirror, part, reg, value);
    if (g_sndlog) {
        fprintf(stderr, "%6ld  FM%c %02X <- %02X\n", g_tick,
                part == 0 ? 'I' : '2', reg, value);
    }
    sink_push(a, &cmd);
}

/* (build 111) The hooks (smps.h): the same writes by track. Not hashed --
 * `expect sound-writes` is the chip's writes, and a backend switch leaves
 * both alone. */
static void sink_track(void *ctx, int track, u8 channel, int part, u8 reg,
                       u8 value, bool to_chip)
{
    AudioCmd cmd = { CMD_TRACK, (u8)part, reg, value,
                     (u16)((track + 1) | (channel << 8)),
                     (u8)(to_chip ? 1 : 0) };
    Audio   *a   = ctx;

    shadow_track(&a->mirror, track, channel, part, reg, value, to_chip);
    sink_push(a, &cmd);
}

static void sink_dac(void *ctx, u8 drum)
{
    AudioCmd cmd = { CMD_DAC, 0, 0, drum, 0, 0 };

    write_hash(ctx, CMD_DAC, 0, 0, drum);
    if (g_sndlog) {
        const char *name = drum_name(drum);
        fprintf(stderr, "%6ld  DAC <- %02X  %s\n", g_tick, drum,
                name != NULL ? name : "?");
    }
    sink_push(ctx, &cmd);   /* a drum held is not struck: nothing to keep */
}

static void sink_psg(void *ctx, u8 value)
{
    AudioCmd cmd = { CMD_PSG_RAW, 0, 0, value, 0, 0 };
    Audio   *a   = ctx;

    write_hash(ctx, CMD_PSG_RAW, 0, 0, value);
    shadow_psg(&a->mirror, value);
    if (g_sndlog) {
        fprintf(stderr, "%6ld  PSG <- %02X\n", g_tick, value);
    }
    sink_push(a, &cmd);
}

/* (build 110) A register write to the backend playing, through the shadow:
 * the audio thread's (and audio_init's, before that thread exists). */
static void synth_write(Audio *a, int part, u8 reg, u8 value)
{
    shadow_fm(&a->live, part, reg, value);
    if (a->synth_ready) {
        a->synth->write(a->synth_inst[a->synth_id], part, reg, value);
    }
}

/* (build 111) A write from the hooks to the backend playing, through the
 * tracks' shadow. */
static void synth_track(Audio *a, int track, u8 channel, int part, u8 reg,
                        u8 value, bool to_chip)
{
    shadow_track(&a->live, track, channel, part, reg, value, to_chip);
    if (a->synth_ready) {
        a->synth->track_write(a->synth_inst[a->synth_id], track, channel,
                              part, reg, value, to_chip);
    }
}

/* (build 111) The tracks' shadow into the backend just reset (after the
 * chip's): each track that has written, its claim, its registers in the
 * order the chip's replay uses, and its key-on if its keys were down. */
static void synth_replay_tracks(Audio *a, void *self, const SynthBackend *b)
{
    for (int t = 0; t < SMPS_TRACKS; t++) {
        bool any = false;

        for (int p = 0; p < 2 && !any; p++) {
            for (int i = 0; i < 32; i++) {
                if (a->live.track_set[t][p][i] != 0) {
                    any = true;
                    break;
                }
            }
        }
        if (!any && a->live.track_key[t] == 0) {
            continue;
        }
        bool on = a->live.track_to_chip[t];
        u8 ch = a->live.track_chan[t];
        if (a->live.track_claim[t] != 0) {
            b->track_write(self, t, ch, 0, SMPS_HOOK_CLAIM,
                           (u8)(a->live.track_claim[t] - 1), on);
        }
        /* (build 112) its instrument by number, before its registers */
        if (a->live.track_ins[t] != 0) {
            b->track_write(self, t, ch, 0, SMPS_HOOK_INSTRUMENT,
                           (u8)(a->live.track_ins[t] - 1), on);
        }
        for (int p = 0; p < 2; p++) {
            for (int reg = 0x30; reg < 0xB8; reg++) {
                if (reg >= 0xA0 && reg < 0xB0) {
                    continue;
                }
                if (a->live.track_set[t][p][reg >> 3] & (1 << (reg & 7))) {
                    b->track_write(self, t, ch, p, (u8)reg,
                                   a->live.track_reg[t][p][reg], on);
                }
            }
            for (int slot = 0; slot < 3; slot++) {
                int hi = 0xA4 + slot;
                int lo = 0xA0 + slot;
                if (a->live.track_set[t][p][hi >> 3] & (1 << (hi & 7))) {
                    b->track_write(self, t, ch, p, (u8)hi,
                                   a->live.track_reg[t][p][hi], on);
                    b->track_write(self, t, ch, p, (u8)lo,
                                   a->live.track_reg[t][p][lo], on);
                }
            }
        }
        if (a->live.track_key[t] & 0xF0) {
            b->track_write(self, t, ch, 0, 0x28, a->live.track_key[t], on);
        }
    }

    /* (build 184) The chord voices' records, in the same order: the
     * instrument's number, the operators, $B0 and $B4, the pitch high byte
     * first, then the key if it is down. */
    for (int v = 0; v < SMPS_CHORD_VOICES_MAX; v++) {
        const AudioVoiceShadow *r = &a->live.voice[v];
        int t = SMPS_TRACKS + v;
        if (r->set == 0 && r->key == 0) {
            continue;
        }
        int part = (r->chan & 0x04) ? 1 : 0;
        if (r->ins != 0) {
            b->track_write(self, t, r->chan, 0, SMPS_HOOK_INSTRUMENT,
                           (u8)(r->ins - 1), true);
        }
        for (int place = 0; place < AUDIO_VOICE_REGS; place++) {
            if (r->set & (1u << place)) {
                b->track_write(self, t, r->chan, part,
                               voice_place_reg(place, r->chan), r->reg[place],
                               true);
            }
        }
        if (r->key & 0xF0) {
            b->track_write(self, t, r->chan, 0, 0x28, r->key, true);
        }
    }
}

/* (build 110) The switch, on the audio thread: the backend asked for (the
 * chip if it has no instance) reset, then given the shadow -- the mode
 * registers, every channel's operator registers, algorithm and pan, the
 * frequencies (the high byte first, which the chip latches, then the low,
 * which commits the pair; FM3's special-mode pairs too), and a key-on for
 * each channel whose keys were down. A note held across the switch sounds
 * on the new backend from its attack. About 200 writes: through the chip's
 * write queue, some 8 ms. */
static void synth_switch(Audio *a, int id)
{
    static const u8 fm3_pairs[3][2] = {
        { 0xAC, 0xA8 }, { 0xAD, 0xA9 }, { 0xAE, 0xAA }
    };

    if (id < 0 || id >= SYNTH_COUNT || a->synth_inst[id] == NULL) {
        id = SYNTH_CHIP;
    }
    if (a->synth_inst[id] == NULL) {
        return;
    }
    a->synth_id = id;
    a->synth    = synth_backend((SynthId)id);

    void *self = a->synth_inst[id];
    const SynthBackend *b = a->synth;

    b->reset(self);
    b->write(self, 0, 0x22, a->live.shadow[0][0x22]);
    b->write(self, 0, 0x27, a->live.shadow[0][0x27]);
    b->write(self, 0, 0x2B, a->live.shadow[0][0x2B]);
    for (int p = 0; p < 2; p++) {
        for (int reg = 0x30; reg < 0xA0; reg++) {
            if ((reg & 3) != 3) {
                b->write(self, p, (u8)reg, a->live.shadow[p][reg]);
            }
        }
        for (int reg = 0xB0; reg <= 0xB6; reg++) {
            if ((reg & 3) != 3) {
                b->write(self, p, (u8)reg, a->live.shadow[p][reg]);
            }
        }
        for (int slot = 0; slot < 3; slot++) {
            b->write(self, p, (u8)(0xA4 + slot), a->live.shadow[p][0xA4 + slot]);
            b->write(self, p, (u8)(0xA0 + slot), a->live.shadow[p][0xA0 + slot]);
        }
    }
    for (int i = 0; i < 3; i++) {
        b->write(self, 0, fm3_pairs[i][0], a->live.shadow[0][fm3_pairs[i][0]]);
        b->write(self, 0, fm3_pairs[i][1], a->live.shadow[0][fm3_pairs[i][1]]);
    }
    for (int ch = 0; ch < 6; ch++) {
        if (a->live.keyed[ch] != 0) {
            u8 chid = (u8)((ch < 3) ? ch : ch + 1);
            b->write(self, 0, 0x28, (u8)(a->live.keyed[ch] | chid));
        }
    }
    /* (build 111) The tracks' own: the voices of a backend that takes them
     * (Q), and the chip's marks of the effects' channels. */
    synth_replay_tracks(a, self, b);
}

void audio_init(Audio *a, int sample_rate)
{
    memset(a, 0, sizeof *a);
    psg_init(&a->psg, sample_rate);
    atomic_init(&a->head, 0);
    atomic_init(&a->tail, 0);
    atomic_init(&a->muted, false);

    /* (build 110) Every backend this build has, through the table
     * (synth.h); the chip plays until audio_set_synth says otherwise. */
    for (int id = 0; id < SYNTH_COUNT; id++) {
        const SynthBackend *b = synth_backend((SynthId)id);
        if (b != NULL) {
            a->synth_inst[id] = b->create(sample_rate);
        }
    }
    a->synth_id    = SYNTH_CHIP;
    a->synth_want  = SYNTH_CHIP;
    a->synth       = synth_backend(SYNTH_CHIP);
    a->synth_ready = a->synth_inst[SYNTH_CHIP] != NULL;
    a->write_hash  = 2166136261u;
    atomic_init(&a->master_db, 0);
    atomic_init(&a->music_db, 0);
    atomic_init(&a->effects_db, 0);
    /* Every channel's $B4 starts with both speakers on, as the chip's
     * reset leaves it. */
    for (int p = 0; p < 2; p++) {
        for (int slot = 0; slot < 3; slot++) {
            a->live.shadow[p][0xB4 + slot]   = 0xC0;
            a->mirror.shadow[p][0xB4 + slot] = 0xC0;
        }
    }
    /* (build 120) The mirror's PSG as psg_init leaves the chip, and its
     * DAC on as below. */
    for (int i = 0; i < 4; i++) {
        a->mirror.psg_atten[i] = a->psg.atten[i];
    }
    a->mirror.shadow[0][0x2B] = 0x80;
    atomic_init(&a->resync_busy, false);
    a->voices_now = 1;          /* (build 124) the chip's own: LIMITED */
    if (!a->synth_ready) {
        fprintf(stderr, "audio: no FM core (copy Nuked-OPN2's ym3438.c/h "
                        "into src/audio/); music will be silent\n");
    } else {
        /* The Z80 program's first act after reset is $2B = $80, DAC
         * on, and nothing ever turns it off; FM6 is the drum channel
         * from boot. The audio thread does not exist yet, so writing
         * the backend directly here is safe. */
        synth_write(a, 0, 0x2B, 0x80);
    }

    /* (build 109a) The master 12 dB up on 0.8 (src/audio/mix.h). */
    a->music_gain  = MIX_MUSIC_GAIN;
    a->effects_gain = MIX_MUSIC_GAIN;
    a->sfx_gain    = MIX_SFX_GAIN;
    a->master_gain = MIX_MASTER_GAIN;

    g_sndlog = getenv("PS2_SNDLOG") != NULL;
}

bool audio_load_music(Audio *a, const char *asset_dir)
{
    char data_path[512];
    SmpsSink sink;

    /* (build 125) The game's pack first, where it has one (assetfs.h). */
    smps_set_reader(assetfs_pack_find);
    synth_set_reader(assetfs_pack_find);
    /* (build 139b) A song's .trk or song file: the mod's first. */
    songtext_set_mod_test(assetfs_is_mod);

    /* (build 137a) The song text, built into the driver's image. */
    snprintf(data_path, sizeof data_path, "%s/music", asset_dir);
    bool built = smps_load(&a->smps_data, data_path);
    if (!built) {
        /* (build 138b) A packed game whose mods folder has music the song
         * text refused (said above, by file and line): the game's own
         * music, every mod under music/ set aside. */
        int dropped = assetfs_mods_drop("assets/music/");
        if (dropped > 0) {
            built = smps_load(&a->smps_data, data_path);
            fprintf(stderr, "mods: the music above was refused (its "
                            "assets/ path is the mods folder's); the game's "
                            "own %s\n",
                    built ? "played instead" : "refused too");
        }
    }
    if (!built) {
        fprintf(stderr, "audio: cannot build the sound driver's image from "
                        "%s (the song text); run tools/extract_music.py\n",
                data_path);
        return false;
    }

    /* Every backend loads the drums now: nothing reads assets/ later. */
    snprintf(data_path, sizeof data_path, "%s/music/drums.json", asset_dir);
    for (int id = 0; id < SYNTH_COUNT; id++) {
        if (a->synth_inst[id] == NULL) {
            continue;
        }
        bool drums = synth_backend((SynthId)id)->load_drums(
            a->synth_inst[id], data_path);
        if (!drums && assetfs_mods_drop("assets/music/drums") > 0) {
            /* (build 138b) Refused drums in the mods folder: the game's. */
            fprintf(stderr, "mods: the drums above were refused; the game's "
                            "own used\n");
            drums = synth_backend((SynthId)id)->load_drums(a->synth_inst[id],
                                                           data_path);
        }
        if (!drums) {
            fprintf(stderr, "audio: no drums for %s; run "
                            "tools/extract_drums.py\n",
                    synth_backend((SynthId)id)->name);
        }
    }

    /* (build 112) The patch file, to every backend that takes it. A file
     * that is not there is the ordinary case and says nothing; one that does
     * not read says why on stderr (patches.c). */
    snprintf(data_path, sizeof data_path, "%s/music/q_patches.json",
             asset_dir);
    free(a->patches);
    a->patches = calloc(SMPS_INSTRUMENTS, sizeof *a->patches);
    a->patch_count = (a->patches != NULL)
                   ? patches_load(data_path, &a->smps_data, a->patches,
                                  SMPS_INSTRUMENTS)
                   : 0;
    for (int id = 0; id < SYNTH_COUNT; id++) {
        const SynthBackend *b = synth_backend((SynthId)id);
        if (a->synth_inst[id] != NULL && b->set_patches != NULL) {
            b->set_patches(a->synth_inst[id], a->patches, a->patch_count);
        }
        /* (build 184) room for the chord voices a mod's music can need:
         * none for the game's own */
        if (a->synth_inst[id] != NULL && b->set_chord_voices != NULL) {
            b->set_chord_voices(a->synth_inst[id], a->smps_data.chord_music,
                                a->smps_data.chord_effects);
        }
    }

    sink.fm_write  = sink_fm;
    sink.dac_write = sink_dac;
    sink.psg_write = sink_psg;
    sink.ctx       = a;
    sink.track_write = sink_track;   /* (build 111) the hooks */
    smps_init(&a->smps, &a->smps_data, &sink);
    a->smps_ready = true;
    return true;
}

void audio_shutdown(Audio *a)
{
    if (a->smps_ready) {
        smps_free(&a->smps_data);
        a->smps_ready = false;
    }
    for (int id = 0; id < SYNTH_COUNT; id++) {
        if (a->synth_inst[id] != NULL) {
            synth_backend((SynthId)id)->shutdown(a->synth_inst[id]);
            a->synth_inst[id] = NULL;
        }
    }
    a->synth_ready = false;
    free(a->patches);
    a->patches = NULL;
    a->patch_count = 0;
}

void audio_music_resume(Audio *a)
{
    if (a->smps_ready) {
        a->smps.paused_mode = 0;
    }
}

void audio_music_tick(Audio *a)
{
    g_tick++;
    (void)resync_sent(a);   /* (build 120) as soon as the queue has room */
    if (a->smps_ready) {
        smps_tick(&a->smps);
    }
}

bool audio_track_ended(const Audio *a)
{
    return !a->smps_ready || a->smps.track_timer == 0x80;
}

void audio_track_timer_clear(Audio *a)
{
    a->smps.track_timer = 0;
}

/* (build 160) The rumble's record of what the game asked for. */
static void note_asked(Audio *a, u8 id)
{
    a->asked[id >> 5] |= 1u << (id & 31);
}

void audio_take_asked(Audio *a, u32 out[8])
{
    memcpy(out, a->asked, sizeof a->asked);
    memset(a->asked, 0, sizeof a->asked);
}

bool audio_play_sound(Audio *a, u8 id)
{
    note_asked(a, id);
    if (!a->smps_ready) {
        return false;
    }
    if (id == g_last_sound_id) {
        return false;
    }
    g_last_sound_id = id;
    if (g_sndlog) {
        fprintf(stderr, "%6ld  queue %02X\n", g_tick, id);
    }
    if (g_debug_sound_fn != NULL) {
        g_debug_sound_fn(g_debug_sound_ctx, id);
    }
    smps_play(&a->smps, id);
    return true;
}

void audio_take_queue(Audio *a)
{
    if (a->smps_ready) {
        smps_take_queue(&a->smps);
    }
}

u8 audio_music_started(const Audio *a)
{
    return a->smps_ready ? a->smps.music_started : 0;
}

void audio_set_last_sound(Audio *a, u8 id)
{
    (void)a;
    g_last_sound_id = id;
}

void audio_play_sfx(Audio *a, u8 id)
{
    note_asked(a, id);
    if (!a->smps_ready) {
        return;
    }
    if (g_sndlog) {
        fprintf(stderr, "%6ld  queue %02X\n", g_tick, id);
    }
    if (g_debug_sound_fn != NULL) {
        g_debug_sound_fn(g_debug_sound_ctx, id);
    }
    smps_play(&a->smps, id);
}

/* (build 120) The chips set to the handoff (resync_sent): the PSG's
 * registers and its latch; the FM side as a switch sets a backend -- reset,
 * then every register, key and track replayed -- to the backend playing. */
static void audio_resync(Audio *a)
{
    memcpy(&a->live, &a->handoff, sizeof a->live);
    atomic_store(&a->resync_busy, false);

    const AudioShadow *s = &a->live;
    for (int ch = 0; ch < 3; ch++) {
        psg_set_tone(&a->psg, ch, s->psg_tone[ch], s->psg_atten[ch]);
    }
    psg_set_noise(&a->psg, s->psg_noise, s->psg_atten[3]);
    /* A latch byte of what its register holds: psg.c's own latch then
     * points where the sequencer's next data byte expects. */
    int reg  = s->psg_latched;
    int chan = reg >> 1;
    u8  low  = (reg & 1) ? s->psg_atten[chan]
             : (chan == 3) ? s->psg_noise
                           : (u8)(s->psg_tone[chan] & 0x0F);
    psg_write(&a->psg, (u8)(0x80 | (reg << 4) | low));

    synth_switch(a, a->synth_id);
}

/* Drains queued register writes. Runs on the audio thread. */
static void audio_drain(Audio *a)
{
    unsigned tail = atomic_load_explicit(&a->tail, memory_order_relaxed);
    unsigned head = atomic_load_explicit(&a->head, memory_order_acquire);

    while (tail != head) {
        const AudioCmd *cmd = &a->queue[tail % AUDIO_QUEUE];

        switch (cmd->kind) {
        case CMD_PSG_TONE:
            psg_set_tone(&a->psg, cmd->channel, cmd->period, cmd->atten);
            break;
        case CMD_PSG_NOISE:
            psg_set_noise(&a->psg, (u8)cmd->period, cmd->atten);
            break;
        case CMD_PSG_RAW:
            psg_write(&a->psg, cmd->value);
            break;
        case CMD_FM:
            synth_write(a, cmd->channel, cmd->reg, cmd->value);
            break;
        case CMD_DAC:
            if (a->synth_ready) {
                a->synth->drum(a->synth_inst[a->synth_id], cmd->value);
            }
            break;
        case CMD_SYNTH:
            synth_switch(a, cmd->value);
            break;
        case CMD_OVERSAMPLE:
            /* every instance that has it, playing or not, so a switch to it
             * later finds it set */
            for (int id = 0; id < SYNTH_COUNT; id++) {
                const SynthBackend *b = synth_backend((SynthId)id);
                if (a->synth_inst[id] != NULL && b->set_oversample != NULL) {
                    b->set_oversample(a->synth_inst[id], cmd->value);
                }
            }
            break;
        case CMD_SYNTH_OPTION:
            /* (build 112) as CMD_OVERSAMPLE: every instance that has it */
            for (int id = 0; id < SYNTH_COUNT; id++) {
                const SynthBackend *b = synth_backend((SynthId)id);
                if (a->synth_inst[id] != NULL && b->set_option != NULL) {
                    b->set_option(a->synth_inst[id], cmd->reg, cmd->value);
                }
            }
            /* (build 124) VOICES changed with the chip playing: the chip
             * reset and replayed, the music onto A and each sounding
             * effect onto B (EXTRA), or all onto the one chip (LIMITED) --
             * docs/second_chip.md, Switching. Q takes it as it goes. */
            if (cmd->reg == SYNTH_OPT_VOICES && cmd->value != a->voices_now) {
                a->voices_now = cmd->value;
                if (a->synth_id == SYNTH_CHIP) {
                    synth_switch(a, SYNTH_CHIP);
                }
            }
            break;
        case CMD_TRACK:
            synth_track(a, (int)(cmd->period & 0xFF) - 1, (u8)(cmd->period >> 8),
                        cmd->channel, cmd->reg, cmd->value, cmd->atten != 0);
            break;
        case CMD_RESYNC:
            audio_resync(a);
            break;
        default:
            break;
        }
        tail++;
    }
    atomic_store_explicit(&a->tail, tail, memory_order_release);
}

/* (build 110) A gain in whole dB on the mix's own: 0 dB is the mix's gain
 * exactly (no pow, so the default mix is the same to the bit). */
static float volume_gain(float mix_gain, int db)
{
    if (db <= AUDIO_DB_OFF) {
        return 0.0f;
    }
    if (db == 0) {
        return mix_gain;
    }
    return mix_gain * powf(10.0f, (float)db / 20.0f);
}

void audio_render(Audio *a, float *out, int frames)
{
    audio_drain(a);
    a->master_gain = volume_gain(MIX_MASTER_GAIN, atomic_load(&a->master_db));
    a->music_gain  = volume_gain(MIX_MUSIC_GAIN, atomic_load(&a->music_db));
    a->effects_gain = volume_gain(MIX_MUSIC_GAIN, atomic_load(&a->effects_db));
    memset(out, 0, (size_t)frames * AUDIO_CHANNELS * sizeof *out);

    /* The backend renders both sides itself: each channel's $B4 left
     * and right bits (instrument uploads, cfFF_Pan, cfE3_PanAnim) decide
     * where it goes, the DAC drums with FM6. (build 111) The music and the
     * effects at their own gains. */
    if (a->synth_ready) {
        a->synth->render(a->synth_inst[a->synth_id], out, frames,
                         a->music_gain, a->effects_gain);
    }

    /* The SN76489 has one output, and the Mega Drive feeds it to both
     * sides at once: the same sample left and right. */
    for (int done = 0; done < frames; ) {
        int n = frames - done;
        if (n > AUDIO_MAX_LEN) {
            n = AUDIO_MAX_LEN;
        }
        memset(a->psg_mono, 0, (size_t)n * sizeof a->psg_mono[0]);
        psg_render(&a->psg, a->psg_mono, n, a->sfx_gain);
        for (int i = 0; i < n; i++) {
            out[2 * (done + i)]     += a->psg_mono[i];
            out[2 * (done + i) + 1] += a->psg_mono[i];
        }
        done += n;
    }

    /* (build 109a) The soft knee above -3 dBFS (mix.h). Before, this was a
     * hard clip at full scale under a comment calling it a soft limit; at
     * the old master nothing reached it. */
    for (int i = 0; i < frames * AUDIO_CHANNELS; i++) {
        out[i] = mix_soft_limit(out[i] * a->master_gain);
    }
    if (atomic_load(&a->muted)) {
        memset(out, 0, (size_t)frames * AUDIO_CHANNELS * sizeof *out);
    }
}

void audio_set_muted(Audio *a, bool muted)
{
    atomic_store(&a->muted, muted);
    /* (build 120) Held from now; or the resync first, then queued again. */
    if (muted && !a->holding) {
        a->holding = true;
    } else if (!muted && a->holding) {
        a->holding        = false;
        a->resync_pending = true;
        (void)resync_sent(a);
    }
}

bool audio_synth_available(const Audio *a, int id)
{
    return id >= 0 && id < SYNTH_COUNT && a->synth_inst[id] != NULL;
}

int audio_set_synth(Audio *a, int id)
{
    AudioCmd cmd = { CMD_SYNTH, 0, 0, 0, 0, 0 };

    if (!audio_synth_available(a, id)) {
        id = SYNTH_CHIP;
    }
    cmd.value     = (u8)id;
    a->synth_want = id;
    audio_push(a, &cmd);
    return id;
}

void audio_set_volume(Audio *a, int master_db, int music_db, int effects_db)
{
    atomic_store(&a->master_db, master_db);
    atomic_store(&a->music_db, music_db);
    atomic_store(&a->effects_db, effects_db);
}

void audio_set_oversample(Audio *a, int factor)
{
    AudioCmd cmd = { CMD_OVERSAMPLE, 0, 0, 0, 0, 0 };

    cmd.value = (u8)((factor == 1 || factor == 2) ? factor : 4);
    audio_push(a, &cmd);
}

void audio_set_synth_option(Audio *a, int option, int value)
{
    AudioCmd cmd = { CMD_SYNTH_OPTION, 0, 0, 0, 0, 0 };

    cmd.reg   = (u8)option;
    cmd.value = (u8)value;
    audio_push(a, &cmd);
}

u32 audio_write_hash(const Audio *a, u32 *count)
{
    if (count != NULL) {
        *count = a->write_count;
    }
    return a->write_hash;
}

u8 audio_register_written(const Audio *a, int part, u8 reg)
{
    return a->mirror.shadow[(part != 0) ? 1 : 0][reg];
}

u16 audio_stuck_tracks(const Audio *a)
{
    return a->smps_ready ? smps_stuck_tracks(&a->smps) : 0;
}

void audio_beep(Audio *a, double hz, u8 atten)
{
    AudioCmd cmd = { CMD_PSG_TONE, 2, 0, 0, psg_period_for_hz(hz), atten };

    audio_push(a, &cmd);
}

void audio_silence_beep(Audio *a)
{
    AudioCmd cmd = { CMD_PSG_TONE, 2, 0, 0, 0, 15 };

    audio_push(a, &cmd);
}
