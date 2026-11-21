/* Diagnostic: the real game, headless, through a pause and back, with
 * every chip write the sequencer emits and the sequencer's state at the
 * interesting moments. Needs no synthesiser.
 *
 *     make pause_probe
 *     ./pause_probe                       # store, Start at 400 and 700
 *     ./pause_probe 900 1300 1800 5,13,5,2,57,18 200:b,260:b,320:b
 *
 * Arguments, all optional: the tick Start is pressed on, the tick it is
 * pressed again, how many ticks to run, a PS2_BUILDING spec (or "map"
 * to stay wherever game_init leaves you), and a PS2_PAD-style script
 * of one-frame presses (a, b, c, up, down, left, right). The example
 * leaves the weapon store with B and pauses on the Paseo map.
 *
 * Writes are printed for the 40 ticks after each Start and for any
 * tick in between where something is emitted, which while paused should
 * be nothing. Frequency writes ($A0-$A6) are dropped from the listing
 * unless --freq is given, because vibrato repeats them every tick. */
#include "../src/core/assetfs.h"
#include "../src/core/video.h"
#include "../src/core/audio.h"
#include "../src/game/game.h"
#include "../src/platform/platform.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool g_freq;

static void drain(Audio *a, int tick, bool print)
{
    unsigned tail = atomic_load(&a->tail);
    unsigned head = atomic_load(&a->head);

    while (tail != head) {
        const AudioCmd *c = &a->queue[tail % AUDIO_QUEUE];

        if (print) {
            if (c->kind == CMD_FM) {
                bool freq = c->reg >= 0xA0 && c->reg <= 0xA6;

                if (g_freq || !freq) {
                    printf("  %5d  FM%c %02X <- %02X\n", tick,
                           c->channel == 0 ? 'I' : '2', c->reg, c->value);
                }
            } else if (c->kind == CMD_DAC) {
                printf("  %5d  DAC <- %02X\n", tick, c->value);
            } else {
                printf("  %5d  PSG <- %02X\n", tick, c->value);
            }
        }
        tail++;
    }
    atomic_store(&a->tail, tail);
}

static void dump(const Smps *s, int tick, int screen)
{
    printf("--- tick %d, screen %d: paused_mode=%d play_id=%02X prio=%02X "
           "queue=%02X\n", tick, screen, s->paused_mode, s->play_sound_id,
           s->current_sfx_priority, s->sound_queue[0]);
    for (int i = 0; i < SMPS_TRACKS; i++) {
        const SmpsTrack *t = &s->tracks[i];

        if ((t->f.flags & (SMPS_F_IN_USE | SMPS_F_OVERRIDDEN)) == 0
            && t->f.channel == 0) {
            continue;
        }
        printf("    trk%d flags=%02X ch=%02X ptr=%02X%02X to=%02X%02X "
               "frq=%02X%02X vol=%02X ins=%02X stk=%02X mod=%02X\n",
               i, t->f.flags, t->f.channel, t->f.ptr_hi, t->f.ptr_lo,
               t->f.timeout_hi, t->f.timeout_lo, t->f.freq_hi, t->f.freq_lo,
               t->f.volume, t->f.instrument, t->f.stack, t->f.mod_envelope);
    }
}

static void press(Input *in, const char *name)
{
    if (name[0] == 'a') {
        in->held.a = in->pressed.a = true;
    } else if (name[0] == 'b') {
        in->held.b = in->pressed.b = true;
    } else if (name[0] == 'c') {
        in->held.c = in->pressed.c = true;
    } else if (name[0] == 'u') {
        in->held.up = in->pressed.up = true;
    } else if (name[0] == 'd') {
        in->held.down = in->pressed.down = true;
    } else if (name[0] == 'l') {
        in->held.left = in->pressed.left = true;
    } else if (name[0] == 'r') {
        in->held.right = in->pressed.right = true;
    } else if (name[0] == 's') {
        in->held.start = in->pressed.start = true;
    }
}

static void script(Input *in, const char *pad, int tick)
{
    while (pad != NULL && *pad != '\0') {
        int frame = atoi(pad);
        const char *colon = strchr(pad, ':');

        if (colon == NULL) {
            return;
        }
        if (frame == tick) {
            press(in, colon + 1);
        }
        pad = strchr(colon, ',');
        if (pad != NULL) {
            pad++;
        }
    }
}

int main(int argc, char **argv)
{
    static Video video;
    static Game  game;
    static Audio audio;
    int   pause_at   = 400;
    int   unpause_at = 700;
    int   total      = 1000;
    const char *building = "5,13,5,2,57,18";
    const char *pad      = NULL;
    int   pos = 0;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--freq") == 0) {
            g_freq = true;
            continue;
        }
        switch (pos++) {
            case 0: {
                pause_at = atoi(argv[i]);
                break;
            }
            case 1: {
                unpause_at = atoi(argv[i]);
                break;
            }
            case 2: {
                total = atoi(argv[i]);
                break;
            }
            case 3: {
                building = argv[i];
                break;
            }
            case 4: {
                pad = argv[i];
                break;
            }
            default: {
                break;
            }
        }
    }

    assetfs_preload("assets/manifest.txt");
    video_init(&video, NULL);
    if (!game_init(&game, &video)) {
        return 1;
    }
    audio_init(&audio, AUDIO_RATE);
    if (!audio_load_music(&audio, "assets")) {
        return 1;
    }
    game_attach_audio(&game, &audio);

    if (strcmp(building, "map") != 0) {
        long  v[6] = { 0, 0, 0, 0, -1, -1 };
        char *end = (char *)building;

        for (int i = 0; i < 6 && *end != '\0'; i++) {
            v[i] = strtol(end, &end, 0);
            if (*end == ',') {
                end++;
            }
        }
        game_debug_building(&game, (int)v[0], (int)v[1], (int)v[2],
                            (int)v[3], (int)v[4], (int)v[5]);
    }

    for (int tick = 0; tick < total; tick++) {
        Input in;
        bool  window = (tick >= pause_at - 1 && tick <= pause_at + 40)
                    || (tick >= unpause_at - 1 && tick <= unpause_at + 40);

        input_clear(&in);   /* (build 160) no key, no gamepad input */
        if (tick == pause_at || tick == unpause_at) {
            press(&in, "start");
        }
        script(&in, pad, tick);

        if (tick == pause_at - 1 || tick == unpause_at - 1) {
            dump(&audio.smps, tick, game.screen);
        }
        game_tick(&game, &in);

        /* Between the two Starts everything is printed: with the driver
         * paused there should be no lines at all. */
        bool paused = tick > pause_at + 40 && tick < unpause_at - 1;

        drain(&audio, tick, window || paused);
        if (tick == pause_at + 40 || tick == unpause_at + 40
            || tick == total - 1) {
            dump(&audio.smps, tick, game.screen);
        }
    }
    return 0;
}
