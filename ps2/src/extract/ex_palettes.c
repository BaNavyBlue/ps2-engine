/* (build 187) tools/extract_palettes.py in C: the named palettes (the
 * Pal_* labels) into palettes.json. A palette runs from its label to its
 * `...End` label where it has one, else to the next label; each two bytes
 * are a CRAM word, shown also through the DAC ladder. */

#include "excommon.h"

#include <string.h>

void ex_step_palettes(Extract *x, const ExArgs *a);

/* PAL_RE, ^Pal_[A-Za-z0-9_]+$ */
static bool is_palette_label(const char *n)
{
    if (!ex_starts(n, "Pal_") || n[4] == '\0') {
        return false;
    }
    for (const char *c = n + 4; *c != '\0'; c++) {
        bool ok = (*c >= 'A' && *c <= 'Z') || (*c >= 'a' && *c <= 'z')
               || (*c >= '0' && *c <= '9') || *c == '_';
        if (!ok) {
            return false;
        }
    }
    return true;
}

/* `name` among the matching labels, exactly (the Python's set). */
static bool among(Extract *x, const char *name)
{
    for (int i = 0; i < x->map.nlabels; i++) {
        const char *n = x->map.labels[i].name;
        if (is_palette_label(n) && strcmp(n, name) == 0) {
            return true;
        }
    }
    return false;
}

void ex_step_palettes(Extract *x, const ExArgs *a)
{
    JOut *doc = jo_obj(x);
    int count = 0;
    ExStr skipped;
    ex_str_init(x, &skipped);
    for (int i = 0; i < x->map.nlabels; i++) {
        const char *name  = x->map.labels[i].name;
        u32         start = x->map.labels[i].addr;
        if (!is_palette_label(name) || ex_ends(name, "End")) {
            continue;
        }
        char *end_name = ex_fmt(x, "%sEnd", name);
        long long end = among(x, end_name) ? ex_addr(x, end_name)
                                           : ex_next_addr(x, name);
        long long len = end - (long long)start;
        const u8 *data = ex_read(x, start, len);
        if (len < 0) {
            len = 0;
        }
        if (len % 2 != 0) {
            ex_str_addf(&skipped, "  skipped %s: an odd number of bytes\n",
                        name);
            continue;
        }
        int words = (int)(len / 2);
        JOut *pal  = jo_obj(x);
        JOut *cram = jo_arr(x);
        JOut *rgb  = jo_arr(x);
        for (int w = 0; w < words; w++) {
            u16 word = (u16)(data[2 * w] << 8 | data[2 * w + 1]);
            u8 c[3];
            ex_cram_rgb(word, c);
            jo_add(cram, jo_str(x, ex_fmt(x, "0x%04X", word)));
            jo_add(rgb, jo_str(x, ex_fmt(x, "#%02X%02X%02X", c[0], c[1],
                                         c[2])));
        }
        jo_seti(x, pal, "count", words);
        jo_set(x, pal, "cram", cram);
        jo_set(x, pal, "rgb", rgb);
        if (jo_get(doc, name) == NULL) {
            count++;
        }
        jo_set(x, doc, name, pal);
    }
    ex_mkdirs(x, a->out);
    char *path = ex_path(x, a->out, "palettes.json");
    ex_write_text(x, path, jo_dump(x, doc, 2, false));
    ex_say(x, "extracted %d palettes -> %s", count, path);
    if (skipped.len > 0) {
        skipped.buf[skipped.len - 1] = '\0';
        ex_warn(x, "%s", skipped.buf);
    }
}
