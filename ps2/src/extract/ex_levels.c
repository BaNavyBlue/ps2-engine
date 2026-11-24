/* (build 188) tools/extract_levels.py in C: the level maps. Each
 * LevelData record (20 bytes) names its two planes' block maps, its
 * collision table and its interaction list; its tileset picks a pair out
 * of loc_9342 -- the tiles (Nemesis) and the block definitions
 * (DecompressArt2's format). Out: the tilesets' sheets, the blocks as text
 * and drawn at 32x32, each level's Tiled map(s) and JSON, levels.json, the
 * two tile animations and the block-map patches LoadLevelData makes. */

#include "exart.h"
#include "excommon.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void ex_step_levels(Extract *x, const ExArgs *a);

#define BLOCK_PX      32
#define SHEET_BLOCKS  16
#define BACKDROP_COLS 16
#define BACKDROP_ROWS 8
#define TILESET_TABLE "loc_9342"
#define MAX_TILESETS  16

static const int RECORD_LONGS[4] = { 2, 6, 10, 14 };

/* Every byte from a label to the end of the ROM: a compressed stream runs
 * on through labels that point into its middle. */
static const u8 *stream(Extract *x, const char *label, size_t *len)
{
    long long at = ex_addr(x, label);
    *len = x->map.size - (size_t)at;
    return ex_read(x, at, (long long)*len);
}

typedef struct {
    u32         flags;
    const char *name;
} Pointer;

static Pointer pointer(Extract *x, u32 value, const char *where)
{
    Pointer p;
    p.name = ex_label_closing_at(x, value & 0xFFFFFF);
    if (p.name == NULL) {
        ex_fail(x, "%s: $%08X points where no label is", where, (unsigned)value);
    }
    p.flags = value >> 24;
    return p;
}

/* Each MusicID_ constant's name by its value, the first in the map's
 * order (music_names: rom.consts, unsorted, setdefault). */
static const char *music_name(Extract *x, long long value)
{
    for (int i = 0; i < x->map.nconsts; i++) {
        const ExConst *c = &x->map.consts[i];
        if (ex_starts(c->name, "MusicID_") && c->value == value) {
            return c->name;
        }
    }
    return NULL;
}

typedef struct {
    const char *art;
    const char *defs;
} TilesetPtrs;

static int tileset_pointers(Extract *x, TilesetPtrs *out)
{
    size_t len = 0;
    const u8 *raw = ex_blob(x, TILESET_TABLE, &len);
    const char *syms[2 * MAX_TILESETS + 2];
    int n = 0;
    for (size_t i = 0; i + 3 < len; i += 4) {
        u32 v = (u32)raw[i] << 24 | (u32)raw[i + 1] << 16
              | (u32)raw[i + 2] << 8 | raw[i + 3];
        if (n == 2 * MAX_TILESETS) {
            ex_fail(x, "%s: more tilesets than %d", TILESET_TABLE,
                    MAX_TILESETS);
        }
        syms[n++] = pointer(x, v, TILESET_TABLE).name;
    }
    for (int i = 0; i + 1 < n; i += 2) {
        out[i / 2].art  = syms[i];
        out[i / 2].defs = syms[i + 1];
    }
    return n / 2;
}

static const char *tileset_name(Extract *x, long long n)
{
    for (int i = 0; EX_TILESET_NAMES[i].name != NULL; i++) {
        if (EX_TILESET_NAMES[i].index == n) {
            return EX_TILESET_NAMES[i].name;
        }
    }
    ex_fail(x, "tileset %lld has no name in tools/asset_names.py", n);
}

static u16 word_at(const u8 *p, size_t len, size_t o)
{
    /* int.from_bytes of what is there: a lone last byte is its value */
    if (o + 1 < len) {
        return (u16)(p[o] << 8 | p[o + 1]);
    }
    return (o < len) ? p[o] : 0;
}

/* ---- the blocks ---- */

static void write_blocks_text(Extract *x, const char *path, const u8 *defs,
                              size_t len, int tileset)
{
    size_t count = len / 32;
    ExStr s;
    ex_str_init(x, &s);
    ex_str_addf(&s, "# tileset %d: %zu blocks of 4x4 nametable entries\n",
                tileset, count);
    ex_str_add(&s, "# entry: P CC V H TTTTTTTTTTT  (priority, palette line, "
                   "vflip, hflip, tile)\n");
    ex_str_add(&s, "# one block per line, 16 words, row-major\n");
    for (size_t b = 0; b < count; b++) {
        for (int i = 0; i < 16; i++) {
            ex_str_addf(&s, "%s%04X", (i > 0) ? " " : "",
                        word_at(defs, len, b * 32 + (size_t)i * 2));
        }
        ex_str_addc(&s, '\n');
    }
    ex_write_text(x, path, s.buf);
}

static void render_blocks(Extract *x, const char *path, const u8 *defs,
                          size_t len, const ExTiles *tiles,
                          const u16 *palette, int npalette, int *across_out,
                          int *down_out)
{
    int count  = (int)(len / 32);
    int across = (count < SHEET_BLOCKS) ? count : SHEET_BLOCKS;
    if (across == 0) {
        across = 1;
    }
    int down    = (count + across - 1) / across;
    int sheet_w = across * BLOCK_PX;
    u8 *px = ex_alloc(x, (size_t)sheet_w * (size_t)(down * BLOCK_PX + 1));
    static const u8 BLANK[EX_TILE_BYTES];
    for (int b = 0; b < count; b++) {
        int bx = (b % across) * BLOCK_PX;
        int by = (b / across) * BLOCK_PX;
        for (int r = 0; r < 4; r++) {
            for (int c = 0; c < 4; c++) {
                size_t o = (size_t)b * 32 + (size_t)(r * 4 + c) * 2;
                u16 w = word_at(defs, len, o);
                int t = w & 0x7FF;
                const u8 *tile = (t < tiles->count)
                               ? tiles->data + (size_t)t * EX_TILE_BYTES
                               : BLANK;
                int hf   = (w >> 11) & 1;
                int vf   = (w >> 12) & 1;
                int line = (w >> 13) & 3;
                for (int y = 0; y < 8; y++) {
                    int sy = vf ? 7 - y : y;
                    for (int xx = 0; xx < 8; xx++) {
                        int sx = hf ? 7 - xx : xx;
                        int k  = sy * 8 + sx;
                        u8 byte = tile[k / 2];
                        u8 v = (k % 2 == 0) ? (u8)(byte >> 4)
                                            : (u8)(byte & 0x0F);
                        px[(size_t)(by + r * 8 + y) * (size_t)sheet_w
                           + (size_t)(bx + c * 8 + xx)]
                            = v ? (u8)(line * 16 + v) : 0;
                    }
                }
            }
        }
    }
    ExRgb rgb;
    memset(&rgb, 0, sizeof rgb);
    for (int i = 0; i < npalette; i++) {
        ex_rgb_cram(&rgb, palette[i]);
    }
    while (rgb.count < 64) {
        ex_rgb_add(&rgb, 0, 0, 0);
    }
    ex_png_flat(x, path, sheet_w, down * BLOCK_PX, px, &rgb);
    *across_out = across;
    *down_out   = down;
}

/* ---- Tiled maps ---- */

typedef struct {
    const char *name;
    int         w;
    int         h;
    const u8   *data;
} Layer;

typedef struct {
    const char *key;
    long long   value;
} Prop;

static int cmp_prop(const void *a, const void *b)
{
    return strcmp(((const Prop *)a)->key, ((const Prop *)b)->key);
}

static void write_tmx(Extract *x, const char *path, int tileset, int count,
                      int across, int down, const Layer *layers, int nlayers,
                      Prop *props, int nprops)
{
    qsort(props, (size_t)nprops, sizeof *props, cmp_prop);
    const char *ts = tileset_name(x, tileset);
    ExStr s;
    ex_str_init(x, &s);
    ex_str_add(&s, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n");
    ex_str_addf(&s, "<map version=\"1.10\" orientation=\"orthogonal\" "
                    "renderorder=\"right-down\" width=\"%d\" height=\"%d\" "
                    "tilewidth=\"32\" tileheight=\"32\" infinite=\"0\">\n",
                layers[0].w, layers[0].h);
    ex_str_add(&s, " <properties>\n");
    for (int i = 0; i < nprops; i++) {
        ex_str_addf(&s, "  <property name=\"%s\" type=\"int\" value=\"%lld\"/>\n",
                    props[i].key, props[i].value);
    }
    ex_str_add(&s, " </properties>\n");
    ex_str_addf(&s, " <tileset firstgid=\"1\" name=\"blocks_%s\" "
                    "tilewidth=\"32\" tileheight=\"32\" tilecount=\"%d\" "
                    "columns=\"%d\">\n", ts, count, across);
    ex_str_addf(&s, "  <image source=\"blocks_%s.png\" width=\"%d\" "
                    "height=\"%d\"/>\n", ts, across * BLOCK_PX,
                down * BLOCK_PX);
    ex_str_add(&s, " </tileset>\n");
    for (int n = 0; n < nlayers; n++) {
        const Layer *l = &layers[n];
        ex_str_addf(&s, " <layer id=\"%d\" name=\"%s\" width=\"%d\" "
                        "height=\"%d\">\n", n + 1, l->name, l->w, l->h);
        ex_str_add(&s, "  <data encoding=\"csv\">\n");
        for (int y = 0; y < l->h; y++) {
            for (int xx = 0; xx < l->w; xx++) {
                ex_str_addf(&s, "%s%d", (xx > 0) ? "," : "",
                            l->data[y * l->w + xx] + 1);
            }
            ex_str_add(&s, (y + 1 < l->h) ? ",\n" : "\n");
        }
        if (l->h == 0) {
            ex_str_addc(&s, '\n');
        }
        ex_str_add(&s, "  </data>\n </layer>\n");
    }
    ex_str_add(&s, "</map>\n");
    ex_write_text(x, path, s.buf);
}

/* ---- the tile animations and the map patches ---- */

typedef struct {
    int         level;
    const char *art;
    int         tiles;
    int         vram;
    int         steps;
    const char *order;      /* NULL: the steps are 0, 1, ... */
} TileAnim;

static const TileAnim TILE_ANIMS[2] = {
    { 0x00, "loc_760D8", 8,  0x0020, 2, NULL },
    { 0x13, "loc_762D8", 16, 0x21C0, 4, "loc_11978" },
};

#define TILE_ANIM_PERIOD 0x20
#define TILE_ANIM_SHIFT  8

static void write_tile_anims(Extract *x, const char *outdir)
{
    char *tile_dir = ex_path(x, outdir, "tiles");
    char *data_dir = ex_path(x, outdir, "data");
    ex_mkdirs(x, data_dir);
    JOut *out = jo_arr(x);
    ExRgb grey;
    memset(&grey, 0, sizeof grey);
    ex_rgb_grey_ramp(&grey);
    for (int k = 0; k < 2; k++) {
        const TileAnim *an = &TILE_ANIMS[k];
        size_t len = 0;
        const u8 *raw = ex_blob(x, an->art, &len);
        size_t frame_bytes = (size_t)an->tiles * 32;
        size_t count = len / frame_bytes;
        JOut *sequence = jo_arr(x);
        const u8 *order = (an->order != NULL)
                        ? ex_read(x, ex_addr(x, an->order), an->steps) : NULL;
        for (int s = 0; s < an->steps; s++) {
            long long step = (order != NULL) ? order[s] : s;
            jo_add(sequence, jo_int(x, (step << TILE_ANIM_SHIFT)
                                       / (long long)frame_bytes));
        }
        const char *anim_name = ex_pair(EX_LEVEL_ANIM_NAMES, an->art);
        if (anim_name == NULL) {
            ex_fail(x, "%s has no name in tools/asset_names.py", an->art);
        }
        JOut *files = jo_arr(x);
        for (size_t f = 0; f < count; f++) {
            char *name = ex_fmt(x, "anim_%s_%zu.png", anim_name, f);
            ex_png_tiles(x, ex_path(x, tile_dir, name), raw + f * frame_bytes,
                         an->tiles, &grey, 16, true, NULL, NULL);
            JOut *file = jo_obj(x);
            jo_sets(x, file, "file", name);
            jo_add(files, file);
        }
        JOut *e = jo_obj(x);
        jo_seti(x, e, "level", an->level);
        jo_sets(x, e, "source", an->art);
        jo_seti(x, e, "tile", an->vram / 32);
        jo_seti(x, e, "period", TILE_ANIM_PERIOD);
        jo_set(x, e, "frames", files);
        jo_set(x, e, "sequence", sequence);
        jo_add(out, e);
    }
    JOut *doc = jo_obj(x);
    jo_sets(x, doc, "source", "loc_118F4");
    jo_set(x, doc, "anims", out);
    char *path = ex_path(x, data_dir, "level_anims.json");
    ex_write_text(x, path, jo_dump(x, doc, 1, false));
    ex_say(x, "%d tile animations -> %s", jo_len(out), path);
}

static JOut *ints(Extract *x, const int *v, int n)
{
    JOut *arr = jo_arr(x);
    for (int i = 0; i < n; i++) {
        jo_add(arr, jo_int(x, v[i]));
    }
    return arr;
}

static JOut *strs(Extract *x, const char *const *v, int n)
{
    JOut *arr = jo_arr(x);
    for (int i = 0; i < n; i++) {
        jo_add(arr, jo_str(x, v[i]));
    }
    return arr;
}

/* A loc_9302 run list: row, column, n to its $FF. */
static JOut *runs(Extract *x, const char *label)
{
    size_t len = 0;
    const u8 *raw = ex_blob(x, label, &len);
    JOut *out = jo_arr(x);
    for (size_t i = 0; i < len; i += 3) {
        if (raw[i] == 0xFF) {
            return out;
        }
        JOut *entry = jo_arr(x);
        for (size_t k = i; k < i + 3 && k < len; k++) {
            jo_add(entry, jo_int(x, raw[k]));
        }
        jo_add(out, entry);
    }
    ex_fail(x, "%s: no $FF before the end of the list", label);
}

static void write_map_patches(Extract *x, const char *outdir)
{
    char *data_dir = ex_path(x, outdir, "data");
    ex_mkdirs(x, data_dir);
    static const struct {
        int         flag;
        const char *label;
    } RUNS[4] = {
        { 0x2F, "loc_17C94" }, { 0x33, "loc_17CDA" },
        { 0x31, "loc_17D4A" }, { 0x2D, "loc_17DA2" },
    };
    JOut *patches = jo_arr(x);

    JOut *p = jo_obj(x);
    jo_seti(x, p, "level", 0x00);
    static const char *const C735[1] = { "$C735" };
    jo_set(x, p, "flags", strs(x, C735, 1));
    jo_sets(x, p, "plane", "B");
    JOut *rep = jo_obj(x);
    jo_seti(x, rep, "from", 0x8B);
    jo_seti(x, rep, "to", 0x89);
    jo_seti(x, rep, "bytes", 0x1800);
    jo_set(x, p, "replace", rep);
    JOut *set = jo_obj(x);
    jo_seti(x, set, "block", 0x89);
    static const int AT_89[4] = { 0x8C0, 0xC2C, 0xEC8, 0xC17 };
    jo_set(x, set, "at", ints(x, AT_89, 4));
    jo_set(x, p, "set", set);
    jo_add(patches, p);

    for (int i = 0; i < 4; i++) {
        p = jo_obj(x);
        jo_seti(x, p, "level", 0x00);
        const char *flags[2] = { "$C735", ex_fmt(x, "$C7%02X", RUNS[i].flag) };
        jo_set(x, p, "flags", strs(x, flags, 2));
        jo_sets(x, p, "plane", "B");
        jo_sets(x, p, "source", RUNS[i].label);
        JOut *r = jo_obj(x);
        jo_seti(x, r, "stride", 0x60);
        jo_seti(x, r, "block", 0x87);
        jo_set(x, r, "entries", runs(x, RUNS[i].label));
        jo_set(x, p, "runs", r);
        jo_add(patches, p);
    }

    p = jo_obj(x);
    jo_seti(x, p, "level", 0x03);
    jo_seti(x, p, "item", ex_const(x, "ItemID_Prism"));
    jo_sets(x, p, "item_name", "ItemID_Prism");
    jo_sets(x, p, "plane", "B");
    set = jo_obj(x);
    jo_seti(x, set, "block", 0x63);
    static const int AT_63[4] = { 0x096, 0x0EC, 0x7B9, 0xED0 };
    jo_set(x, set, "at", ints(x, AT_63, 4));
    jo_set(x, p, "set", set);
    jo_add(patches, p);

    p = jo_obj(x);
    jo_seti(x, p, "level", 0x64);
    static const char *const C747[1] = { "$C747" };
    jo_set(x, p, "flags", strs(x, C747, 1));
    jo_sets(x, p, "plane", "A");
    set = jo_obj(x);
    jo_seti(x, set, "block", 0x28);
    static const int AT_28[1] = { 0x1B0 };
    jo_set(x, set, "at", ints(x, AT_28, 1));
    jo_set(x, p, "set", set);
    jo_add(patches, p);

    JOut *doc = jo_obj(x);
    jo_sets(x, doc, "source", "loc_9258");
    jo_set(x, doc, "patches", patches);
    char *path = ex_path(x, data_dir, "map_patches.json");
    ex_write_text(x, path, jo_dump(x, doc, 1, false));
    ex_say(x, "%d map patches -> %s", jo_len(patches), path);
}

/* ---- the levels ---- */

typedef struct {
    const char *label;
    JOut       *json;
    int         tileset;
    int         palette;
    long long   width_px;
    long long   height_px;
    u32         collision_flags;
    const char *map_b;
    const char *map_a;
    const char *collision;
    const char *interactions;
    int         music_id;
    u32         enemy_group;
    u32         enemy_group_late;
    int         extra_art;
} Level;

static void parse_level(Extract *x, const char *label, Level *lv)
{
    long long at = ex_addr(x, label);
    u8 kind = ex_u8(x, at);
    u8 size = ex_u8(x, at + 1);
    Pointer longs[4];
    for (int i = 0; i < 4; i++) {
        longs[i] = pointer(x, ex_u32(x, at + RECORD_LONGS[i]),
                           ex_fmt(x, "%s+%d", label, RECORD_LONGS[i]));
    }
    int music_id = ex_u8(x, at + 19);
    const char *music = music_name(x, music_id);
    if (music == NULL) {
        music = ex_fmt(x, "$%02X", music_id);
    }
    lv->label            = label;
    lv->tileset          = kind & 0x0F;
    lv->palette          = (int)longs[3].flags;
    lv->width_px         = (size & 0x0F) * 0x100;
    lv->height_px        = (size >> 4) * 0x100;
    lv->collision_flags  = longs[2].flags;
    lv->map_b            = longs[0].name;
    lv->map_a            = longs[1].name;
    lv->collision        = longs[2].name;
    lv->interactions     = longs[3].name;
    lv->music_id         = music_id;
    lv->enemy_group      = longs[0].flags;
    lv->enemy_group_late = longs[1].flags;
    lv->extra_art        = ex_u8(x, at + 18);

    JOut *j = jo_obj(x);
    jo_sets(x, j, "label", label);
    jo_sets(x, j, "name", ex_starts(label, "Level_") ? label + 6 : label);
    jo_seti(x, j, "planet", kind >> 7);
    jo_seti(x, j, "tileset", lv->tileset);
    jo_seti(x, j, "width_px", lv->width_px);
    jo_seti(x, j, "height_px", lv->height_px);
    jo_sets(x, j, "map_b", lv->map_b);
    jo_sets(x, j, "map_a", lv->map_a);
    jo_sets(x, j, "collision", lv->collision);
    jo_seti(x, j, "collision_flags", lv->collision_flags);
    jo_seti(x, j, "enemy_group", lv->enemy_group);
    jo_seti(x, j, "enemy_group_late", lv->enemy_group_late);
    jo_sets(x, j, "interactions", lv->interactions);
    jo_seti(x, j, "palette", lv->palette);
    jo_seti(x, j, "extra_art", lv->extra_art);
    jo_sets(x, j, "music", music);
    jo_seti(x, j, "music_id", music_id);
    lv->json = j;
}

typedef struct {
    bool  used;
    int   blocks;
    int   across;
    int   down;
} Tileset;

void ex_step_levels(Extract *x, const ExArgs *a)
{
    char *lvl_dir  = ex_path(x, a->out, "levels");
    char *tile_dir = ex_path(x, a->out, "tiles");
    ex_mkdirs(x, lvl_dir);
    ex_mkdirs(x, tile_dir);

    /* the Level_* labels after LevelData */
    int nlabels = 0;
    u32 end = 0;
    const ExLabel **labels = ex_run_of_labels(x, "LevelData", ex_part_prefix,
                                              "Level_", &nlabels, &end);
    Level *levels = ex_alloc(x, sizeof *levels * (size_t)(nlabels + 1));
    bool used[MAX_TILESETS];
    memset(used, 0, sizeof used);
    for (int i = 0; i < nlabels; i++) {
        parse_level(x, labels[i]->name, &levels[i]);
        if (levels[i].tileset < MAX_TILESETS) {
            used[levels[i].tileset] = true;
        }
    }

    int npal = 0;
    ExPalEntry *pal = ex_palette_table(x, "PalettePtrs", &npal);

    ExStr problems;
    ex_str_init(x, &problems);

    TilesetPtrs ptrs[MAX_TILESETS];
    int nsets = tileset_pointers(x, ptrs);
    Tileset sets[MAX_TILESETS];
    memset(sets, 0, sizeof sets);
    JOut *tilesets = jo_obj(x);
    ExRgb grey;
    memset(&grey, 0, sizeof grey);
    ex_rgb_grey_ramp(&grey);
    ExStr summary;
    ex_str_init(x, &summary);
    for (int n = 0; n < nsets; n++) {
        if (!used[n]) {
            ex_str_addf(&problems, "  tileset %d (%s/%s): no level uses it, "
                                   "skipped\n", n, ptrs[n].art, ptrs[n].defs);
            continue;
        }
        size_t len = 0;
        const u8 *s = stream(x, ptrs[n].art, &len);
        ExTiles tiles = ex_nemesis(x, s, len);
        s = stream(x, ptrs[n].defs, &len);
        ExTiles blocks = ex_art(x, s, len);
        const u8 *defs = blocks.data;
        size_t defs_len = (size_t)blocks.count * EX_TILE_BYTES;
        const char *tsn = tileset_name(x, n);
        ex_png_tiles(x, ex_path(x, tile_dir, ex_fmt(x, "tileset_%s.png", tsn)),
                     tiles.data, tiles.count, &grey, 16, true, NULL, NULL);
        write_blocks_text(x, ex_path(x, lvl_dir, ex_fmt(x, "blocks_%s.txt",
                                                        tsn)),
                          defs, defs_len, n);

        /* the palette: the first level on the tileset's, lines 0-1 the
         * shared entry 2's */
        int first = -1;
        for (int i = 0; i < nlabels && first < 0; i++) {
            if (levels[i].tileset == n) {
                first = levels[i].palette;
            }
        }
        if (first < 0 || first >= npal || npal <= 2) {
            ex_fail(x, "tileset %d: palette %d is no PalettePtrs entry", n,
                    first);
        }
        u16 words[64];
        int nwords = 0;
        int which[2] = { 2, first };
        for (int k = 0; k < 2; k++) {
            size_t blen = 0;
            const u8 *blob = ex_blob(x, pal[which[k]].symbol, &blen);
            size_t lim = (blen < 64) ? blen : 64;
            for (size_t i = 0; i < lim && nwords < 64; i += 2) {
                words[nwords++] = word_at(blob, blen, i);
            }
        }
        int across = 0;
        int down = 0;
        render_blocks(x, ex_path(x, lvl_dir, ex_fmt(x, "blocks_%s.png", tsn)),
                      defs, defs_len, &tiles, words, nwords, &across, &down);
        int max_tile = 0;
        for (size_t i = 0; i < defs_len; i += 2) {
            int t = word_at(defs, defs_len, i) & 0x7FF;
            if (t > max_tile) {
                max_tile = t;
            }
        }
        sets[n].used   = true;
        sets[n].blocks = (int)(defs_len / 32);
        sets[n].across = across;
        sets[n].down   = down;
        JOut *t = jo_obj(x);
        jo_sets(x, t, "art", ptrs[n].art);
        jo_seti(x, t, "art_tiles", tiles.count);
        jo_seti(x, t, "art_compressed", (long long)tiles.used);
        jo_sets(x, t, "defs", ptrs[n].defs);
        jo_seti(x, t, "blocks", sets[n].blocks);
        jo_seti(x, t, "defs_compressed", (long long)blocks.used);
        jo_seti(x, t, "sheet_across", across);
        jo_seti(x, t, "sheet_down", down);
        jo_seti(x, t, "max_tile_used", max_tile);
        jo_set(x, tilesets, ex_fmt(x, "%d", n), t);
        ex_str_addf(&summary, "  tileset %d: %d tiles, %d blocks, highest tile "
                              "referenced %d\n", n, tiles.count,
                    sets[n].blocks, max_tile);
    }

    /* (build 141b) the mist tilesets 1, 3 and 6's backdrops use */
    size_t mlen = 0;
    const u8 *ms = stream(x, "loc_412F8", &mlen);
    ExTiles mist = ex_art(x, ms, mlen);
    ex_png_tiles(x, ex_path(x, tile_dir, "level_mist.png"), mist.data,
                 mist.count, &grey, 16, true, NULL, NULL);

    JOut *out = jo_arr(x);
    for (int index = 0; index < nlabels; index++) {
        Level *lv = &levels[index];
        JOut *j = lv->json;
        jo_seti(x, j, "index", index);
        long long cols = lv->width_px / 32;
        long long rows = lv->height_px / 32;
        long long size_w[2];
        long long size_h[2];
        bool backdrop[2];
        /* map_a (bit $08) then map_b (bit $04), as the Python sets them */
        static const char *const KEYS[2] = { "map_a", "map_b" };
        static const u32 BITS[2] = { 0x08, 0x04 };
        for (int k = 0; k < 2; k++) {
            backdrop[k] = (lv->collision_flags & BITS[k]) != 0;
            size_w[k] = backdrop[k] ? BACKDROP_COLS : cols;
            size_h[k] = backdrop[k] ? BACKDROP_ROWS : rows;
            jo_setb(x, j, ex_fmt(x, "%s_backdrop", KEYS[k]), backdrop[k]);
        }
        if (lv->tileset >= MAX_TILESETS || !sets[lv->tileset].used) {
            ex_fail(x, "%s: tileset %d was not made", lv->label, lv->tileset);
        }
        const Tileset *ts = &sets[lv->tileset];

        /* map_b, then map_a */
        const u8 *maps[2];
        const char *srcs[2] = { lv->map_a, lv->map_b };
        for (int k = 1; k >= 0; k--) {
            long long want = size_w[k] * size_h[k];
            size_t len = 0;
            const u8 *s = stream(x, srcs[k], &len);
            ExTiles chunks = ex_art(x, s, len);
            long long have = (long long)chunks.count * EX_TILE_BYTES;
            jo_seti(x, j, ex_fmt(x, "%s_compressed", KEYS[k]),
                    (long long)chunks.used);
            if (have < want) {
                ex_str_addf(&problems, "  %s: %s decodes to %lld bytes, needs "
                                       "%lld\n", lv->label, KEYS[k], have,
                            want);
                ex_fail(x, "%s: %s is short", lv->label, KEYS[k]);
            }
            maps[k] = chunks.data;
            int top = 0;
            for (long long i = 0; i < want; i++) {
                if (maps[k][i] > top) {
                    top = maps[k][i];
                }
            }
            if (top >= ts->blocks) {
                ex_str_addf(&problems, "  %s: %s block id %d >= %d\n",
                            lv->label, KEYS[k], top, ts->blocks);
            }
        }

        char *stem = ex_fmt(x, "%02x_%s", index,
                            ex_starts(lv->label, "Level_") ? lv->label + 6
                                                           : lv->label);
        Layer full[2];
        Layer apart[2];
        int nfull = 0;
        int napart = 0;
        static const char *const LAYER_NAMES[2] = { "planeA", "planeB" };
        for (int k = 1; k >= 0; k--) {
            Layer l = { LAYER_NAMES[k], (int)size_w[k], (int)size_h[k],
                        maps[k] };
            if (backdrop[k]) {
                apart[napart++] = l;
            } else {
                full[nfull++] = l;
            }
        }
        Prop props[10] = {
            { "index", index },
            { "cols", cols },
            { "rows", rows },
            { "tileset", lv->tileset },
            { "palette", lv->palette },
            { "music", lv->music_id },
            { "flags", lv->collision_flags },
            { "enemy_group", lv->enemy_group },
            { "enemy_group_late", lv->enemy_group_late },
            { "extra_art", lv->extra_art },
        };
        if (nfull > 0) {
            write_tmx(x, ex_path(x, lvl_dir, ex_fmt(x, "%s.tmx", stem)),
                      lv->tileset, ts->blocks, ts->across, ts->down, full,
                      nfull, props, 10);
        }
        JOut *back = jo_obj(x);
        for (int k = 0; k < napart; k++) {
            char suffix = apart[k].name[strlen(apart[k].name) - 1];
            char *file = ex_fmt(x, "%s_%c.tmx", stem, suffix);
            write_tmx(x, ex_path(x, lvl_dir, file), lv->tileset, ts->blocks,
                      ts->across, ts->down, &apart[k], 1, props, 10);
            jo_sets(x, back, ex_fmt(x, "%c", suffix), file);
        }

        jo_seti(x, j, "cols", cols);
        jo_seti(x, j, "rows", rows);
        JOut *files = jo_obj(x);
        if (nfull > 0) {
            jo_sets(x, files, "map", ex_fmt(x, "%s.tmx", stem));
        } else {
            jo_set(x, files, "map", jo_null(x));
        }
        jo_set(x, files, "backdrop", back);
        const char *tsn = tileset_name(x, lv->tileset);
        jo_sets(x, files, "blocks", ex_fmt(x, "blocks_%s.txt", tsn));
        jo_sets(x, files, "tiles", ex_fmt(x, "tileset_%s.png", tsn));
        jo_set(x, j, "files", files);
        ex_write_text(x, ex_path(x, lvl_dir, ex_fmt(x, "%s.json", stem)),
                      jo_dump(x, j, 2, false));
        size_t blen = 0;
        ex_blob(x, lv->collision, &blen);
        jo_seti(x, j, "collision_table", (long long)blen);
        ex_blob(x, lv->interactions, &blen);
        jo_seti(x, j, "interaction_bytes", (long long)blen);
        jo_add(out, j);
    }

    JOut *doc = jo_obj(x);
    jo_set(x, doc, "tilesets", tilesets);
    jo_set(x, doc, "levels", out);
    ex_write_text(x, ex_path(x, lvl_dir, "levels.json"),
                  jo_dump(x, doc, 2, false));

    write_tile_anims(x, a->out);
    write_map_patches(x, a->out);

    ex_say(x, "%d levels, %d tilesets", jo_len(out), jo_len(tilesets));
    if (summary.len > 0) {
        summary.buf[summary.len - 1] = '\0';
        ex_say(x, "%s", summary.buf);
    }
    if (problems.len > 0) {
        problems.buf[problems.len - 1] = '\0';
        ex_warn(x, "%s", problems.buf);
    } else {
        ex_say(x, "  every map decodes to at least its full size, every "
                  "block id in range");
    }
}
