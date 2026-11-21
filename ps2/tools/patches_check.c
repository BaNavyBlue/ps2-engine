/* (build 112) `make patches_check`: the patch file's reader (src/audio/
 * patches.c) on a fixture of its edge cases (tools/patches_test.json), each
 * result held against the ROM's instrument read on its own; then the game's
 * own file (assets/music/q_patches.json) read and listed. C only, no Q: the
 * patches are resolved in C before any backend sees them. Exit 0 when every
 * check passes. */

#include "../src/audio/patches.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_fail;

static void check(bool ok, const char *what)
{
    printf("%s  %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) {
        g_fail++;
    }
}

static const SynthPatch *find(const SynthPatch *p, int n, int ins)
{
    for (int i = 0; i < n; i++) {
        if (p[i].instrument == ins) {
            return &p[i];
        }
    }
    return NULL;
}

int main(int argc, char **argv)
{
    const char *dir = (argc > 1) ? argv[1] : ".";
    char data_path[512], path[512];
    SmpsData data;
    static SynthPatch p[SMPS_INSTRUMENTS];
    u8 rom[26];

    snprintf(data_path, sizeof data_path, "%s/assets/music", dir);
    if (!smps_load(&data, data_path)) {
        printf("FAIL  cannot load %s\n", data_path);
        return 1;
    }
    snprintf(path, sizeof path, "%s/tools/patches_test.json", dir);
    int n = patches_load(path, &data, p, SMPS_INSTRUMENTS);
    check(n == 2, "two patches enabled: 74 and 70 (70's second entry replacing its first; 64 disabled; 300 and the nameless skipped)");

    const SynthPatch *a = find(p, n, 74);
    check(a != NULL && smps_instrument(&data, 74, rom), "74 read");
    if (a != NULL) {
        bool same = true;
        for (int b = 0; b < 26; b++) {
            u8 r = smps_ins_regs[b];
            if (a->rom[r] != rom[b]) {
                same = false;
            }
            if (r != 0x8C && a->reg[r] != rom[b]) {
                same = false;
            }
        }
        check(same, "74: the ROM's upload by register, and every field but one unchanged");
        check(a->reg[0x8C] == ((rom[25] & 0xF0) | 2), "74: operator 4's release 2, its sustain level the ROM's");
        check(a->volume_db == -3.5f && a->tail == SYNTH_TAIL_ROW, "74: volume -3.5 dB, tail as the TAILS row");
    }
    const SynthPatch *b = find(p, n, 70);
    check(b != NULL && smps_instrument(&data, 70, rom), "70 read");
    if (b != NULL) {
        check(b->tail == SYNTH_TAIL_FADE, "70: the later entry's tail (fade)");
        check((b->reg[0x30] >> 4) == 5 && (b->reg[0x30] & 15) == (rom[6] & 15), "70: operator 1's detune -1 (the chip's 5), its multiple the ROM's");
        check(b->reg[0xB0] == rom[0] && b->fine_cents[0] == 0.0f, "70: algorithm and fine cents the ROM's (the first entry is gone whole)");
    }
    check(find(p, n, 64) == NULL, "64: disabled, not read");

    /* the first 70 entry alone: out of range brought in */
    snprintf(path, sizeof path, "%s/tools/patches_test.json", dir);
    SynthPatch one[SMPS_INSTRUMENTS];
    /* (build 113) On Windows "/tmp" is C:/tmp, which is not there: the
     * fixture goes in %TEMP%, and a fixture that cannot be written fails
     * rather than leaving its four checks out. */
    char one_path[512];
#ifdef _WIN32
    const char *tmp = getenv("TEMP");
    snprintf(one_path, sizeof one_path, "%s/patches_check_one.json",
             (tmp != NULL) ? tmp : ".");
#else
    snprintf(one_path, sizeof one_path, "/tmp/patches_check_one.json");
#endif
    FILE *f = fopen(one_path, "wb");
    if (f != NULL) {
        fputs("{\"patches\": [{\"instrument\": 70, \"tail\": \"ring\", \"algorithm\": 4, "
              "\"feedback\": 9, \"operators\": [{\"detune\": -2, \"fine_cents\": 7.5, "
              "\"level\": 200, \"multiple\": 3}]}]}", f);
        fclose(f);
        int m = patches_load(one_path, &data, one, SMPS_INSTRUMENTS);
        check(m == 1 && one[0].tail == SYNTH_TAIL_RING, "one entry: ring");
        check(m == 1 && (one[0].reg[0xB0] & 7) == 4 && ((one[0].reg[0xB0] >> 3) & 7) == 7, "algorithm 4, feedback 9 brought to 7");
        check(m == 1 && one[0].reg[0x40] == 127 && one[0].reg[0x30] == ((6 << 4) | 3), "level 200 brought to 127; detune -2 is the chip's 6, multiple 3");
        check(m == 1 && one[0].fine_cents[0] == 7.5f, "fine cents 7.5");
    } else {
        check(false, "the one-entry fixture cannot be written");
    }

    /* the game's own file */
    snprintf(path, sizeof path, "%s/assets/music/q_patches.json", dir);
    int g = patches_load(path, &data, p, SMPS_INSTRUMENTS);
    printf("\nassets/music/q_patches.json: %d enabled\n", g);
    for (int i = 0; i < g; i++) {
        int changed = 0;
        for (int r = 0; r < 256; r++) {
            changed += (p[i].rom[r] != p[i].reg[r]) ? 1 : 0;
        }
        printf("  instrument %3d: %d registers changed, tail %s, volume %+.1f dB\n",
               p[i].instrument, changed,
               (p[i].tail == SYNTH_TAIL_RING) ? "ring"
               : ((p[i].tail == SYNTH_TAIL_FADE) ? "fade" : "as the row"),
               (double)p[i].volume_db);
    }
    smps_free(&data);
    printf("\n%s  %d failed\n", g_fail ? "FAIL" : "PASS", g_fail);
    return g_fail ? 1 : 0;
}
