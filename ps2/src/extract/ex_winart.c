/* (build 189) tools/extract_winart.py in C: the WinArt_* window maps --
 * a byte a cell, a tile of the font's (or a portrait's) -- into
 * assets/winart.json and, a Tiled map each, assets/winart/<name>.tmx. A
 * map's size is the one of the window records' (WindowArtLayoutPtrs, 8
 * bytes: Y, X, the map, its width plus one and height less one) that
 * fits its label's block; a portrait's rows inside its frame are drawn
 * from the portrait's sheet, the rest from the font's. */

#include "excommon.h"
#include "exsheets.h"

#include <stdlib.h>
#include <string.h>

void ex_step_winart(Extract *x, const ExArgs *a);

#define RECORD_MAP    2
#define RECORD_WIDTH  6
#define RECORD_HEIGHT 7
#define FONT_SHEET    "FontsIconsArt"
#define MAX_SIZES     16

typedef struct {
    const char *name;
    int         w[MAX_SIZES];
    int         h[MAX_SIZES];
    int         n;
} Sizes;

static int cmp_u32(const void *a, const void *b)
{
    u32 p = *(const u32 *)a;
    u32 q = *(const u32 *)b;
    return (p > q) - (p < q);
}

static void add_size(Extract *x, Sizes *s, int w, int h)
{
    for (int i = 0; i < s->n; i++) {
        if (s->w[i] == w && s->h[i] == h) {
            return;
        }
    }
    if (s->n == MAX_SIZES) {
        ex_fail(x, "%s: more than %d window sizes", s->name, MAX_SIZES);
    }
    /* kept sorted, as sorted() of the set gives them */
    int at = s->n;
    while (at > 0 && (s->w[at - 1] > w
                      || (s->w[at - 1] == w && s->h[at - 1] > h))) {
        s->w[at] = s->w[at - 1];
        s->h[at] = s->h[at - 1];
        at--;
    }
    s->w[at] = w;
    s->h[at] = h;
    s->n++;
}

/* window_sizes: each map's sizes, by the records that draw it. A pointer
 * into RAM is into window_art_buffer, which LoadDynWindowsInRam fills
 * from DynamicWindowsStart on. */
static Sizes *window_sizes(Extract *x, int *count)
{
    const ExMap *m = &x->map;
    u32 ram = (u32)(ex_const(x, "window_art_buffer") & 0xFFFFFF);
    u32 dynamic = (u32)ex_addr(x, "DynamicWindowsStart");
    u32 *records = ex_alloc(x, sizeof *records * (size_t)(m->nlabels + 1));
    int nrec = 0;
    for (int i = 0; i < m->nlabels; i++) {
        if (ex_starts(m->labels[i].name, "PtrWin_")) {
            records[nrec++] = m->labels[i].addr;
        }
    }
    qsort(records, (size_t)nrec, sizeof *records, cmp_u32);
    Sizes *sizes = ex_alloc(x, sizeof *sizes * (size_t)(m->nlabels + 1));
    int n = 0;
    for (int r = 0; r < nrec; r++) {
        if (r > 0 && records[r] == records[r - 1]) {
            continue;
        }
        u32 at = records[r];
        u32 ptr = ex_u32(x, at + RECORD_MAP) & 0xFFFFFF;
        if (ptr >= ram) {
            ptr = ptr - ram + dynamic;
        }
        int w = ex_u8(x, at + RECORD_WIDTH) - 1;
        int h = ex_u8(x, at + RECORD_HEIGHT) + 1;
        int nn = 0;
        const ExLabel **names = ex_names_at(x, ptr, &nn);
        for (int k = 0; k < nn; k++) {
            const char *name = names[k]->name;
            if (!ex_starts(name, "WinArt_")) {
                continue;
            }
            Sizes *s = NULL;
            for (int i = 0; i < n && s == NULL; i++) {
                if (strcmp(sizes[i].name, name) == 0) {
                    s = &sizes[i];
                }
            }
            if (s == NULL) {
                s = &sizes[n++];
                memset(s, 0, sizeof *s);
                s->name = name;
            }
            add_size(x, s, w, h);
        }
    }
    *count = n;
    return sizes;
}

typedef struct {
    const char *name;
    int         w;
    int         h;
    const u8   *data;
} Map;

typedef struct {
    const char *name;
    int         count;
    int         cols;
    int         iw;
    int         ih;
    int         base;
} Tileset;

static Tileset sheet(ExSheets *art, const char *name, int base)
{
    const ExSheet *s = ex_sheet(art, name);
    Tileset t = { name, s->count, s->across, s->across * 8, s->rows * 8,
                  base };
    return t;
}

static void write_tmx(Extract *x, const char *path, const char *file,
                      const Map *m, const Tileset *sets, int nsets,
                      const int *row_sets)
{
    int first[2];
    int gid = 1;
    for (int i = 0; i < nsets; i++) {
        first[i] = gid;
        gid += sets[i].count;
    }
    ExStr t;
    ex_str_init(x, &t);
    ex_str_add(&t, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n");
    ex_str_addf(&t, "<map version=\"1.10\" orientation=\"orthogonal\" "
                    "renderorder=\"right-down\" width=\"%d\" height=\"%d\" "
                    "tilewidth=\"8\" tileheight=\"8\" infinite=\"0\" "
                    "nextlayerid=\"2\" nextobjectid=\"1\">\n", m->w, m->h);
    for (int i = 0; i < nsets; i++) {
        const Tileset *s = &sets[i];
        ex_str_addf(&t, " <tileset firstgid=\"%d\" name=\"%s\" "
                        "tilewidth=\"8\" tileheight=\"8\" tilecount=\"%d\" "
                        "columns=\"%d\">\n", first[i], s->name, s->count,
                    s->cols);
        ex_str_add(&t, "  <properties>\n");
        ex_str_addf(&t, "   <property name=\"byte_base\" type=\"int\" "
                        "value=\"%d\"/>\n", s->base);
        ex_str_add(&t, "  </properties>\n");
        ex_str_addf(&t, "  <image source=\"../tiles/%s.png\" width=\"%d\" "
                        "height=\"%d\"/>\n", s->name, s->iw, s->ih);
        ex_str_add(&t, " </tileset>\n");
    }
    ex_str_addf(&t, " <layer id=\"1\" name=\"window\" width=\"%d\" "
                    "height=\"%d\">\n", m->w, m->h);
    ex_str_add(&t, "  <data encoding=\"csv\">\n");
    for (int r = 0; r < m->h; r++) {
        int k = row_sets[r];
        const Tileset *s = &sets[k];
        for (int c = 0; c < m->w; c++) {
            int b = m->data[r * m->w + c];
            if (b < s->base || b >= s->base + s->count) {
                ex_fail(x, "%s: byte $%02X in row %d is outside %s "
                           "($%02X-$%02X)", file, b, r, s->name, s->base,
                        s->base + s->count - 1);
            }
            ex_str_addf(&t, "%s%d", (c > 0) ? "," : "", first[k] + b - s->base);
        }
        ex_str_add(&t, (r + 1 < m->h) ? ",\n" : "\n");
    }
    ex_str_add(&t, "</data>\n </layer>\n</map>\n");
    ex_write_text(x, path, t.buf);
}

/* Path(out).parent */
static char *parent_of(Extract *x, const char *path)
{
    const char *slash = NULL;
    for (const char *p = path; *p != '\0'; p++) {
        if (*p == '/' || *p == '\\') {
            slash = p;
        }
    }
    if (slash == NULL) {
        return ex_strdup(x, ".");
    }
    if (slash == path) {
        return ex_strdup(x, "/");
    }
    return ex_strndup(x, path, (size_t)(slash - path));
}

static long long num(Extract *x, const JsonValue *o, const char *key)
{
    const JsonValue *v = json_get(o, key);
    if (v == NULL || v->type != JSON_NUMBER) {
        ex_fail(x, "an entry with no \"%s\"", key);
    }
    return (long long)v->number;
}

/* portrait_art: the map named ...Portrait -> the art of the first portrait
 * slot that draws it and is its own, and the byte its art starts at. */
static bool portrait_art(Extract *x, const JsonValue *wins,
                         const JsonValue *ports, const char *map,
                         const char **art, int *base)
{
    for (const JsonValue *w = wins->child; w != NULL; w = w->next) {
        const char *name = json_str(w, "art", "");
        long long slot = num(x, w, "portrait");
        if (strcmp(name, map) != 0 || !ex_ends(name, "Portrait")
            || slot == 25 || slot == 28 || slot == 29) {
            continue;
        }
        const char *found = NULL;
        for (const JsonValue *p = ports->child; p != NULL; p = p->next) {
            const JsonValue *i = json_get(p, "index");
            if (i != NULL && i->type == JSON_NUMBER
                && (long long)i->number == slot) {
                found = json_str(p, "art", NULL);
            }
        }
        *art  = (found != NULL && found[0] != '\0') ? found : "rolf_portrait";
        *base = (slot == 0 || slot == 26) ? 0xA0 : 0;
        return true;
    }
    return false;
}

void ex_step_winart(Extract *x, const ExArgs *a)
{
    ExSheets *art = ex_sheets(x);
    int nsizes = 0;
    Sizes *sizes = window_sizes(x, &nsizes);
    const ExMap *mp = &x->map;
    Map *maps = ex_alloc(x, sizeof *maps * (size_t)(mp->nlabels + 1));
    int nmaps = 0;
    ExStr failures;
    ex_str_init(x, &failures);
    JOut *doc = jo_obj(x);
    for (int i = 0; i < mp->nlabels; i++) {
        const char *name = mp->labels[i].name;
        if (!ex_starts(name, "WinArt_")) {
            continue;
        }
        u32 start = mp->labels[i].addr;
        size_t len = 0;
        const u8 *block = ex_blob(x, name, &len);
        const Sizes *s = NULL;
        for (int k = 0; k < nsizes && s == NULL; k++) {
            if (strcmp(sizes[k].name, name) == 0) {
                s = &sizes[k];
            }
        }
        int fits = 0;
        int fw = 0;
        int fh = 0;
        ExStr all;
        ex_str_init(x, &all);
        for (int k = 0; s != NULL && k < s->n; k++) {
            long long n = (long long)s->w[k] * s->h[k];
            ex_str_addf(&all, "%s(%d, %d)", (k > 0) ? ", " : "", s->w[k],
                        s->h[k]);
            if ((long long)len == n
                || ((long long)len == n + 1 && (start + n) % 2 != 0)) {
                fits++;
                fw = s->w[k];
                fh = s->h[k];
            }
        }
        if (fits != 1) {
            ex_str_addf(&failures, "  FAILED %s: %d window record sizes of "
                                   "[%s] fit its %zu bytes\n", name, fits,
                        (all.buf != NULL) ? all.buf : "", len);
            continue;
        }
        Map m = { name, fw, fh, block };
        maps[nmaps++] = m;
        JOut *rows = jo_arr(x);
        for (int r = 0; r < fh; r++) {
            JOut *row = jo_arr(x);
            for (int c = 0; c < fw; c++) {
                jo_add(row, jo_int(x, block[r * fw + c]));
            }
            jo_add(rows, row);
        }
        JOut *j = jo_obj(x);
        jo_seti(x, j, "width", fw);
        jo_seti(x, j, "height", fh);
        jo_set(x, j, "rows", rows);
        jo_set(x, doc, name, j);
    }
    char *assets = parent_of(x, a->out);
    ex_mkdirs(x, assets);
    ex_write_text(x, a->out, jo_dump(x, doc, 1, true));

    char *bin_dir = ex_path(x, assets, "winart");
    ex_mkdirs(x, bin_dir);
    ex_remove_ending(x, bin_dir, ".map");
    const JsonValue *wins = ex_read_json(x, ex_path(x, assets,
                                                    "data/portrait_windows.json"));
    const JsonValue *ports = ex_read_json(x, ex_path(x, assets,
                                                     "data/portraits.json"));
    if (wins->type != JSON_ARRAY || ports->type != JSON_ARRAY) {
        ex_fail(x, "portrait_windows.json or portraits.json is not a list");
    }
    for (int i = 0; i < nmaps; i++) {
        const Map *m = &maps[i];
        Tileset sets[2];
        int nsets = 0;
        sets[nsets++] = sheet(art, FONT_SHEET, 0);
        int *row_sets = ex_alloc(x, sizeof *row_sets * (size_t)(m->h + 1));
        const char *portrait = NULL;
        int base = 0;
        if (portrait_art(x, wins, ports, m->name, &portrait, &base)) {
            sets[nsets++] = sheet(art, portrait, base);
            for (int r = 1; r + 1 < m->h; r++) {
                row_sets[r] = 1;
            }
        }
        char *file = ex_fmt(x, "%s.tmx", m->name);
        write_tmx(x, ex_path(x, bin_dir, file), file, m, sets, nsets,
                  row_sets);
    }
    ex_say(x, "extracted %d window maps -> %s", nmaps, a->out);
    if (failures.len > 0) {
        failures.buf[failures.len - 1] = '\0';
        ex_warn(x, "%s", failures.buf);
    }
}
