/* (build 187) The C extractors (src/extract/) from a command line, each
 * as its Python twin takes it:
 *
 *     ./ps2_extract NAME IMAGE [-o OUT] [--assets DIR] [--map MAP]
 *     ./ps2_extract --list
 *     ./ps2_extract japanese JP_IMAGE RETRANSLATION [-o OUT]
 *                   [--assets DIR] [--commit COMMIT]
 *
 *     ./ps2_extract assets FILE... [--ja JP_IMAGE] [--retranslation R]
 *                   [--keep-going] [--game DIR]
 *     ./ps2_extract digest FILE...
 *
 * (build 192) `assets` is tools/make_assets.py's twin (src/extract/
 * exmake.c): the game's files made from what is given, each file told
 * apart by what it is -- the US cartridge in any of the ways an image is
 * kept, the Japanese cartridge, the retranslation -- every step run, the
 * files checked against tools/assets.sha256 where it is, its exit
 * status the Python's. DIR is the game's folder (the current one by
 * default), where assets/, tools/ and the log are. `digest` prints each
 * file's digest as that list holds it (make_assets.py's file_digest).
 * tools/make_assets_c_check.py holds both to the Python.
 *
 * (build 191) `japanese` is tools/import_japanese.py's twin
 * (src/extract/ex_japanese.c): JP_IMAGE the Japanese cartridge or a zip
 * of it, RETRANSLATION the folder holding work/dialogue.json; OUT
 * defaults to DIR/lang/ja, DIR (the game's own files) to assets.
 * tools/japanese_c_check.py holds it to the Python. It is not one of
 * the steps --list names.
 *
 * NAME is the Makefile's (EXTRACTORS); IMAGE the raw cartridge image, as
 * `make extract ROM=...` passes it; OUT the Python's -o (its default
 * too); MAP tools/rom_map.json. tools/extract_c_check.py runs these beside
 * the Python ones and holds them to the same files. Exit 1 when the step
 * stops, with its reason. C11 alone.
 *
 * (build 193) Every path is taken as UTF-8 (src/extract/exfs.c): on
 * Windows the arguments are read again from the wide command line, so a
 * file of any name opens, as Python's arguments and the game's window's
 * do. */

#include "../src/extract/extract.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static u8 *read_all(const char *path, size_t *size)
{
    FILE *fh = ex_fopen(path, "rb");
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
                    "[--map MAP]\n       ps2_extract --list\n"
                    "       ps2_extract japanese JP_IMAGE RETRANSLATION "
                    "[-o OUT] [--assets DIR] [--commit COMMIT]\n"
                    "       ps2_extract assets FILE... [--ja JP_IMAGE] "
                    "[--retranslation R] [--keep-going] [--game DIR]\n"
                    "       ps2_extract digest FILE...\n");
    return 2;
}

/* make_assets.py's arguments: the files in any order, then --ja's and
 * --retranslation's, as its main() puts them together. */
static int assets(int argc, char **argv)
{
    const char **files = calloc((size_t)argc + 1, sizeof *files);
    if (files == NULL) {
        return 1;
    }
    int nfiles = 0;
    const char *ja = NULL;
    const char *rt = NULL;
    ExMakeArgs a;
    memset(&a, 0, sizeof a);
    for (int i = 2; i < argc; i++) {
        bool more = i + 1 < argc;
        if (strcmp(argv[i], "--ja") == 0 && more) {
            ja = argv[++i];
        } else if (strcmp(argv[i], "--retranslation") == 0 && more) {
            rt = argv[++i];
        } else if (strcmp(argv[i], "--keep-going") == 0) {
            a.keep_going = true;
        } else if (strcmp(argv[i], "--game") == 0 && more) {
            a.game = argv[++i];
        } else if (argv[i][0] == '-' && argv[i][1] == '-') {
            free(files);
            return usage();
        } else {
            files[nfiles++] = argv[i];
        }
    }
    if (ja != NULL) {
        files[nfiles++] = ja;
    }
    if (rt != NULL) {
        files[nfiles++] = rt;
    }
    a.files = files;
    a.nfiles = nfiles;
    int status = ex_make_assets(&a);
    free(files);
    return status;
}

typedef struct {
    const char *path;
    char        digest[80];
} Digest;

static void digest_one(Extract *x, const void *arg)
{
    Digest *d = (Digest *)arg;
    const char *got = ex_file_digest(x, d->path);
    snprintf(d->digest, sizeof d->digest, "%s",
             (got != NULL) ? got : "(unreadable)");
}

/* Each file's digest, as tools/assets.sha256 lists it. */
static int digests(int argc, char **argv)
{
    Extract x;
    memset(&x, 0, sizeof x);
    int status = 0;
    for (int i = 2; i < argc; i++) {
        Digest d;
        d.path = argv[i];
        d.digest[0] = '\0';
        if (!ex_call(&x, digest_one, &d)) {
            fprintf(stderr, "ps2_extract digest: %s\n", x.error);
            status = 1;
            continue;
        }
        printf("%s  %s\n", d.digest, argv[i]);
    }
    return status;
}

static int japanese(int argc, char **argv)
{
    if (argc < 4) {
        return usage();
    }
    ExJapanese a;
    memset(&a, 0, sizeof a);
    a.image = argv[2];
    a.retranslation = argv[3];
    for (int i = 4; i < argc; i++) {
        bool more = i + 1 < argc;
        if ((strcmp(argv[i], "-o") == 0 || strcmp(argv[i], "--out") == 0)
            && more) {
            a.out = argv[++i];
        } else if (strcmp(argv[i], "--assets") == 0 && more) {
            a.assets = argv[++i];
        } else if (strcmp(argv[i], "--commit") == 0 && more) {
            a.commit = argv[++i];
        } else {
            return usage();
        }
    }
    Extract x;
    memset(&x, 0, sizeof x);
    bool ok = ex_japanese(&x, &a);
    if (!ok) {
        fprintf(stderr, "ps2_extract japanese: %s\n", x.error);
    }
    return ok ? 0 : 1;
}

static int run(int argc, char **argv)
{
    if (argc == 2 && strcmp(argv[1], "--list") == 0) {
        for (int i = 0; i < EX_STEP_COUNT; i++) {
            printf("%s\n", EX_STEPS[i].name);
        }
        return 0;
    }
    if (argc >= 2 && strcmp(argv[1], "japanese") == 0) {
        return japanese(argc, argv);
    }
    if (argc >= 2 && strcmp(argv[1], "assets") == 0) {
        return assets(argc, argv);
    }
    if (argc >= 2 && strcmp(argv[1], "digest") == 0) {
        return digests(argc, argv);
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

int main(int argc, char **argv)
{
    char **utf8 = ex_utf8_args(argc, argv);
    int status = run(argc, (utf8 != NULL) ? utf8 : argv);
    free(utf8);
    return status;
}
