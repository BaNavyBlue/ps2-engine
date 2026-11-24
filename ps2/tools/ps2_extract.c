/* (build 187) The C extractors (src/extract/) from a command line, each
 * as its Python twin takes it:
 *
 *     ./ps2_extract NAME IMAGE [-o OUT] [--assets DIR] [--map MAP]
 *     ./ps2_extract --list
 *
 * NAME is the Makefile's (EXTRACTORS); IMAGE the raw cartridge image, as
 * `make extract ROM=...` passes it; OUT the Python's -o (its default
 * too); MAP tools/rom_map.json. tools/extract_c_check.py runs these beside
 * the Python ones and holds them to the same files. Exit 1 when the step
 * stops, with its reason. C11 alone. */

#include "../src/extract/extract.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static u8 *read_all(const char *path, size_t *size)
{
    FILE *fh = fopen(path, "rb");
    if (fh == NULL) {
        return NULL;
    }
    size_t cap = 1 << 20;
    size_t len = 0;
    u8 *buf = malloc(cap);
    while (buf != NULL) {
        size_t got = fread(buf + len, 1, cap - len, fh);
        len += got;
        if (len < cap) {
            break;
        }
        cap *= 2;
        u8 *nb = realloc(buf, cap);
        if (nb == NULL) {
            free(buf);
            buf = NULL;
        } else {
            buf = nb;
        }
    }
    fclose(fh);
    *size = len;
    return buf;
}

static int usage(void)
{
    fprintf(stderr, "usage: ps2_extract NAME IMAGE [-o OUT] [--assets DIR] "
                    "[--map MAP]\n       ps2_extract --list\n");
    return 2;
}

int main(int argc, char **argv)
{
    if (argc == 2 && strcmp(argv[1], "--list") == 0) {
        for (int i = 0; i < EX_STEP_COUNT; i++) {
            printf("%s\n", EX_STEPS[i].name);
        }
        return 0;
    }
    if (argc < 3) {
        return usage();
    }
    const ExStep *step = ex_step(argv[1]);
    if (step == NULL) {
        fprintf(stderr, "ps2_extract: no step %s in C (ps2_extract --list)\n",
                argv[1]);
        return 2;
    }
    const char *image = argv[2];
    const char *map = "tools/rom_map.json";
    ExArgs a = { NULL, NULL };
    for (int i = 3; i < argc; i++) {
        bool more = i + 1 < argc;
        if ((strcmp(argv[i], "-o") == 0 || strcmp(argv[i], "--out") == 0
             || strcmp(argv[i], "--outdir") == 0) && more) {
            a.out = argv[++i];
        } else if (strcmp(argv[i], "--assets") == 0 && more) {
            a.assets = argv[++i];
        } else if (strcmp(argv[i], "--map") == 0 && more) {
            map = argv[++i];
        } else {
            return usage();
        }
    }

    size_t size = 0;
    u8 *rom = read_all(image, &size);
    if (rom == NULL) {
        fprintf(stderr, "ps2_extract: cannot read %s\n", image);
        return 1;
    }
    Extract x;
    memset(&x, 0, sizeof x);
    if (!ex_open(&x, map, rom, size)) {
        fprintf(stderr, "ps2_extract: %s\n", x.error);
        free(rom);
        return 1;
    }
    bool ok = ex_run(&x, step, &a);
    if (!ok) {
        fprintf(stderr, "ps2_extract %s: %s\n", step->name, x.error);
    }
    ex_close(&x);
    free(rom);
    return ok ? 0 : 1;
}
