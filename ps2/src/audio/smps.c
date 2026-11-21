#include "smps.h"
#include "drums.h"
#include "songtext.h"
#include "../core/json.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* A port of the driver at ps2.asm:89234. Routine names follow the
 * disassembly so the two can be read side by side; where the original
 * has a `loc_B9xxx` with no name, the C function is named for what it
 * does and the address is in a comment.
 *
 * Two things are kept deliberately rather than tidied, because the
 * music depends on them:
 *
 *   - The stream cursor is a 16-bit offset from the base, not a
 *     pointer, because `cfF6_GoTo` adds a 16-bit displacement and
 *     `cfF8_GoSub` stores the cursor in the track block. Widening it
 *     would change where a wrapped jump lands.
 *
 *   - `cfF2_TrkEnd` unwinds by popping its caller's return address, so
 *     the byte after the flag is never read and the note loop resumes
 *     with the flag's own table index still in d5. That index is then
 *     used as a duration. The track is already switched off by then, so
 *     nothing hears it, but the pointer and timeout it leaves behind
 *     are what the original leaves behind.
 *
 * The underscore macros in the disassembly (`_btst`, `_bclr`,
 * `_move.b`) are assembler wrappers for the same instructions and are
 * ported as the plain operation. */

#define TRACK_STRIDE   SMPS_TRACK_SIZE
#define SFX_OFFSET     0x90            /* a BGM track to its SFX track */
#define PAL_TEMPO      0x100           /* loc_B8100 */
#define PAL_TICK_MULT  0x120           /* loc_B8120 */

/* The seven z80ptr words at SoundDriverBase. */
#define ROOT_PRIORITIES   0x00
#define ROOT_TEMPO        0x02
#define ROOT_MUSIC        0x04
#define ROOT_FM_INS       0x06
#define ROOT_MOD_ENV      0x08
#define ROOT_MUSIC_2      0x0A
#define ROOT_SFX          0x0C

_Static_assert(sizeof(SmpsTrack) == SMPS_TRACK_SIZE, "track block is $30");

/* ------------------------------------------------------------------ */
/* Image access                                                        */

static u8 image_u8(const Smps *s, u32 off)
{
    if (off >= s->data.size) {
        return 0;
    }
    return s->data.image[off];
}

/* SndDrv_ReadPtr: a z80ptr word is stored low byte first, so reading it
 * big-endian and rotating by 8 gives the offset back. */
static u32 read_ptr(const Smps *s, u32 table, u32 index)
{
    u32 at = table + index * 2;
    return (u32)image_u8(s, at) | ((u32)image_u8(s, at + 1) << 8);
}

static u32 root(const Smps *s, u32 which)
{
    return read_ptr(s, 0, which / 2);
}

/* ------------------------------------------------------------------ */
/* Track access                                                        */

/* (build 184) A track 0-9, or chord voice ti - SMPS_TRACKS's block. */
static SmpsTrack *trk(Smps *s, int ti)
{
    if (ti >= SMPS_TRACKS) {
        return &s->voices[ti - SMPS_TRACKS];
    }
    return &s->tracks[ti];
}

/* (build 184) Whether a track is a chord voice: its writes go to the hooks
 * alone, never to the Mega Drive's one chip. */
static bool is_voice(int ti)
{
    return ti >= SMPS_TRACKS;
}

/* Byte access that can leave the block, because two callers do.
 * `cfF8_GoSub` on a track whose stack byte is still zero indexes $FC
 * forward, and the chord-mode loops walk into the following tracks.
 * (build 114a) The tracks are addressed as the bytes of the whole array,
 * which C allows; `s->tracks[0].raw + i` stepped past the first track's
 * 48 bytes, which it does not (clang's bounds check says so on every route
 * with music). The same byte either way: a track is exactly its bytes. */
_Static_assert(sizeof(SmpsTrack) == TRACK_STRIDE,
               "track_byte needs the tracks packed, TRACK_STRIDE apart");

static u8 *track_byte(Smps *s, int ti, int off)
{
    static u8 sink;
    int i = ti * TRACK_STRIDE + off;

    if (i >= 0 && i < SMPS_TRACK_RAM) {
        return (u8 *)s->tracks + i;
    }
    /* An SFX track's zero stack pointer puts cfF8's return address in
     * the RAM after the tracks, which holds it as the hardware did. */
    i -= SMPS_TRACK_RAM;
    if (i >= 0 && i < (int)sizeof s->ram_after_tracks) {
        return &s->ram_after_tracks[i];
    }
    sink = 0;
    return &sink;
}

static u16 track_word(const SmpsTrack *t, int off)
{
    return (u16)((t->raw[off] << 8) | t->raw[off + 1]);
}

static void set_track_word(SmpsTrack *t, int off, u16 v)
{
    t->raw[off] = (u8)(v >> 8);
    t->raw[off + 1] = (u8)v;
}

static u16 track_ptr(const SmpsTrack *t)
{
    return (u16)(t->f.ptr_lo | (t->f.ptr_hi << 8));
}

static void set_track_ptr(SmpsTrack *t, u16 off)
{
    t->f.ptr_lo = (u8)off;
    t->f.ptr_hi = (u8)(off >> 8);
}

/* ------------------------------------------------------------------ */
/* Chip writes                                                         */

static void write_fm_i(Smps *s, u8 reg, u8 value)
{
    s->sink.fm_write(s->sink.ctx, 0, reg, value);
}

static void write_fm_ii(Smps *s, u8 reg, u8 value)
{
    s->sink.fm_write(s->sink.ctx, 1, reg, value);
}

/* (build 111) The hooks (smps.h): a write by the track that made it, before
 * the chip gets it or, while an effect holds the channel, does not. */
static void hook_on(Smps *s, int ti, u8 channel, int part, u8 reg, u8 value,
                    bool to_chip)
{
    if (s->sink.track_write != NULL) {
        s->sink.track_write(s->sink.ctx, ti, channel, part, reg, value,
                            to_chip);
    }
}

/* A track's write, its own channel from its block. */
static void hook(Smps *s, int ti, int part, u8 reg, u8 value, bool to_chip)
{
    u8 channel = (ti >= 0 && ti < SMPS_TRACKS + SMPS_CHORD_VOICES_MAX)
               ? trk(s, ti)->f.channel : 0xFF;

    hook_on(s, ti, channel, part, reg, value, to_chip);
}

/* The driver's own writes, which no track makes. */
static void write_fm_i_driver(Smps *s, u8 reg, u8 value)
{
    hook(s, SMPS_TRACK_DRIVER, 0, reg, value, true);
    write_fm_i(s, reg, value);
}

static void write_fm_ii_driver(Smps *s, u8 reg, u8 value)
{
    hook(s, SMPS_TRACK_DRIVER, 1, reg, value, true);
    write_fm_ii(s, reg, value);
}

/* WriteFM1Main: no channel offset, and silent while overridden. Used
 * for key on/off, where the channel travels in the value instead. */
static void write_fm1_main(Smps *s, int ti, u8 reg, u8 value)
{
    bool on = !(trk(s, ti)->f.flags & SMPS_F_OVERRIDDEN);

    hook(s, ti, 0, reg, value, on);
    if (!on || is_voice(ti)) {
        return;
    }
    write_fm_i(s, reg, value);
}

/* WriteFMIorII: the register gets the channel added, and channels 4-6
 * (bit 2 set) go to the second port with that bit cleared. */
static void write_fm_i_or_ii(Smps *s, int ti, u8 reg, u8 value)
{
    SmpsTrack *t = trk(s, ti);
    bool on = !(t->f.flags & SMPS_F_OVERRIDDEN);
    bool chip = on && !is_voice(ti);    /* (184) a voice: the hooks alone */

    if (t->f.channel & 0x04) {
        u8 r = (u8)(reg + (t->f.channel & ~0x04));

        hook(s, ti, 1, r, value, on);
        if (chip) {
            write_fm_ii(s, r, value);
        }
    } else {
        u8 r = (u8)(reg + t->f.channel);

        hook(s, ti, 0, r, value, on);
        if (chip) {
            write_fm_i(s, r, value);
        }
    }
}

/* Writes a register for a channel id rather than a track, because
 * SetMaxRelRate is handed a track header instead of a track block. `ti` is
 * the track the hooks name; the chip gets it only when `on`. */
static void write_fm_channel(Smps *s, int ti, u8 channel, u8 reg, u8 value,
                             bool on)
{
    bool chip = on && !is_voice(ti);    /* (184) a voice: the hooks alone */

    if (channel & 0x04) {
        u8 r = (u8)(reg + (channel & ~0x04));

        hook_on(s, ti, channel, 1, r, value, on);
        if (chip) {
            write_fm_ii(s, r, value);
        }
    } else {
        u8 r = (u8)(reg + channel);

        hook_on(s, ti, channel, 0, r, value, on);
        if (chip) {
            write_fm_i(s, r, value);
        }
    }
}

static void write_psg(Smps *s, u8 value)
{
    if (s->sink.psg_write != NULL) {
        s->sink.psg_write(s->sink.ctx, value);
    }
}

/* SilenceFM: key off all six channels, then every operator's total
 * level to maximum attenuation on both ports. */
static void silence_fm(Smps *s)
{
    for (int ch = 0; ch < 7; ch++) {
        write_fm_i_driver(s, 0x28, (u8)ch);
    }
    for (u8 reg = 0x40; reg < 0x50; reg += 4) {
        write_fm_i_driver(s, reg, 0xFF);
        write_fm_ii_driver(s, reg, 0xFF);
    }
}

static void silence_psg(Smps *s)
{
    static const u8 mute[4] = { 0x9F, 0xBF, 0xDF, 0xFF };

    for (int i = 0; i < 4; i++) {
        write_psg(s, mute[i]);
    }
}

/* SetMaxRelRate (ps2.asm, after RestoreBGMChn): meant as release rate $F
 * on all four operators of the channel an effect is taking. PlaySFX calls
 * it with a3 at the effect's nine header bytes rather than at a track block,
 * so the flags and channel are the header's.
 *
 * (build 132b) As the ROM writes it, drift and all. The loop adds 4 to its
 * register number and never reloads it, while WriteFMIorII adds the
 * channel's offset into that same register on every pass (WriteFMIIPart the
 * offset with the part bit cleared). On FM1 and FM4 (offset 0) that is the
 * four release rates. On FM5 it is FM5 op1's release rate, FM6 op3's, a
 * slot no channel has, then $FF into FM4 op1's SSG-EG register ($90, part
 * II); on FM3, FM3 op1's, FM1 op2's, FM3 op4's, then FM1 op3's SSG-EG ($94).
 * Nothing else in the driver writes $90-$9E, so on the Mega Drive FM4's
 * operator 1 keeps SSG-EG $F from the first effect on FM5 (the pause
 * jingle's echo is one) for the rest of the session, FM1's operator 3 from
 * the first on FM3. Before 132b the port wrote the four release rates
 * (docs/history.md, Builds 132 and 132b). The hooks get each write where it
 * lands: a release rate or SSG-EG on another channel than the effect's. */
static void set_max_rel_rate_channel(Smps *s, int ti, u8 flags, u8 channel)
{
    int part = (channel & 0x04) ? 1 : 0;
    u8  off  = part ? (u8)(channel & ~0x04) : channel;
    u8  reg  = 0x80;

    if (flags & SMPS_F_OVERRIDDEN) {
        return;
    }
    for (int i = 0; i < 4; i++) {
        reg = (u8)(reg + off);                  /* WriteFMIorII's add */
        hook_on(s, ti, channel, part, reg, 0xFF, true);
        if (part) {
            write_fm_ii(s, reg, 0xFF);
        } else {
            write_fm_i(s, reg, 0xFF);
        }
        reg = (u8)(reg + 4);                    /* addq.w #4, d0 */
    }
}

/* ------------------------------------------------------------------ */
/* Volume and instruments                                              */

/* RefreshVolume (ps2.asm:92360). Which operators are carriers depends
 * on the algorithm, so a volume change writes a different set of total
 * level registers per patch. The jump table at loc_BA8BC falls through,
 * which is why the cases below are written as one descending chain. */
static void refresh_volume(Smps *s, int ti, u8 d3)
{
    SmpsTrack *t = trk(s, ti);
    u8 algo = t->f.algorithm & 7;

    if (algo == 7) {
        write_fm_i_or_ii(s, ti, 0x40, (u8)(d3 + t->f.tl_op1));
    }
    if (algo >= 5) {
        write_fm_i_or_ii(s, ti, 0x44, (u8)(d3 + t->f.tl_op3));
    }
    if (algo >= 4) {
        write_fm_i_or_ii(s, ti, 0x48, (u8)(d3 + t->raw[0x27]));
    }
    write_fm_i_or_ii(s, ti, 0x4C, (u8)(d3 + t->f.tl_op4));
    if (!is_voice(ti) && d3 == t->f.volume
        && !(t->f.flags & SMPS_F_OVERRIDDEN)) {
        s->quiet = (u16)(s->quiet & ~(1u << ti));   /* (132c) back up */
    }
}

/* The register order SetFMIns uploads in (loc_BA818). (build 112) Public
 * as smps_ins_regs, for the patch file's reader (src/audio/patches.c). */
const u8 smps_ins_regs[26] = {
    0xB0, 0xB4, 0x40, 0x48, 0x44, 0x4C, 0x30, 0x38, 0x34, 0x3C,
    0x50, 0x58, 0x54, 0x5C, 0x60, 0x68, 0x64, 0x6C, 0x70, 0x78,
    0x74, 0x7C, 0x80, 0x88, 0x84, 0x8C
};

/* SetFMIns (ps2.asm:92309). Six of the 26 values are also kept in the
 * track block, because later volume and pan changes need them -- but
 * they are still written to the chip on the way past, so the algorithm
 * and feedback byte lands like any other. */
static void set_fm_ins(Smps *s, int ti, u32 ins, u8 index)
{
    SmpsTrack *t = trk(s, ti);

    /* (build 112) The hooks: which instrument, before its upload (smps.h,
     * SMPS_HOOK_INSTRUMENT). No chip write. */
    hook(s, ti, 0, SMPS_HOOK_INSTRUMENT, index,
         !(t->f.flags & SMPS_F_OVERRIDDEN));

    for (int i = 0; i < 26; i++) {
        u8 reg = smps_ins_regs[i];
        u8 value = image_u8(s, ins + (u32)i);

        switch (reg) {
        case 0xB0:
            t->f.algorithm = value;
            break;
        case 0x40:
            t->f.tl_op1 = value;
            break;
        case 0x48:
            t->raw[0x27] = value;
            break;
        case 0x44:
            t->f.tl_op3 = value;
            break;
        case 0x4C:
            t->f.tl_op4 = value;
            break;
        case 0xB4:
            t->f.pan_ams_fms = value;
            break;
        default:
            break;
        }

        write_fm_channel(s, ti, t->f.channel, reg, value,
                         !(t->f.flags & SMPS_F_OVERRIDDEN));
    }
    if (!is_voice(ti) && !(t->f.flags & SMPS_F_OVERRIDDEN)) {
        s->quiet = (u16)(s->quiet & ~(1u << ti));   /* (132c) back up */
    }

    if (t->f.volume != 0) {
        refresh_volume(s, ti, t->f.volume);
    }
}

/* ------------------------------------------------------------------ */
/* Note on and off                                                     */

static void do_note_on(Smps *s, int ti)
{
    write_fm1_main(s, ti, 0x28, (u8)(trk(s, ti)->f.channel | 0xF0));
}

static void fm_note_off(Smps *s, int ti)
{
    write_fm1_main(s, ti, 0x28, trk(s, ti)->f.channel);
}

/* ------------------------------------------------------------------ */
/* (build 184) Chord parts (smps.h)                                    */

/* The track that plays part i of lead ti's chord: the lead itself for 0,
 * else the part it took, or -1 for none. The ROM's loops index ti + i. */
static int part_track(const Smps *s, int ti, int i)
{
    if (i == 0) {
        return ti;
    }
    if (ti < 0 || ti >= SMPS_TRACKS || i - 1 >= s->chord_parts[ti]) {
        return -1;
    }
    u8 u = s->chord_part[ti][i - 1];
    return (u == SMPS_CHORD_NONE) ? -1 : u;
}

/* A free voice of the music's (or the effects') range, its block cleared and
 * on its own channel; SMPS_CHORD_NONE when the range is used up. */
static u8 take_voice(Smps *s, bool effect)
{
    int from = effect ? s->data.chord_music : 0;
    int to   = from + (effect ? s->data.chord_effects : s->data.chord_music);

    if (from < 0) {
        from = 0;
    }
    if (to > SMPS_CHORD_VOICES_MAX) {
        to = SMPS_CHORD_VOICES_MAX;
    }
    for (int v = from; v < to; v++) {
        if (s->voice_taken[v]) {
            continue;
        }
        SmpsTrack *t = trk(s, SMPS_TRACKS + v);
        s->voice_taken[v] = true;
        memset(t->raw, 0, TRACK_STRIDE);
        t->f.channel = smps_voice_channel(s->data.chord_music, v);
        return (u8)(SMPS_TRACKS + v);
    }
    return SMPS_CHORD_NONE;
}

/* cfE5 with `count` notes: the lead's parts up to count - 1, in order -- a
 * music track after it that the song leaves out and no chord has taken,
 * FM1-FM5 by its place (PlayMusic gave FM4's and FM5's theirs), then a
 * voice. The game's chords, three on FM3's track in songs of four tracks,
 * take tracks 4 and 5: the ROM's ti + 1, ti + 2. */
static void give_parts(Smps *s, int ti, int count)
{
    int want = count - 1;

    if (ti < 0 || ti >= SMPS_TRACKS) {
        return;
    }
    if (want > SMPS_CHORD_PARTS) {
        want = SMPS_CHORD_PARTS;
    }
    while (s->chord_parts[ti] < want) {
        u8 got = SMPS_CHORD_NONE;

        if (ti < SMPS_MUSIC_TRACKS) {
            int u = (ti + 1 > s->music_tracks) ? ti + 1 : s->music_tracks;
            for (; u < SMPS_MUSIC_TRACKS && got == SMPS_CHORD_NONE; u++) {
                if (s->part_taken[u]) {
                    continue;
                }
                s->part_taken[u] = true;
                if (u <= 3) {
                    trk(s, u)->f.channel = (u8)(u - 1);
                }
                got = (u8)u;
            }
        }
        if (got == SMPS_CHORD_NONE) {
            got = take_voice(s, ti >= SMPS_MUSIC_TRACKS);
        }
        s->chord_part[ti][s->chord_parts[ti]] = got;
        s->chord_parts[ti]++;
    }
}

/* The lead's parts given back: a new song, or PlaySFX clearing the track. */
static void release_parts(Smps *s, int ti)
{
    for (int p = 0; p < s->chord_parts[ti]; p++) {
        u8 u = s->chord_part[ti][p];

        if (u == SMPS_CHORD_NONE) {
            continue;
        }
        if (u < SMPS_TRACKS) {
            s->part_taken[u] = false;
        } else {
            s->voice_taken[u - SMPS_TRACKS] = false;
        }
        s->chord_part[ti][p] = SMPS_CHORD_NONE;
    }
    s->chord_parts[ti] = 0;
}

/* Voices from..to-1 silenced as SilenceFM silences a channel, for the voices
 * alone: keyed off, then every operator's level to the most attenuation.
 * Through the hooks -- nothing here reaches the Mega Drive's chip. */
static void silence_voices(Smps *s, int from, int to)
{
    for (int v = from; v < to && v < SMPS_CHORD_VOICES_MAX; v++) {
        u8  ch   = smps_voice_channel(s->data.chord_music, v);
        int part = (ch & 0x04) ? 1 : 0;

        hook_on(s, SMPS_TRACKS + v, ch, 0, 0x28, ch, true);
        for (u8 reg = 0x40; reg < 0x50; reg += 4) {
            hook_on(s, SMPS_TRACKS + v, ch, part, (u8)(reg + (ch & 3)), 0xFF,
                    true);
        }
    }
}

/* The music's parts given back and its voices silenced: a song starting or
 * stopping (cfE0). */
static void music_parts_end(Smps *s)
{
    for (int ti = 0; ti < SMPS_MUSIC_TRACKS; ti++) {
        release_parts(s, ti);
    }
    silence_voices(s, 0, s->data.chord_music);
}

/* The pause's walk (cfE8) on a lead: its chord voices turned down with it,
 * or back up. Its parts on tracks the walk reaches itself. */
static void pause_voices(Smps *s, int ti, bool down)
{
    for (int p = 0; p < s->chord_parts[ti]; p++) {
        int u = s->chord_part[ti][p];

        if (u == SMPS_CHORD_NONE || !is_voice(u)) {
            continue;
        }
        SmpsTrack *v = trk(s, u);
        refresh_volume(s, u, down ? (u8)(0xFF - v->f.volume) : v->f.volume);
    }
}

/* Chord mode: one stream drives `chord_count` tracks, so every key event
 * repeats across them. (build 184) Across its parts (part_track). */
static void do_note_on_a(Smps *s, int ti)
{
    SmpsTrack *t = trk(s, ti);
    int count;

    if (t->f.flags & SMPS_F_HOLD) {
        return;
    }
    count = t->f.chord_count;
    if (count == 0) {
        do_note_on(s, ti);
        return;
    }
    for (int i = 0; i < count; i++) {
        int pt = part_track(s, ti, i);
        if (pt >= 0) {
            do_note_on(s, pt);
        }
    }
}

static void do_note_off_a(Smps *s, int ti)
{
    SmpsTrack *t = trk(s, ti);
    int count;

    if (t->f.flags & SMPS_F_HOLD) {
        return;
    }
    count = t->f.chord_count;
    if (count == 0) {
        fm_note_off(s, ti);
        return;
    }
    for (int i = 0; i < count; i++) {
        int pt = part_track(s, ti, i);
        if (pt >= 0) {
            fm_note_off(s, pt);
        }
    }
}

/* ------------------------------------------------------------------ */
/* Frequency                                                           */

static void get_fm_freq(Smps *s, int ti, u8 note)
{
    SmpsTrack *t = trk(s, ti);
    u16 index = (u16)((note + t->f.transpose) & 0x3F);
    u16 freq = 0;

    if (index < (u16)s->data.freq_count) {
        freq = s->data.freqs[index];
    }
    set_track_word(t, 0x0C, freq);
}

/* ReadRawFreq: the note byte is the high half of a literal YM2612
 * frequency word, the next stream byte the low half, with the track's
 * transpose sign-extended and added. */
static void read_raw_freq(Smps *s, int ti, u8 note, u32 *cursor)
{
    SmpsTrack *t = trk(s, ti);
    u16 value = (u16)(note << 8);

    value = (u16)(value + image_u8(s, (*cursor)++));
    value = (u16)(value + (s16)(s8)t->f.transpose);
    set_track_word(t, 0x0C, value);
}

/* GetFrequency_A: $80 is a rest, which sets the hold bit so nothing
 * keys on again until the next real note. In chord mode the remaining
 * parts read their own note bytes from the same stream. */
static void get_frequency_a(Smps *s, int ti, u8 note, u32 *cursor)
{
    SmpsTrack *t = trk(s, ti);
    int count;

    if (note == 0x80) {
        t->f.flags |= SMPS_F_HOLD;
        do_note_off_a(s, ti);
        return;
    }

    note = (u8)(note - 0x80);
    count = t->f.chord_count;
    if (count == 0) {
        get_fm_freq(s, ti, note);
        return;
    }

    get_fm_freq(s, ti, note);
    for (int i = 1; i < count; i++) {
        u8  part_note = image_u8(s, (*cursor)++);
        int pt = part_track(s, ti, i);
        if (pt >= 0) {
            get_fm_freq(s, pt, part_note);
        }
    }
}

/* DoPitchSlide: the frequency word carries the octave in bits 11-13, so
 * sliding past either end of the mantissa range has to be folded into
 * the neighbouring octave by hand. */
static void do_pitch_slide(Smps *s, int ti)
{
    SmpsTrack *t = trk(s, ti);
    u16 freq = track_word(t, 0x0C);
    u8 speed = t->f.slide_speed;

    if (!(speed & 0x80)) {
        freq = (u16)(freq + speed);
        if ((freq & 0x3FF) >= 0x4D1) {
            freq = (u16)(freq + 0x59B);
        }
    } else {
        freq = (u16)(freq - (u8)(-(s8)speed));
        if ((freq & 0x3FF) < 0x26C) {
            freq = (u16)(freq - 0x59B);
        }
    }
    set_track_word(t, 0x0C, freq);
}

/* SendFM3Freqs: FM3's special mode gives each operator its own
 * frequency, from four registers that are not contiguous. */
static void send_fm3_freqs(Smps *s, int ti, u16 freq)
{
    static const u8 regs[4] = { 0xAD, 0xAE, 0xAC, 0xA6 };

    if (trk(s, ti)->f.channel != 2) {
        return;
    }
    for (int i = 0; i < 4; i++) {
        u16 v = (u16)(freq + s->special_fm3_notes[i]);

        write_fm1_main(s, ti, regs[i], (u8)(v >> 8));
        write_fm1_main(s, ti, (u8)(regs[i] - 4), (u8)v);
    }
}

static void send_fm_freq(Smps *s, int ti, u16 freq)
{
    if (trk(s, ti)->f.flags & SMPS_F_FM3_SPECIAL) {
        send_fm3_freqs(s, ti, freq);
        return;
    }
    write_fm_i_or_ii(s, ti, 0xA4, (u8)(freq >> 8));
    write_fm_i_or_ii(s, ti, 0xA0, (u8)freq);
}

/* DoModEnv: the vibrato envelope. Data bytes are signed deltas scaled
 * by a multiplier; $80 and up are commands.
 *
 * The sign extension at ModEnv_Positive is wrong in the original --
 * `ori.w #$FFFF` makes every negative byte $FFFF rather than extending
 * it, so a delta of -2 and a delta of -8 both come out as -1 before the
 * multiply. That is what the music was written against, so it is kept;
 * fixing it changes every vibrato in the game. */
static void do_mod_env(Smps *s, int ti)
{
    SmpsTrack *t = trk(s, ti);
    u16 freq = track_word(t, 0x0C);
    u8 env = t->f.mod_envelope;
    u32 table;
    u32 index;
    u16 mult;

    if (env == 0) {
        send_fm_freq(s, ti, freq);
        return;
    }

    mult = t->f.mod_env_mult;
    table = read_ptr(s, root(s, ROOT_MOD_ENV), (u32)(env - 1));
    index = t->f.mod_env_index;

    for (;;) {
        u8 b = image_u8(s, table + index);

        index++;
        t->f.mod_env_index = (u8)index;

        if (b == 0x83 || b == 0x81) {
            t->f.mod_env_state = 0x80;
            return;
        }
        if (b == 0x85) {
            index = image_u8(s, table + index);
            continue;
        }
        if (b == 0x84) {
            t->f.mod_env_mult = (u8)(t->f.mod_env_mult
                                     + image_u8(s, table + index));
            mult = t->f.mod_env_mult;
            index++;
            t->f.mod_env_index = (u8)index;
            continue;
        }
        if (b == 0x80) {
            index = 0;
            continue;
        }

        {
            u16 delta = b;

            if (b > 0x80) {
                delta = 0xFFFF;   /* the original's broken sign extension */
            }
            freq = (u16)(freq + (u16)(delta * mult));
        }
        send_fm_freq(s, ti, freq);
        return;
    }
}

static void send_fm_freq_a(Smps *s, int ti)
{
    int count = trk(s, ti)->f.chord_count;

    if (count == 0) {
        do_mod_env(s, ti);
        return;
    }
    for (int i = 0; i < count; i++) {
        int pt = part_track(s, ti, i);
        if (pt >= 0) {
            do_mod_env(s, pt);
        }
    }
}

static void exec_mod_env(Smps *s, int ti)
{
    SmpsTrack *t = trk(s, ti);

    if (t->f.mod_envelope == 0) {
        return;
    }
    if (t->f.mod_env_state != 0) {
        return;
    }
    send_fm_freq_a(s, ti);
}

/* DoNoteStop: three ticks before the next note, release -- unless the
 * next thing in the stream is a hold flag. */
static void do_note_stop(Smps *s, int ti)
{
    SmpsTrack *t = trk(s, ti);

    if (track_word(t, 0x0A) != 3) {
        return;
    }
    if (image_u8(s, track_ptr(t)) == 0xE6) {
        return;
    }
    do_note_off_a(s, ti);
}

/* ------------------------------------------------------------------ */
/* Pan animation                                                       */

/* loc_B9D56. The chord loop reloads the register number every pass but
 * not the value, so all the parts are sent the parent's pan byte. */
static void send_pan(Smps *s, int ti)
{
    int count = trk(s, ti)->f.chord_count;
    u8 value = trk(s, ti)->f.pan_ams_fms;

    if (count == 0) {
        write_fm_i_or_ii(s, ti, 0xB4, value);
        return;
    }
    for (int i = 0; i < count; i++) {
        int pt = part_track(s, ti, i);
        if (pt >= 0) {
            write_fm_i_or_ii(s, pt, 0xB4, value);
        }
    }
}

/* (build 111) The pan animation's starts and steps, each a body of its own
 * where the ROM's jump tables share one (StartPAJmpTbl, ExecPAJmpTbl): one
 * case a line. The ROM's own entries name no type; these are named for what
 * they do to the note's pan. */

/* Type 3: the note starts on the left alone. */
static void pan_start_left_only(SmpsTrack *t)
{
    t->f.pan_anim_timeout = t->f.pan_anim_reset;
    t->f.pan_ams_fms &= 0xBF;
}

/* Types 1, 4, 5 and 6: the note starts on the right alone. */
static void pan_start_right_only(SmpsTrack *t)
{
    t->f.pan_anim_timeout = t->f.pan_anim_reset;
    t->f.pan_ams_fms &= 0x7F;
}

/* `cmpi.b #$C0` then `bpl`: the test is the sign of the 8-bit difference,
 * not an unsigned comparison -- true while the note is not yet on both
 * speakers. */
static bool pan_not_both(const SmpsTrack *t)
{
    return ((u8)(t->f.pan_ams_fms - 0xC0) & 0x80) != 0;
}

/* Types 3 and 4: once the count runs out, both speakers. Whether to send. */
static bool pan_settle_both(SmpsTrack *t)
{
    if (!pan_not_both(t)) {
        return false;
    }
    t->f.pan_anim_timeout--;
    if (t->f.pan_anim_timeout != 0) {
        return false;
    }
    t->f.pan_ams_fms |= 0xC0;
    return true;
}

/* Types 1, 5 and 6: each time the count runs out, $40 more -- right alone,
 * then left alone, then both. Whether to send. */
static bool pan_step_across(SmpsTrack *t)
{
    if (!pan_not_both(t)) {
        return false;
    }
    t->f.pan_anim_timeout--;
    if (t->f.pan_anim_timeout != 0) {
        return false;
    }
    t->f.pan_anim_timeout = t->f.pan_anim_reset;
    t->f.pan_ams_fms = (u8)(t->f.pan_ams_fms + 0x40);
    return true;
}

static void start_pan_anim(Smps *s, int ti)
{
    static const u8 pan_data[4] = { 0x80, 0xC0, 0x40, 0xC0 };
    SmpsTrack *t = trk(s, ti);

    if (t->f.flags & SMPS_F_HOLD) {
        return;
    }

    switch (t->f.pan_anim_type) {
    case 0:
        return;
    case 2:
        if (t->f.pan_anim_timeout != 0) {
            t->f.pan_anim_timeout--;
            return;
        }
        t->f.pan_anim_timeout = (u8)(t->f.pan_anim_reset - 1);
        t->f.pan_ams_fms = (u8)((t->f.pan_ams_fms & 0x1F)
                                | pan_data[t->f.pan_anim_index & 3]);
        if (t->f.pan_anim_index == 3) {
            t->f.pan_anim_index = 0;
        } else {
            t->f.pan_anim_index++;
        }
        break;
    case 3:
        pan_start_left_only(t);
        break;
    case 1:
        pan_start_right_only(t);
        break;
    case 4:
        pan_start_right_only(t);
        break;
    case 5:
        pan_start_right_only(t);
        break;
    case 6:
        pan_start_right_only(t);
        break;
    default:
        return;
    }
    send_pan(s, ti);
}

static void exec_pan_anim(Smps *s, int ti)
{
    SmpsTrack *t = trk(s, ti);
    bool send = false;

    switch (t->f.pan_anim_type) {
    case 3:
        send = pan_settle_both(t);
        break;
    case 4:
        send = pan_settle_both(t);
        break;
    case 1:
        send = pan_step_across(t);
        break;
    case 5:
        send = pan_step_across(t);
        break;
    case 6:
        send = pan_step_across(t);
        break;
    default:
        /* 0: no animation; 2 moves only at a note's start. */
        break;
    }
    if (send) {
        send_pan(s, ti);
    }
}

/* ------------------------------------------------------------------ */
/* Coordination flags                                                  */

static void restore_bgm_chn(Smps *s, int ti);
static void stop_sfx_tracks(Smps *s);
static void play_music_at(Smps *s, u8 index);
static void silence_all(Smps *s);

/* cfF2_TrkEnd (and cfE1 and the pause flag, which fall into it): the track
 * off and held, its note released, and an effect's channel given back to
 * the music. Unwinds the note loop: always true. */
static bool cf_track_end(Smps *s, int ti)
{
    SmpsTrack *t = trk(s, ti);

    s->track_timer = 0x80;
    t->f.flags = 0;
    s->current_sfx_priority = 0;
    do_note_off_a(s, ti);
    t->f.flags |= SMPS_F_HOLD;
    if (s->music_or_sfx != 0) {
        restore_bgm_chn(s, ti);
    }
    return true;
}

/* (build 129c) cfE8_MusPause's two walks, as the ROM has them (ps2.asm,
 * cfE8_MusPause). Pausing walks nine tracks from the second music track
 * (music 1-5, the three effects, the special one), turning down each in use
 * and not lent to an effect (255 less its volume); then the special track
 * lent and turned down, and the flag falls into cfF2: the pause track ends.
 * But the walk's test for an unused track branches into the *unpause*
 * walk's step (loc_BA0FA), so from the first unused track on the rest of
 * the nine are turned back UP to their volumes, the special track is given
 * back instead of lent, and the flag returns without ending the track --
 * the next byte ends it. Unpausing walks the five music tracks after the
 * first, turning each in use and not lent back up, and gives the special
 * track back. Before 129c the port skipped an unused track and went on
 * turning down: the Mega Drive's pause leaves the later tracks sounding
 * when a song has a gap in its tracks. Returns whether the track ended. */
static bool mus_pause(Smps *s, int ti, bool pausing)
{
    int  i     = 1;
    int  count = pausing ? 9 : 5;
    bool down  = pausing;

    /* (build 132) The port's own, not the ROM's. A music part lent to an
     * effect (the pause jingle lends FM4 and FM5) is silent on the Mega
     * Drive: its channel is the effect's, and its notes never reach the
     * chip. VOICES EXTRA plays such a part on voices of its own -- the
     * music chip, Q's voices -- and the walk below skips it, as the ROM's
     * does, so a note it struck just before the pause rang on through it
     * (Shure's chord pad on FM4, -35 dBFS 90 ticks in). Keyed off here;
     * the part is lent, so the write goes to the hooks alone (write_fm1_main
     * leaves the chip out): the chip's writes, LIMITED and the ROM's order
     * are as before. The part's next strike after the pause sounds again. */
    if (pausing) {
        for (int m = 0; m < SMPS_MUSIC_TRACKS; m++) {
            SmpsTrack *lent = trk(s, m);

            if ((lent->f.flags & SMPS_F_OVERRIDDEN)
                && !(lent->f.channel & 0x80)) {
                fm_note_off(s, m);
            }
        }
    }

    for (; count > 0; count--, i++) {
        SmpsTrack *o = trk(s, i);

        if (!(o->f.flags & SMPS_F_IN_USE)) {
            down = false;               /* loc_BA0FA: the unpause walk's step */
            continue;
        }
        if (o->f.flags & SMPS_F_OVERRIDDEN) {
            continue;
        }
        if (down) {
            refresh_volume(s, i, (u8)(0xFF - o->f.volume));
            s->quiet = (u16)(s->quiet | (1u << i));   /* (132c) */
        } else {
            refresh_volume(s, i, o->f.volume);
        }
        pause_voices(s, i, down);       /* (184) its chord voices with it */
    }

    SmpsTrack *sp = trk(s, SMPS_TRACKS - 1);
    if (!down) {
        sp->f.flags &= (u8)~SMPS_F_OVERRIDDEN;
        return false;                   /* rts: the stream goes on */
    }
    if (sp->f.flags & SMPS_F_IN_USE) {
        sp->f.flags |= SMPS_F_OVERRIDDEN;
        refresh_volume(s, SMPS_TRACKS - 1, (u8)(0xFF - sp->f.volume));
        s->quiet = (u16)(s->quiet | (1u << (SMPS_TRACKS - 1)));
        pause_voices(s, SMPS_TRACKS - 1, true);
    }
    return cf_track_end(s, ti);         /* bra.w cfF2_TrkEnd */
}

/* Returns true when the flag unwound the note loop (cfF2 and friends).
 * `cursor` is the stream position, `ti` the track. */
static bool cf_handler(Smps *s, int ti, u8 *d5, u32 *cursor)
{
    SmpsTrack *t = trk(s, ti);
    u8 flag = *d5;
    bool ended = false;

    *d5 = (u8)((flag - 0xE0) * 4);

    switch (flag) {
    case 0xE0: {                      /* cfE0_PlayBGM */
        u8 arg = image_u8(s, (*cursor)++);

        if (arg != 0) {
            trk(s, SMPS_TRACKS - 1)->f.flags &= (u8)~SMPS_F_OVERRIDDEN;
            for (int i = 0; i < SMPS_MUSIC_TRACKS; i++) {
                memset(trk(s, i)->raw, 0, TRACK_STRIDE);
            }
            silence_fm(s);
            music_parts_end(s);         /* (184) */
            if (s->coord_flag_e0 >= 0x81) {
                play_music_at(s, (u8)(s->coord_flag_e0 - 0x81));
            }
        } else {
            silence_fm(s);
            music_parts_end(s);         /* (184) */
            for (int i = 0; i < SMPS_MUSIC_TRACKS; i++) {
                trk(s, i)->f.flags = 0;
                trk(s, i)->f.channel = 0;
            }
            trk(s, SMPS_TRACKS - 1)->f.flags |= SMPS_F_OVERRIDDEN;
        }
        break;
    }

    case 0xE1:                        /* cfE1_TrkEnd2, falls into cfF2 */
        ended = cf_track_end(s, ti);
        break;

    case 0xF2:                        /* cfF2_TrkEnd */
        ended = cf_track_end(s, ti);
        break;

    case 0xE2:                        /* cfE2_PlaySnd */
        s->sound_queue[0] = image_u8(s, (*cursor)++);
        break;

    case 0xE3:                        /* cfE3_PanAnim */
        t->f.pan_anim_type = image_u8(s, (*cursor)++);
        t->f.pan_anim_reset = image_u8(s, (*cursor)++);
        break;

    case 0xE4:                        /* cfEF_SetTempo, despite the name */
        s->init_tempo_value = image_u8(s, *cursor);
        s->tempo_timeout = image_u8(s, (*cursor)++);
        break;

    case 0xE5: {                      /* cfE5_ChordMode */
        u8 count = image_u8(s, (*cursor)++);

        t->f.chord_count = count;
        if (count != 0) {
            give_parts(s, ti, count);           /* (184) */
            for (int i = 1; i < count; i++) {
                int pt = part_track(s, ti, i);
                if (pt < 0) {
                    continue;
                }
                trk(s, pt)->f.transpose = t->f.transpose;
                trk(s, pt)->f.mod_envelope = t->f.mod_envelope;
                trk(s, pt)->f.volume = t->f.volume;
            }
        }
        break;
    }

    case 0xE6:                        /* cfE6_HoldOn */
        if (image_u8(s, *cursor) < 0x80) {
            t->f.flags |= SMPS_F_HOLD;
        }
        break;

    case 0xE7:                        /* cfE7_ChordStop */
        if (image_u8(s, *cursor) < 0x80) {
            do_note_off_a(s, ti);
            t->f.flags |= SMPS_F_HOLD;
        }
        break;

    case 0xE8: {                      /* cfE8_MusPause */
        u8 arg = image_u8(s, (*cursor)++);

        s->paused_mode = arg;
        ended = mus_pause(s, ti, arg != 0);
        break;
    }

    case 0xE9:                        /* cfE9_SetComm */
        s->communication_byte = image_u8(s, (*cursor)++);
        break;

    case 0xEA:                        /* cfEA_SetFMS */
        /* `and.b` where SMPS has `or.b`: the flag can only clear bits,
         * so setting an FMS depth here never takes effect. */
        t->f.pan_ams_fms = (u8)((t->f.pan_ams_fms & 0xF0)
                                & image_u8(s, (*cursor)++));
        write_fm_i_or_ii(s, ti, 0xB4, t->f.pan_ams_fms);
        break;

    case 0xEB:                        /* cfEB_SetAMS, same inversion */
        t->f.pan_ams_fms = (u8)((t->f.pan_ams_fms & 0xC3)
                                & image_u8(s, (*cursor)++));
        write_fm_i_or_ii(s, ti, 0xB4, t->f.pan_ams_fms);
        break;

    case 0xEC:                        /* cfEC_ChangeVol */
        t->f.volume = (u8)(t->f.volume + image_u8(s, (*cursor)++));
        refresh_volume(s, ti, t->f.volume);
        break;

    case 0xED:                        /* cfED_unused */
        break;

    case 0xEE:                        /* cfEE_HoldOnOff */
        if (image_u8(s, (*cursor)++) != 0) {
            t->f.flags |= SMPS_F_HOLD;
        } else {
            t->f.flags &= (u8)~SMPS_F_HOLD;
        }
        break;

    case 0xEF: {                      /* cfEF_SetFMIns */
        u8 index = image_u8(s, (*cursor)++);
        u32 ins = read_ptr(s, root(s, ROOT_FM_INS), index);
        int count = t->f.chord_count;

        t->f.instrument = index;
        if (count == 0) {
            set_fm_ins(s, ti, ins, index);
            break;
        }
        for (int i = 0; i < count; i++) {
            int pt = part_track(s, ti, i);
            if (pt < 0) {
                continue;
            }
            trk(s, pt)->f.instrument = t->f.instrument;
            trk(s, pt)->f.volume = t->f.volume;
            set_fm_ins(s, pt, ins, index);
        }
        break;
    }

    case 0xF0:                        /* cfF0_SetVol */
        t->f.volume = image_u8(s, (*cursor)++);
        refresh_volume(s, ti, t->f.volume);
        break;

    case 0xF1:                        /* cfF1_null */
        break;

    case 0xF3:                        /* cfF3_NoiseMode */
        /* `andi.b` where SMPS has `ori.b`, so the $E0 that selects the
         * noise register is masked off rather than added. */
        write_psg(s, (u8)(image_u8(s, (*cursor)++) & 0xE0));
        break;

    case 0xF4:                        /* cfF4_ModType */
        t->f.mod_envelope = image_u8(s, (*cursor)++);
        break;

    case 0xF5:                        /* cfF5_SetPSGIns */
        t->f.instrument = image_u8(s, (*cursor)++);
        break;

    case 0xF6: {                      /* cfF6_GoTo */
        u16 disp = image_u8(s, (*cursor)++);

        disp = (u16)(disp | (u16)(image_u8(s, *cursor) << 8));
        *cursor = (u32)((u16)(*cursor + disp));
        break;
    }

    case 0xF7: {                      /* cfF7_Loop */
        u8 slot = image_u8(s, (*cursor)++);
        u8 count = image_u8(s, (*cursor)++);
        u8 *counter = track_byte(s, ti, 0x22 + slot);

        if (*counter == 0) {
            *counter = count;
        }
        (*counter)--;
        if (*counter != 0) {
            u16 disp = image_u8(s, (*cursor)++);

            disp = (u16)(disp | (u16)(image_u8(s, *cursor) << 8));
            *cursor = (u32)((u16)(*cursor + disp));
        } else {
            *cursor += 2;
        }
        break;
    }

    case 0xF8: {                      /* cfF8_GoSub */
        u8 sp = (u8)(t->f.stack - 4);
        u16 disp;

        t->f.stack = sp;
        for (int i = 0; i < 4; i++) {
            *track_byte(s, ti, sp + i) = (u8)(*cursor >> (24 - 8 * i));
        }
        disp = image_u8(s, (*cursor)++);
        disp = (u16)(disp | (u16)(image_u8(s, *cursor) << 8));
        *cursor = (u32)((u16)(*cursor + disp));
        break;
    }

    case 0xF9: {                      /* cfF9_Return */
        u8 sp = t->f.stack;
        u32 saved = 0;

        for (int i = 0; i < 4; i++) {
            saved = (saved << 8) | *track_byte(s, ti, sp + i);
        }
        *cursor = saved + 2;
        t->f.stack = (u8)(sp + 4);
        break;
    }

    case 0xFA:                        /* cfFA_TickMult */
        t->f.tick_multiplier = image_u8(s, (*cursor)++);
        break;

    case 0xFB:                        /* cfFB_ChgTransp */
        t->f.transpose = (u8)(t->f.transpose + image_u8(s, (*cursor)++));
        break;

    case 0xFC:                        /* cfFC_PitchSlide */
        if (image_u8(s, (*cursor)++) != 0) {
            t->f.flags |= SMPS_F_PITCH_SLIDE;
        } else {
            t->f.flags &= (u8)~SMPS_F_PITCH_SLIDE;
        }
        break;

    case 0xFD:                        /* cfFD_RawFrqMode */
        if (image_u8(s, (*cursor)++) != 0) {
            t->f.flags |= SMPS_F_RAW_FREQ;
        } else {
            t->f.flags &= (u8)~SMPS_F_RAW_FREQ;
        }
        break;

    case 0xFE: {                      /* cfFE_FM3SpcMode */
        static const u16 fm3_freqs[4] = { 0, 0x132, 0x18E, 0x1E4 };

        t->f.flags |= SMPS_F_FM3_SPECIAL;
        for (int i = 0; i < 4; i++) {
            s->special_fm3_notes[i] = fm3_freqs[image_u8(s, (*cursor)++) & 3];
        }
        break;
    }

    case 0xFF:                        /* cfFF_Pan */
        t->f.pan_ams_fms = (u8)((t->f.pan_ams_fms & 0x3F)
                                | image_u8(s, (*cursor)++));
        write_fm1_main(s, ti, 0xB4, t->f.pan_ams_fms);
        break;

    default:
        break;
    }

    if (!ended) {
        *d5 = image_u8(s, (*cursor)++);
    }
    return ended;
}

/* Reads coordination flags until a note or duration byte turns up. */
static bool read_flags(Smps *s, int ti, u8 *d5, u32 *cursor)
{
    while (*d5 >= 0xE0) {
        if (cf_handler(s, ti, d5, cursor)) {
            return true;
        }
    }
    return false;
}

/* ------------------------------------------------------------------ */
/* Stream parsing                                                      */

/* TickMultiplier: a duration byte is multiplied by the track's tick
 * multiplier and stored as the note length. */
static void tick_multiplier(Smps *s, int ti, u8 d5)
{
    SmpsTrack *t = trk(s, ti);

    set_track_word(t, 0x0E, (u16)(t->f.tick_multiplier * (u16)d5));
}

/* FinishTrkUpdate: arm the timeout from the note length, save the
 * cursor, and reset the modulation envelope on every chord part. */
static void finish_trk_update(Smps *s, int ti, u32 cursor)
{
    SmpsTrack *t = trk(s, ti);
    int count;

    set_track_word(t, 0x0A, track_word(t, 0x0E));
    set_track_ptr(t, (u16)cursor);

    if (t->f.flags & SMPS_F_HOLD) {
        return;
    }
    count = t->f.chord_count;
    if (count == 0) {
        t->f.mod_env_state = 0;
        t->f.mod_env_mult = 0;
        return;
    }
    for (int i = 0; i < count; i++) {
        int pt = part_track(s, ti, i);
        if (pt >= 0) {
            trk(s, pt)->f.mod_env_state = 0;
            trk(s, pt)->f.mod_env_mult = 0;
        }
    }
}

/* ProcFMTrack and its three variants (ps2.asm:91087-91248).
 *
 * The one thing worth naming here: a duration byte where a note would
 * be expected is not a rest and not an error. `loc_B9A40` tests the
 * byte before calling GetFrequency_A, and if it is under $80 it skips
 * the frequency read entirely -- the track keeps the frequency it
 * already had, takes the new duration, and because the caller runs
 * DoNoteOff_A before and DoNoteOn_A after, the same pitch is keyed off
 * and on again. It is a repeated note, written without repeating the
 * note byte. A rest is $80, which sets the hold bit instead.
 *
 * The mirror case is two note bytes in a row: the second is pushed
 * back and the first plays for whatever length the previous note had. */
static void proc_fm_track(Smps *s, int ti)
{
    SmpsTrack *t = trk(s, ti);
    u32 cursor;
    u8 d5;
    bool slide;
    bool raw;

    /* The four entry points differ only here. ProcFMTrack clears the
     * hold bit; the other three release the note instead. */
    if ((t->f.flags & (SMPS_F_RAW_FREQ | SMPS_F_PITCH_SLIDE)) == 0) {
        t->f.flags &= (u8)~SMPS_F_HOLD;
    } else {
        do_note_off_a(s, ti);
    }

    cursor = track_ptr(t);
    d5 = image_u8(s, cursor++);
    if (read_flags(s, ti, &d5, &cursor)) {
        return;
    }

    /* A flag may have turned either mode on or off since the entry
     * point was chosen, and each variant re-tests bit 5 here for
     * exactly that reason. */
    slide = (t->f.flags & SMPS_F_PITCH_SLIDE) != 0;
    raw = (t->f.flags & SMPS_F_RAW_FREQ) != 0;

    if (raw) {
        read_raw_freq(s, ti, d5, &cursor);
        if (slide) {
            t->f.slide_speed = image_u8(s, cursor++);
        }
        d5 = image_u8(s, cursor++);
        tick_multiplier(s, ti, d5);
        finish_trk_update(s, ti, cursor);
        return;
    }

    if (slide) {
        get_frequency_a(s, ti, d5, &cursor);
        cursor++;                               /* the dummy byte */
        t->f.slide_speed = image_u8(s, cursor++);
        d5 = image_u8(s, cursor++);
        if (!(d5 & 0x80)) {
            tick_multiplier(s, ti, d5);
        }
        finish_trk_update(s, ti, cursor);
        return;
    }

    /* loc_B9A40, the ordinary path. */
    do_note_off_a(s, ti);

    if (!(d5 & 0x80)) {
        tick_multiplier(s, ti, d5);             /* same pitch, new length */
        finish_trk_update(s, ti, cursor);
        return;
    }

    get_frequency_a(s, ti, d5, &cursor);
    d5 = image_u8(s, cursor++);
    if (d5 & 0x80) {
        cursor--;                               /* unget: keep the length */
        finish_trk_update(s, ti, cursor);
        return;
    }
    tick_multiplier(s, ti, d5);
    finish_trk_update(s, ti, cursor);
}

/* UpdateDrumTrack (ps2.asm:91012). The drum byte picks the speaker --
 * the high tom left, the low tom right, everything else both (drums.h) --
 * and is then handed to the Z80, which owns sample playback. */
static void update_drum_track(Smps *s, int ti)
{
    SmpsTrack *t = trk(s, ti);
    u32 cursor;
    u8 d5;
    u8 drum;

    set_track_word(t, 0x0A, (u16)(track_word(t, 0x0A) - 1));
    if (track_word(t, 0x0A) != 0) {
        return;
    }

    cursor = track_ptr(t);
    d5 = image_u8(s, cursor++);
    if (read_flags(s, ti, &d5, &cursor)) {
        return;
    }

    if (d5 & 0x80) {
        t->f.freq_hi = d5;
        d5 = image_u8(s, cursor++);
        if (d5 & 0x80) {
            cursor--;
        } else {
            tick_multiplier(s, ti, d5);
        }
    } else {
        tick_multiplier(s, ti, d5);
    }
    finish_trk_update(s, ti, cursor);

    drum = t->f.freq_hi;
    if (drum == DRUM_HIGH_TOM) {
        hook(s, ti, 1, 0xB6, 0x80, true);
        write_fm_ii(s, 0xB6, 0x80);
    } else if (drum == DRUM_LOW_TOM) {
        hook(s, ti, 1, 0xB6, 0x40, true);
        write_fm_ii(s, 0xB6, 0x40);
    } else {
        hook(s, ti, 1, 0xB6, 0xC0, true);
        write_fm_ii(s, 0xB6, 0xC0);
    }
    if (s->sink.dac_write != NULL) {
        s->sink.dac_write(s->sink.ctx, drum);
    }
}

static void update_fm_track(Smps *s, int ti)
{
    SmpsTrack *t = trk(s, ti);
    bool slide = (t->f.flags & SMPS_F_PITCH_SLIDE) != 0;

    set_track_word(t, 0x0A, (u16)(track_word(t, 0x0A) - 1));
    if (track_word(t, 0x0A) == 0) {
        proc_fm_track(s, ti);
        send_fm_freq_a(s, ti);
        do_note_on_a(s, ti);
        start_pan_anim(s, ti);
        return;
    }

    exec_pan_anim(s, ti);
    do_note_stop(s, ti);
    if (slide) {
        do_pitch_slide(s, ti);
        send_fm_freq_a(s, ti);
    } else {
        exec_mod_env(s, ti);
    }
}

static void update_track(Smps *s, int ti)
{
    if (trk(s, ti)->f.channel & 0x80) {
        update_drum_track(s, ti);
        return;
    }
    update_fm_track(s, ti);
}

static void update_tracks(Smps *s)
{
    s->music_or_sfx = 0;
    for (int i = 0; i < SMPS_MUSIC_TRACKS; i++) {
        if (trk(s, i)->f.flags & SMPS_F_IN_USE) {
            update_track(s, i);
        }
    }

    s->music_or_sfx = 0x80;
    for (int i = 0; i < SMPS_SFX_TRACKS; i++) {
        int ti = SMPS_MUSIC_TRACKS + i;

        if (trk(s, ti)->f.flags & SMPS_F_IN_USE) {
            update_track(s, ti);
        }
    }
}

/* ------------------------------------------------------------------ */
/* Tempo, fading, channel handover                                     */

/* DoTempo: not a divider but a stall. Every `init_tempo_value` ticks,
 * one tick is given back to every music track, which slows the song by
 * that fraction. */
static void do_tempo(Smps *s)
{
    if (s->init_tempo_value == 0) {
        return;
    }
    s->tempo_timeout--;
    if (s->tempo_timeout != 0) {
        return;
    }
    s->tempo_timeout = s->init_tempo_value;
    for (int i = 0; i < SMPS_MUSIC_TRACKS; i++) {
        SmpsTrack *t = trk(s, i);

        set_track_word(t, 0x0A, (u16)(track_word(t, 0x0A) + 1));
    }
}

static void do_fading(Smps *s)
{
    if (s->fade_out_remain == 0) {
        return;
    }
    if (s->fade_timeout != 0) {
        s->fade_timeout--;
        return;
    }
    s->fade_out_remain--;
    if (s->fade_out_remain == 0) {
        silence_all(s);
        return;
    }
    s->fade_timeout = 4;
    for (int i = 1; i < SMPS_MUSIC_TRACKS; i++) {
        SmpsTrack *t = trk(s, i);

        t->f.volume++;
        refresh_volume(s, i, t->f.volume);
    }
    /* (build 184) The music's chord voices step with the music's tracks,
     * as the game's chords' parts on tracks 4 and 5 do. */
    for (int v = 0; v < s->data.chord_music && v < SMPS_CHORD_VOICES_MAX;
         v++) {
        if (!s->voice_taken[v]) {
            continue;
        }
        SmpsTrack *t = trk(s, SMPS_TRACKS + v);
        t->f.volume++;
        refresh_volume(s, SMPS_TRACKS + v, t->f.volume);
    }
}

/* FadeOutMusic (ps2.asm:92206): forty steps of DoFading, the drum track
 * off, and the chord's voice -- FM3's track's algorithm and four levels --
 * copied to the chord's other parts, whose tracks DoFading's RefreshVolume
 * steps one by one.
 *
 * (build 183) As the ROM copies it, one track too far. The loop makes as
 * many copies as the chord has parts (d6 = the count less 1, then dbf), and
 * starts at the track after FM3's (music_tracks + $C0), where the chord's
 * parts start at FM3's own (SendFMFreq_A): so the last copy lands on the
 * track past the chord. Every song's chord is three parts, and the track
 * past it is the first effect track, FM3's. An effect sounding there as the
 * fade starts has its own algorithm and levels written over, and its next
 * volume changes (cfEC, cfF0) write the music's levels, on the music's
 * carriers, until its next instrument or its end; an effect that starts
 * later clears its track first (PlaySFX). Once a game: the explosion at
 * Gaira's control panel, 22 frames before the fade under Mystery -- its end
 * quieter under the song's first chord voice, left ringing under its second
 * (docs/audio.md, "The fade-out's overrun"; routes fade_out_overrun and
 * fade_out_overrun_second_chord). Until 183 the port stopped at the chord's
 * last part. The bytes go through track_byte, as the other writes that can
 * leave a track's block do: a count that runs past the tracks lands in the
 * RAM after them, as on the hardware, and no further.
 * (build 184) Copy i goes to the chord's part i + 1 where it has one (the
 * game's: tracks 4 and 5, as before; a mod's longer chord: its voices too),
 * else to the ROM's track 4 + i -- the overrun, one copy past the chord. */
static void fade_out_music(Smps *s)
{
    SmpsTrack *lead = trk(s, 3);
    int count;

    s->fade_out_remain = 0x28;
    s->fade_timeout = 4;
    trk(s, 0)->f.flags = 0;

    count = lead->f.chord_count;
    for (int i = 0; i < count; i++) {
        int pt = part_track(s, 3, i + 1);

        if (pt >= 0 && is_voice(pt)) {
            SmpsTrack *v = trk(s, pt);
            v->f.algorithm = lead->f.algorithm;
            v->f.tl_op1    = lead->f.tl_op1;
            v->raw[0x27]   = lead->raw[0x27];
            v->f.tl_op3    = lead->f.tl_op3;
            v->f.tl_op4    = lead->f.tl_op4;
            continue;
        }
        int ti = (pt >= 0) ? pt : 4 + i;

        *track_byte(s, ti, 0x20) = lead->f.algorithm;
        *track_byte(s, ti, 0x1C) = lead->f.tl_op1;
        *track_byte(s, ti, 0x27) = lead->raw[0x27];
        *track_byte(s, ti, 0x1E) = lead->f.tl_op3;
        *track_byte(s, ti, 0x1F) = lead->f.tl_op4;
    }
}

/* RestoreBGMChn: an SFX track sits $90 past the music track it stole,
 * so giving the channel back means reloading that track's instrument.
 * (build 143) The hooks get the upload bracketed (smps.h,
 * SMPS_HOOK_RESTORE); the chip's writes are as before. */
static void restore_bgm_chn(Smps *s, int ti)
{
    int bgm = ti - 3;
    SmpsTrack *t;

    if (bgm < 0) {
        return;
    }
    t = trk(s, bgm);
    t->f.flags &= (u8)~SMPS_F_OVERRIDDEN;
    hook(s, bgm, 0, SMPS_HOOK_RESTORE, 1, true);
    set_fm_ins(s, bgm, read_ptr(s, root(s, ROOT_FM_INS), t->f.instrument),
               t->f.instrument);
    hook(s, bgm, 0, SMPS_HOOK_RESTORE, 0, true);
}

static void silence_all(Smps *s)
{
    u8 saved_e0 = s->coord_flag_e0;
    u8 saved_timer = s->track_timer;

    write_fm_i_driver(s, 0x2B, 0x80);

    memset(s->tracks, 0, sizeof s->tracks);
    /* (184) every chord's parts given back, the voices silenced */
    silence_voices(s, 0, s->data.chord_music + s->data.chord_effects);
    s->music_tracks = 0;
    memset(s->chord_part, SMPS_CHORD_NONE, sizeof s->chord_part);
    memset(s->chord_parts, 0, sizeof s->chord_parts);
    memset(s->part_taken, 0, sizeof s->part_taken);
    memset(s->voice_taken, 0, sizeof s->voice_taken);
    memset(s->voices, 0, sizeof s->voices);
    s->tempo_timeout = 0;
    s->init_tempo_value = 0;
    s->play_sound_id = 0;
    memset(s->sound_queue, 0, sizeof s->sound_queue);
    s->current_sfx_priority = 0;
    memset(s->special_fm3_notes, 0, sizeof s->special_fm3_notes);
    s->paused_mode = 0;
    s->quiet       = 0;                /* (132c) a new song: none down */
    s->communication_byte = 0;
    s->fade_out_remain = 0;
    s->fade_timeout = 0;

    s->coord_flag_e0 = saved_e0;
    s->track_timer = saved_timer;

    silence_fm(s);
    silence_psg(s);
}

/* loc_BA656 and StopSFX: the first clears the three SFX tracks that
 * PlaySFX is about to reuse, the second also releases the special
 * track. The special track reaches its music track by subtracting $90
 * twice, once at the call site and once inside RestoreBGMChn. */
static void stop_sfx_tracks(Smps *s)
{
    for (int i = 0; i < 3; i++) {
        int ti = SMPS_MUSIC_TRACKS + i;

        if (!(trk(s, ti)->f.flags & SMPS_F_IN_USE)) {
            continue;
        }
        trk(s, ti)->f.flags = 0;
        do_note_off_a(s, ti);
        restore_bgm_chn(s, ti);
    }
}

static void stop_sfx(Smps *s)
{
    int special = SMPS_TRACKS - 1;

    stop_sfx_tracks(s);

    if (trk(s, special)->f.flags == 0 && trk(s, special)->f.channel == 0) {
        return;
    }
    trk(s, special)->f.flags = 0;
    do_note_off_a(s, special);
    restore_bgm_chn(s, special - 3);
}

/* ------------------------------------------------------------------ */
/* Starting sounds                                                     */

/* Which music track an SFX channel id displaces. Index is the channel
 * id minus 2; the SFX itself runs in the track $90 further on. */
static const int sfx_host[5] = { 3, 3, 4, 5, 6 };

static void play_sfx(Smps *s, u8 index)
{
    u32 header = read_ptr(s, root(s, ROOT_SFX), index);
    int count;

    s->track_timer = 0;
    stop_sfx_tracks(s);

    count = image_u8(s, header++);
    for (int n = 0; n < count; n++) {
        u32 h = header + (u32)n * 9;
        u8 flags = image_u8(s, h);
        u8 channel = image_u8(s, h + 1);
        int host;
        int ti;

        if (channel < 2 || channel > 6) {
            continue;
        }
        host = sfx_host[channel - 2];
        ti = host + 3;

        trk(s, host)->f.flags |= SMPS_F_OVERRIDDEN;
        release_parts(s, ti);           /* (184) the last effect's chord's */
        memset(trk(s, ti)->raw, 0, TRACK_STRIDE);
        hook_on(s, ti, channel, 0, SMPS_HOOK_CLAIM, channel, true);
        set_max_rel_rate_channel(s, ti, flags, channel);

        for (int b = 0; b < 9; b++) {
            trk(s, ti)->raw[b] = image_u8(s, h + (u32)b);
        }
        set_track_word(trk(s, ti), 0x0A, 1);
    }
}

static void play_spc_sfx(Smps *s, u8 index)
{
    u32 header = read_ptr(s, s->data.spc_sfx_ptrs, index);
    int ti = SMPS_TRACKS - 1;

    header++;   /* the track count, which this path ignores */
    for (int b = 0; b < 9; b++) {
        trk(s, ti)->raw[b] = image_u8(s, header + (u32)b);
    }
    set_track_word(trk(s, ti), 0x0A, 1);
    trk(s, 3)->f.flags |= SMPS_F_OVERRIDDEN;
    hook(s, ti, 0, SMPS_HOOK_CLAIM, trk(s, ti)->f.channel, true);
}

/* PlayMusic_JmpIn: tempo, then nine header bytes per channel into
 * consecutive track blocks. Tracks 4, 5 and the first SFX track get
 * their channel ids written unconditionally, so a five-channel song
 * still leaves FM4-FM6 addressable by sound effects. */
static void play_music_at(Smps *s, u8 index)
{
    u32 tempo_table = s->pal ? PAL_TEMPO : root(s, ROOT_TEMPO);
    u32 header;
    int count;

    s->init_tempo_value = image_u8(s, tempo_table + index);
    s->tempo_timeout = image_u8(s, tempo_table + index);
    s->pal_tick_mult = image_u8(s, PAL_TICK_MULT + index);

    header = read_ptr(s, root(s, ROOT_MUSIC), index);
    count = image_u8(s, header++);

    /* (build 184) the tracks a chord's parts may take are the ones after */
    for (int ti = 0; ti < SMPS_MUSIC_TRACKS; ti++) {
        release_parts(s, ti);
    }
    s->music_tracks = (u8)((count < SMPS_MUSIC_TRACKS) ? count
                                                      : SMPS_MUSIC_TRACKS);

    for (int n = 0; n < count && n < SMPS_MUSIC_TRACKS; n++) {
        SmpsTrack *t = trk(s, n);

        for (int b = 0; b < 9; b++) {
            t->raw[b] = image_u8(s, header + (u32)n * 9 + (u32)b);
        }
        set_track_word(t, 0x0A, 1);
        t->f.stack = 0x30;
    }

    trk(s, 4)->f.channel = 4;
    trk(s, 5)->f.channel = 5;
    trk(s, SMPS_MUSIC_TRACKS)->f.channel = 6;

    if (s->pal) {
        for (int i = 0; i <= SMPS_MUSIC_TRACKS; i++) {
            trk(s, i)->f.tick_multiplier = s->pal_tick_mult;
        }
    }
}

/* PlaySoundID: the id decides everything. */
static void play_sound_id(Smps *s)
{
    u8 id = s->play_sound_id;

    if (!(id & 0x80)) {
        silence_all(s);
    } else if (id < 0xA0) {
        if (id >= 0x81) {
            silence_all(s);
            play_music_at(s, (u8)(id - 0x81));
            s->music_started = id;      /* (build 139f) */
        }
    } else if (id < 0xD8) {
        play_sfx(s, (u8)(id - 0xA0));
        s->sfx_started = id;            /* (build 161) */
        s->sfx_starts++;
    } else if (id < 0xE0) {
        play_spc_sfx(s, (u8)(id - 0xD8));
    } else if (id == 0xE0) {
        fade_out_music(s);
    } else if (id == 0xE1) {
        stop_sfx(s);
    } else {
        silence_all(s);
    }
    s->play_sound_id = 0x80;
}

/* DoSoundQueue: three slots, resolved by the priority table. A negative
 * priority plays unconditionally; otherwise it has to be at least as
 * high as whatever is already sounding.
 *
 * The $D8-and-up path clears the slot without advancing to the next
 * one, so the loop re-reads the slot it just emptied and the two slots
 * behind it are left for the following frame. */
static void do_sound_queue(Smps *s)
{
    int slot = 0;
    u8 priority = 0;

    for (int i = 0; i < 3; i++) {
        u8 id = s->sound_queue[slot];

        if (id >= 0xD8) {
            s->sound_queue[slot] = 0;
            s->play_sound_id = id;
            if (priority & 0x01) {
                s->coord_flag_e0 = id;
            }
            continue;
        }

        s->sound_queue[slot] = 0;
        slot++;
        if (id < 0x81) {
            continue;
        }

        priority = image_u8(s, root(s, ROOT_PRIORITIES) + (u32)(id - 0x81));
        if (priority & 0x80) {
            s->play_sound_id = id;
            if (priority & 0x01) {
                s->coord_flag_e0 = id;
            }
            continue;
        }
        if (priority < s->current_sfx_priority) {
            continue;
        }
        s->current_sfx_priority = priority;
        s->play_sound_id = id;
    }
}

/* ------------------------------------------------------------------ */
/* Entry points                                                        */

static void driver_frame(Smps *s)
{
    if (s->paused_mode != 0) {
        return;
    }
    do_sound_queue(s);
    play_sound_id(s);
    do_tempo(s);
    do_fading(s);
    update_tracks(s);
}

void smps_take_queue(Smps *s)
{
    if (s->paused_mode != 0) {
        return;
    }
    do_sound_queue(s);
    play_sound_id(s);
}

void smps_tick(Smps *s)
{
    /* SoundDriverInput: on PAL the driver runs twice every fifth field
     * to make up the 50 Hz shortfall. */
    if (!s->pal) {
        driver_frame(s);
        return;
    }
    if (s->pal_divider != 0) {
        s->pal_divider--;
        driver_frame(s);
        return;
    }
    s->pal_divider = 4;
    driver_frame(s);
    driver_frame(s);
}

/* Every one of the ROM's eighty writes to the queue is
 * `move.b #id, (sound_queue).w`: slot 0, overwritten. Nothing ever
 * writes slots 1 and 2, so DoSoundQueue's three-slot scan only ever
 * finds one sound, and two ids queued in the same field keep the
 * last. Filling the first empty slot instead let both play. */
void smps_play(Smps *s, u8 sound_id)
{
    s->sound_queue[0] = sound_id;
}

void smps_init(Smps *s, const SmpsData *data, const SmpsSink *sink)
{
    memset(s, 0, sizeof *s);
    s->data = *data;
    s->sink = *sink;
    s->play_sound_id = 0x80;
}

u32 smps_music_offset(const Smps *s, int index)
{
    return read_ptr(s, root(s, ROOT_MUSIC), (u32)index);
}

u16 smps_sfx_tracks(const Smps *s, u8 id)
{
    u16 mask = 0;

    if (id < 0xA0 || id >= 0xD8 || s->data.image == NULL) {
        return 0;
    }
    /* PlaySFX's walk of the header, nothing written */
    u32 header = read_ptr(s, root(s, ROOT_SFX), (u32)(id - 0xA0));
    int count = image_u8(s, header++);
    for (int n = 0; n < count; n++) {
        u8 channel = image_u8(s, header + (u32)n * 9 + 1);
        if (channel < 2 || channel > 6) {
            continue;
        }
        mask |= (u16)(1u << (sfx_host[channel - 2] + 3));
    }
    return mask;
}

u16 smps_stuck_tracks(const Smps *s)
{
    u16 mask = 0;

    for (int m = 0; m < SMPS_MUSIC_TRACKS; m++) {
        bool held = false;

        if (!(s->tracks[m].f.flags & SMPS_F_OVERRIDDEN)) {
            continue;
        }
        if (m >= 3 && (s->tracks[m + 3].f.flags & SMPS_F_IN_USE)) {
            held = true;                /* PlaySFX's lender, $90 past it */
        }
        if (m == 3 && (s->tracks[SMPS_TRACKS - 1].f.flags & SMPS_F_IN_USE)) {
            held = true;                /* PlaySpcSFX's special track */
        }
        if (!held) {
            mask = (u16)(mask | (1u << m));
        }
    }
    /* (build 132c) In the high byte, the music tracks a pause turned down
     * that nothing turned back up, once the driver runs again: in use and
     * holding their channel, so they play on at the pause's level. */
    if (s->paused_mode == 0) {
        for (int m = 0; m < SMPS_MUSIC_TRACKS; m++) {
            const SmpsTrack *t = &s->tracks[m];

            if ((s->quiet & (1u << m)) && (t->f.flags & SMPS_F_IN_USE)
                && !(t->f.flags & SMPS_F_OVERRIDDEN)) {
                mask = (u16)(mask | (1u << (m + 8)));
            }
        }
    }
    return mask;
}

/* ------------------------------------------------------------------ */
/* Loading                                                             */

static Ps2Reader g_reader;          /* (build 125) smps_set_reader */

void smps_set_reader(Ps2Reader reader)
{
    g_reader = reader;
}

static u8 *read_file(const char *path, long *len)
{
    u8 *buf;
    long size;

    /* (build 125) The reader's bytes, copied: smps_load frees what it
     * gets. */
    if (g_reader != NULL) {
        size_t n = 0;
        const u8 *data = g_reader(path, &n);
        if (data != NULL) {
            buf = malloc(n > 0 ? n : 1);
            if (buf == NULL) {
                return NULL;
            }
            memcpy(buf, data, n);
            *len = (long)n;
            return buf;
        }
    }

    FILE *fh = fopen(path, "rb");
    if (fh == NULL) {
        return NULL;
    }
    fseek(fh, 0, SEEK_END);
    size = ftell(fh);
    fseek(fh, 0, SEEK_SET);

    buf = malloc((size_t)size);
    if (buf == NULL || fread(buf, 1, (size_t)size, fh) != (size_t)size) {
        free(buf);
        fclose(fh);
        return NULL;
    }
    fclose(fh);
    *len = size;
    return buf;
}

/* One field of a frequency entry: a whole number 0..top, or -1. */
static long freq_field(const JsonValue *e, const char *key, long top)
{
    const JsonValue *v = json_get(e, key);
    if (v == NULL || v->type != JSON_NUMBER || v->number < 0.0
        || v->number > (double)top || v->number != (double)(long)v->number) {
        return -1;
    }
    return (long)v->number;
}

/* (build 136c) FMFreqs from music/note_frequencies.json: "entries", each
 * {"block", "fnum"} to the word block << 11 | fnum ("note", "hz" and
 * "cents" are for reading). GetFMFreq reads entries 0-63, so fewer than
 * SMPS_FREQS_MIN is refused, as is more than SMPS_FREQS_MAX, a block past
 * 7 or an F-number past 2047, naming the entry. */
static bool load_freqs(SmpsData *d, const char *path)
{
    long len = 0;
    u8 *raw = read_file(path, &len);
    char *text = (raw != NULL) ? malloc((size_t)len + 1) : NULL;

    if (text == NULL) {
        free(raw);
        fprintf(stderr, "smps: cannot read %s\n", path);
        return false;
    }
    memcpy(text, raw, (size_t)len);
    text[len] = 0;
    free(raw);

    size_t where = 0;
    JsonValue *root = json_parse(text, &where);
    free(text);
    if (root == NULL) {
        fprintf(stderr, "smps: %s is not JSON (at byte %zu)\n", path, where);
        return false;
    }

    const JsonValue *entries = json_get(root, "entries");
    int  n  = json_length(entries);
    bool ok = entries != NULL && entries->type == JSON_ARRAY
           && n >= SMPS_FREQS_MIN && n <= SMPS_FREQS_MAX;
    if (!ok) {
        fprintf(stderr, "smps: %s: \"entries\" holds %d frequencies, not "
                        "%d-%d\n", path, n, SMPS_FREQS_MIN, SMPS_FREQS_MAX);
    }
    d->freqs = ok ? malloc(sizeof(u16) * (size_t)n) : NULL;
    ok = ok && d->freqs != NULL;
    for (int i = 0; ok && i < n; i++) {
        const JsonValue *e = json_at(entries, i);
        long block = freq_field(e, "block", 7);
        long fnum  = freq_field(e, "fnum", 0x7FF);
        if (block < 0 || fnum < 0) {
            fprintf(stderr, "smps: %s: entry %d needs a block 0-7 and an "
                            "fnum 0-2047\n", path, i);
            ok = false;
        } else {
            d->freqs[i] = (u16)((block << 11) | fnum);
        }
    }

    d->freq_count = ok ? n : 0;
    if (!ok) {
        free(d->freqs);
        d->freqs = NULL;
    }
    json_free(root);
    return ok;
}

/* ------------------------------------------------------------------ */
/* (build 184) The chord voices the music can need                     */

/* Every stream is walked as the sequencer reads it -- proc_fm_track and
 * update_drum_track's bytes, cf_handler's flags and their arguments, both
 * ways at a loop, into a gosub and back -- for the largest chord (cfE5) it
 * can set. A state is where an event starts, the chord in force, the two
 * modes and the gosub stack; one seen before ends that way. */
#define WALK_DEPTH   4             /* gosubs deep: the stack's room */
#define WALK_EVENTS  400000        /* a stream's events, then the walk stops */

typedef struct {
    u16 pos;
    u8  chord;
    u8  mode;                      /* SMPS_F_RAW_FREQ | SMPS_F_PITCH_SLIDE */
    u8  depth;
    u16 ret[WALK_DEPTH];
} WalkState;

typedef struct {
    u64       *seen;               /* open addressing, 0 empty */
    u32        cap, used;
    WalkState *todo;
    int        ntodo, todo_cap;
    int        largest;
} Walk;

static u8 data_u8(const SmpsData *d, u32 off)
{
    return (off < d->size) ? d->image[off] : 0;
}

static u32 data_ptr(const SmpsData *d, u32 table, u32 index)
{
    u32 at = table + index * 2;
    return (u32)data_u8(d, at) | ((u32)data_u8(d, at + 1) << 8);
}

static u64 walk_key(const WalkState *w)
{
    u64 top = (w->depth > 0) ? w->ret[w->depth - 1] : 0;
    return (1ull << 63) | ((u64)top << 32) | ((u64)w->depth << 29)
         | ((u64)(w->mode & 0x28) << 21) | ((u64)w->chord << 16) | w->pos;
}

/* True when the state is new (and now seen). */
static bool walk_see(Walk *wk, const WalkState *w)
{
    if (wk->used * 2 >= wk->cap) {
        u32  cap  = wk->cap ? wk->cap * 2 : 1024;
        u64 *seen = calloc(cap, sizeof *seen);
        if (seen == NULL) {
            return false;
        }
        for (u32 i = 0; i < wk->cap; i++) {
            if (wk->seen[i] != 0) {
                u32 h = (u32)(wk->seen[i] * 0x9E3779B97F4A7C15ull >> 40) & (cap - 1);
                while (seen[h] != 0) {
                    h = (h + 1) & (cap - 1);
                }
                seen[h] = wk->seen[i];
            }
        }
        free(wk->seen);
        wk->seen = seen;
        wk->cap  = cap;
    }
    u64 key = walk_key(w);
    u32 h   = (u32)(key * 0x9E3779B97F4A7C15ull >> 40) & (wk->cap - 1);
    while (wk->seen[h] != 0) {
        if (wk->seen[h] == key) {
            return false;
        }
        h = (h + 1) & (wk->cap - 1);
    }
    wk->seen[h] = key;
    wk->used++;
    return true;
}

static void walk_push(Walk *wk, const WalkState *w)
{
    if (wk->ntodo == wk->todo_cap) {
        int cap = wk->todo_cap ? wk->todo_cap * 2 : 64;
        WalkState *t = realloc(wk->todo, (size_t)cap * sizeof *t);
        if (t == NULL) {
            return;
        }
        wk->todo     = t;
        wk->todo_cap = cap;
    }
    wk->todo[wk->ntodo++] = *w;
}

/* Each flag's argument bytes, $E0-$FF (cf_handler); the jumps, the gosub,
 * its return and the two ends are taken apart in walk_event. */
static const u8 kFlagArgs[32] = {
    1, 0, 1, 2, 1, 1, 0, 0,     /* E0-E7 */
    1, 1, 1, 1, 1, 0, 1, 1,     /* E8-EF */
    1, 0, 0, 1, 1, 1, 2, 4,     /* F0-F7 */
    2, 0, 1, 1, 1, 1, 4, 1      /* F8-FF */
};

/* One event from w->pos: its flags, then its note or drum and length.
 * False where the stream ends (or the walk cannot follow it). */
static bool walk_event(const SmpsData *d, Walk *wk, WalkState *w, bool drums)
{
    u32 cur   = w->pos;
    int flags = 0;
    u8  d5    = data_u8(d, cur++);

    while (d5 >= 0xE0) {
        if (++flags > 4096) {
            return false;               /* a jump to itself: the ROM hangs */
        }
        u32 at = cur;                   /* the flag's first argument */
        if (d5 == 0xE1 || d5 == 0xF2) {
            return false;               /* the track's end */
        }
        if (d5 == 0xF6) {
            u16 disp = (u16)(data_u8(d, at) | (data_u8(d, at + 1) << 8));
            cur = (u16)(at + 1 + disp);
        } else if (d5 == 0xF7) {
            u16 disp = (u16)(data_u8(d, at + 2) | (data_u8(d, at + 3) << 8));
            WalkState out = *w;
            out.pos = (u16)(at + 4);    /* the loop done: on past it */
            walk_push(wk, &out);
            cur = (u16)(at + 3 + disp);
        } else if (d5 == 0xF8) {
            u16 disp = (u16)(data_u8(d, at) | (data_u8(d, at + 1) << 8));
            if (w->depth >= WALK_DEPTH) {
                return false;
            }
            w->ret[w->depth++] = (u16)at;
            cur = (u16)(at + 1 + disp);
        } else if (d5 == 0xF9) {
            if (w->depth == 0) {
                return false;           /* a return with nothing to return to */
            }
            cur = (u16)(w->ret[--w->depth] + 2);
        } else {
            u8 arg = data_u8(d, at);
            if (d5 == 0xE5) {
                w->chord = arg;
                if (arg > wk->largest) {
                    wk->largest = arg;
                }
            } else if (d5 == 0xFC) {
                w->mode = (u8)((arg != 0) ? (w->mode | SMPS_F_PITCH_SLIDE)
                                          : (w->mode & ~SMPS_F_PITCH_SLIDE));
            } else if (d5 == 0xFD) {
                w->mode = (u8)((arg != 0) ? (w->mode | SMPS_F_RAW_FREQ)
                                          : (w->mode & ~SMPS_F_RAW_FREQ));
            }
            cur = at + kFlagArgs[d5 - 0xE0];
        }
        d5 = data_u8(d, cur++);
    }

    int extra = (w->chord >= 2) ? w->chord - 1 : 0;   /* get_frequency_a */
    if (drums) {
        if (d5 & 0x80) {
            if (data_u8(d, cur++) & 0x80) {
                cur--;
            }
        }
    } else if (w->mode & SMPS_F_RAW_FREQ) {
        cur += (w->mode & SMPS_F_PITCH_SLIDE) ? 3 : 2;  /* low, speed, length */
    } else if (w->mode & SMPS_F_PITCH_SLIDE) {
        cur += ((d5 != 0x80) ? (u32)extra : 0) + 3;     /* dummy, speed, length */
    } else if (d5 & 0x80) {
        cur += (d5 != 0x80) ? (u32)extra : 0;
        if (data_u8(d, cur++) & 0x80) {
            cur--;
        }
    }
    w->pos = (u16)cur;
    return true;
}

/* The largest chord a track whose header is at `h` can set. */
static int walk_track(const SmpsData *d, u32 h)
{
    Walk      wk;
    WalkState w;
    bool      drums = (data_u8(d, h + 1) & 0x80) != 0;
    long      events = 0;

    memset(&wk, 0, sizeof wk);
    memset(&w, 0, sizeof w);
    w.pos  = (u16)(data_u8(d, h + 3) | (data_u8(d, h + 4) << 8));
    w.mode = (u8)(data_u8(d, h) & (SMPS_F_RAW_FREQ | SMPS_F_PITCH_SLIDE));
    walk_push(&wk, &w);
    while (wk.ntodo > 0 && events < WALK_EVENTS) {
        w = wk.todo[--wk.ntodo];
        while (events++ < WALK_EVENTS && walk_see(&wk, &w)
               && walk_event(d, &wk, &w, drums)) {
        }
    }
    free(wk.seen);
    free(wk.todo);
    return wk.largest;
}

/* A chord of `largest` notes: the parts it takes, at most SMPS_CHORD_PARTS. */
static int chord_parts_of(int largest)
{
    int parts = largest - 1;
    return (parts < 0) ? 0 : (parts > SMPS_CHORD_PARTS) ? SMPS_CHORD_PARTS
                                                         : parts;
}

/* Songs: the parts its tracks take, less the music tracks its header leaves
 * out (every one is after every lead); the most of any song. Effects: each
 * effect track's most (PlaySFX's tracks by channel, PlaySpcSFX's the last),
 * summed -- a track keeps its parts until PlaySFX clears it. */
static void count_chord_voices(SmpsData *d)
{
    int music = 0;
    int on_track[SMPS_SFX_TRACKS] = { 0 };

    for (int i = 0; i < d->music_count; i++) {
        u32 header = data_ptr(d, data_ptr(d, 0, ROOT_MUSIC / 2), (u32)i);
        int count  = data_u8(d, header++);
        int tracks = (count < SMPS_MUSIC_TRACKS) ? count : SMPS_MUSIC_TRACKS;
        int parts  = 0;

        for (int n = 0; n < tracks; n++) {
            parts += chord_parts_of(walk_track(d, header + (u32)n * 9));
        }
        int need = parts - (SMPS_MUSIC_TRACKS - tracks);
        if (need > music) {
            music = need;
        }
    }
    for (int i = 0; i < d->sfx_count; i++) {
        u32 header = data_ptr(d, data_ptr(d, 0, ROOT_SFX / 2), (u32)i);
        int count  = data_u8(d, header++);

        for (int n = 0; n < count; n++) {
            u32 h       = header + (u32)n * 9;
            u8  channel = data_u8(d, h + 1);
            if (channel < 2 || channel > 6) {
                continue;
            }
            int slot  = sfx_host[channel - 2] + 3 - SMPS_MUSIC_TRACKS;
            int parts = chord_parts_of(walk_track(d, h));
            if (parts > on_track[slot]) {
                on_track[slot] = parts;
            }
        }
    }
    for (int i = 0; i < d->spc_count; i++) {
        u32 h     = data_ptr(d, d->spc_sfx_ptrs, (u32)i) + 1;
        int parts = chord_parts_of(walk_track(d, h));
        if (parts > on_track[SMPS_SFX_TRACKS - 1]) {
            on_track[SMPS_SFX_TRACKS - 1] = parts;
        }
    }
    d->chord_music   = music;
    d->chord_effects = 0;
    for (int k = 0; k < SMPS_SFX_TRACKS; k++) {
        d->chord_effects += on_track[k];
    }
}

/* (build 137a) The frequencies, then the image from the song text: the
 * driver's data as text (songtext.h), SoundData.bin's "PS2S" image until
 * then. (build 184) Then the chord voices it can need. */
bool smps_load(SmpsData *d, const char *music_dir)
{
    char path[512];

    memset(d, 0, sizeof *d);
    snprintf(path, sizeof path, "%s/note_frequencies.json", music_dir);
    if (!load_freqs(d, path)) {
        return false;
    }
    if (!songtext_build(music_dir, g_reader, d)) {
        smps_free(d);
        return false;
    }
    count_chord_voices(d);
    return true;
}

void smps_free(SmpsData *d)
{
    free(d->image);
    free(d->freqs);
    d->image = NULL;
    d->freqs = NULL;
}

/* (build 112) smps.h. The same reads as cfEF_SetFMIns's (SndDrv_ReadPtr on
 * FMInsPtrs, the root at $06), on the image rather than a player. */
bool smps_instrument(const SmpsData *d, int index, u8 out[26])
{
    if (d == NULL || d->image == NULL || index < 0
        || index >= SMPS_INSTRUMENTS || d->size < 8) {
        return false;
    }
    u32 table = (u32)d->image[6] | ((u32)d->image[7] << 8);
    u32 at    = table + (u32)index * 2;
    if (at + 1 >= d->size) {
        return false;
    }
    u32 ins = (u32)d->image[at] | ((u32)d->image[at + 1] << 8);
    if (ins + 26 > d->size) {
        return false;
    }
    memcpy(out, d->image + ins, 26);
    return true;
}
