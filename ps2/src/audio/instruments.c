/* (build 136d) The FM instruments as a file: assets/music/fm_instruments.json,
 * each register's fields named, read back into the 26 bytes a piece the
 * upload routine (SetFMIns) sends (instruments.h, instruments_load).
 * tools/extract_music.py writes it. */
#include "instruments.h"
#include "../core/json.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The registers SetFMIns writes, by the fields they hold: the channel's
 * two, and six an operator. Operators 1-4 are the chip's numbering, at
 * register offsets +0, +8, +4, +C; a name in "upload_order" and
 * "ignored_bits" is the channel register's, or opN.<name> for an
 * operator's. */
typedef struct {
    const char *name;
    u8          base;
    bool        per_operator;
} InsRegister;

static const InsRegister kRegisters[] = {
    { "feedback_algorithm",    0xB0, false },
    { "pan_lfo",               0xB4, false },
    { "detune_multiple",       0x30, true },
    { "total_level",           0x40, true },
    { "rate_scaling_attack",   0x50, true },
    { "am_decay",              0x60, true },
    { "sustain_rate",          0x70, true },
    { "sustain_level_release", 0x80, true },
};
#define REGISTER_KINDS (int)(sizeof kRegisters / sizeof kRegisters[0])

static const u8 kOperatorOffset[4] = { 0x0, 0x8, 0x4, 0xC };

/* A field: its register, its lowest bit and how many bits. */
typedef struct {
    const char *name;
    u8          base;
    u8          shift;
    u8          bits;
} InsField;

static const InsField kChannelFields[] = {
    { "algorithm",          0xB0, 0, 3 },
    { "feedback",           0xB0, 3, 3 },
    { "pan",                0xB4, 6, 2 },
    { "lfo_am_sensitivity", 0xB4, 4, 2 },
    { "lfo_fm_sensitivity", 0xB4, 0, 3 },
};
#define CHANNEL_FIELDS (int)(sizeof kChannelFields / sizeof kChannelFields[0])

static const InsField kOperatorFields[] = {
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
#define OPERATOR_FIELDS (int)(sizeof kOperatorFields / sizeof kOperatorFields[0])

/* $B4 bit 7 is the left speaker, bit 6 the right: the pan field's value. */
static const char *const kPanNames[4] = { "none", "right", "left", "center" };

/* A whole file, through the pack reader first, else the disk: a malloc'd
 * copy with a NUL after it (the JSON reader wants a string), or NULL. */
static char *read_text(const char *path, Ps2Reader reader)
{
    size_t size = 0;
    const u8 *packed = (reader != NULL) ? reader(path, &size) : NULL;
    if (packed != NULL) {
        char *copy = malloc(size + 1);
        if (copy != NULL) {
            memcpy(copy, packed, size);
            copy[size] = 0;
        }
        return copy;
    }

    FILE *fh = fopen(path, "rb");
    if (fh == NULL) {
        return NULL;
    }
    char *buf = NULL;
    long len = (fseek(fh, 0, SEEK_END) == 0) ? ftell(fh) : -1;
    if (len >= 0 && fseek(fh, 0, SEEK_SET) == 0) {
        buf = malloc((size_t)len + 1);
    }
    if (buf != NULL && fread(buf, 1, (size_t)len, fh) != (size_t)len) {
        free(buf);
        buf = NULL;
    }
    fclose(fh);
    if (buf != NULL) {
        buf[len] = 0;
    }
    return buf;
}

/* The register a name in "upload_order" or "ignored_bits" means, channel
 * 1's number; -1 for a name that is none of the 26. */
static int register_number(const char *name)
{
    int op = 0;
    if (strncmp(name, "op", 2) == 0 && name[2] >= '1' && name[2] <= '4'
        && name[3] == '.') {
        op = name[2] - '0';
        name += 4;
    }
    for (int k = 0; k < REGISTER_KINDS; k++) {
        if (strcmp(name, kRegisters[k].name) != 0) {
            continue;
        }
        if (kRegisters[k].per_operator != (op != 0)) {
            return -1;
        }
        return kRegisters[k].base
             + (kRegisters[k].per_operator ? kOperatorOffset[op - 1] : 0);
    }
    return -1;
}

/* The bits of a register (a base: $B0, $B4, $30-$80) the chip reads: its
 * fields'. */
static u8 read_mask(u8 base)
{
    unsigned mask = 0;
    for (int i = 0; i < CHANNEL_FIELDS; i++) {
        if (kChannelFields[i].base == base) {
            mask |= ((1u << kChannelFields[i].bits) - 1u)
                  << kChannelFields[i].shift;
        }
    }
    for (int i = 0; i < OPERATOR_FIELDS; i++) {
        if (kOperatorFields[i].base == base) {
            mask |= ((1u << kOperatorFields[i].bits) - 1u)
                  << kOperatorFields[i].shift;
        }
    }
    return (u8)mask;
}

/* A whole number 0..top, else -1. */
static long whole(const JsonValue *v, long top)
{
    if (v == NULL || v->type != JSON_NUMBER || v->number < 0.0
        || v->number > (double)top || v->number != (double)(long)v->number) {
        return -1;
    }
    return (long)v->number;
}

static bool load_order(const JsonValue *root, const char *path,
                       u8 order[INSTRUMENT_REGISTERS])
{
    const JsonValue *list = json_get(root, "upload_order");
    int n = json_length(list);
    if (list == NULL || list->type != JSON_ARRAY
        || n != INSTRUMENT_REGISTERS) {
        fprintf(stderr, "instruments: %s: \"upload_order\" holds %d "
                        "registers, not %d\n", path, n, INSTRUMENT_REGISTERS);
        return false;
    }
    for (int i = 0; i < n; i++) {
        const JsonValue *v = json_at(list, i);
        int reg = (v != NULL && v->type == JSON_STRING)
                ? register_number(v->string) : -1;
        if (reg < 0) {
            fprintf(stderr, "instruments: %s: \"upload_order\" entry %d "
                            "names no instrument register\n", path, i);
            return false;
        }
        for (int j = 0; j < i; j++) {
            if (order[j] == (u8)reg) {
                fprintf(stderr, "instruments: %s: \"upload_order\" names "
                                "%s twice\n", path, v->string);
                return false;
            }
        }
        order[i] = (u8)reg;
    }
    return true;
}

/* The channel's fields into `val` (indexed by register number). */
static bool load_channel_fields(const JsonValue *ins, int n, const char *path,
                                u8 *val)
{
    for (int f = 0; f < CHANNEL_FIELDS; f++) {
        const InsField *fd = &kChannelFields[f];
        const JsonValue *v = json_get(ins, fd->name);
        long x = -1;
        if (strcmp(fd->name, "pan") == 0) {
            for (int k = 0; v != NULL && v->type == JSON_STRING && k < 4;
                 k++) {
                if (strcmp(v->string, kPanNames[k]) == 0) {
                    x = k;
                }
            }
        } else {
            x = whole(v, (1L << fd->bits) - 1);
        }
        if (x < 0) {
            fprintf(stderr, "instruments: %s: instrument %d's \"%s\" is "
                            "missing or out of range\n", path, n, fd->name);
            return false;
        }
        val[fd->base] |= (u8)(x << fd->shift);
    }
    return true;
}

/* The operators' rows of four into `val`. */
static bool load_operator_fields(const JsonValue *ins, int n,
                                 const char *path, u8 *val)
{
    for (int f = 0; f < OPERATOR_FIELDS; f++) {
        const InsField *fd = &kOperatorFields[f];
        const JsonValue *row = json_get(ins, fd->name);
        bool is_flag = strcmp(fd->name, "lfo_am") == 0;
        if (row == NULL || row->type != JSON_ARRAY || json_length(row) != 4) {
            fprintf(stderr, "instruments: %s: instrument %d's \"%s\" is not "
                            "a row of four, operators 1-4\n", path, n,
                    fd->name);
            return false;
        }
        for (int op = 0; op < 4; op++) {
            const JsonValue *v = json_at(row, op);
            long x = -1;
            if (is_flag) {
                x = (v != NULL && v->type == JSON_BOOL) ? (long)v->boolean
                                                         : -1;
            } else {
                x = whole(v, (1L << fd->bits) - 1);
            }
            if (x < 0) {
                fprintf(stderr, "instruments: %s: instrument %d's \"%s\" "
                                "for operator %d is %s\n", path, n, fd->name,
                        op + 1, is_flag ? "not true or false"
                                        : "missing or out of range");
                return false;
            }
            val[fd->base + kOperatorOffset[op]] |= (u8)(x << fd->shift);
        }
    }
    return true;
}

/* "ignored_bits", if there: register name -> bit numbers, each one the
 * chip does not read. */
static bool load_ignored_bits(const JsonValue *ins, int n, const char *path,
                              u8 *val)
{
    const JsonValue *ign = json_get(ins, "ignored_bits");
    if (ign == NULL) {
        return true;
    }
    if (ign->type != JSON_OBJECT) {
        fprintf(stderr, "instruments: %s: instrument %d's \"ignored_bits\" "
                        "is not an object\n", path, n);
        return false;
    }
    for (const JsonValue *m = ign->child; m != NULL; m = m->next) {
        int reg = (m->key != NULL) ? register_number(m->key) : -1;
        if (reg < 0 || m->type != JSON_ARRAY) {
            fprintf(stderr, "instruments: %s: instrument %d's \"ignored_bits\" "
                            "\"%s\" is not a register's list of bits\n", path,
                    n, (m->key != NULL) ? m->key : "");
            return false;
        }
        u8 base = (reg >= 0xB0) ? (u8)reg : (u8)(reg & 0xF0);
        for (int i = 0; i < json_length(m); i++) {
            long bit = whole(json_at(m, i), 7);
            if (bit < 0 || (read_mask(base) >> bit) & 1) {
                fprintf(stderr, "instruments: %s: instrument %d's "
                                "\"ignored_bits\" \"%s\" names a bit that is "
                                "not 0-7 or that the chip reads\n", path, n,
                        m->key);
                return false;
            }
            val[reg] |= (u8)(1u << bit);
        }
    }
    return true;
}

/* (build 137a) One instrument's "name", checked against those before it. */
static bool load_name(const JsonValue *ins, int i, const char *path,
                      char (*names)[INSTRUMENT_NAME_MAX], bool *named)
{
    const JsonValue *nm = json_get(ins, "name");
    if (i == 0) {
        *named = (nm != NULL);
    }
    if ((nm != NULL) != *named) {
        fprintf(stderr, "instruments: %s: instrument %d %s a \"name\", "
                        "instrument 0 %s: names on all or none\n", path, i,
                (nm != NULL) ? "has" : "has no", *named ? "has" : "has not");
        return false;
    }
    if (nm == NULL) {
        names[i][0] = 0;
        return true;
    }
    const char *s = (nm->type == JSON_STRING) ? nm->string : NULL;
    size_t len = (s != NULL) ? strlen(s) : 0;
    bool word = len > 0 && len < INSTRUMENT_NAME_MAX
             && !(s[0] >= '0' && s[0] <= '9');
    for (size_t k = 0; word && k < len; k++) {
        char c = s[k];
        word = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')
            || (c >= '0' && c <= '9') || c == '_';
    }
    if (!word) {
        fprintf(stderr, "instruments: %s: instrument %d's \"name\" is not "
                        "one word (letters, digits, _; at most %d)\n", path,
                i, INSTRUMENT_NAME_MAX - 1);
        return false;
    }
    for (int k = 0; k < i; k++) {
        if (strcmp(names[k], s) == 0) {
            fprintf(stderr, "instruments: %s: instruments %d and %d are both "
                            "named %s\n", path, k, i, s);
            return false;
        }
    }
    memcpy(names[i], s, len + 1);
    return true;
}

bool instruments_load(const char *path, Ps2Reader reader,
                      u8 order[INSTRUMENT_REGISTERS], u8 **bank, int *count)
{
    return instruments_load_named(path, reader, order, bank, count, NULL);
}

bool instruments_load_named(const char *path, Ps2Reader reader,
                            u8 order[INSTRUMENT_REGISTERS], u8 **bank,
                            int *count, char (**names_out)[INSTRUMENT_NAME_MAX])
{
    size_t where = 0;
    bool named = false;

    *bank  = NULL;
    *count = 0;
    if (names_out != NULL) {
        *names_out = NULL;
    }

    char *text = read_text(path, reader);
    if (text == NULL) {
        fprintf(stderr, "instruments: cannot open %s\n", path);
        return false;
    }
    JsonValue *root = json_parse(text, &where);
    free(text);
    if (root == NULL) {
        fprintf(stderr, "instruments: %s is not JSON (at byte %zu)\n", path,
                where);
        return false;
    }

    const JsonValue *list = json_get(root, "instruments");
    int  n  = json_length(list);
    bool ok = load_order(root, path, order);
    if (ok && (list == NULL || list->type != JSON_ARRAY || n < 1
               || n > INSTRUMENTS_MAX)) {
        fprintf(stderr, "instruments: %s: \"instruments\" holds %d, not "
                        "1-%d\n", path, n, INSTRUMENTS_MAX);
        ok = false;
    }
    u8 *out = ok ? malloc((size_t)n * INSTRUMENT_REGISTERS) : NULL;
    char (*names)[INSTRUMENT_NAME_MAX] =
        ok ? calloc((size_t)n, INSTRUMENT_NAME_MAX) : NULL;
    if (ok && (out == NULL || names == NULL)) {
        ok = false;
    }
    for (int i = 0; ok && i < n; i++) {
        const JsonValue *ins = json_at(list, i);
        u8 val[256];
        memset(val, 0, sizeof val);
        if (whole(json_get(ins, "number"), INSTRUMENTS_MAX - 1) != i) {
            fprintf(stderr, "instruments: %s: instrument %d's \"number\" is "
                            "not %d, its place\n", path, i, i);
            ok = false;
            break;
        }
        ok = load_channel_fields(ins, i, path, val)
          && load_operator_fields(ins, i, path, val)
          && load_ignored_bits(ins, i, path, val)
          && load_name(ins, i, path, names, &named);
        for (int r = 0; ok && r < INSTRUMENT_REGISTERS; r++) {
            out[(size_t)i * INSTRUMENT_REGISTERS + (size_t)r] = val[order[r]];
        }
    }

    json_free(root);
    if (!ok) {
        free(out);
        free(names);
        return false;
    }
    *bank  = out;
    *count = n;
    if (names_out != NULL) {
        *names_out = names;
    } else {
        free(names);
    }
    return true;
}
