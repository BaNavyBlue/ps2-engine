/* (build 188) tools/extract_script.py in C: every block of window text.
 * GameScriptPtrs names each block; a block starts with a table of deltas,
 * the first the table's own size, each after it the length of the script
 * before, as LoadScript walks them. A script's text runs from where the
 * deltas land to the next label, its zeros after the last end code
 * dropped; each byte decoded by the script's charset, the control codes
 * by name, a code with no character as {XX}. */

#include "excommon.h"

#include <string.h>

void ex_step_script(Extract *x, const ExArgs *a);

#define PTR_TABLE "GameScriptPtrs"
#define FIRST_END 0xC4

static const char *control(u8 c)
{
    switch (c) {
    case 0xBB:
        return "{char}";
    case 0xBC:
        return "{char2}";
    case 0xBD:
        return "{enemy}";
    case 0xBE:
        return "{tech}";
    case 0xBF:
        return "{item}";
    case 0xC0:
        return "{meseta}";
    case 0xC1:
        return "\n";
    case 0xC2:
        return "{cont}";
    case 0xC3:
        return "{pause}";
    default:
        return NULL;
    }
}

/* The script text's charset, as extract_script.py's GLYPH: 0 when the
 * code has no character. */
static char glyph(u8 c)
{
    if (c == 0x00) {
        return ' ';
    }
    if (c >= 1 && c <= 10) {
        return (char)('0' + c - 1);
    }
    if (c >= 11 && c <= 36) {
        return (char)('A' + c - 11);
    }
    if (c >= 37 && c <= 62) {
        return (char)('a' + c - 37);
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
        return 0;
    }
}

static void decode(ExStr *s, const u8 *codes, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        u8 c = codes[i];
        const char *ctl = control(c);
        char g = glyph(c);
        if (c >= FIRST_END) {
            ex_str_addf(s, "{end:%02X}", c);
        } else if (ctl != NULL) {
            ex_str_add(s, ctl);
        } else if (g != 0) {
            ex_str_addc(s, g);
        } else {
            ex_str_addf(s, "{%02X}", c);
        }
    }
}

/* An entry's bytes: from its start to the next label, the zeros past its
 * last end code dropped. */
static const u8 *script_bytes(Extract *x, u32 start, size_t *len)
{
    int n = 0;
    const ExLabel **names = ex_names_at(x, start, &n);
    if (n == 0) {
        ex_fail(x, "a script at $%X has no label", (unsigned)start);
    }
    long long length = (long long)ex_next_addr(x, names[n - 1]->name) - start;
    const u8 *data = ex_read(x, start, length);
    size_t size = (length > 0) ? (size_t)length : 0;
    long long last_end = -1;
    for (size_t i = 0; i < size; i++) {
        if (data[i] >= FIRST_END) {
            last_end = (long long)i;
        }
    }
    if (last_end >= 0) {
        bool any = false;
        for (size_t i = (size_t)last_end + 1; i < size; i++) {
            if (data[i] != 0) {
                any = true;
            }
        }
        if (!any) {
            size = (size_t)last_end + 1;
        }
    }
    *len = size;
    return data;
}

void ex_step_script(Extract *x, const ExArgs *a)
{
    ex_mkdirs(x, a->out);
    long long table = ex_addr(x, PTR_TABLE);
    size_t tlen = 0;
    ex_blob(x, PTR_TABLE, &tlen);
    JOut *banks = jo_obj(x);
    long long total = 0;
    for (size_t index = 0; index < tlen / 4; index++) {
        u32 block = ex_u32(x, table + 4 * (long long)index) & 0xFFFFFF;
        int nn = 0;
        const ExLabel **names = ex_names_at(x, block, &nn);
        if (nn != 1) {
            ExStr which;
            ex_str_init(x, &which);
            for (int k = 0; k < nn; k++) {
                ex_str_addf(&which, "%s%s", (k > 0) ? ", " : "",
                            names[k]->name);
            }
            ex_fail(x, "%s entry 0x%02zx at $%X: %s", PTR_TABLE, index,
                    (unsigned)block, (nn > 0) ? which.buf : "no label");
        }
        const char *name = names[0]->name;

        u8 count = ex_u8(x, block);
        const u8 *deltas = ex_read(x, block, count);
        if (count == 0) {
            continue;
        }
        ExStr text;
        ex_str_init(x, &text);
        ex_str_addf(&text, "# %s, bank 0x%02zx; script_id = 0x%02zxNN, NN "
                           "1-based\n", name, index, index);
        long long at = block;
        for (int n = 0; n < count; n++) {
            at += deltas[n];
            size_t len = 0;
            const u8 *bytes = script_bytes(x, (u32)at, &len);
            ex_str_addf(&text, "\n[%d]\n", n + 1);
            decode(&text, bytes, len);
            ex_str_addc(&text, '\n');
        }
        char *file = ex_fmt(x, "%s.txt", name);
        ex_write_text(x, ex_path(x, a->out, file), text.buf);

        JOut *b = jo_obj(x);
        jo_seti(x, b, "bank", (long long)index);
        jo_seti(x, b, "count", count);
        jo_sets(x, b, "file", file);
        jo_set(x, banks, name, b);
        total += count;
    }
    ex_write_text(x, ex_path(x, a->out, "script.json"),
                  jo_dump(x, banks, 2, false));
    ex_say(x, "%d script blocks, %lld scripts", jo_len(banks), total);
}
