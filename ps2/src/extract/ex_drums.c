/* (build 190) tools/extract_drums.py in C: the DAC drums, read from the
 * Z80 drum program the cartridge carries.
 *
 * The program is assembled at Z80 address 0 from PCMDrums on, so a Z80
 * address is PCMDrums plus it. zloc_68 reads the drum byte from its
 * mailbox and tests bits 0..6 in that order, a `bit n, a` / `jp nz` pair a
 * bit, then jumps for $80 alone; each branch is `ld de, sample` / `ld hl,
 * length` / `ld a, delay`. A sample runs from the drum label `de` points
 * at to the drum file's next label.
 *
 * Writes drums/<name>.wav for each sample (the ROM's bytes, 8-bit
 * unsigned mono; the rate, the first drum's that plays it, is a label
 * only) and drums.json naming the samples and the drums (EX_DRUM_NAMES,
 * exsong.c), and deletes the PCMDrums.bin they were until build 136b. */

#include "asset_names.h"
#include "exsong.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void ex_step_drums(Extract *x, const ExArgs *a);

#define Z80_CLOCK    3579545    /* NTSC, master / 15 */
#define T_FIXED      240        /* everything in the loop but djnz (fm.c) */
#define T_PER_DELAY  13
#define LABEL_DELAY  4          /* a sample no drum plays: its WAV's rate */

/* (build 185) The Z80 opcodes the dispatch is made of; a word operand
 * follows low byte first. */
#define Z80_LD_A_MEM 0x3A       /* ld a, (nn): the drum byte from its mailbox */
#define Z80_BIT_A    0xCB       /* bit n, a is $CB, $47 + 8n */
#define Z80_BIT_0_A  0x47
#define Z80_JP_Z     0xCA       /* jp z, nn */
#define Z80_JP_NZ    0xC2       /* jp nz, nn */
#define Z80_JP       0xC3       /* jp nn */
#define Z80_LD_DE    0x11       /* ld de, nn: the sample's address */
#define Z80_LD_HL    0x21       /* ld hl, nn: how many bytes it plays */
#define Z80_LD_A     0x3E       /* ld a, n: the delay */

#define WAV_HEADER   44

static const char DRUMS_ABOUT[] =
    "The DAC drums (ps2_pcm_drums.asm, which LoadPCMDrums copies into Z80 "
    "RAM). Each sample is its WAV's bytes -- unsigned 8-bit PCM, the ROM's "
    "own; the Z80 halves each as it plays it -- placed in the 8 KB of Z80 "
    "RAM at z80_address (from $118, past the program's own bytes); RAM no "
    "sample covers is zero. A drum byte a song sends plays `length` bytes "
    "from its sample's address, one every 240 + 13 * delay Z80 cycles "
    "(3579545 Hz), so the WAV's own rate is only a label: the toms are one "
    "sample at three delays. Drum $90 plays 513 bytes of an 18-byte sample, "
    "running on into that zeroed RAM, as in the ROM; no song sends it.";

/* The drum program's bytes, by Z80 address. */
typedef struct {
    Extract  *x;
    long long base;             /* PCMDrums */
} Z80Code;

static u8 z_byte(const Z80Code *c, long long z80)
{
    return ex_u8(c->x, c->base + z80);
}

static int z_word(const Z80Code *c, long long z80)
{
    return z_byte(c, z80) | z_byte(c, z80 + 1) << 8;
}

static void z_expect(const Z80Code *c, long long z80, u8 opcode,
                     const char *what)
{
    u8 b = z_byte(c, z80);
    if (b != opcode) {
        ex_fail(c->x, "the drum program at Z80 $%04llX: $%02X, not %s "
                      "($%02X)", (unsigned long long)z80, b, what, opcode);
    }
}

/* What a drum byte plays: its sample's Z80 address, the length, the
 * delay. */
typedef struct {
    bool have;
    int  sample;
    int  length;
    int  delay;
} Branch;

typedef struct {
    int byte;
    int at;                     /* the branch's Z80 address */
} Jump;

static void add_jump(Extract *x, Jump **list, int *n, int *cap, int byte,
                     int at)
{
    if (*n == *cap) {
        int more = (*cap == 0) ? 8 : *cap * 2;
        Jump *grown = ex_alloc(x, sizeof *grown * (size_t)more);
        if (*n > 0) {
            memcpy(grown, *list, sizeof *grown * (size_t)*n);
        }
        *list = grown;
        *cap = more;
    }
    (*list)[*n].byte = byte;
    (*list)[*n].at = at;
    (*n)++;
}

/* (build 185) table[drum byte] as zloc_68 dispatches: `ld a, (mailbox)`,
 * `bit 7, a` / `jp z` (no drum yet), then a `bit n, a` / `jp nz` pair a
 * bit, then `jp` for $80 alone; each branch `ld de, sample` / `ld hl,
 * length` / `ld a, delay`. A byte two branches take keeps the later. */
static void dispatch(Extract *x, const Z80Code *code, Branch table[256])
{
    long long at = (long long)ex_addr(x, "zloc_68") - code->base;
    z_expect(code, at, Z80_LD_A_MEM, "ld a, (nn)");
    at += 3;
    Jump *jumps = NULL;
    int njumps = 0;
    int cap = 0;
    while (z_byte(code, at) == Z80_BIT_A) {
        u8 op = z_byte(code, at + 1);
        long long bit  = ex_floordiv((long long)op - Z80_BIT_0_A, 8);
        long long rest = ex_mod((long long)op - Z80_BIT_0_A, 8);
        if (rest != 0 || bit < 0 || bit > 7) {
            ex_fail(x, "the drum program at Z80 $%04llX: $CB $%02X is not "
                       "bit n, a", (unsigned long long)at, op);
        }
        u8 jump = z_byte(code, at + 2);
        if (bit == 7) {
            if (jump != Z80_JP_Z) {
                ex_fail(x, "the drum program: bit 7, a without jp z");
            }
        } else {
            z_expect(code, at + 2, Z80_JP_NZ, "jp nz, nn");
            add_jump(x, &jumps, &njumps, &cap, 0x80 | 1 << bit,
                     z_word(code, at + 3));
        }
        at += 5;
    }
    z_expect(code, at, Z80_JP, "jp nn");
    add_jump(x, &jumps, &njumps, &cap, 0x80, z_word(code, at + 1));
    for (int i = 0; i < njumps; i++) {
        int branch = jumps[i].at;
        z_expect(code, branch, Z80_LD_DE, "ld de, nn");
        z_expect(code, branch + 3, Z80_LD_HL, "ld hl, nn");
        z_expect(code, branch + 6, Z80_LD_A, "ld a, n");
        Branch *b = &table[jumps[i].byte];
        b->have   = true;
        b->sample = z_word(code, branch + 1);
        b->length = z_word(code, branch + 4);
        b->delay  = z_byte(code, branch + 7);
    }
}

/* A drum label by its Z80 address, sorted as Python sorts (address, name)
 * pairs. */
typedef struct {
    long long   z80;
    const char *name;
} DrumLabel;

static int cmp_label(const void *pa, const void *pb)
{
    const DrumLabel *a = pa;
    const DrumLabel *b = pb;
    if (a->z80 != b->z80) {
        return (a->z80 < b->z80) ? -1 : 1;
    }
    return strcmp(a->name, b->name);
}

static int cmp_int(const void *pa, const void *pb)
{
    int a = *(const int *)pa;
    int b = *(const int *)pb;
    return (a > b) - (a < b);
}

typedef struct {
    const char *label;
    int         z80;
    const u8   *data;
    size_t      len;
    const char *name;           /* EX_DRUM_SAMPLE_FILES's */
} Sample;

/* (build 185) Each sample the dispatch points at, in Z80 address order:
 * from the address to the drum file's next label, the bytes the ROM has
 * there. */
static Sample *read_samples(Extract *x, const Z80Code *code,
                            const Branch table[256], int *count)
{
    int nl = x->map.ndrums;
    DrumLabel *labels = ex_alloc(x, sizeof *labels * (size_t)(nl + 1));
    for (int i = 0; i < nl; i++) {
        labels[i].z80  = (long long)x->map.drums[i].addr - code->base;
        labels[i].name = x->map.drums[i].name;
    }
    qsort(labels, (size_t)nl, sizeof *labels, cmp_label);

    int starts[256];
    int nstarts = 0;
    for (int b = 0; b < 256; b++) {
        if (table[b].have) {
            starts[nstarts++] = table[b].sample;
        }
    }
    qsort(starts, (size_t)nstarts, sizeof starts[0], cmp_int);

    Sample *out = ex_alloc(x, sizeof *out * (size_t)(nstarts + 1));
    int n = 0;
    for (int s = 0; s < nstarts; s++) {
        if (s > 0 && starts[s] == starts[s - 1]) {
            continue;
        }
        int z80 = starts[s];
        int here = 0;
        const char *here_name = NULL;
        const DrumLabel *later = NULL;
        for (int i = 0; i < nl; i++) {
            if (labels[i].z80 == z80) {
                here_name = (here == 0) ? labels[i].name : here_name;
                here++;
            } else if (labels[i].z80 > z80 && later == NULL) {
                later = &labels[i];
            }
        }
        if (here != 1 || later == NULL) {
            ex_fail(x, "Z80 $%04X: a sample with %d labels and %s label "
                       "after it", z80, here, (later != NULL) ? "a" : "no");
        }
        /* Looked up by name, as the Python does for the map's list of the
         * names the extractors use. */
        (void)ex_addr(x, here_name);
        (void)ex_addr(x, later->name);
        long long len = later->z80 - z80;
        out[n].label = here_name;
        out[n].z80   = z80;
        out[n].data  = ex_read(x, code->base + z80, len);
        out[n].len   = (size_t)len;
        n++;
    }
    *count = n;
    return out;
}

static double rate_for(int delay)
{
    return (double)Z80_CLOCK / (double)(T_FIXED + T_PER_DELAY * delay);
}

/* Python's round() of a positive float: to the nearest whole number, a
 * half to the even one. */
static long long round_even(double v)
{
    long long whole = (long long)v;
    double frac = v - (double)whole;
    if (frac > 0.5) {
        return whole + 1;
    }
    if (frac < 0.5) {
        return whole;
    }
    return (whole % 2 == 0) ? whole : whole + 1;
}

static void put_le(u8 *p, u32 v, int bytes)
{
    for (int i = 0; i < bytes; i++) {
        p[i] = (u8)(v >> (8 * i));
    }
}

/* 8-bit unsigned mono, the bytes as the DAC would see them. */
static void write_wav(Extract *x, const char *path, const u8 *data,
                      size_t len, u32 rate)
{
    u8 *buf = ex_alloc(x, WAV_HEADER + len);
    memcpy(buf, "RIFF", 4);
    put_le(buf + 4, (u32)(36 + len), 4);
    memcpy(buf + 8, "WAVEfmt ", 8);
    put_le(buf + 16, 16, 4);    /* the fmt chunk's size */
    put_le(buf + 20, 1, 2);     /* PCM */
    put_le(buf + 22, 1, 2);     /* one channel */
    put_le(buf + 24, rate, 4);
    put_le(buf + 28, rate, 4);  /* bytes a second */
    put_le(buf + 32, 1, 2);     /* bytes a frame */
    put_le(buf + 34, 8, 2);     /* bits a sample */
    memcpy(buf + 36, "data", 4);
    put_le(buf + 40, (u32)len, 4);
    if (len > 0) {
        memcpy(buf + WAV_HEADER, data, len);
    }
    ex_write(x, path, buf, WAV_HEADER + len);
}

/* Path.exists() for a file. */
static bool file_there(const char *path)
{
    FILE *fh = fopen(path, "rb");
    if (fh == NULL) {
        return false;
    }
    fclose(fh);
    return true;
}

/* The sample a branch's address starts. */
static const Sample *sample_at(const Sample *samples, int n, int z80)
{
    for (int i = 0; i < n; i++) {
        if (samples[i].z80 == z80) {
            return &samples[i];
        }
    }
    return NULL;
}

/* (build 136b) drums/<name>.wav for each sample and drums.json naming them
 * and the drums (byte, name, sample, length, delay). */
static void write_drum_set(Extract *x, const char *outdir,
                           const Sample *samples, int nsamples,
                           const Branch table[256])
{
    char *wav_dir = ex_path(x, outdir, "drums");
    ex_mkdirs(x, wav_dir);
    ExStr t;
    ex_str_init(x, &t);
    ex_str_add(&t, "{\n  \"source\": \"ps2_pcm_drums.asm\",\n  \"about\": ");
    ex_str_add(&t, jo_quote(x, DRUMS_ABOUT));
    ex_str_add(&t, ",\n  \"samples\": [\n");
    for (int s = 0; s < nsamples; s++) {
        const char *name = samples[s].name;
        /* the first drum (in EX_DRUM_NAMES's order) that plays it */
        int delay = LABEL_DELAY;
        bool played = false;
        for (int d = 0; d < EX_DRUM_NAME_COUNT && !played; d++) {
            const Branch *b = &table[EX_DRUM_NAMES[d].byte];
            const Sample *of = sample_at(samples, nsamples, b->sample);
            if (strcmp(of->name, name) == 0) {
                delay = b->delay;
                played = true;
            }
        }
        char *file = ex_path(x, wav_dir, ex_fmt(x, "%s.wav", name));
        write_wav(x, file, samples[s].data, samples[s].len,
                  (u32)round_even(rate_for(delay)));
        if (s > 0) {
            ex_str_add(&t, ",\n");
        }
        ex_str_addf(&t, "    {\"name\": \"%s\", \"file\": \"drums/%s.wav\", "
                        "\"z80_address\": %d}", name, name, samples[s].z80);
    }
    ex_str_add(&t, "\n  ],\n  \"drums\": [\n");
    for (int d = 0; d < EX_DRUM_NAME_COUNT; d++) {
        const Branch *b = &table[EX_DRUM_NAMES[d].byte];
        const Sample *of = sample_at(samples, nsamples, b->sample);
        if (d > 0) {
            ex_str_add(&t, ",\n");
        }
        ex_str_addf(&t, "    {\"byte\": %d, \"name\": \"%s\", \"sample\": "
                        "\"%s\", \"length\": %d, \"delay\": %d}",
                    EX_DRUM_NAMES[d].byte, EX_DRUM_NAMES[d].name, of->name,
                    b->length, b->delay);
    }
    ex_str_add(&t, "\n  ]\n}\n");
    ex_write_text(x, ex_path(x, outdir, "drums.json"), t.buf);
}

void ex_step_drums(Extract *x, const ExArgs *a)
{
    Z80Code code = { x, ex_addr(x, "PCMDrums") };
    Branch table[256];
    memset(table, 0, sizeof table);
    dispatch(x, &code, table);

    /* the bytes it dispatches, in order, must be the drums named */
    bool same = true;
    int k = 0;
    for (int b = 0; b < 256; b++) {
        if (!table[b].have) {
            continue;
        }
        if (k >= EX_DRUM_NAME_COUNT || EX_DRUM_NAMES[k].byte != b) {
            same = false;
        }
        k++;
    }
    if (!same || k != EX_DRUM_NAME_COUNT) {
        ExStr list;
        ex_str_init(x, &list);
        for (int b = 0; b < 256; b++) {
            if (table[b].have) {
                ex_str_addf(&list, "%s$%02X", (list.len > 0) ? ", " : "", b);
            }
        }
        ex_fail(x, "the drum program dispatches bytes %s, not the drums "
                   "named here", (list.len > 0) ? list.buf : "");
    }

    int nsamples = 0;
    Sample *samples = read_samples(x, &code, table, &nsamples);
    long long end = 0;
    for (int s = 0; s < nsamples; s++) {
        long long reach = samples[s].z80 + (long long)samples[s].len;
        end = (reach > end) ? reach : end;
    }

    for (int d = 0; d < EX_DRUM_NAME_COUNT; d++) {
        const Branch *b = &table[EX_DRUM_NAMES[d].byte];
        const Sample *of = sample_at(samples, nsamples, b->sample);
        char note[64] = "";
        if ((long long)of->len < b->length) {
            snprintf(note, sizeof note, "  (only %zu bytes in ROM)", of->len);
        }
        ex_say(x, "  $%02X  %-10s  %-10s at %04X  %4d bytes  delay %d  "
                  "~%5.0f Hz%s", EX_DRUM_NAMES[d].byte, EX_DRUM_NAMES[d].name,
               of->label, b->sample, b->length, b->delay,
               rate_for(b->delay), note);
    }

    ex_mkdirs(x, a->out);
    for (int s = 0; s < nsamples; s++) {
        samples[s].name = ex_pair(EX_DRUM_SAMPLE_FILES, samples[s].label);
        if (samples[s].name == NULL) {
            ex_fail(x, "%s: a sample asset_names.DRUM_SAMPLE_FILES does not "
                       "name", samples[s].label);
        }
    }
    write_drum_set(x, a->out, samples, nsamples, table);
    char *old = ex_path(x, a->out, "PCMDrums.bin");
    if (file_there(old)) {
        ex_remove(x, old);
        ex_say(x, "deleted %s (drums.json and its WAVs now)", old);
    }
    ex_say(x, "drums: %d drums, %d samples, %lld bytes of ROM -> %s",
           EX_DRUM_NAME_COUNT, nsamples, end,
           ex_path(x, a->out, "drums.json"));
}
