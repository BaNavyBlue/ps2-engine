/* Diagnostic for the SMPS68k sequencer.
 *
 * Runs the driver at the tick rate against the extracted sound data and
 * prints what comes out, without a synthesiser. The point is to check
 * that the note streams parse and that the timing is right before any
 * of it has to sound correct: a piano roll of the real game data is
 * readable, and a wrong duration or a mis-parsed coordination flag
 * shows up in it immediately.
 *
 *     make smps_trace
 *     ./smps_trace --list
 *     ./smps_trace --song 0 --frames 600
 *     ./smps_trace --song 0 --frames 120 --regs
 *     ./smps_trace --image FILE        (build 139b) the image, written out
 *     ./smps_trace --chords            (build 184) the chord voices the
 *                                      music can need (smps_load's walk)
 *     ./smps_trace --song 0 --voices   (build 184) with --regs or alone: the
 *                                      chord voices' writes, which the hooks
 *                                      alone carry, as `V<n>` lines
 *     ./smps_trace --play E0@300       (build 184) queue a sound id (hex) at
 *                                      a frame: a fade, an effect; repeatable
 *     ./smps_trace --resume 700        (build 184) clear the pause at a frame,
 *                                      as the game does (audio_music_resume):
 *                                      a paused sequencer reads no queue, so
 *                                      `--play D6@700` (the unpause) wants it
 */

#include "../src/audio/smps.h"
#include "../src/audio/drums.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_EVENTS 64

typedef struct {
    Smps *smps;
    bool  log_regs;
    bool  log_voices;       /* (build 184) */
    int   frame;

    /* Key-on and key-off events seen during the current tick. */
    u8    key[MAX_EVENTS];
    int   keys;
    u8    dac[MAX_EVENTS];
    int   dacs;
} Trace;

static const char *NOTE_NAMES[12] = {
    "C-", "C#", "D-", "D#", "E-", "F-", "F#", "G-", "G#", "A-", "A#", "B-"
};

static void trace_fm(void *ctx, int part, u8 reg, u8 value)
{
    Trace *tr = ctx;

    if (tr->log_regs) {
        printf("  %5d  FM%c %02X <- %02X\n", tr->frame,
               part == 0 ? 'I' : '2', reg, value);
    }
    if (reg == 0x28 && tr->keys < MAX_EVENTS) {
        tr->key[tr->keys++] = value;
    }
}

static void trace_dac(void *ctx, u8 drum)
{
    Trace *tr = ctx;

    if (tr->log_regs) {
        const char *name = drum_name(drum);
        printf("  %5d  DAC   <- %02X  %s\n", tr->frame, drum,
               name != NULL ? name : "?");
    }
    if (tr->dacs < MAX_EVENTS) {
        tr->dac[tr->dacs++] = drum;
    }
}

static void trace_psg(void *ctx, u8 value)
{
    Trace *tr = ctx;

    if (tr->log_regs) {
        printf("  %5d  PSG   <- %02X\n", tr->frame, value);
    }
}

/* (build 184) The hooks: a chord voice's write, on its chord chip
 * (smps_voice_chip), `V<v>` and the chip's part. The tracks' own are the
 * chip's writes above. */
static void trace_track(void *ctx, int track, u8 channel, int part, u8 reg,
                        u8 value, bool to_chip)
{
    Trace *tr = ctx;

    (void)channel;
    (void)to_chip;
    if (tr->log_voices && track >= SMPS_TRACKS && reg >= 0x28) {
        printf("  %5d  V%-3d %c %02X <- %02X\n", tr->frame, track - SMPS_TRACKS,
               part == 0 ? 'I' : '2', reg, value);
    }
}

/* The frequency word is an index into FMFreqs until a modulation
 * envelope bends it, so an exact match names the note and anything else
 * prints as the raw word. Entry 0 is silence. (build 136c) A name is the
 * nearest concert note (A440): entry 1 is B0 + 26 cents, and every entry
 * sits 25-27 cents above the note it is named for (docs/audio.md;
 * music/note_frequencies.json names each entry the same way). Until 136c
 * entry 1 was called C0, a semitone high and an octave low. */
#define FREQ_ENTRY1_SEMITONE 11     /* B0: 11 semitones above C0 */

static const char *note_name(const SmpsData *d, u16 freq, char *buf)
{
    for (int i = 1; i < d->freq_count; i++) {
        if (d->freqs[i] == freq) {
            int semi = FREQ_ENTRY1_SEMITONE + (i - 1);
            snprintf(buf, 8, "%s%d", NOTE_NAMES[semi % 12], (semi / 12) % 100);
            return buf;
        }
    }
    if (freq == 0) {
        return "  . ";
    }
    snprintf(buf, 8, "%04X", freq);
    return buf;
}

static int track_for_channel(const Smps *s, u8 channel)
{
    for (int i = 0; i < SMPS_TRACKS; i++) {
        if ((s->tracks[i].f.flags & SMPS_F_IN_USE)
            && s->tracks[i].f.channel == channel) {
            return i;
        }
    }
    return -1;
}

static void print_roll(Trace *tr, const u8 *channels, int nchan)
{
    const Smps *s = tr->smps;
    char cell[8];
    bool any = tr->dacs > 0;

    for (int i = 0; i < tr->keys; i++) {
        if ((tr->key[i] & 0xF0) != 0) {
            any = true;
        }
    }
    if (!any) {
        return;
    }

    printf("%6d ", tr->frame);
    for (int c = 0; c < nchan; c++) {
        u8 channel = channels[c];
        int ti = track_for_channel(s, channel);
        bool keyed = false;

        if (channel == 0x80) {
            if (tr->dacs > 0) {
                printf("| $%02X  ", tr->dac[tr->dacs - 1]);
            } else {
                printf("|      ");
            }
            continue;
        }
        for (int i = 0; i < tr->keys; i++) {
            if ((tr->key[i] & 0x0F) == channel && (tr->key[i] & 0xF0) != 0) {
                keyed = true;
            }
        }
        if (!keyed || ti < 0) {
            printf("|      ");
            continue;
        }
        printf("| %-5s", note_name(&s->data, (u16)((s->tracks[ti].f.freq_hi << 8)
                                                   | s->tracks[ti].f.freq_lo),
                                   cell));
    }
    printf("\n");
}

static void list_songs(Smps *s)
{
    for (int i = 0; i < s->data.music_count; i++) {
        u32 header = smps_music_offset(s, i);
        int count = s->data.image[header];

        printf("  %2d  id %02X  offset %04X  %d tracks:", i, 0x81 + i,
               header, count);
        for (int n = 0; n < count; n++) {
            u8 channel = s->data.image[header + 1 + n * 9 + 1];

            if (channel == 0x80) {
                printf(" DAC");
            } else {
                printf(" FM%d", channel < 4 ? channel + 1 : channel);
            }
        }
        printf("\n");
    }
}

int main(int argc, char **argv)
{
    static Smps smps;
    SmpsData data;
    Trace tr;
    SmpsSink sink;
    const char *dir = "assets/music";
    int song = 0;
    int frames = 600;
    bool list = false;
    bool chords = false;
    const char *image_out = NULL;
    int  play_id[16];
    int  play_at[16];
    int  plays = 0;
    int  resume_at = -1;
    u8 channels[SMPS_MUSIC_TRACKS];
    int nchan = 0;

    memset(&tr, 0, sizeof tr);

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--song") == 0 && i + 1 < argc) {
            song = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--frames") == 0 && i + 1 < argc) {
            frames = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--regs") == 0) {
            tr.log_regs = true;
        } else if (strcmp(argv[i], "--list") == 0) {
            list = true;
        } else if (strcmp(argv[i], "--chords") == 0) {
            chords = true;
        } else if (strcmp(argv[i], "--voices") == 0) {
            tr.log_voices = true;
        } else if (strcmp(argv[i], "--play") == 0 && i + 1 < argc
                   && plays < 16) {
            unsigned id = 0;
            int at = 0;
            if (sscanf(argv[++i], "%x@%d", &id, &at) != 2 || id > 0xFF) {
                fprintf(stderr, "--play wants ID@FRAME, the id in hex\n");
                return 1;
            }
            play_id[plays] = (int)id;
            play_at[plays] = at;
            plays++;
        } else if (strcmp(argv[i], "--resume") == 0 && i + 1 < argc) {
            resume_at = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--assets") == 0 && i + 1 < argc) {
            dir = argv[++i];
        } else if (strcmp(argv[i], "--image") == 0 && i + 1 < argc) {
            image_out = argv[++i];
        } else {
            fprintf(stderr, "usage: %s [--song N] [--frames N] [--regs]"
                            " [--voices] [--play ID@FRAME] [--resume FRAME]"
                            " [--list]"
                            " [--chords] [--assets DIR] [--image FILE]\n",
                    argv[0]);
            return 1;
        }
    }

    if (!smps_load(&data, dir)) {
        fprintf(stderr, "could not build the sound image from %s (the song "
                        "text)\n", dir);
        fprintf(stderr, "run: python3 tools/extract_music.py ps2.asm"
                        " -o assets/music\n");
        return 1;
    }

    /* (build 139b) The image the song text built -- with a song's .trk read
     * in place of its stream text -- to be held against tools/tracker.py's
     * byte for byte. */
    if (image_out != NULL) {
        FILE *fh = fopen(image_out, "wb");
        bool ok = fh != NULL
               && fwrite(data.image, 1, data.size, fh) == data.size;
        if (fh != NULL && fclose(fh) != 0) {
            ok = false;
        }
        if (!ok) {
            fprintf(stderr, "cannot write %s\n", image_out);
            smps_free(&data);
            return 1;
        }
        printf("%u bytes -> %s\n", (unsigned)data.size, image_out);
        smps_free(&data);
        return 0;
    }

    sink.fm_write = trace_fm;
    sink.dac_write = trace_dac;
    sink.psg_write = trace_psg;
    sink.ctx = &tr;
    sink.track_write = tr.log_voices ? trace_track : NULL;

    smps_init(&smps, &data, &sink);
    tr.smps = &smps;

    if (chords) {
        printf("chord voices: music %d, effects %d\n", data.chord_music,
               data.chord_effects);
        smps_free(&data);
        return 0;
    }
    if (list) {
        list_songs(&smps);
        smps_free(&data);
        return 0;
    }

    if (song < 0 || song >= data.music_count) {
        fprintf(stderr, "song %d out of range (0..%d)\n",
                song, data.music_count - 1);
        smps_free(&data);
        return 1;
    }

    smps_play(&smps, (u8)(0x81 + song));

    /* The first tick is where the queue resolves and the song starts,
     * so it carries the opening chord and has to be captured like any
     * other -- but the channel map only exists once it has run. */
    tr.frame = 1;
    smps_tick(&smps);

    for (int i = 0; i < SMPS_MUSIC_TRACKS; i++) {
        if (smps.tracks[i].f.flags & SMPS_F_IN_USE) {
            channels[nchan++] = smps.tracks[i].f.channel;
        }
    }

    if (!tr.log_regs) {
        printf("song %d (id %02X), %d channels, tempo %02X\n\n",
               song, 0x81 + song, nchan, smps.init_tempo_value);
        printf("  tick ");
        for (int c = 0; c < nchan; c++) {
            if (channels[c] == 0x80) {
                printf("| DAC  ");
            } else {
                printf("| FM%-3d",
                       channels[c] < 4 ? channels[c] + 1 : channels[c]);
            }
        }
        printf("\n");
    }

    if (!tr.log_regs) {
        print_roll(&tr, channels, nchan);
    }

    for (tr.frame = 2; tr.frame <= frames; tr.frame++) {
        tr.keys = 0;
        tr.dacs = 0;
        if (tr.frame == resume_at) {
            smps.paused_mode = 0;
        }
        for (int k = 0; k < plays; k++) {
            if (play_at[k] == tr.frame) {
                smps_play(&smps, (u8)play_id[k]);
            }
        }
        smps_tick(&smps);
        if (!tr.log_regs) {
            print_roll(&tr, channels, nchan);
        }
    }

    smps_free(&data);
    return 0;
}
