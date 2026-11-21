/* Diagnostic for src/core/psg.c: exercises every channel and writes a WAV.
 *
 * This is a test signal, not game data. It exists to confirm the audio
 * path and to make the PSG's behaviour audible -- attenuation steps, the
 * noise modes, and the noise rate slaved to tone channel 2.
 *
 *   cc -std=c11 -I src -o psg_demo tools/psg_demo.c src/core/psg.c -lm
 *   ./psg_demo psg_demo.wav
 */

#include "../src/core/psg.h"

#include <stdio.h>
#include <string.h>

#define RATE    48000
#define CHUNK   256

static void put_u32(FILE *fh, unsigned v)
{
    unsigned char b[4] = {
        (unsigned char)(v), (unsigned char)(v >> 8),
        (unsigned char)(v >> 16), (unsigned char)(v >> 24)
    };
    fwrite(b, 1, 4, fh);
}

static unsigned frames_written;

static void render(FILE *fh, Psg *psg, double seconds)
{
    int total = (int)(seconds * RATE);

    while (total > 0) {
        int n = (total > CHUNK) ? CHUNK : total;
        float buf[CHUNK];

        memset(buf, 0, sizeof buf);
        psg_render(psg, buf, n, 1.0f);

        for (int i = 0; i < n; i++) {
            int v = (int)(buf[i] * 26000.0f);
            unsigned char le[2] = {
                (unsigned char)(v & 0xFF), (unsigned char)((v >> 8) & 0xFF)
            };
            fwrite(le, 1, 2, fh);
        }
        frames_written += (unsigned)n;
        total -= n;
    }
}

static void silence_all(Psg *psg)
{
    for (int c = 0; c < 3; c++) {
        psg_set_tone(psg, c, 0, 15);
    }
    psg_set_noise(psg, 0, 15);
}

int main(int argc, char **argv)
{
    const char *path = (argc > 1) ? argv[1] : "psg_demo.wav";

    FILE *fh = fopen(path, "wb");
    if (fh == NULL) {
        fprintf(stderr, "cannot write %s\n", path);
        return 1;
    }

    unsigned char header[44] = { 0 };
    memcpy(header, "RIFF", 4);
    memcpy(header + 8, "WAVEfmt ", 8);
    header[16] = 16;
    header[20] = 1;
    header[22] = 1;
    header[24] = (unsigned char)(RATE & 0xFF);
    header[25] = (unsigned char)((RATE >> 8) & 0xFF);
    header[26] = (unsigned char)((RATE >> 16) & 0xFF);
    header[32] = 2;
    header[34] = 16;
    memcpy(header + 36, "data", 4);
    fwrite(header, 1, sizeof header, fh);

    Psg psg;
    psg_init(&psg, RATE);

    /* 1. Each tone channel alone, so they can be told apart. */
    const double pitch[3] = { 220.0, 330.0, 440.0 };
    for (int c = 0; c < 3; c++) {
        silence_all(&psg);
        psg_set_tone(&psg, c, psg_period_for_hz(pitch[c]), 2);
        render(fh, &psg, 0.45);
    }

    /* 2. All three together, to check summing and headroom.
     *
     * Deliberately NOT 220/330/440: those are harmonics 2, 3 and 4 of a
     * 110 Hz fundamental, so they fuse into one perceived pitch at 110
     * even though nothing is playing there. That hides exactly what this
     * section is meant to expose. These three have no small-integer
     * relationship and stay separable by ear. Note that transposing the
     * same chord does not help: 233.1/349.2/466.2 is the same
     * fifth-fourth-octave stack in equal temperament and fuses just as
     * readily. The intervals have to change, not the key. */
    const double stack[3] = { 197.0, 283.0, 419.0 };
    silence_all(&psg);
    for (int c = 0; c < 3; c++) {
        psg_set_tone(&psg, c, psg_period_for_hz(stack[c]), 4);
    }
    render(fh, &psg, 0.9);

    /* 3. The 16 attenuation steps, 2 dB apart, loudest to silent. */
    silence_all(&psg);
    for (int a = 0; a < 16; a++) {
        psg_set_tone(&psg, 0, psg_period_for_hz(440.0), (u8)a);
        render(fh, &psg, 0.12);
    }

    /* 4. Noise: the three fixed rates, white then periodic. */
    for (int white = 1; white >= 0; white--) {
        for (int rate = 0; rate < 3; rate++) {
            silence_all(&psg);
            psg_set_noise(&psg, (u8)((white << 2) | rate), 3);
            render(fh, &psg, 0.35);
        }
    }

    /* 5. Noise slaved to tone channel 2 (rate 3), swept -- this is how
     *    the driver gets pitched percussion out of the noise channel. */
    silence_all(&psg);
    psg_set_noise(&psg, 0x07, 3);
    for (int i = 0; i < 40; i++) {
        psg_set_tone(&psg, 2, (u16)(0x10 + i * 12), 15);
        render(fh, &psg, 0.04);
    }

    /* 6. A pitch sweep on one channel, to hear the period quantisation. */
    silence_all(&psg);
    for (int i = 0; i < 60; i++) {
        double hz = 110.0 * (1.0 + (double)i / 20.0);
        psg_set_tone(&psg, 0, psg_period_for_hz(hz), 3);
        render(fh, &psg, 0.03);
    }

    unsigned data_bytes = frames_written * 2;
    fseek(fh, 4, SEEK_SET);
    put_u32(fh, 36 + data_bytes);
    fseek(fh, 40, SEEK_SET);
    put_u32(fh, data_bytes);
    fclose(fh);

    printf("%s: %.2f s\n", path, (double)frames_written / RATE);
    return 0;
}
