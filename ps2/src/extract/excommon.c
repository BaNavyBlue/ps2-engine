/* (build 187) tools/extract_gamedata.py's shared helpers, ps2png.py's
 * palette strip and asset_names.py's lookups, for the C steps
 * (excommon.h). */

#include "excommon.h"
#include "../core/pngwrite.h"

#include <stdlib.h>
#include <string.h>

/* ---- asset_names.py ---- */

const char *ex_pair(const ExNamePair *list, const char *key)
{
    for (int i = 0; list[i].key != NULL; i++) {
        if (strcmp(list[i].key, key) == 0) {
            return list[i].value;
        }
    }
    return NULL;
}

bool ex_listed(const char *const *list, const char *s)
{
    for (int i = 0; list[i] != NULL; i++) {
        if (strcmp(list[i], s) == 0) {
            return true;
        }
    }
    return false;
}

const char *ex_art_name(const char *stem)
{
    const char *renamed = ex_pair(EX_ART_RENAMES, stem);
    return (renamed != NULL) ? renamed : stem;
}

const char *ex_palette_name(int index)
{
    if (index < 0 || index >= EX_PALETTE_NAME_COUNT) {
        return NULL;
    }
    return EX_PALETTE_NAMES[index];
}

/* ---- the game's name codes ---- */

#define NAME_END 0xC4

static char code_char(u8 c)
{
    if (c == 0) {
        return ' ';
    }
    if (c >= 1 && c <= 10) {
        return (char)('0' + (c - 1));
    }
    if (c >= 11 && c <= 36) {
        return (char)('A' + (c - 11));
    }
    if (c >= 37 && c <= 62) {
        return (char)('a' + (c - 37));
    }
    switch (c) {
    case 0x3F:
        return ',';
    case 0x40:
        return '.';
    case 0x41:
        return ';';
    case 0x42:
        return '"';
    case 0x43:
        return '?';
    case 0x44:
        return '!';
    case 0x45:
        return '\'';
    case 0x46:
        return '-';
    case 0x77:
        return ':';
    default:
        return '?';
    }
}

char *ex_decode_name(Extract *x, const u8 *codes, size_t n)
{
    char *s = ex_alloc(x, n + 1);
    size_t k = 0;
    for (size_t i = 0; i < n && codes[i] != NAME_END; i++) {
        s[k++] = code_char(codes[i]);
    }
    s[k] = '\0';
    return s;
}

/* ---- families of constants ---- */

ExValueNames ex_constant_names(Extract *x, const char *prefix,
                               const char *const *skip)
{
    ExMap *m = &x->map;
    ExValueNames out;
    out.at    = ex_alloc(x, sizeof *out.at * (size_t)(m->nconsts + 1));
    out.count = 0;
    size_t plen = strlen(prefix);
    for (int i = 0; i < m->nconsts; i++) {
        const ExConst *c = &m->consts[m->const_by_name[i].rank];
        if (!ex_starts(c->name, prefix)) {
            continue;
        }
        bool skipped = false;
        for (int s = 0; skip != NULL && skip[s] != NULL; s++) {
            if (ex_ends(c->name, skip[s])) {
                skipped = true;
            }
        }
        if (skipped) {
            continue;
        }
        const char *had = ex_value_name(&out, c->value);
        if (had != NULL) {
            ex_fail(x, "%s%s and %s are both %lld", prefix, had, c->name,
                    c->value);
        }
        out.at[out.count].value = c->value;
        out.at[out.count].name  = c->name + plen;
        out.count++;
    }
    return out;
}

const char *ex_value_name(const ExValueNames *v, long long value)
{
    for (int i = 0; i < v->count; i++) {
        if (v->at[i].value == value) {
            return v->at[i].name;
        }
    }
    return NULL;
}

const char **ex_id_keys(Extract *x, const char *prefix, int *count)
{
    ExValueNames names = ex_constant_names(x, prefix, NULL);
    const char **keys = ex_alloc(x, sizeof *keys * (size_t)(names.count + 1));
    for (int i = 0; i < names.count; i++) {
        const char *n = ex_value_name(&names, i);
        if (n == NULL) {
            ex_fail(x, "the %s constants are not 0..%d", prefix,
                    names.count - 1);
        }
        keys[i] = n;
    }
    *count = names.count;
    return keys;
}

/* ---- runs of labels ---- */

bool ex_part_prefix(const char *name, const void *prefix)
{
    return ex_starts(name, prefix);
}

bool ex_part_contains(const char *name, const void *needle)
{
    return strstr(name, needle) != NULL;
}

const ExLabel **ex_run_of_labels(Extract *x, const char *first,
                                 ExPartOf part_of, const void *ctx,
                                 int *count, u32 *end)
{
    ExMap *m = &x->map;
    int i = ex_index(x, first) + 1;
    const ExLabel **out = ex_alloc(x, sizeof *out * (size_t)(m->nlabels + 1));
    int n = 0;
    for (; i < m->nlabels; i++) {
        if (!part_of(m->labels[i].name, ctx)) {
            *count = n;
            *end   = m->labels[i].addr;
            return out;
        }
        out[n++] = &m->labels[i];
    }
    *count = n;
    *end   = (u32)m->size;
    return out;
}

/* ---- what is at an address ---- */

const u8 *ex_block_at(Extract *x, u32 address, size_t *length)
{
    int n = 0;
    const ExLabel **names = ex_names_at(x, address, &n);
    if (n == 0) {
        ex_fail(x, "no label at $%X", (unsigned)address);
    }
    long long len = (long long)ex_next_addr(x, names[n - 1]->name)
                  - (long long)address;
    const u8 *p = ex_read(x, address, len);
    *length = (len > 0) ? (size_t)len : 0;
    return p;
}

const char *ex_label_at(Extract *x, u32 address, const char *prefix)
{
    int n = 0;
    const ExLabel **names = ex_names_at(x, address, &n);
    const char *found = NULL;
    int matches = 0;
    for (int i = 0; i < n; i++) {
        if (ex_starts(names[i]->name, prefix)) {
            found = names[i]->name;
            matches++;
        }
    }
    if (matches > 1) {
        ExStr s;
        ex_str_init(x, &s);
        for (int i = 0; i < n; i++) {
            if (ex_starts(names[i]->name, prefix)) {
                ex_str_addf(&s, "%s%s", (s.len > 0) ? ", " : "",
                            names[i]->name);
            }
        }
        ex_fail(x, "$%X is %s: which is meant?", (unsigned)address, s.buf);
    }
    if (matches == 0) {
        return NULL;
    }
    return found;
}

const char *ex_file_at(Extract *x, u32 address)
{
    const char *found = NULL;
    int matches = 0;
    for (int i = 0; i < x->map.nfiles; i++) {
        if (x->map.files[i].addr == address) {
            if (found == NULL) {
                found = x->map.files[i].name;
            }
            matches++;
        }
    }
    if (matches > 1) {
        ex_fail(x, "$%X is several files: which is meant?", (unsigned)address);
    }
    return found;
}

char *ex_file_stem(Extract *x, const char *name)
{
    const char *base = name;
    for (const char *c = name; *c != '\0'; c++) {
        if (*c == '/' || *c == '\\') {
            base = c + 1;
        }
    }
    const char *dot = strrchr(base, '.');
    return (dot != NULL) ? ex_strndup(x, base, (size_t)(dot - base))
                         : ex_strdup(x, base);
}

/* ---- extract_paletteptrs.py's ---- */

/* Whether a label is `<label>End`, closing another label's block. */
static bool closes_block(Extract *x, const char *name)
{
    if (!ex_ends(name, "End")) {
        return false;
    }
    return ex_has(x, ex_strndup(x, name, strlen(name) - 3));
}

const char *ex_label_closing_at(Extract *x, u32 address)
{
    int n = 0;
    const ExLabel **names = ex_names_at(x, address, &n);
    if (n <= 1) {
        return (n == 1) ? names[0]->name : NULL;
    }
    const char *start = NULL;
    int starts = 0;
    for (int i = 0; i < n; i++) {
        if (!closes_block(x, names[i]->name)) {
            start = names[i]->name;
            starts++;
        }
    }
    if (starts != 1) {
        ExStr s;
        ex_str_init(x, &s);
        for (int i = 0; i < n; i++) {
            ex_str_addf(&s, "%s%s", (i > 0) ? ", " : "", names[i]->name);
        }
        ex_fail(x, "$%X is %s: which is meant?", (unsigned)address, s.buf);
    }
    return start;
}

#define PAL_ENTRY_BYTES 8

ExPalEntry *ex_palette_table(Extract *x, const char *label, int *count)
{
    long long at = ex_addr(x, label);
    int cap = 64;
    int n = 0;
    ExPalEntry *e = ex_alloc(x, sizeof *e * (size_t)cap);
    while (at + PAL_ENTRY_BYTES <= (long long)x->map.size) {
        const char *symbol = ex_label_closing_at(x, ex_u32(x, at));
        if (symbol == NULL) {
            break;
        }
        if (n == cap) {
            ExPalEntry *more = ex_alloc(x, sizeof *e * (size_t)cap * 2);
            memcpy(more, e, sizeof *e * (size_t)cap);
            e = more;
            cap *= 2;
        }
        e[n].index  = n;
        e[n].symbol = symbol;
        e[n].dest   = ex_u16(x, at + 4);
        e[n].words  = (ex_u16(x, at + 6) + 1) * 2;
        n++;
        at += PAL_ENTRY_BYTES;
    }
    *count = n;
    return e;
}

/* ---- ps2png.py ---- */

static const u8 DAC_LEVELS[8] = { 0, 52, 87, 116, 144, 172, 206, 255 };

void ex_cram_rgb(u16 word, u8 rgb[3])
{
    rgb[0] = DAC_LEVELS[(word >> 1) & 7];
    rgb[1] = DAC_LEVELS[(word >> 5) & 7];
    rgb[2] = DAC_LEVELS[(word >> 9) & 7];
}

void ex_png_palette(Extract *x, const char *path, const u16 *words, int n)
{
    const int per_line = 16;
    int rows = (n + per_line - 1) / per_line;
    u8 *plte = ex_alloc(x, 256 * 3);
    for (int i = 0; i < n && i < 256; i++) {
        ex_cram_rgb(words[i], &plte[3 * i]);
    }
    u8 *px = ex_alloc(x, (size_t)(per_line * (rows > 0 ? rows : 1)));
    for (int i = 0; i < n; i++) {
        px[i] = (u8)i;
    }
    if (!png_write(path, per_line, rows, 8, 3, px, (size_t)per_line, plte,
                   256, NULL)) {
        ex_fail(x, "%s: cannot write it", path);
    }
}
