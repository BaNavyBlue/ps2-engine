#include "patches.h"
#include "../core/json.h"

#include <stdio.h>
#include <string.h>

/* (build 112) The patch file's reader (patches.h). */

/* Each operator's register offset from channel 1's: operators 1-4 are the
 * bank's $x0, $x8, $x4, $xC (SetFMIns's order). */
static const int OP_OFFSET[4] = { 0x0, 0x8, 0x4, 0xC };

/* A number in [lo, hi] from obj[key], or `keep` when the key is missing or
 * not a number; out of range it is brought in, and says so. */
static int field(const JsonValue *obj, const char *key, int lo, int hi,
                 int keep, int instrument)
{
    const JsonValue *v = json_get(obj, key);

    if (v == NULL || v->type != JSON_NUMBER) {
        return keep;
    }
    long n = json_int(obj, key, keep);
    if (n < lo || n > hi) {
        fprintf(stderr, "patches: instrument %d's %s %ld is outside %d-%d\n",
                instrument, key, n, lo, hi);
        n = (n < lo) ? lo : hi;
    }
    return (int)n;
}

static float real(const JsonValue *obj, const char *key, float keep)
{
    const JsonValue *v = json_get(obj, key);

    return (v != NULL && v->type == JSON_NUMBER) ? (float)v->number : keep;
}

/* The chip's DT field (0-3 up, 4-7 down: 5 is -1) as -3..+3, and back. */
static int detune_from_chip(int dt)
{
    return (dt & 4) ? -(dt & 3) : (dt & 3);
}

static int detune_to_chip(int d)
{
    return (d < 0) ? (4 | (-d & 3)) : (d & 3);
}

static void load_operator(SynthPatch *p, int op, const JsonValue *o)
{
    int  n   = p->instrument;
    u8  *r   = p->reg;
    int  off = OP_OFFSET[op];
    u8   dm  = r[0x30 + off];
    u8   ka  = r[0x50 + off];
    u8   slr = r[0x80 + off];

    int mul = field(o, "multiple", 0, 15, dm & 15, n);
    int dt  = field(o, "detune", -3, 3, detune_from_chip((dm >> 4) & 7), n);
    r[0x30 + off] = (u8)((detune_to_chip(dt) << 4) | mul);
    r[0x40 + off] = (u8)field(o, "level", 0, 127, r[0x40 + off] & 0x7F, n);
    int ks = field(o, "key_scale", 0, 3, ka >> 6, n);
    int ar = field(o, "attack", 0, 31, ka & 31, n);
    r[0x50 + off] = (u8)((ks << 6) | ar);
    r[0x60 + off] = (u8)((r[0x60 + off] & 0xE0)
                         | field(o, "decay", 0, 31, r[0x60 + off] & 31, n));
    r[0x70 + off] = (u8)field(o, "sustain_rate", 0, 31, r[0x70 + off] & 31, n);
    int sl = field(o, "sustain_level", 0, 15, slr >> 4, n);
    int rr = field(o, "release", 0, 15, slr & 15, n);
    r[0x80 + off] = (u8)((sl << 4) | rr);
    p->fine_cents[op] = real(o, "fine_cents", 0.0f);
}

int patches_load(const char *path, const SmpsData *data, SynthPatch *out,
                 int max)
{
    JsonValue *root = json_parse_file(path);
    int        count = 0;

    if (root == NULL) {
        return 0;
    }
    const JsonValue *list = json_get(root, "patches");
    int n = (list != NULL) ? json_length(list) : 0;

    for (int i = 0; i < n; i++) {
        const JsonValue *e = json_at(list, i);
        const JsonValue *id = json_get(e, "instrument");
        u8 bytes[26];

        if (id == NULL || id->type != JSON_NUMBER) {
            fprintf(stderr, "patches: entry %d has no instrument\n", i);
            continue;
        }
        int ins = (int)id->number;
        if (!smps_instrument(data, ins, bytes)) {
            fprintf(stderr, "patches: no instrument %d in the bank (0-%d)\n",
                    ins, SMPS_INSTRUMENTS - 1);
            continue;
        }
        if (!json_bool(e, "enabled", true)) {
            continue;
        }
        /* one an instrument: a later entry replaces an earlier */
        int at = count;
        for (int k = 0; k < count; k++) {
            if (out[k].instrument == ins) {
                at = k;
            }
        }
        if (at >= max) {
            continue;
        }
        SynthPatch *p = &out[at];
        memset(p, 0, sizeof *p);
        p->instrument = ins;
        for (int b = 0; b < 26; b++) {
            p->rom[smps_ins_regs[b]] = bytes[b];
            p->reg[smps_ins_regs[b]] = bytes[b];
        }
        int alg = field(e, "algorithm", 0, 7, p->reg[0xB0] & 7, ins);
        int fb  = field(e, "feedback", 0, 7, (p->reg[0xB0] >> 3) & 7, ins);
        p->reg[0xB0] = (u8)((p->reg[0xB0] & 0xC0) | (fb << 3) | alg);
        p->volume_db = real(e, "volume_db", 0.0f);
        const char *tail = json_str(e, "tail", "row");
        if (strcmp(tail, "ring") == 0) {
            p->tail = SYNTH_TAIL_RING;
        } else if (strcmp(tail, "fade") == 0) {
            p->tail = SYNTH_TAIL_FADE;
        } else {
            p->tail = SYNTH_TAIL_ROW;
        }
        const JsonValue *ops = json_get(e, "operators");
        for (int op = 0; op < 4; op++) {
            const JsonValue *o = (ops != NULL) ? json_at(ops, op) : NULL;
            if (o != NULL) {
                load_operator(p, op, o);
            }
        }
        if (at == count) {
            count++;
        }
    }
    json_free(root);
    return count;
}
