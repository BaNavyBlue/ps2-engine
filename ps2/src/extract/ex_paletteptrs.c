/* (build 187) tools/extract_paletteptrs.py in C: every PalettePtrs entry
 * by index, each a palette strip named by tools/asset_names.py, and
 * paletteptrs.json. An entry is 8 bytes: a pointer to the CRAM words, the
 * destination in the palette buffer ($FB00 is line 0) and the longs to
 * copy less one. The table has no end mark: it ends at the first entry
 * whose pointer is no label's address (ex_palette_table, excommon.c,
 * which the levels' step reads too since build 188). */

#include "excommon.h"

#include <string.h>

void ex_step_paletteptrs(Extract *x, const ExArgs *a);

#define PAL_BASE 0xFB00

void ex_step_paletteptrs(Extract *x, const ExArgs *a)
{
    ex_mkdirs(x, a->out);
    int n = 0;
    ExPalEntry *entries = ex_palette_table(x, "PalettePtrs", &n);
    JOut *out = jo_arr(x);
    int unnamed = 0;
    int first_unnamed = -1;
    int last_unnamed = -1;
    for (int k = 0; k < n; k++) {
        const ExPalEntry *e = &entries[k];
        /* The words should all be its label's own: running into the next
         * label would mean the count or the pointer is wrong. */
        long long start = ex_addr(x, e->symbol);
        long long avail = ex_floordiv((long long)ex_next_addr(x, e->symbol)
                                      - start, 2);
        if (avail < e->words) {
            ex_warn(x, "  entry %d (%s): short, %lld/%d words", e->index,
                    e->symbol, avail, e->words);
            continue;
        }
        u16 *w = ex_alloc(x, sizeof *w * (size_t)e->words);
        for (int i = 0; i < e->words; i++) {
            w[i] = ex_u16(x, start + 2 * i);
        }
        const char *name = ex_palette_name(e->index);
        if (name == NULL) {
            ex_fail(x, "PalettePtrs entry %d has no name in "
                       "tools/asset_names.py", e->index);
        }
        char *file = ex_fmt(x, "%s.png", name);
        ex_png_palette(x, ex_path(x, a->out, file), w, e->words);

        JOut *j = jo_obj(x);
        jo_seti(x, j, "index", e->index);
        jo_sets(x, j, "symbol", e->symbol);
        jo_seti(x, j, "dest", e->dest);
        jo_seti(x, j, "words", e->words);
        jo_seti(x, j, "line", ex_floordiv(e->dest - PAL_BASE, 32));
        jo_seti(x, j, "entry", ex_floordiv(ex_mod(e->dest - PAL_BASE, 32), 2));
        jo_sets(x, j, "name", name);
        jo_sets(x, j, "file", file);
        jo_add(out, j);
        if (ex_starts(e->symbol, "loc_")) {
            if (first_unnamed < 0) {
                first_unnamed = e->index;
            }
            last_unnamed = e->index;
            unnamed++;
        }
    }
    ex_write_text(x, ex_path(x, a->out, "paletteptrs.json"),
                  jo_dump(x, out, 2, false));
    ex_say(x, "%d PalettePtrs entries", jo_len(out));
    if (unnamed > 0) {
        ex_say(x, "  %d with no name, indices %d..%d", unnamed, first_unnamed,
               last_unnamed);
    } else {
        ex_say(x, "  all named");
    }
}
