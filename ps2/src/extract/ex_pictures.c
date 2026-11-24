/* (build 189) tools/extract_pictures.py in C: the pictures, the art as the
 * game draws it (docs/asset_format.md), from what the earlier steps wrote
 * -- the window maps (assets/winart), the nametables, the palette PNGs --
 * and the art itself from the cartridge (exsheets.c). A portrait is its
 * window's interior drawn from its sheet in its palette line; a cut-scene
 * picture its nametable drawn whole in the screen's palettes, cut to the
 * cells that name a tile. Each is an RGBA PNG with its JSON, and where an
 * effect recolours it a slot map. extract_pictures.py says what each
 * table below is; they were written in from it by a script. */

#include "excommon.h"
#include "exsheets.h"
#include "../core/png.h"
#include "../core/pngwrite.h"

#include <stdlib.h>
#include <string.h>

void ex_step_pictures(Extract *x, const ExArgs *a);

#define PORTRAIT_LINE 2
#define H_FLIP 0x80000000ull
#define V_FLIP 0x40000000ull
#define D_FLIP 0x20000000ull

typedef struct {
    const char *art;
    long long   palette;
    const char *name;
    int         line;
    int         art_byte;
} Portrait;

typedef struct {
    const char *thing;
    int         lo;
    int         hi;
} Slots;

typedef struct {
    const char *thing;
    const char *nametable;
    int         palettes[4];
    int         rows;           /* a stacked frame's rows */
    int         frames;         /* 0: one frame, "1" */
} Scene;

/* BUILDING_PORTRAITS[0] and LIBRARY_GRAPH */
static const Portrait ROLF = { "rolf_portrait", 0x11, "Rolf", 1, 0xA0 };
static const Portrait LIBRARY_GRAPH = { "graph_portrait", 0x2B, "LibraryGraph", 1, 0xA0 };
#define LIBRARY_WINDOW "WinArt_LibraryGraphPortrait"
#define LIBRARY_THING  "library_graph"

/* NEVER_SHOWN */
static const int NEVER_SHOWN[] = { 0x19, 0x1A, 0x1C, 0x1D };

/* SLOTS: the entries lo..hi an effect writes */
static const Slots SLOTS[] = {
    { "radar", 5, 5 },
    { "library_graph", 1, 15 },
    { "sega_logo", 2, 12 },
    { "ending_stars", 9, 12 },
    { "tyler_monitor", 12, 15 },
    { "mother_brain_stars", 8, 15 },
};

/* SCENES: the nametable, the palettes by line (a PalettePtrs index,
 * -1 for none), the stacked frames' rows (0: one frame) */
static const Scene SCENES[] = {
    { "lutz", "LutzPortraitTileInd",
      { -1, -1, -1, 0x3B }, 0, 0 },
    { "flight_planet_to_dezolis", "SpaceshipPlanetTileInd",
      { -1, -1, -1, 0x39 }, 0, 0 },
    { "flight_planet_to_motavia", "SpaceshipPlanetTileInd",
      { -1, -1, -1, 0x3A }, 0, 0 },
    { "nei_face", "NeiDeathTileInd",
      { -1, 0x35, -1, -1 }, 0, 0 },
    { "nei_face_starry", "NeiDeathTileInd",
      { -1, 0x36, -1, -1 }, 0, 0 },
    { "nei_eyes_closed", "NeiBodyTileInd",
      { -1, 0x36, -1, -1 }, 0, 0 },
    { "battle_floor", "BattleFloorTileInd",
      { 0x2F, -1, -1, -1 }, 0, 0 },
    { "ending_rudo", "EndingPage1PortraitTileInd",
      { -1, -1, 0x44, -1 }, 0, 0 },
    { "ending_amy", "EndingPage2PortraitTileInd",
      { -1, -1, 0x45, -1 }, 0, 0 },
    { "ending_kain", "EndingPage3PortraitTileInd",
      { -1, -1, 0x46, -1 }, 0, 0 },
    { "ending_shir", "EndingPage4PortraitTileInd",
      { -1, -1, 0x47, -1 }, 0, 0 },
    { "ending_hugh", "EndingPage5PortraitTileInd",
      { -1, -1, 0x48, -1 }, 0, 0 },
    { "ending_anna", "EndingPage6PortraitTileInd",
      { -1, -1, 0x49, -1 }, 0, 0 },
    { "ending_rolf", "EndingLastPictureTileInd",
      { -1, -1, 0x4A, -1 }, 0, 0 },
    { "sega_logo", "SegaLogoTileInd",
      { 0x00, -1, -1, -1 }, 0, 0 },
    { "ending_stars", "EndingStarfieldTileInd",
      { -1, -1, -1, 0x4B }, 0, 0 },
    { "tyler_monitor", "TylerSpcshp_MonitorTileInd",
      { -1, -1, -1, 0x2A }, 12, 5 },
    { "dream_waking", "DreamWakingTileInd",
      { -1, -1, -1, 0x30 }, 0, 0 },
    { "dream_battle", "DreamBattleTileInd",
      { -1, -1, -1, 0x01 }, 0, 0 },
    { "dream_help", "DreamHelpTileInd",
      { -1, -1, -1, 0x01 }, 0, 0 },
    { "tyler_portrait", "TylerPortraitTileInd",
      { -1, -1, 0x2A, -1 }, 0, 0 },
    { "title", "TitleScrPlaneATileInd",
      { -1, -1, 0x01, -1 }, 0, 0 },
    { "title_background", "TitleScrBGTileInd",
      { -1, 0x01, -1, -1 }, 0, 0 },
    { "gameover_background", "GameOverBGTileInd",
      { -1, 0x01, -1, -1 }, 0, 0 },
    { "mother_brain_stars", "MotherBrainMachineTileInd",
      { -1, -1, -1, 0x2F }, 0, 0 },
};

static const Slots *slots_of(const char *thing)
{
    for (size_t i = 0; i < sizeof SLOTS / sizeof SLOTS[0]; i++) {
        if (strcmp(SLOTS[i].thing, thing) == 0) {
            return &SLOTS[i];
        }
    }
    return NULL;
}

static bool in_slots(const Slots *s, int i)
{
    return s != NULL && i >= s->lo && i <= s->hi;
}

/* ---- the Tiled maps the earlier steps wrote ---- */

typedef struct {
    long long   first;
    long long   byte_base;
    long long   palette_line;
    long long   vram_base;
    const char *stem;           /* the image's file name less its suffix */
} Set;

typedef struct {
    Set                *sets;
    int                 nsets;
    int                 w;
    int                 h;
    unsigned long long *gids;
    int                 ngids;
} Tmx;

static bool digits_at(const char *p, long long *v, const char **end)
{
    const char *s = p;
    bool neg = false;
    if (*s == '-') {
        neg = true;
        s++;
    }
    if (*s < '0' || *s > '9') {
        return false;
    }
    long long n = 0;
    while (*s >= '0' && *s <= '9') {
        n = n * 10 + (*s - '0');
        s++;
    }
    *v   = neg ? -n : n;
    *end = s;
    return true;
}

static bool word_char(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')
        || (c >= '0' && c <= '9') || c == '_';
}

/* A tileset's body: its int properties (the last of a name wins, as a
 * dict built from re.findall) and its image. */
static void read_set(Extract *x, const char *body, const char *stop, Set *s)
{
    static const char PROP[] = "<property name=\"";
    static const char MID[] = "\" type=\"int\" value=\"";
    for (const char *p = body; p < stop;) {
        const char *at = strstr(p, PROP);
        if (at == NULL || at >= stop) {
            break;
        }
        const char *name = at + strlen(PROP);
        const char *e = name;
        while (word_char(*e)) {
            e++;
        }
        long long v = 0;
        const char *after = NULL;
        if (e > name && strncmp(e, MID, strlen(MID)) == 0
            && digits_at(e + strlen(MID), &v, &after) && *after == '"'
            && after < stop) {
            size_t n = (size_t)(e - name);
            if (n == 9 && strncmp(name, "byte_base", 9) == 0) {
                s->byte_base = v;
            } else if (n == 12 && strncmp(name, "palette_line", 12) == 0) {
                s->palette_line = v;
            } else if (n == 9 && strncmp(name, "vram_base", 9) == 0) {
                s->vram_base = v;
            }
            p = after + 1;
        } else {
            p = at + 1;
        }
    }
    static const char IMAGE[] = "<image source=\"";
    const char *img = strstr(body, IMAGE);
    if (img != NULL && img < stop) {
        const char *src = img + strlen(IMAGE);
        const char *q = strchr(src, '"');
        if (q != NULL && q > src) {
            const char *base = src;
            for (const char *c = src; c < q; c++) {
                if (*c == '/' || *c == '\\') {
                    base = c + 1;
                }
            }
            const char *dot = NULL;
            for (const char *c = base + 1; c < q; c++) {
                if (*c == '.') {
                    dot = c;
                }
            }
            s->stem = ex_strndup(x, base, (size_t)(((dot != NULL) ? dot : q)
                                                   - base));
        }
    }
}

static int cmp_set(const void *a, const void *b)
{
    const Set *p = a;
    const Set *q = b;
    if (p->first != q->first) {
        return (p->first < q->first) ? -1 : 1;
    }
    return (p->byte_base > q->byte_base) - (p->byte_base < q->byte_base);
}

/* The layer tag's last ` key="N"` before its '>'. */
static long long layer_attr(Extract *x, const char *text, const char *key,
                            const char *path)
{
    const char *layer = strstr(text, "<layer");
    const char *close = (layer != NULL) ? strchr(layer, '>') : NULL;
    char *pat = ex_fmt(x, " %s=\"", key);
    long long v = -1;
    for (const char *p = layer; p != NULL && close != NULL;) {
        const char *at = strstr(p, pat);
        if (at == NULL || at >= close) {
            break;
        }
        long long n = 0;
        const char *end = NULL;
        if (digits_at(at + strlen(pat), &n, &end) && *end == '"'
            && at[strlen(pat)] != '-' && end < close) {
            v = n;
        }
        p = at + 1;
    }
    if (v < 0) {
        ex_fail(x, "%s: no layer %s", path, key);
    }
    return v;
}

static Tmx read_tmx(Extract *x, const char *path)
{
    Tmx t;
    memset(&t, 0, sizeof t);
    char *text = (char *)ex_read_file(x, path, NULL);
    if (text == NULL) {
        ex_fail(x, "%s: no such file", path);
    }
    int cap = 8;
    t.sets = ex_alloc(x, sizeof *t.sets * (size_t)cap);
    static const char OPEN[] = "<tileset firstgid=\"";
    for (const char *p = text;;) {
        const char *at = strstr(p, OPEN);
        if (at == NULL) {
            break;
        }
        long long first = 0;
        const char *end = NULL;
        const char *stop = NULL;
        if (!digits_at(at + strlen(OPEN), &first, &end) || *end != '"'
            || at[strlen(OPEN)] == '-'
            || (stop = strstr(end + 1, "</tileset>")) == NULL) {
            p = at + 1;
            continue;
        }
        if (t.nsets == cap) {
            Set *more = ex_alloc(x, sizeof *more * (size_t)cap * 2);
            memcpy(more, t.sets, sizeof *more * (size_t)cap);
            t.sets = more;
            cap *= 2;
        }
        Set s;
        memset(&s, 0, sizeof s);
        s.first = first;
        read_set(x, end + 1, stop, &s);
        t.sets[t.nsets++] = s;
        p = stop + strlen("</tileset>");
    }
    if (t.nsets > 1) {
        /* sets.sort(): by firstgid, the byte base after (stable enough:
         * the maps never give two tilesets one firstgid) */
        qsort(t.sets, (size_t)t.nsets, sizeof *t.sets, cmp_set);
    }
    t.w = (int)layer_attr(x, text, "width", path);
    t.h = (int)layer_attr(x, text, "height", path);
    static const char DATA[] = "<data encoding=\"csv\">";
    const char *csv = strstr(text, DATA);
    const char *csv_end = (csv != NULL) ? strstr(csv, "</data>") : NULL;
    if (csv_end == NULL) {
        ex_fail(x, "%s: no CSV layer", path);
    }
    csv += strlen(DATA);
    t.gids = ex_alloc(x, sizeof *t.gids * (size_t)(csv_end - csv + 1));
    const char *p = csv;
    while (p <= csv_end) {
        const char *comma = memchr(p, ',', (size_t)(csv_end - p));
        const char *stop = (comma != NULL) ? comma : csv_end;
        /* the value with the newlines out and its blanks round it */
        const char *s = p;
        const char *e = stop;
        while (s < e && (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n')) {
            s++;
        }
        while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r'
                         || e[-1] == '\n')) {
            e--;
        }
        if (e > s) {
            unsigned long long v = 0;
            for (const char *c = s; c < e; c++) {
                if (*c == '\n') {
                    continue;
                }
                if (*c < '0' || *c > '9') {
                    ex_fail(x, "%s: a cell \"%.*s\" is not a number", path,
                            (int)(e - s), s);
                }
                v = v * 10 + (unsigned long long)(*c - '0');
            }
            t.gids[t.ngids++] = v;
        }
        if (comma == NULL) {
            break;
        }
        p = comma + 1;
    }
    return t;
}

/* ---- the palettes ---- */

typedef struct {
    u8   rgb[16][3];
    bool have[16];
    const char *name;           /* the PalettePtrs entry's */
} Line;

static const JsonValue *ptr_entry(Extract *x, const JsonValue *ptrs,
                                  long long index)
{
    for (const JsonValue *e = ptrs->child; e != NULL; e = e->next) {
        const JsonValue *i = json_get(e, "index");
        if (i != NULL && i->type == JSON_NUMBER
            && (long long)i->number == index) {
            return e;
        }
    }
    ex_fail(x, "no PalettePtrs entry %lld in paletteptrs.json", index);
}

static long long num(Extract *x, const JsonValue *o, const char *key)
{
    const JsonValue *v = json_get(o, key);
    if (v == NULL || v->type != JSON_NUMBER) {
        ex_fail(x, "an entry with no \"%s\"", key);
    }
    return (long long)v->number;
}

/* The palette PNG's colours, read back as Pillow's getpalette() gives
 * them. */
static int png_palette(Extract *x, const char *path, u8 pal[256][3])
{
    size_t size = 0;
    u8 *file = ex_read_file(x, path, &size);
    PngImage img;
    if (!png_decode_indexed(&img, file, size, path)) {
        ex_fail(x, "%s: not a palette PNG", path);
    }
    int n = img.pal_count;
    memcpy(pal, img.pal, sizeof img.pal);
    png_free(&img);
    return n;
}

/* palette_line: entry `index`'s first 16 colours. palette_for_line (for
 * `line` >= 0): its colours for that palette line, by where it loads. */
static Line palette(Extract *x, const char *assets, const JsonValue *ptrs,
                    long long index, int line)
{
    const JsonValue *e = ptr_entry(x, ptrs, index);
    Line out;
    memset(&out, 0, sizeof out);
    out.name = json_str(e, "name", "");
    u8 pal[256][3];
    int n = png_palette(x, ex_path(x, assets, ex_fmt(x, "palettes/%s",
                                                     json_str(e, "file", ""))),
                        pal);
    long long start = num(x, e, "line") * 16 + num(x, e, "entry");
    long long words = num(x, e, "words");
    for (int k = 0; k < 16; k++) {
        long long at = k;
        bool loaded = true;
        if (line >= 0) {
            at = line * 16 + k - start;
            loaded = at >= 0 && at < words;
        }
        if (!loaded) {
            continue;
        }
        if (at >= n) {
            ex_fail(x, "PalettePtrs %lld: its PNG has %d colours", index, n);
        }
        memcpy(out.rgb[k], pal[at], 3);
        out.have[k] = true;
    }
    return out;
}

/* ---- writing ---- */

static void write_rgba(Extract *x, const char *path, int w, int h,
                       const u8 *rgba)
{
    if (!png_write(path, w, h, 8, 6, rgba, (size_t)w * 4, NULL, 0, NULL)) {
        ex_fail(x, "%s: cannot write it", path);
    }
}

/* A slot map: indices, a grey ramp of 16 for a palette (Pillow keeps the
 * 16 it is given). */
static void write_slots(Extract *x, const char *path, int w, int h,
                        const u8 *idx)
{
    u8 grey[16 * 3];
    for (int i = 0; i < 16; i++) {
        grey[3 * i] = grey[3 * i + 1] = grey[3 * i + 2] = (u8)(i * 17);
    }
    if (!png_write(path, w, h, 8, 3, idx, (size_t)w, grey, 16, NULL)) {
        ex_fail(x, "%s: cannot write it", path);
    }
}

static void write_json(Extract *x, const char *path, const JOut *v)
{
    ex_write_text(x, path, ex_fmt(x, "%s\n", jo_dump(x, v, 2, false)));
}

static JOut *pair(Extract *x, long long p, long long q)
{
    JOut *a = jo_arr(x);
    jo_add(a, jo_int(x, p));
    jo_add(a, jo_int(x, q));
    return a;
}

/* ---- the portraits ---- */

typedef struct {
    int         w;
    int         h;
    u8         *rgba;
    const char *thing;
} Made;

typedef struct {
    Extract         *x;
    const char      *assets;
    const char      *out;
    ExSheets        *art;
    const JsonValue *ptrs;
    Made            *made;
    int              nmade;
    ExStr            problems;
} Ctx;

static void problem(Ctx *c, const char *text)
{
    ex_str_addf(&c->problems, "  %s\n", text);
}

/* re.sub(r"(?<!^)(?=[A-Z])", "_", name).lower(), runs of "_" one */
static char *snake_name(Extract *x, const char *name)
{
    ExStr s;
    ex_str_init(x, &s);
    for (size_t i = 0; name[i] != '\0'; i++) {
        char c = name[i];
        if (i > 0 && c >= 'A' && c <= 'Z') {
            ex_str_addc(&s, '_');
        }
        ex_str_addc(&s, (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c);
    }
    ExStr t;
    ex_str_init(x, &t);
    for (size_t i = 0; i < s.len; i++) {
        if (s.buf[i] == '_' && t.len > 0 && t.buf[t.len - 1] == '_') {
            continue;
        }
        ex_str_addc(&t, s.buf[i]);
    }
    return (t.buf != NULL) ? t.buf : ex_strdup(x, "");
}

/* make_picture: the window's interior cells drawn from the portrait's
 * art; its thing, or NULL. Windows that draw the same picture share
 * one. */
static const char *make_picture(Ctx *c, const char *win_art,
                                const Portrait *p, const char *thing)
{
    Extract *x = c->x;
    Tmx t = read_tmx(x, ex_path(x, c->assets,
                                ex_fmt(x, "winart/%s.tmx", win_art)));
    const ExSheet *sheet = ex_sheet(c->art, p->art);
    Line colours = palette(x, c->assets, c->ptrs, p->palette, -1);
    if (t.ngids < t.w * t.h) {
        ex_fail(x, "%s: %d cells, not %dx%d", win_art, t.ngids, t.w, t.h);
    }
    /* the cells inside the frame whose byte is the portrait's art */
    int r0 = -1;
    int r1 = -1;
    int c0 = -1;
    int c1 = -1;
    int *local = ex_alloc(x, sizeof *local * (size_t)(t.w * t.h + 1));
    for (int i = 0; i < t.w * t.h; i++) {
        local[i] = -1;
    }
    for (int r = 1; r < t.h - 1; r++) {
        for (int col = 0; col < t.w; col++) {
            long long gid = (long long)t.gids[r * t.w + col];
            const Set *set = NULL;
            for (int k = 0; k < t.nsets; k++) {
                if (t.sets[k].first <= gid) {
                    set = &t.sets[k];
                }
            }
            if (set == NULL) {
                ex_fail(x, "%s: gid %lld is in no tileset", win_art, gid);
            }
            long long byte = gid - set->first + set->byte_base;
            long long at = byte - p->art_byte;
            if (at < 0 || at >= sheet->count) {
                continue;
            }
            local[r * t.w + col] = (int)at;
            r0 = (r0 < 0 || r < r0) ? r : r0;
            r1 = (r > r1) ? r : r1;
            c0 = (c0 < 0 || col < c0) ? col : c0;
            c1 = (col > c1) ? col : c1;
        }
    }
    if (r0 < 0) {
        problem(c, ex_fmt(x, "%s: no cell from %s", win_art, p->art));
        return NULL;
    }
    int pw = (c1 - c0 + 1) * 8;
    int ph = (r1 - r0 + 1) * 8;
    u8 *rgba = ex_alloc(x, (size_t)pw * (size_t)ph * 4);
    u8 *entries = ex_alloc(x, (size_t)pw * (size_t)ph);
    for (int r = r0; r <= r1; r++) {
        for (int col = c0; col <= c1; col++) {
            int at = local[r * t.w + col];
            if (at < 0) {
                continue;
            }
            const u8 *tile = sheet->px + (size_t)at * 64;
            for (int i = 0; i < 64; i++) {
                int v = tile[i];
                if (v == 0) {
                    continue;
                }
                if (!colours.have[v]) {
                    ex_fail(x, "%s: colour %d is not in its palette", win_art,
                            v);
                }
                size_t o = (size_t)((r - r0) * 8 + i / 8) * (size_t)pw
                         + (size_t)((col - c0) * 8 + i % 8);
                memcpy(rgba + o * 4, colours.rgb[v], 3);
                rgba[o * 4 + 3] = 255;
                entries[o] = (u8)v;
            }
        }
    }
    if (thing == NULL) {
        thing = ex_ends(p->art, "_portrait")
              ? ex_strndup(x, p->art, strlen(p->art) - strlen("_portrait"))
              : p->art;
    }
    for (int i = 0; i < c->nmade; i++) {
        const Made *m = &c->made[i];
        if (m->w == pw && m->h == ph
            && memcmp(m->rgba, rgba, (size_t)pw * (size_t)ph * 4) == 0) {
            return m->thing;
        }
    }
    for (int i = 0; i < c->nmade; i++) {
        if (strcmp(c->made[i].thing, thing) == 0) {
            thing = snake_name(x, p->name);
            break;
        }
    }
    Made m = { pw, ph, rgba, thing };
    c->made[c->nmade++] = m;

    char *d = ex_path(x, c->out, ex_fmt(x, "portraits/%s", thing));
    ex_mkdirs(x, d);
    write_rgba(x, ex_path(x, d, ex_fmt(x, "%s.png", thing)), pw, ph, rgba);
    JOut *frame = jo_obj(x);
    jo_sets(x, frame, "file", ex_fmt(x, "%s.png", thing));
    const Slots *slots = slots_of(thing);
    if (slots != NULL) {
        for (int i = 0; i < pw * ph; i++) {
            entries[i] = in_slots(slots, entries[i]) ? entries[i] : 0;
        }
        write_slots(x, ex_path(x, d, ex_fmt(x, "%s_slots.png", thing)), pw,
                    ph, entries);
        jo_sets(x, frame, "slots", ex_fmt(x, "%s_slots.png", thing));
    }
    JOut *frames = jo_obj(x);
    jo_set(x, frames, "1", frame);
    JOut *desc = jo_obj(x);
    jo_sets(x, desc, "thing", thing);
    jo_sets(x, desc, "kind", "portrait");
    jo_set(x, desc, "size", pair(x, pw, ph));
    jo_set(x, desc, "anchor", pair(x, 0, 0));
    jo_set(x, desc, "frames", frames);
    jo_sets(x, desc, "window", win_art);
    jo_set(x, desc, "at", pair(x, c0 + 1, r0));
    jo_sets(x, desc, "source", ex_fmt(x, "%s.png, PalettePtrs %lld (%s) in "
                                         "line %d, %s", p->art, p->palette,
                                      colours.name, p->line, win_art));
    write_json(x, ex_path(x, d, ex_fmt(x, "%s.json", thing)), desc);
    return thing;
}

/* ---- the cut-scene pictures ---- */

typedef struct {
    int         frame;
    int         r;
    int         c;
    u8          tile[64];
    int         line;
} Drawn;

typedef struct {
    const char    *stem;
    const ExSheet *sheet;
} Used;

static int cmp_str(const void *a, const void *b)
{
    return strcmp(*(const char *const *)a, *(const char *const *)b);
}

static bool scene_picture(Ctx *c, const Scene *sp)
{
    Extract *x = c->x;
    const char *name = sp->nametable;
    Tmx t = read_tmx(x, ex_path(x, c->assets,
                                ex_fmt(x, "nametables/%s.tmx", name)));
    Line lines[4];
    bool has_line[4];
    for (int l = 0; l < 4; l++) {
        has_line[l] = sp->palettes[l] >= 0;
        if (has_line[l]) {
            lines[l] = palette(x, c->assets, c->ptrs, sp->palettes[l], l);
        }
    }
    int nframes = (sp->frames > 0) ? sp->frames : 1;
    int rows = (sp->frames > 0) ? sp->rows : t.h;
    bool *named = ex_alloc(x, sizeof *named * (size_t)(rows * t.w + 1));
    Used used[16];
    int nused = 0;
    Drawn *drawn = ex_alloc(x, sizeof *drawn
                                * (size_t)(nframes * rows * t.w + 1));
    int ndrawn = 0;
    for (int f = 0; f < nframes; f++) {
        int first = (sp->frames > 0) ? f * sp->rows : 0;
        for (int r = 0; r < rows; r++) {
            for (int col = 0; col < t.w; col++) {
                int k = (first + r) * t.w + col;
                if (k >= t.ngids) {
                    ex_fail(x, "%s: no cell %d,%d", name, col, first + r);
                }
                unsigned long long g = t.gids[k];
                long long gid = (long long)(g & ~(H_FLIP | V_FLIP | D_FLIP));
                if (gid == 0) {
                    continue;
                }
                char *at = ex_fmt(x, "%d,%d", col, first + r);
                if (g & D_FLIP) {
                    problem(c, ex_fmt(x, "%s: a diagonal flip at %s", name, at));
                    continue;
                }
                const Set *set = NULL;
                for (int s = 0; s < t.nsets; s++) {
                    if (t.sets[s].first <= gid
                        && (set == NULL || t.sets[s].first > set->first)) {
                        set = &t.sets[s];
                    }
                }
                if (set == NULL || set->stem == NULL) {
                    ex_fail(x, "%s: gid %lld is in no tileset", name, gid);
                }
                if (set->vram_base + gid - set->first != 0) {
                    named[r * t.w + col] = true;
                }
                const ExSheet *sheet = NULL;
                for (int u = 0; u < nused; u++) {
                    if (strcmp(used[u].stem, set->stem) == 0) {
                        sheet = used[u].sheet;
                    }
                }
                if (sheet == NULL) {
                    if (nused == 16) {
                        ex_fail(x, "%s: more than 16 sheets", name);
                    }
                    sheet = ex_sheet(c->art, set->stem);
                    used[nused].stem  = set->stem;
                    used[nused].sheet = sheet;
                    nused++;
                }
                long long idx = gid - set->first;
                if (idx >= sheet->count) {
                    ex_fail(x, "%s: tile %lld of %s, which has %d", name, idx,
                            set->stem, sheet->count);
                }
                Drawn d;
                memset(&d, 0, sizeof d);
                const u8 *src = sheet->px + (size_t)idx * 64;
                bool blank = true;
                for (int y = 0; y < 8; y++) {
                    int sy = (g & V_FLIP) ? 7 - y : y;
                    for (int xx = 0; xx < 8; xx++) {
                        int sx = (g & H_FLIP) ? 7 - xx : xx;
                        d.tile[y * 8 + xx] = src[sy * 8 + sx];
                        blank = blank && src[sy * 8 + sx] == 0;
                    }
                }
                if (blank) {
                    continue;
                }
                long long line = set->palette_line;
                if (line < 0 || line > 3 || !has_line[line]) {
                    problem(c, ex_fmt(x, "%s: cell %s draws in line %lld, "
                                         "which SCENES gives no palette",
                                      name, at, line));
                    continue;
                }
                bool missing = false;
                for (int i = 0; i < 64; i++) {
                    if (d.tile[i] != 0 && !lines[line].have[d.tile[i]]) {
                        missing = true;
                    }
                }
                if (missing) {
                    problem(c, ex_fmt(x, "%s: cell %s draws a colour of line "
                                         "%lld its palette does not load",
                                      name, at, line));
                    continue;
                }
                d.frame = f;
                d.r     = r;
                d.c     = col;
                d.line  = (int)line;
                drawn[ndrawn++] = d;
            }
        }
    }
    if (ndrawn == 0) {
        problem(c, ex_fmt(x, "%s: nothing drawn", name));
        return false;
    }
    int r0 = -1;
    int r1 = -1;
    int c0 = -1;
    int c1 = -1;
    for (int r = 0; r < rows; r++) {
        for (int col = 0; col < t.w; col++) {
            if (!named[r * t.w + col]) {
                continue;
            }
            r0 = (r0 < 0 || r < r0) ? r : r0;
            r1 = (r > r1) ? r : r1;
            c0 = (c0 < 0 || col < c0) ? col : c0;
            c1 = (col > c1) ? col : c1;
        }
    }
    if (r0 < 0) {
        ex_fail(x, "%s: no cell names a tile", name);
    }
    int pw = (c1 - c0 + 1) * 8;
    int ph = (r1 - r0 + 1) * 8;
    const Slots *slots = slots_of(sp->thing);
    char *d = ex_path(x, c->out, ex_fmt(x, "scenes/%s", sp->thing));
    ex_mkdirs(x, d);
    JOut *described = jo_obj(x);
    for (int f = 0; f < nframes; f++) {
        char *key = (sp->frames > 0) ? ex_fmt(x, "%d", f) : "1";
        char *stem = (nframes == 1) ? ex_strdup(x, sp->thing)
                                    : ex_fmt(x, "%s_%s", sp->thing, key);
        u8 *rgba = ex_alloc(x, (size_t)pw * (size_t)ph * 4);
        u8 *entries = ex_alloc(x, (size_t)pw * (size_t)ph);
        for (int k = 0; k < ndrawn; k++) {
            const Drawn *dr = &drawn[k];
            if (dr->frame != f) {
                continue;
            }
            for (int i = 0; i < 64; i++) {
                int v = dr->tile[i];
                if (v == 0) {
                    continue;
                }
                int px = (dr->c - c0) * 8 + i % 8;
                int py = (dr->r - r0) * 8 + i / 8;
                if (px < 0 || px >= pw || py < 0 || py >= ph) {
                    ex_fail(x, "%s: cell %d,%d drawn outside the picture",
                            name, dr->c, dr->r);
                }
                size_t o = (size_t)py * (size_t)pw + (size_t)px;
                memcpy(rgba + o * 4, lines[dr->line].rgb[v], 3);
                rgba[o * 4 + 3] = 255;
                entries[o] = in_slots(slots, v) ? (u8)v : 0;
            }
        }
        write_rgba(x, ex_path(x, d, ex_fmt(x, "%s.png", stem)), pw, ph, rgba);
        JOut *frame = jo_obj(x);
        jo_sets(x, frame, "file", ex_fmt(x, "%s.png", stem));
        if (slots != NULL) {
            write_slots(x, ex_path(x, d, ex_fmt(x, "%s_slots.png", stem)), pw,
                        ph, entries);
            jo_sets(x, frame, "slots", ex_fmt(x, "%s_slots.png", stem));
        }
        jo_set(x, described, key, frame);
    }

    ExStr pals;
    ex_str_init(x, &pals);
    for (int l = 0; l < 4; l++) {
        if (has_line[l]) {
            ex_str_addf(&pals, "%sline %d PalettePtrs %d (%s)",
                        (pals.len > 0) ? ", " : "", l, sp->palettes[l],
                        lines[l].name);
        }
    }
    ExStr table;
    ex_str_init(x, &table);
    ex_str_add(&table, name);
    if (nframes > 1) {
        ex_str_addf(&table, " (%d rows a frame, from row ", rows);
        for (int f = 0; f < nframes; f++) {
            ex_str_addf(&table, "%s%d for %d", (f > 0) ? ", " : "",
                        f * sp->rows, f);
        }
        ex_str_add(&table, ")");
    }
    const char **files = ex_alloc(x, sizeof *files * (size_t)(nused + 1));
    for (int u = 0; u < nused; u++) {
        files[u] = ex_fmt(x, "%s.png", used[u].stem);
    }
    if (nused > 1) {
        qsort(files, (size_t)nused, sizeof *files, cmp_str);
    }
    ExStr source;
    ex_str_init(x, &source);
    for (int u = 0; u < nused; u++) {
        ex_str_addf(&source, "%s%s", (u > 0) ? ", " : "", files[u]);
    }
    ex_str_addf(&source, ", %s, %s", pals.buf, table.buf);

    JOut *desc = jo_obj(x);
    jo_sets(x, desc, "thing", sp->thing);
    jo_sets(x, desc, "kind", "scene");
    jo_set(x, desc, "size", pair(x, pw, ph));
    jo_set(x, desc, "anchor", pair(x, 0, 0));
    jo_set(x, desc, "frames", described);
    jo_sets(x, desc, "nametable", name);
    jo_set(x, desc, "at", pair(x, c0, r0));
    jo_sets(x, desc, "source", source.buf);
    write_json(x, ex_path(x, d, ex_fmt(x, "%s.json", sp->thing)), desc);
    return true;
}

/* ---- the step ---- */

static bool never_shown(long long n)
{
    for (size_t i = 0; i < sizeof NEVER_SHOWN / sizeof NEVER_SHOWN[0]; i++) {
        if (NEVER_SHOWN[i] == n) {
            return true;
        }
    }
    return false;
}

void ex_step_pictures(Extract *x, const ExArgs *a)
{
    Ctx c;
    memset(&c, 0, sizeof c);
    c.x      = x;
    c.assets = (a->assets != NULL) ? a->assets : "assets";
    c.out    = a->out;
    c.art    = ex_sheets(x);
    ex_str_init(x, &c.problems);
    const JsonValue *windows = ex_read_json(x, ex_path(x, c.assets,
                                                       "data/portrait_windows.json"));
    const JsonValue *portraits = ex_read_json(x, ex_path(x, c.assets,
                                                         "data/portraits.json"));
    c.ptrs = ex_read_json(x, ex_path(x, c.assets, "palettes/paletteptrs.json"));
    if (windows->type != JSON_ARRAY || portraits->type != JSON_ARRAY
        || c.ptrs->type != JSON_ARRAY) {
        ex_fail(x, "portrait_windows.json, portraits.json or "
                   "paletteptrs.json is not a list");
    }
    c.made = ex_alloc(x, sizeof *c.made * (size_t)(json_length(windows) + 2));

    JOut *index = jo_obj(x);
    int pictures = 0;
    for (const JsonValue *win = windows->child; win != NULL; win = win->next) {
        long long n = num(x, win, "portrait");
        const JsonValue *entry = NULL;
        for (const JsonValue *p = portraits->child; p != NULL; p = p->next) {
            const JsonValue *i = json_get(p, "index");
            if (i != NULL && i->type == JSON_NUMBER
                && (long long)i->number == n) {
                entry = p;
            }
        }
        if (never_shown(n)) {
            continue;
        }
        Portrait p;
        if (entry != NULL) {
            p.art      = json_str(entry, "art", "");
            p.palette  = num(x, entry, "palette");
            p.name     = json_str(entry, "name", "");
            p.line     = (int)json_int(entry, "line", PORTRAIT_LINE);
            p.art_byte = (int)json_int(entry, "art_byte", 0);
        } else if (n == 0) {
            p = ROLF;
        } else {
            problem(&c, ex_fmt(x, "window %lld: no portrait", n));
            continue;
        }
        const char *thing = make_picture(&c, json_str(win, "art", ""), &p,
                                         NULL);
        if (thing != NULL) {
            char *key = ex_fmt(x, "%lld", n);
            bool had = jo_get(index, key) != NULL;
            jo_sets(x, index, key, thing);
            pictures += had ? 0 : 1;
        }
    }
    const char *graph = make_picture(&c, LIBRARY_WINDOW, &LIBRARY_GRAPH,
                                     LIBRARY_THING);
    char *pdir = ex_path(x, c.out, "portraits");
    ex_mkdirs(x, pdir);
    JOut *doc = jo_obj(x);
    jo_set(x, doc, "windows", index);
    jo_set(x, doc, "library_graph", (graph != NULL) ? jo_str(x, graph)
                                                    : jo_null(x));
    write_json(x, ex_path(x, pdir, "portraits.json"), doc);
    ex_say(x, "portraits: %d pictures and the library graph -> %s", pictures,
           pdir);

    int scenes = 0;
    for (size_t i = 0; i < sizeof SCENES / sizeof SCENES[0]; i++) {
        scenes += scene_picture(&c, &SCENES[i]) ? 1 : 0;
    }
    ex_say(x, "scenes: %d pictures -> %s", scenes, ex_path(x, c.out,
                                                           "scenes"));
    if (c.problems.len > 0) {
        c.problems.buf[c.problems.len - 1] = '\0';
        ex_fail(x, "%s", c.problems.buf);
    }
}
