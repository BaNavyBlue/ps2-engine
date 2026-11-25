/* (build 190) tools/extract_music.py in C: the sound driver's data as one
 * image, the ROM's bytes from SoundDriverBase to PCMDrums with the
 * driver's own code between the regions zeroed, every offset where the
 * ROM had it; then
 *
 *   note_frequencies.json   FMFreqs, block and F-number a note
 *   fm_instruments.json     the instruments FMInsPtrs points at, in
 *                           SetFMIns's upload order
 *   sounds.txt, envelopes.txt, effects.txt, songs/<file>.txt
 *                           the image as text (ex_songtext.c), checked to
 *                           build back to the image with the game's own
 *                           song builder (src/audio/songtext.c) before it
 *                           is written
 *   songs/<song>.trk        every song as a tracker (ex_tracker.c), which
 *                           the game reads in its stream text's place:
 *                           checked to be read by the song builder
 *   songs.json              names and ids, for tools and the eye
 *
 * The pointer tables are read as the driver reads them, a byte-swapped
 * word an entry, and each entry is named by the label it points at. */

#include "exmusic.h"
#include "extracker.h"
#include "asset_names.h"
#include "../audio/songtext.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

void ex_step_music(Extract *x, const ExArgs *a);

/* The prose the files carry: extract_music.py's, written in by a script
 * from it (NOTES_ABOUT, INSTRUMENTS_ABOUT), not retyped. */
static const char NOTES_ABOUT[] =
    "The note frequencies (FMFreqs). GetFMFreq adds the track's transpose to a note byte ($81 up), keeps the low six bits and reads that entry, 0-63, of this table: a YM2612 frequency word, block (the octave) and F-number. Entry 0 is silence; entries 64-84 are in the ROM but nothing reads them. \"note\" names each slot by the nearest concert note (A440) -- the table is equal-tempered about a quarter of a semitone sharp, entry 1 being B0 + 26 cents -- and \"hz\" and \"cents\" are what the chip makes of block and F-number at its 7670454 Hz clock: the extractor's arithmetic, for reading; the game reads block and fnum.";

static const char INSTRUMENTS_ABOUT[] =
    "The FM instruments (FMInsPtrs). A track's header or its instrument command (cfEF) names one by \"number\", its place here, and SetFMIns writes its 26 registers to the track's channel, in \"upload_order\" (the ROM's table at loc_BA818); then, if the track's volume is not 0, RefreshVolume writes each carrier's total_level plus that volume. Each operator field is a row of four, operators 1-4 in the chip's numbering (register offsets +0, +8, +4, +C). The algorithm decides the carriers, as RefreshVolume reads it: 0-3 operator 4; 4 operators 2 and 4; 5-6 operators 2-4; 7 all four. multiple 0-15 (0 is a half); detune 0-7 (1-3 up, 5-7 down, 0 and 4 none); total_level 0-127, attenuation, 0.75 dB a step; attack_rate, decay_rate and sustain_rate 0-31, release_rate 0-15, higher faster; sustain_level 0-15, where the first decay hands over to the second, 3 dB a step; rate_scaling 0-3, how much faster the rates run for higher notes; lfo_am whether the LFO's tremolo reaches the operator, at the channel's lfo_am_sensitivity (0-3), its vibrato at lfo_fm_sensitivity (0-7); feedback 0-7, operator 1 into itself; pan center, left, right or none. \"name\" is what the song text calls it (build 137; one word, no two the same). \"ignored_bits\" are bits set in the ROM's byte that the chip does not read, kept so the bytes are the ROM's: they make no sound. The game reads its instruments from SoundData.bin's image; this file is read by tools/fm_demo and q_demo and the song text's tools until the game builds that image from the text.";

/* A pointer table runs from its label to the first label after it whose
 * name does not start with this: MusicPtrs and SFXPtrs give each entry a
 * label of its own (PtrMusic_Phantasy). */
#define ENTRY_PREFIX "Ptr"

/* The driver's tables, in songs.json's "roots". */
static const char *const ROOTS[EXS_ROOTS] = {
    "SoundPriorities", "MusicTempo", "MusicPtrs", "FMInsPtrs",
    "ModEnvelopePtrs", "SFXPtrs", "SpcSFXPtrs"
};

#define FM_CLOCK        7670454.0   /* the YM2612's master clock, NTSC */
#define ENTRY1_SEMITONE 11          /* entry 1 sounds as B0 (+26 cents) */
#define FREQ_ENTRIES    85          /* song_text.len_freqs: a silence and
                                       seven octaves */

static const char *const NOTE_NAMES[12] = {
    "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
};

/* Python's round(): to the nearest whole number, a half to the even. */
static long long py_round(double v)
{
    double f = floor(v);
    double rest = v - f;
    long long r = (long long)f;
    if (rest > 0.5) {
        return r + 1;
    }
    if (rest < 0.5) {
        return r;
    }
    return (r % 2 == 0) ? r : r + 1;
}

static const char *music_file(Extract *x, const char *key)
{
    const char *name = ex_pair(EX_MUSIC_FILES, key);
    if (name == NULL) {
        ex_fail(x, "asset_names.MUSIC_FILES has no %s", key);
    }
    return name;
}

/* ---- the image ---- */

typedef struct {
    u8    *image;
    size_t size;
    u32    base;
} Image;

/* build_image: the ROM's bytes from SoundDriverBase to PCMDrums, the
 * driver's code between the two regions zeroed; `placed` marks the data. */
static void build_image(Extract *x, Image *im, u8 **placed)
{
    static const char *const regions[2][2] = {
        { "SoundDriverBase", "SoundDriverInput" },
        { "FMFreqs", "PCMDrums" },
    };
    im->base = ex_addr(x, "SoundDriverBase");
    u32 end = ex_addr(x, regions[1][1]);
    if (end <= im->base) {
        ex_fail(x, "PCMDrums is not after SoundDriverBase");
    }
    im->size = end - im->base;
    im->image = ex_alloc(x, im->size);
    memcpy(im->image, ex_read(x, im->base, (long long)im->size), im->size);
    *placed = ex_alloc(x, im->size);
    for (int r = 0; r < 2; r++) {
        u32 from = ex_addr(x, regions[r][0]) - im->base;
        u32 to = ex_addr(x, regions[r][1]) - im->base;
        for (u32 k = from; k < to && k < im->size; k++) {
            (*placed)[k] = 1;
        }
    }
    for (size_t k = 0; k < im->size; k++) {
        if (!(*placed)[k]) {
            im->image[k] = 0;
        }
    }
}

/* ptr_word: a pointer word as SndDrv_ReadPtr reads it, byte-swapped, an
 * offset from SoundDriverBase. */
static u32 ptr_word(Extract *x, const Image *im, long long off)
{
    if (off < 0 || (size_t)off + 2 > im->size) {
        ex_fail(x, "a pointer word at $%llX, outside the driver's image", off);
    }
    return (u32)im->image[off] | (u32)im->image[off + 1] << 8;
}

/* table_entries: a pointer table's words, from its label to the first
 * label after it that is not an entry's own. */
static u32 *table_entries(Extract *x, const Image *im, const char *table,
                          int *count)
{
    u32 start = ex_addr(x, table);
    int idx = ex_index(x, table);
    long long end = -1;
    for (int i = idx + 1; i < x->map.nlabels; i++) {
        const char *name = x->map.labels[i].name;
        if (!ex_starts(name, ENTRY_PREFIX)) {
            end = ex_addr(x, name);
            break;
        }
    }
    if (end < 0) {
        ex_fail(x, "%s: no label after it", table);
    }
    long long bytes = end - (long long)start;
    if (ex_mod(bytes, 2) != 0) {
        ex_fail(x, "%s: %lld bytes, not whole words", table, bytes);
    }
    int n = bytes > 0 ? (int)(bytes / 2) : 0;
    u32 *out = ex_alloc(x, (size_t)(n + 1) * sizeof *out);
    for (int k = 0; k < n; k++) {
        out[k] = ptr_word(x, im, (long long)start + 2 * k - im->base);
    }
    *count = n;
    return out;
}

/* pointer_names: the labels a pointer table points at, in order, the one
 * label at each entry's address; two there would leave the name in doubt. */
static const char **pointer_names(Extract *x, const Image *im,
                                  const char *table, int *count)
{
    int n = 0;
    u32 *offs = table_entries(x, im, table, &n);
    const char **names = ex_alloc(x, (size_t)(n + 1) * sizeof *names);
    for (int k = 0; k < n; k++) {
        int nfound = 0;
        const ExLabel **found = ex_names_at(x, im->base + offs[k], &nfound);
        if (nfound != 1) {
            ExStr s;
            ex_str_init(x, &s);
            for (int i = 0; i < nfound; i++) {
                ex_str_add(&s, i > 0 ? ", " : "");
                ex_str_add(&s, found[i]->name);
            }
            ex_fail(x, "%s entry %d points at $%X, where the map has %s: not "
                       "one name", table, k, im->base + offs[k],
                    nfound > 0 ? s.buf : "no label");
        }
        names[k] = found[0]->name;
    }
    *count = n;
    return names;
}

/* stream_reach: which bytes of the image a stream reads -- every track of
 * every sound's header, walked as the sequencer walks it. */
static void on_reach(Extract *x, void *ctx, const ExsEvent *ev,
                     const ExsState *st)
{
    (void)x;
    (void)st;
    const Image *im = ((void **)ctx)[0];
    u8 *reached = ((void **)ctx)[1];
    for (u32 k = ev->at; k < ev->at + (u32)ev->size && k < im->size; k++) {
        reached[k] = 1;
    }
}

static u8 *stream_reach(Extract *x, const Image *im, const u32 *headers,
                        int nheaders)
{
    u8 *reached = ex_alloc(x, im->size + 1);
    void *ctx[2] = { (void *)im, reached };
    for (int h = 0; h < nheaders; h++) {
        u32 off = headers[h];
        for (int i = 0; i < im->image[off]; i++) {
            u32 at = off + 1 + 9 * (u32)i;
            if ((size_t)at + 9 > im->size) {
                ex_fail(x, "a header at $%04X runs past the image", off);
            }
            ExsTrack t;
            exs_track(&t, "", at, im->image + at, true);
            exs_walk(x, im->image, im->size, &t, on_reach, ctx);
        }
    }
    return reached;
}

/* ---- note_frequencies.json ---- */

/* note_slot_name: entry i's concert name, entry 1 B0, each a semitone up. */
static char *note_slot_name(Extract *x, int i)
{
    if (i == 0) {
        return ex_strdup(x, "silence");
    }
    int semi = ENTRY1_SEMITONE + i - 1;
    return ex_fmt(x, "%s%d", NOTE_NAMES[semi % 12], semi / 12);
}

static char *note_frequencies_text(Extract *x, const u16 *words, int n)
{
    ExStr s;
    ex_str_init(x, &s);
    ex_str_add(&s, "{\n  \"source\": \"FMFreqs\",\n  \"about\": ");
    ex_str_add(&s, jo_quote(x, NOTES_ABOUT));
    ex_str_add(&s, ",\n  \"entries\": [\n");
    for (int i = 0; i < n; i++) {
        u16 w = words[i];
        if (w & ~0x3FFF) {
            ex_fail(x, "FMFreqs entry %d is $%04X: bits above the block that "
                       "a block and F-number cannot say", i, w);
        }
        int block = w >> 11;
        int fnum = w & 0x7FF;
        ex_str_addf(&s, "%s    {\"note\": \"%s\", \"block\": %d, \"fnum\": %d",
                    i > 0 ? ",\n" : "", note_slot_name(x, i), block, fnum);
        if (fnum != 0) {
            double hz = (double)fnum * pow(2.0, block - 1) * FM_CLOCK / 144.0
                        / 1048576.0;
            double semi = 12.0 * log2(hz / 440.0) + 57.0;
            long long cents = py_round((semi - (double)py_round(semi)) * 100.0);
            ex_str_addf(&s, ", \"hz\": %.2f, \"cents\": %lld", hz, cents);
        }
        ex_str_add(&s, "}");
    }
    ex_str_add(&s, "\n  ]\n}\n");
    return s.buf;
}

/* ---- fm_instruments.json ---- */

/* SetFMIns writes 26 registers to the track's channel, the register
 * numbers from the table at loc_BA818, the values from the instrument,
 * byte i to register i; each is named by the fields it holds, an
 * operator's as opN -- operators in the chip's numbering, 1-4 at register
 * offsets +0, +8, +4, +C. */
#define FM_INS_REGISTERS 26

static const u8 OP_OFFSETS[4] = { 0x0, 0x8, 0x4, 0xC };   /* operators 1-4 */

typedef struct {
    const char *name;
    u8          base;
    int         shift;
    int         bits;
} Field;

/* The channel's fields, then an operator's in the order an envelope runs,
 * as the file lists them. */
static const Field CHANNEL_FIELDS[] = {
    { "algorithm",          0xB0, 0, 3 },
    { "feedback",           0xB0, 3, 3 },
    { "pan",                0xB4, 6, 2 },
    { "lfo_am_sensitivity", 0xB4, 4, 2 },
    { "lfo_fm_sensitivity", 0xB4, 0, 3 },
};

static const Field OPERATOR_FIELDS[] = {
    { "multiple",      0x30, 0, 4 },
    { "detune",        0x30, 4, 3 },
    { "total_level",   0x40, 0, 7 },
    { "attack_rate",   0x50, 0, 5 },
    { "decay_rate",    0x60, 0, 5 },
    { "sustain_level", 0x80, 4, 4 },
    { "sustain_rate",  0x70, 0, 5 },
    { "release_rate",  0x80, 0, 4 },
    { "rate_scaling",  0x50, 6, 2 },
    { "lfo_am",        0x60, 7, 1 },
};

#define NCHANNEL_FIELDS  ((int)(sizeof CHANNEL_FIELDS / sizeof CHANNEL_FIELDS[0]))
#define NOPERATOR_FIELDS ((int)(sizeof OPERATOR_FIELDS / sizeof OPERATOR_FIELDS[0]))

/* $B4 bit 7 is the left speaker, bit 6 the right. */
static const char *const PAN_NAMES[4] = { "none", "right", "left", "center" };

static const char *channel_register(u8 reg)
{
    if (reg == 0xB0) {
        return "feedback_algorithm";
    }
    if (reg == 0xB4) {
        return "pan_lfo";
    }
    return NULL;
}

static const char *operator_register(u8 base)
{
    switch (base) {
    case 0x30:
        return "detune_multiple";
    case 0x40:
        return "total_level";
    case 0x50:
        return "rate_scaling_attack";
    case 0x60:
        return "am_decay";
    case 0x70:
        return "sustain_rate";
    case 0x80:
        return "sustain_level_release";
    default:
        return NULL;
    }
}

static int operator_of_offset(u8 offset)
{
    for (int i = 0; i < 4; i++) {
        if (OP_OFFSETS[i] == offset) {
            return i + 1;
        }
    }
    return 0;
}

/* register_name: a register SetFMIns writes, by the fields it holds. */
static char *register_name(Extract *x, u8 reg)
{
    if (channel_register(reg) != NULL) {
        return ex_strdup(x, channel_register(reg));
    }
    u8 base = reg & 0xF0;
    int op = operator_of_offset(reg & 0x0F);
    if (operator_register(base) == NULL || op == 0) {
        ex_fail(x, "SetFMIns writes $%02X, which is not a channel 1 "
                   "instrument register", reg);
    }
    return ex_fmt(x, "op%d.%s", op, operator_register(base));
}

/* read_mask: the bits of a register the chip reads, its fields'. */
static u8 read_mask(u8 base)
{
    unsigned mask = 0;
    for (int i = 0; i < NCHANNEL_FIELDS; i++) {
        if (CHANNEL_FIELDS[i].base == base) {
            mask |= ((1u << CHANNEL_FIELDS[i].bits) - 1)
                    << CHANNEL_FIELDS[i].shift;
        }
    }
    for (int i = 0; i < NOPERATOR_FIELDS; i++) {
        if (OPERATOR_FIELDS[i].base == base) {
            mask |= ((1u << OPERATOR_FIELDS[i].bits) - 1)
                    << OPERATOR_FIELDS[i].shift;
        }
    }
    return (u8)mask;
}

/* An instrument's value for a register, `order` the upload order. */
static u8 reg_value(Extract *x, const u8 *order, const u8 *ins, u8 reg)
{
    for (int i = 0; i < FM_INS_REGISTERS; i++) {
        if (order[i] == reg) {
            return ins[i];
        }
    }
    ex_fail(x, "loc_BA818 does not write $%02X", reg);
}

static int field_value(Extract *x, const u8 *order, const u8 *ins, u8 reg,
                       const Field *f)
{
    return (reg_value(x, order, ins, reg) >> f->shift) & ((1 << f->bits) - 1);
}

/* instrument_text: one instrument as the file holds it. */
static char *instrument_text(Extract *x, int number, const u8 *order,
                             const u8 *ins, int ninstrument_names)
{
    /* head */
    const char *head[2 + NCHANNEL_FIELDS];
    int nhead = 0;
    head[nhead++] = ex_fmt(x, "\"number\": %d", number);
    if (number < ninstrument_names) {
        head[nhead++] = ex_fmt(x, "\"name\": \"%s\"",
                               EX_INSTRUMENT_NAMES[number]);
    }
    for (int i = 0; i < NCHANNEL_FIELDS; i++) {
        const Field *f = &CHANNEL_FIELDS[i];
        int v = field_value(x, order, ins, f->base, f);
        head[nhead++] = strcmp(f->name, "pan") == 0
            ? ex_fmt(x, "\"%s\": \"%s\"", f->name, PAN_NAMES[v])
            : ex_fmt(x, "\"%s\": %d", f->name, v);
    }
    ExStr s;
    ex_str_init(x, &s);
    ex_str_add(&s, "    {");
    for (int i = 0; i < 2 && i < nhead; i++) {
        ex_str_add(&s, i > 0 ? ", " : "");
        ex_str_add(&s, head[i]);
    }
    ex_str_add(&s, ",\n     ");
    for (int i = 2; i < nhead; i++) {
        ex_str_add(&s, i > 2 ? ", " : "");
        ex_str_add(&s, head[i]);
    }
    ex_str_add(&s, ",\n     ");
    for (int i = 0; i < NOPERATOR_FIELDS; i++) {
        const Field *f = &OPERATOR_FIELDS[i];
        char *key = ex_fmt(x, "%s:", jo_quote(x, f->name));
        ex_str_addf(&s, "%s%-17s[", i > 0 ? ",\n     " : "", key);
        for (int o = 0; o < 4; o++) {
            int v = field_value(x, order, ins, (u8)(f->base + OP_OFFSETS[o]),
                                f);
            ex_str_add(&s, o > 0 ? ", " : "");
            if (strcmp(f->name, "lfo_am") == 0) {
                ex_str_add(&s, v ? "true" : "false");
            } else {
                ex_str_addf(&s, "%3d", v);
            }
        }
        ex_str_add(&s, "]");
    }
    ExStr ignored;
    ex_str_init(x, &ignored);
    int nignored = 0;
    for (int i = 0; i < FM_INS_REGISTERS; i++) {        /* in upload order */
        u8 reg = order[i];
        u8 base = channel_register(reg) != NULL ? reg : (u8)(reg & 0xF0);
        u8 stray = (u8)(ins[i] & ~read_mask(base) & 0xFF);
        if (stray == 0) {
            continue;
        }
        ex_str_addf(&ignored, "%s\"%s\": [", nignored > 0 ? ", " : "",
                    register_name(x, reg));
        int nbits = 0;
        for (int b = 0; b < 8; b++) {
            if (stray >> b & 1) {
                ex_str_addf(&ignored, "%s%d", nbits > 0 ? ", " : "", b);
                nbits++;
            }
        }
        ex_str_add(&ignored, "]");
        nignored++;
    }
    if (nignored > 0) {
        ex_str_addf(&s, ",\n     \"ignored_bits\":  {%s}", ignored.buf);
    }
    ex_str_add(&s, "}");
    return s.buf;
}

static char *fm_instruments_text(Extract *x, const u8 *order, size_t norder,
                                 u8 *const *bank, int nbank)
{
    if (norder != FM_INS_REGISTERS) {
        ex_fail(x, "loc_BA818: %zu registers, not %d different ones", norder,
                FM_INS_REGISTERS);
    }
    char *names[FM_INS_REGISTERS];
    for (int i = 0; i < FM_INS_REGISTERS; i++) {
        names[i] = register_name(x, order[i]);
    }
    for (int i = 0; i < FM_INS_REGISTERS; i++) {
        for (int j = 0; j < i; j++) {
            if (order[i] == order[j]) {
                ex_fail(x, "loc_BA818: %zu registers, not %d different ones",
                        norder, FM_INS_REGISTERS);
            }
        }
    }
    /* Every register the fields name: the channel's two, then each
     * operator register at each operator's offset. */
    static const u8 wanted_channel[2] = { 0xB0, 0xB4 };
    static const u8 wanted_bases[6] = { 0x30, 0x40, 0x50, 0x60, 0x70, 0x80 };
    for (int i = 0; i < 2 + 6 * 4; i++) {
        u8 want = i < 2 ? wanted_channel[i]
                        : (u8)(wanted_bases[(i - 2) / 4]
                               + OP_OFFSETS[(i - 2) % 4]);
        (void)reg_value(x, order, order, want);
    }
    int ninstrument_names = 0;
    while (EX_INSTRUMENT_NAMES[ninstrument_names] != NULL) {
        ninstrument_names++;
    }
    ExStr s;
    ex_str_init(x, &s);
    ex_str_add(&s, "{\n  \"source\": \"FMInsPtrs, loc_BA818 (SetFMIns)\",\n"
                   "  \"about\": ");
    ex_str_add(&s, jo_quote(x, INSTRUMENTS_ABOUT));
    ex_str_add(&s, ",\n  \"upload_order\": [\n    ");
    /* The channel's two, then each kind of operator register four a line. */
    ex_str_addf(&s, "%s, %s", jo_quote(x, names[0]), jo_quote(x, names[1]));
    for (int i = 2; i < FM_INS_REGISTERS; i += 4) {
        ex_str_add(&s, ",\n    ");
        for (int k = i; k < i + 4 && k < FM_INS_REGISTERS; k++) {
            ex_str_add(&s, k > i ? ", " : "");
            ex_str_add(&s, jo_quote(x, names[k]));
        }
    }
    ex_str_add(&s, "\n  ],\n  \"instruments\": [\n");
    for (int n = 0; n < nbank; n++) {
        ex_str_add(&s, n > 0 ? ",\n" : "");
        ex_str_add(&s, instrument_text(x, n, order, bank[n],
                                       ninstrument_names));
    }
    ex_str_add(&s, "\n  ]\n}\n");
    return s.buf;
}

/* ---- the song text built back (the game's songtext.c) ---- */

/* songtext_build reads through a Ps2Reader, a plain function: these are
 * the files it is given while one build runs, under a folder that is on
 * no disk, so nothing is read from one. */
static const ExsFile *g_files;
static int            g_nfiles;
static bool           g_trackers;
static const char     MEM_DIR[] = "\001music";

static const u8 *mem_reader(const char *path, size_t *size)
{
    size_t dn = sizeof MEM_DIR - 1;
    if (strncmp(path, MEM_DIR, dn) != 0 || path[dn] != '/') {
        return NULL;
    }
    const char *rel = path + dn + 1;
    if (!g_trackers && ex_ends(rel, ".trk")) {
        return NULL;
    }
    for (int i = 0; i < g_nfiles; i++) {
        if (strcmp(g_files[i].path, rel) == 0) {
            *size = strlen(g_files[i].text);
            return (const u8 *)g_files[i].text;
        }
    }
    return NULL;
}

/* The image the files build, with or without the trackers read in their
 * songs' stream text's place; a stop when the song builder refuses them.
 * The stream text alone must give `im` again byte for byte. With the
 * trackers the image is laid out otherwise (a tracker song's blocks go
 * after the streams, its repeats written out) and plays the same, which
 * tools/tracker.py's prove shows by the chip's writes: here it is only
 * built, so that a .trk the game could not read is never written. */
static void check_build(Extract *x, const Image *im, const ExsFile *files,
                        int nfiles, u16 *freqs, int nfreqs, bool trackers,
                        const char *what)
{
    SmpsData d;
    memset(&d, 0, sizeof d);
    d.freqs = freqs;
    d.freq_count = nfreqs;
    g_files = files;
    g_nfiles = nfiles;
    g_trackers = trackers;
    bool ok = songtext_build(MEM_DIR, mem_reader, &d);
    g_files = NULL;
    g_nfiles = 0;
    if (!ok) {
        free(d.image);
        ex_fail(x, "%s: the game's song builder refuses it (above)", what);
    }
    if (trackers) {
        free(d.image);
        return;
    }
    size_t n = d.size < im->size ? d.size : im->size;
    size_t k = 0;
    while (k < n && d.image[k] == im->image[k]) {
        k++;
    }
    size_t built = d.size;
    free(d.image);
    if (built != im->size || k < n) {
        ex_fail(x, "%s builds %zu bytes, not the image's %zu; first "
                   "difference at $%04zX", what, built, im->size, k);
    }
}

/* drum_names(): drums.json's drums by byte and name, as the song text
 * names them (the drums step writes drums.json after this one). */
static char *drums_json(Extract *x)
{
    JOut *doc = jo_obj(x);
    JOut *drums = jo_arr(x);
    for (int i = 0; i < EX_DRUM_NAME_COUNT; i++) {
        JOut *d = jo_obj(x);
        jo_seti(x, d, "byte", EX_DRUM_NAMES[i].byte);
        jo_sets(x, d, "name", EX_DRUM_NAMES[i].name);
        jo_add(drums, d);
    }
    jo_set(x, doc, "drums", drums);
    return jo_dump(x, doc, -1, false);
}

/* ---- songs.json ---- */

static JOut *describe(Extract *x, const Image *im, u32 off)
{
    JOut *tracks = jo_arr(x);
    for (int i = 0; i < im->image[off]; i++) {
        const u8 *h = im->image + off + 1 + 9 * (u32)i;
        JOut *t = jo_obj(x);
        jo_seti(x, t, "flags", h[0]);
        jo_seti(x, t, "channel", h[1]);
        jo_seti(x, t, "tick_multiplier", h[2]);
        jo_seti(x, t, "stream", h[3] | h[4] << 8);
        jo_seti(x, t, "transpose", h[5] > 127 ? h[5] - 256 : h[5]);
        jo_seti(x, t, "mod_envelope", h[6]);
        jo_seti(x, t, "instrument", h[7]);
        jo_seti(x, t, "volume", h[8]);
        jo_add(tracks, t);
    }
    return tracks;
}

static JOut *sound_list(Extract *x, const Image *im, const char *const *labels,
                        int n, int first_id)
{
    JOut *list = jo_arr(x);
    for (int i = 0; i < n; i++) {
        u32 off = ex_addr(x, labels[i]) - im->base;
        JOut *e = jo_obj(x);
        jo_seti(x, e, "id", first_id + i);
        jo_sets(x, e, "name", labels[i]);
        jo_seti(x, e, "offset", off);
        jo_set(x, e, "tracks", describe(x, im, off));
        jo_add(list, e);
    }
    return list;
}

/* ---- the step ---- */

static int cmp_u32(const void *a, const void *b)
{
    u32 l = *(const u32 *)a;
    u32 r = *(const u32 *)b;
    return l < r ? -1 : (l > r ? 1 : 0);
}

static void write_under(Extract *x, const char *dir, const char *rel,
                        const char *text)
{
    ex_write_text(x, ex_path(x, dir, rel), text);
}

void ex_step_music(Extract *x, const ExArgs *a)
{
    const char *outdir = a->out;
    Image im;
    u8 *placed = NULL;
    build_image(x, &im, &placed);
    ex_mkdirs(x, outdir);

    int nmusic = 0;
    int nsfx = 0;
    int nspecial = 0;
    const char **music = pointer_names(x, &im, "MusicPtrs", &nmusic);
    const char **sfx = pointer_names(x, &im, "SFXPtrs", &nsfx);
    const char **special = pointer_names(x, &im, "SpcSFXPtrs", &nspecial);

    /* Every sound's header, sorted, walked for the bytes a stream reads. */
    int nall = nmusic + nsfx + nspecial;
    u32 *headers = ex_alloc(x, (size_t)(nall + 1) * sizeof *headers);
    int nheaders = 0;
    for (int i = 0; i < nall; i++) {
        const char *lbl = i < nmusic ? music[i]
                        : (i < nmusic + nsfx ? sfx[i - nmusic]
                                             : special[i - nmusic - nsfx]);
        u32 off = ex_addr(x, lbl) - im.base;
        if (off >= im.size) {
            ex_fail(x, "%s: past the driver's image", lbl);
        }
        headers[nheaders++] = off;
    }
    qsort(headers, (size_t)nheaders, sizeof *headers, cmp_u32);
    int nunique = 0;
    for (int i = 0; i < nheaders; i++) {
        if (nunique == 0 || headers[nunique - 1] != headers[i]) {
            headers[nunique++] = headers[i];
        }
    }
    u8 *reached = stream_reach(x, &im, headers, nunique);

    /* drop_end_pad: where the streams before PCMDrums end at an odd
     * address, AS's `even` pads them with a zero that is no data. */
    size_t last = im.size - 1;
    if (im.image[last] == 0 && (im.base + last) % 2 != 0 && !reached[last]) {
        im.size = last;
    }
    size_t gaps = 0;
    for (size_t k = 0; k < im.size; k++) {
        gaps += placed[k] == 0;
    }
    ex_say(x, "the driver's image: %zu bytes (%zu not data: the driver's own "
              "code), as text below", im.size, gaps);
    ex_remove(x, ex_path(x, outdir, "SoundData.bin"));

    /* FMFreqs: as many entries as the song text's layout holds; no stream
     * may read a byte of them. */
    u32 freqs_at = ex_addr(x, "FMFreqs") - im.base;
    if ((size_t)freqs_at + 2 * FREQ_ENTRIES > im.size) {
        ex_fail(x, "FMFreqs: its %d entries run past the image", FREQ_ENTRIES);
    }
    for (u32 k = freqs_at; k < freqs_at + 2 * FREQ_ENTRIES; k++) {
        if (reached[k]) {
            ex_fail(x, "FMFreqs: a stream reads its %d entries", FREQ_ENTRIES);
        }
    }
    u16 words[FREQ_ENTRIES];
    const char *entry_notes[FREQ_ENTRIES];
    for (int i = 0; i < FREQ_ENTRIES; i++) {
        words[i] = (u16)(im.image[freqs_at + 2 * (u32)i] << 8
                         | im.image[freqs_at + 2 * (u32)i + 1]);
        entry_notes[i] = note_slot_name(x, i);
    }
    const char *freqs_name = music_file(x, "FMFreqs");
    char *freqs_text = note_frequencies_text(x, words, FREQ_ENTRIES);
    write_under(x, outdir, freqs_name, freqs_text);
    ex_remove(x, ex_path(x, outdir, "FMFreqs.bin"));
    ex_say(x, "FMFreqs: %d entries -> %s", FREQ_ENTRIES,
           ex_path(x, outdir, freqs_name));

    /* SetFMIns's register order, inside the driver's code, so from the
     * ROM: the bytes from loc_BA818 to SetFMIns. Every instrument
     * FMInsPtrs names, 26 bytes from where its word points, as SetFMIns
     * reads them -- straight on past any label. */
    size_t norder = 0;
    const u8 *order = ex_blob(x, "loc_BA818", &norder);
    int nbank = 0;
    u32 *ins_at = table_entries(x, &im, "FMInsPtrs", &nbank);
    u8 **bank = ex_alloc(x, (size_t)(nbank + 1) * sizeof *bank);
    for (int n = 0; n < nbank; n++) {
        if ((size_t)ins_at[n] + FM_INS_REGISTERS > im.size) {
            ex_fail(x, "FMInsPtrs entry %d runs past the image", n);
        }
        bank[n] = im.image + ins_at[n];
    }
    const char *ins_name = music_file(x, "FMInsPtrs");
    char *ins_text = fm_instruments_text(x, order, norder, bank, nbank);
    write_under(x, outdir, ins_name, ins_text);
    ex_say(x, "FMInsPtrs: %d instruments, %zu registers each -> %s", nbank,
           norder, ex_path(x, outdir, ins_name));

    /* The same image as text. */
    int ninstruments = 0;
    while (EX_INSTRUMENT_NAMES[ninstruments] != NULL) {
        ninstruments++;
    }
    int nenvelopes = 0;
    while (EX_ENVELOPE_NAMES[nenvelopes] != NULL) {
        nenvelopes++;
    }
    ExsNames names;
    exs_names_init(x, &names, entry_notes, FREQ_ENTRIES, EX_INSTRUMENT_NAMES,
                   ninstruments, EX_ENVELOPE_NAMES, nenvelopes);
    ExsText text;
    memset(&text, 0, sizeof text);
    exs_disassemble(x, im.image, im.size, im.base, music, nmusic, sfx, nsfx,
                    special, nspecial, &names, freqs_at, &text);

    /* write_song_text: written only if it builds back to the image byte
     * for byte, with the files beside it the song builder reads. */
    int nfiles = text.nfiles + 3 + text.nsongs;
    ExsFile *files = ex_alloc(x, (size_t)nfiles * sizeof *files);
    for (int i = 0; i < text.nfiles; i++) {
        files[i] = text.files[i];
    }
    files[text.nfiles].path = freqs_name;
    files[text.nfiles].text = freqs_text;
    files[text.nfiles + 1].path = ins_name;
    files[text.nfiles + 1].text = ins_text;
    files[text.nfiles + 2].path = "drums.json";
    files[text.nfiles + 2].text = drums_json(x);
    check_build(x, &im, files, text.nfiles + 3, words, FREQ_ENTRIES, false,
                "the song text");
    char *songs_dir = ex_path(x, outdir, "songs");
    ex_mkdirs(x, songs_dir);
    ex_remove_ending(x, songs_dir, ".txt");
    for (int i = 0; i < text.nfiles; i++) {
        write_under(x, outdir, text.files[i].path, text.files[i].text);
    }
    ex_say(x, "song text: %d files, built back to the image byte for byte -> "
              "%s/sounds.txt, envelopes.txt, effects.txt, songs/", text.nfiles,
           outdir);

    /* write_trackers: every song laid out as a tracker, which the game
     * reads in the song's stream text's place -- checked to be read by the
     * game's song builder, as the game reads them. */
    for (int i = 0; i < text.nsongs; i++) {
        const ExsSong *s = &text.songs[i];
        files[text.nfiles + 3 + i].path = ex_fmt(x, "songs/%s.trk", s->name);
        files[text.nfiles + 3 + i].text =
            ex_tracker_song(x, im.image, im.size, s->header_at, s->name,
                            s->tempo, s->pal_tempo, s->pal_tick, &names);
    }
    check_build(x, &im, files, nfiles, words, FREQ_ENTRIES, true,
                "the song text with the trackers");
    ex_remove_ending(x, songs_dir, ".trk");
    for (int i = 0; i < text.nsongs; i++) {
        const ExsFile *f = &files[text.nfiles + 3 + i];
        write_under(x, outdir, f->path, f->text);
    }
    ex_say(x, "tracker files: %d songs -> %s/songs/<song>.trk", text.nsongs,
           outdir);

    /* songs.json */
    JOut *songs = jo_obj(x);
    jo_seti(x, songs, "base", im.base);
    JOut *roots = jo_obj(x);
    for (int i = 0; i < EXS_ROOTS; i++) {
        jo_seti(x, roots, ROOTS[i], ex_addr(x, ROOTS[i]));
    }
    jo_set(x, songs, "roots", roots);
    jo_set(x, songs, "music", sound_list(x, &im, music, nmusic, EXS_FIRST_ID));
    jo_set(x, songs, "sfx", sound_list(x, &im, sfx, nsfx,
                                       EXS_FIRST_EFFECT_ID));
    char *path = ex_path(x, outdir, "songs.json");
    ex_write_text(x, path, jo_dump(x, songs, 1, false));
    ex_say(x, "songs: %d music, %d sfx -> %s", nmusic, nsfx, path);
}
