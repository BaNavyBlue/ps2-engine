/* Plays real Phantasy Star II FM instruments through Nuked-OPN2 and
 * writes a stereo WAV, so the first sound out of the chip is a game voice
 * rather than something invented.
 *
 *   make fm_demo && ./fm_demo fm_demo.wav [first_instrument]
 *   ./fm_demo pan.wav smoke     # a sine centre, then left, then right
 */

#include "../src/audio/fm.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RATE  48000

static unsigned frames_written;

static void render(FILE *fh, Fm *fm, double seconds)
{
    int total = (int)(seconds * RATE);

    while (total > 0) {
        int n = (total > 256) ? 256 : total;
        float buf[256 * 2];         /* left, right */

        memset(buf, 0, sizeof buf);
        fm_render(fm, buf, n, 1.0f);

        for (int i = 0; i < n * 2; i++) {
            float s = buf[i];
            if (s > 1.0f) {
                s = 1.0f;
            }
            if (s < -1.0f) {
                s = -1.0f;
            }
            int v = (int)(s * 30000.0f);
            unsigned char le[2] = {
                (unsigned char)(v & 0xFF), (unsigned char)((v >> 8) & 0xFF)
            };
            fwrite(le, 1, 2, fh);
        }
        frames_written += (unsigned)n;
        total -= n;
    }
}

/* Smoke test: drive the chip by hand, bypassing the write queue and the
 * instrument bank entirely. A plain sine on operator 4 at full volume,
 * played three times: both speakers, left only, right only ($B4 bits 7
 * and 6), which is also the quickest check that the output is stereo.
 *
 * This separates two failure modes that look identical in the output. If
 * this produces a tone, the chip is alive and the clocking is right, so
 * anything still broken is in the queue or the instrument upload. If it
 * is silent too, the problem is lower down -- the clock count per sample,
 * or how the output is read. */
static void smoke_test(FILE *fh, Fm *fm)
{
    static const struct { u8 reg, val; } setup[] = {
        { 0x22, 0x00 },   /* LFO off */
        { 0x27, 0x00 },   /* channel 3 normal mode */
        { 0x28, 0x00 },   /* all keys off */
        { 0x2B, 0x00 },   /* DAC off */
        { 0xB0, 0x07 },   /* algorithm 7: all four operators audible */
        { 0xB4, 0xC0 },   /* both speakers */
        { 0x30, 0x01 }, { 0x34, 0x01 }, { 0x38, 0x01 }, { 0x3C, 0x01 },
        { 0x40, 0x7F }, { 0x44, 0x7F }, { 0x48, 0x7F }, { 0x4C, 0x00 },
        { 0x50, 0x1F }, { 0x54, 0x1F }, { 0x58, 0x1F }, { 0x5C, 0x1F },
        { 0x60, 0x00 }, { 0x64, 0x00 }, { 0x68, 0x00 }, { 0x6C, 0x00 },
        { 0x70, 0x00 }, { 0x74, 0x00 }, { 0x78, 0x00 }, { 0x7C, 0x00 },
        { 0x80, 0x0F }, { 0x84, 0x0F }, { 0x88, 0x0F }, { 0x8C, 0x0F },
        { 0xA4, 0x22 },   /* block 4, fnum high */
        { 0xA0, 0x69 },   /* fnum low -- around 440 Hz */
    };

    for (size_t i = 0; i < sizeof setup / sizeof setup[0]; i++) {
        fm_write(fm, 0, setup[i].reg, setup[i].val);
    }
    while (fm_pending_writes(fm) > 0) {
        render(fh, fm, 0.01);
    }

    static const struct { u8 pan; const char *name; } pans[] = {
        { 0xC0, "both" }, { 0x80, "left" }, { 0x40, "right" },
    };
    for (size_t i = 0; i < sizeof pans / sizeof pans[0]; i++) {
        printf("  %s\n", pans[i].name);
        fm_write(fm, 0, 0xB4, pans[i].pan);
        while (fm_pending_writes(fm) > 0) {
            render(fh, fm, 0.01);
        }
        fm_write(fm, 0, 0x28, 0xF0);      /* key on, channel 1 */
        render(fh, fm, 1.0);
        fm_write(fm, 0, 0x28, 0x00);      /* key off */
        render(fh, fm, 0.4);
    }
}

int main(int argc, char **argv)
{
    const char *path = (argc > 1) ? argv[1] : "fm_demo.wav";
    int first = (argc > 2) ? atoi(argv[2]) : 0;
    bool smoke = (argc > 2) && strcmp(argv[2], "smoke") == 0;

    Fm fm;
    if (!fm_init(&fm, RATE)) {
        return 1;
    }
    if (!fm_load_instruments(&fm, "assets/music/fm_instruments.json")) {
        fm_shutdown(&fm);
        return 1;
    }
    printf("%d instruments loaded\n", fm.instrument_count);

    FILE *fh = fopen(path, "wb");
    if (fh == NULL) {
        fm_shutdown(&fm);
        return 1;
    }

    unsigned char header[44] = { 0 };
    memcpy(header, "RIFF", 4);
    memcpy(header + 8, "WAVEfmt ", 8);
    header[16] = 16;
    header[20] = 1;
    header[22] = 2;                           /* stereo */
    header[24] = (unsigned char)(RATE & 0xFF);
    header[25] = (unsigned char)((RATE >> 8) & 0xFF);
    header[26] = (unsigned char)((RATE >> 16) & 0xFF);
    header[28] = (unsigned char)((RATE * 4) & 0xFF);          /* bytes/s */
    header[29] = (unsigned char)(((RATE * 4) >> 8) & 0xFF);
    header[30] = (unsigned char)(((RATE * 4) >> 16) & 0xFF);
    header[32] = 4;                           /* block align */
    header[34] = 16;
    memcpy(header + 36, "data", 4);
    fwrite(header, 1, sizeof header, fh);

    if (smoke) {
        printf("smoke test: single sine on channel 1\n");
        smoke_test(fh, &fm);
    }

    /* Eight instruments, an octave each, so the bank can be surveyed. */
    const double scale[4] = { 261.6, 329.6, 392.0, 523.3 };

    for (int n = 0; !smoke && n < 8; n++) {
        int ins = first + n;
        if (ins >= fm.instrument_count) {
            break;
        }
        printf("  instrument %d\n", ins);

        fm_set_instrument(&fm, 0, ins);

        /* Each write takes two samples to go out, so wait for the queue
         * rather than guessing at a delay. */
        while (fm_pending_writes(&fm) > 0) {
            render(fh, &fm, 0.01);
        }

        for (int s = 0; s < 4; s++) {
            fm_note_on(&fm, 0, scale[s]);
            render(fh, &fm, 0.35);
            fm_note_off(&fm, 0);
            render(fh, &fm, 0.12);
            while (fm_pending_writes(&fm) > 0) {
                render(fh, &fm, 0.01);
            }
        }
    }

    unsigned data_bytes = frames_written * 4;
    unsigned riff = 36 + data_bytes;
    fseek(fh, 4, SEEK_SET);
    fwrite(&riff, 4, 1, fh);
    fseek(fh, 40, SEEK_SET);
    fwrite(&data_bytes, 4, 1, fh);
    fclose(fh);

    printf("%s: %.2f s, stereo\n", path, (double)frames_written / RATE);
    fm_shutdown(&fm);
    return 0;
}
