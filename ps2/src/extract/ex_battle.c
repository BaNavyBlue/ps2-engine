/* (build 189) tools/extract_battle.py in C: everything ProcessRandomBattle
 * and BattleScreen read from the ROM, into assets/data/battle.json -- the
 * enemies (EnemyData, 64 bytes a record, named from EnemyNames), the
 * formations and groups, the encounter zones, the positions, the party's
 * and the enemies' maps and sprites, the animation scripts by table, the
 * weapons' and techniques' effects and their art. A pointer is named by
 * the label at the address it gives (the nearest before it, "+N", when
 * none starts there), art by the binclude'd file at its address. */

#include "excommon.h"

#include <stdlib.h>
#include <string.h>

void ex_step_battle(Extract *x, const ExArgs *a);

#define ENEMY_SIZE     64
#define NAME_LEN       10
#define FORMATION_SIZE 5
#define GROUP_SIZE     8
#define SCRIPT_MAX     0x100

/* ---- names ---- */

typedef bool (*Pattern)(const char *name);

static bool all_of(const char *s, const char *set)
{
    if (*s == '\0') {
        return false;
    }
    for (; *s != '\0'; s++) {
        if (strchr(set, *s) == NULL) {
            return false;
        }
    }
    return true;
}

#define HEX_UPPER "0123456789ABCDEF"
#define WORD_CHARS "0123456789_abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ"

/* r"loc_[0-9A-F]+" */
static bool is_loc(const char *n)
{
    return ex_starts(n, "loc_") && all_of(n + 4, HEX_UPPER);
}

/* r"loc_[0-9A-F]+|AnimOff_\w+" */
static bool is_script(const char *n)
{
    return is_loc(n) || (ex_starts(n, "AnimOff_") && all_of(n + 8, WORD_CHARS));
}

/* Labels.name: the label the pattern takes at an address, else the
 * nearest before it as "label+N". */
static const char *label_name(Extract *x, u32 address, Pattern match,
                              const char *what)
{
    int n = 0;
    const ExLabel **names = ex_names_at(x, address, &n);
    const char *here = NULL;
    int count = 0;
    ExStr all;
    ex_str_init(x, &all);
    for (int i = 0; i < n; i++) {
        if (match(names[i]->name)) {
            ex_str_addf(&all, "%s%s", (count > 0) ? ", " : "", names[i]->name);
            here = names[i]->name;
            count++;
        }
    }
    if (count > 1) {
        ex_fail(x, "$%X is %s: which is meant?", (unsigned)address, all.buf);
    }
    if (count == 1) {
        return here;
    }
    const ExMap *m = &x->map;
    int lo = 0;
    int hi = m->nlabels;
    while (lo < hi) {
        int mid = lo + (hi - lo) / 2;
        if (m->by_addr[mid].addr < address) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    for (int k = lo - 1; k >= 0; k--) {
        const ExLabel *l = &m->labels[m->by_addr[k].rank];
        if (match(l->name)) {
            return ex_fmt(x, "%s+%u", l->name, (unsigned)(address - l->addr));
        }
    }
    ex_fail(x, "no %s label at or before $%X", what, (unsigned)address);
}

static const char *pointer_label(Extract *x, u32 address, const char *prefix)
{
    const char *label = ex_label_at(x, address, prefix);
    if (label == NULL) {
        ex_fail(x, "no %s label at $%X", prefix, (unsigned)address);
    }
    return label;
}

/* The PNG an art block decodes to, from its binclude'd file; NULL when
 * the art is written out in place. */
static const char *art_label_name(Extract *x, u32 address)
{
    const char *f = ex_file_at(x, address);
    return (f == NULL) ? NULL : ex_art_name(ex_file_stem(x, f));
}

static u32 be32(const u8 *p)
{
    return (u32)p[0] << 24 | (u32)p[1] << 16 | (u32)p[2] << 8 | p[3];
}

static int be16(const u8 *p)
{
    return p[0] << 8 | p[1];
}

static int s8v(u8 b)
{
    return (b > 127) ? b - 256 : b;
}

/* words(): big-endian words, a lone last byte its own value. */
static JOut *words_json(Extract *x, const u8 *d, size_t len)
{
    JOut *arr = jo_arr(x);
    for (size_t i = 0; i < len; i += 2) {
        jo_add(arr, jo_int(x, (i + 1 < len) ? be16(d + i) : d[i]));
    }
    return arr;
}

static JOut *bytes_json(Extract *x, const u8 *d, size_t len)
{
    JOut *arr = jo_arr(x);
    for (size_t i = 0; i < len; i++) {
        jo_add(arr, jo_int(x, d[i]));
    }
    return arr;
}

/* ---- a sorted set of strings ---- */

typedef struct {
    const char **at;
    int          count;
    int          cap;
} Strs;

static int strs_find(const Strs *s, const char *v);

static void strs_add(Extract *x, Strs *s, const char *v)
{
    if (strs_find(s, v) >= 0) {
        return;
    }
    if (s->count == s->cap) {
        int cap = (s->cap > 0) ? s->cap * 2 : 16;
        const char **more = ex_alloc(x, sizeof *more * (size_t)cap);
        if (s->count > 0) {
            memcpy(more, s->at, sizeof *more * (size_t)s->count);
        }
        s->at  = more;
        s->cap = cap;
    }
    s->at[s->count++] = v;
}

static int strs_find(const Strs *s, const char *v)
{
    for (int i = 0; i < s->count; i++) {
        if (strcmp(s->at[i], v) == 0) {
            return i;
        }
    }
    return -1;
}

static int cmp_str(const void *a, const void *b)
{
    return strcmp(*(const char *const *)a, *(const char *const *)b);
}

static void strs_sort(Strs *s)
{
    if (s->count > 1) {
        qsort(s->at, (size_t)s->count, sizeof *s->at, cmp_str);
    }
}

/* ---- the art's file names ---- */

typedef struct {
    Strs        labels;     /* the Battle_*Art labels, in line order */
    const char **names;     /* each one's PNG name */
    Strs        inline_;    /* those written out in place */
} ArtFiles;

static ArtFiles art_file_names(Extract *x)
{
    ArtFiles f;
    memset(&f, 0, sizeof f);
    const ExMap *m = &x->map;
    f.names = ex_alloc(x, sizeof *f.names * (size_t)(m->nlabels + 1));
    for (int i = 0; i < m->nlabels; i++) {
        const char *n = m->labels[i].name;
        if (!ex_starts(n, "Battle_") || !ex_ends(n, "Art") || strlen(n) < 10) {
            continue;
        }
        const char *name = art_label_name(x, m->labels[i].addr);
        strs_add(x, &f.labels, n);
        int k = strs_find(&f.labels, n);
        if (name == NULL) {
            char *core = ex_strndup(x, n + 7, strlen(n) - 10);
            f.names[k] = ex_fmt(x, "battle_%s_art", ex_lower(x, core));
            strs_add(x, &f.inline_, n);
        } else {
            f.names[k] = name;
        }
    }
    return f;
}

static const char *art_file(const ArtFiles *f, const char *label)
{
    int i = strs_find(&f->labels, label);
    return (i >= 0) ? f->names[i] : NULL;
}

/* ---- the records ---- */

static JOut *read_enemies(Extract *x, const ArtFiles *files, Strs *maps)
{
    size_t rawlen = 0;
    const u8 *raw = ex_blob(x, "EnemyNames", &rawlen);
    int nnames = 0;
    char **names = ex_alloc(x, sizeof *names * (rawlen / NAME_LEN + 1));
    for (size_t i = 0; i + (NAME_LEN - 1) < rawlen; i += NAME_LEN) {
        names[nnames++] = ex_decode_name(x, raw + i, NAME_LEN);
    }

    int count = 0;
    u32 end = 0;
    const ExLabel **entries = ex_run_of_labels(x, "EnemyData", ex_part_prefix,
                                               "Enemy_", &count, &end);
    JOut *out = jo_arr(x);
    for (int k = 0; k < count; k++) {
        int n = k + 1;
        const char *label = entries[k]->name;
        u32 at = entries[k]->addr;
        const u8 *e = ex_read(x, at, ENEMY_SIZE);
        long long room = (long long)ex_next_addr(x, label) - at;
        if (room < ENEMY_SIZE) {
            ex_fail(x, "%s: %lld bytes", label, room);
        }
        JOut *pal = jo_arr(x);
        for (int i = 0; i < 15; i++) {
            jo_add(pal, jo_str(x, ex_fmt(x, "%04X", be16(e + 2 * i))));
        }
        const char *art = pointer_label(x, be32(e + 30), "Battle_");
        const u8 *r = e + 34;
        const char *map = pointer_label(x, be32(r + 24), "Battle_");
        strs_add(x, maps, map);
        const char *file = art_file(files, art);

        JOut *j = jo_obj(x);
        jo_seti(x, j, "id", n);
        jo_sets(x, j, "label", label);
        jo_sets(x, j, "name", (n < nnames) ? names[n] : "");
        jo_set(x, j, "palette", pal);
        jo_sets(x, j, "art", art);
        jo_set(x, j, "art_file", (file != NULL) ? jo_str(x, file) : jo_null(x));
        jo_seti(x, j, "exp", be16(r));
        jo_seti(x, j, "meseta", be16(r + 2));
        jo_seti(x, j, "attack", be16(r + 4));
        jo_seti(x, j, "defense", be16(r + 6));
        jo_seti(x, j, "type", r[8]);
        jo_seti(x, j, "agility", r[9]);
        jo_seti(x, j, "hp", be16(r + 10));
        jo_seti(x, j, "technique", r[12]);
        jo_seti(x, j, "technique_rate", r[13]);
        jo_seti(x, j, "success_index", r[14]);
        jo_seti(x, j, "escape", r[15]);
        jo_seti(x, j, "object", r[16]);
        jo_seti(x, j, "anim_duration", r[17]);
        jo_set(x, j, "anim_bytes", bytes_json(x, r + 18, 4));
        jo_seti(x, j, "sfx_attack", r[22]);
        jo_seti(x, j, "sfx_technique", r[23]);
        jo_sets(x, j, "map", map);
        jo_seti(x, j, "anim", r[28]);
        jo_seti(x, j, "id_copy", r[29]);
        jo_add(out, j);
    }
    return out;
}

static JOut *read_formations(Extract *x)
{
    size_t len = 0;
    const u8 *t = ex_blob(x, "EnemyBattleFormationData", &len);
    JOut *out = jo_arr(x);
    for (size_t i = 0; i + (FORMATION_SIZE - 1) < len; i += FORMATION_SIZE) {
        JOut *f = jo_obj(x);
        jo_seti(x, f, "enemy1", t[i]);
        jo_seti(x, f, "count1", t[i + 1]);
        jo_seti(x, f, "enemy2", t[i + 2]);
        jo_seti(x, f, "count2", t[i + 3]);
        jo_seti(x, f, "layout", t[i + 4]);
        jo_add(out, f);
    }
    return out;
}

static JOut *read_groups(Extract *x)
{
    size_t len = 0;
    const u8 *d = ex_blob(x, "EnemyFormationTable", &len);
    JOut *out = jo_arr(x);
    for (size_t i = 0; i + (GROUP_SIZE - 1) < len; i += GROUP_SIZE) {
        jo_add(out, bytes_json(x, d + i, GROUP_SIZE));
    }
    return out;
}

static JOut *read_regions(Extract *x)
{
    const u8 *a = ex_run_of(x, "loc_23C3A", 0x60);
    const u8 *b = ex_run_of(x, "loc_23C9A", 0x60);
    const u8 *c = ex_run_of(x, "loc_23CFA", 0xC0);
    const u8 *d = ex_run_of(x, "loc_23DBA", 0x40);
    JOut *out = jo_obj(x);
    jo_set(x, out, "motavia", bytes_json(x, a, 0x60));
    jo_set(x, out, "motavia_scooter", bytes_json(x, b, 0x60));
    jo_set(x, out, "motavia_late", bytes_json(x, c, 0x60));
    jo_set(x, out, "motavia_late_scooter", bytes_json(x, c + 0x60, 0x60));
    jo_set(x, out, "skure", bytes_json(x, d, 0x40));
    return out;
}

static JOut *read_char_sprites(Extract *x, Strs *maps)
{
    size_t len = 0;
    const u8 *d = ex_blob(x, "Battle_CharObjectPtrs", &len);
    JOut *out = jo_arr(x);
    for (size_t i = 0; i + 7 < len; i += 8) {
        u32 m = be32(d + i);
        u32 art = be32(d + i + 4);
        JOut *c = jo_obj(x);
        const char *map = pointer_label(x, m & 0xFFFFFF, "Map_");
        strs_add(x, maps, map);
        jo_sets(x, c, "map", map);
        jo_sets(x, c, "art", pointer_label(x, art, "Battle_"));
        jo_seti(x, c, "palette_line", (m >> 29) & 3);
        jo_add(out, c);
    }
    return out;
}

/* ---- the animation scripts ---- */

/* Where a run of scripts from `start` ends: the `even` after the last (a
 * zero at an odd address after an end mark, a label on the next byte),
 * or SCRIPT_MAX bytes on. */
static long long script_end(Extract *x, long long start)
{
    long long limit = start + SCRIPT_MAX;
    for (long long p = start + 1; p < limit; p++) {
        if (!(p & 1)) {
            continue;
        }
        if (ex_u8(x, p) != 0) {
            continue;
        }
        u8 before = ex_u8(x, p - 1);
        if (before != 0xFE && before != 0xFF) {
            continue;
        }
        int n = 0;
        ex_names_at(x, (u32)(p + 1), &n);
        if (n > 0) {
            return p;
        }
    }
    return limit;
}

static JOut *read_anim_table(Extract *x, const char *table, Pattern match)
{
    long long base = ex_addr(x, table);
    size_t len = 0;
    const u8 *d = ex_blob(x, table, &len);
    JOut *index = jo_arr(x);
    Strs keys;
    memset(&keys, 0, sizeof keys);
    long long *at = ex_alloc(x, sizeof *at * (len / 2 + 2));
    for (size_t i = 0; i < len; i += 2) {
        int off = (i + 1 < len) ? be16(d + i) : d[i];
        long long where = base + off;
        const char *key = label_name(x, (u32)where, match, "script");
        jo_add(index, jo_str(x, key));
        int before = keys.count;
        strs_add(x, &keys, key);
        if (keys.count > before) {
            at[before] = where;
        }
    }
    JOut *scripts = jo_obj(x);
    for (int k = 0; k < keys.count; k++) {
        long long start = at[k];
        long long stop = script_end(x, start);
        jo_set(x, scripts, keys.at[k],
               bytes_json(x, ex_read(x, start, stop - start),
                          (size_t)(stop - start)));
    }
    JOut *out = jo_obj(x);
    jo_set(x, out, "index", index);
    jo_set(x, out, "scripts", scripts);
    return out;
}

/* ---- maps and sprites ---- */

static long long *frame_table(Extract *x, const char *label, int *count)
{
    long long base = ex_addr(x, label);
    size_t len = 0;
    const u8 *d = ex_blob(x, label, &len);
    long long *out = ex_alloc(x, sizeof *out * (len / 2 + 2));
    int n = 0;
    for (size_t i = 0; i < len; i += 2) {
        out[n++] = base + ((i + 1 < len) ? be16(d + i) : d[i]);
    }
    *count = n;
    return out;
}

static JOut *sprite_pieces(Extract *x, const u8 *d)
{
    JOut *pieces = jo_arr(x);
    for (int i = 0; i < d[0]; i++) {
        const u8 *b = d + 1 + i * 5;
        JOut *p = jo_obj(x);
        jo_seti(x, p, "y", s8v(b[0]));
        jo_seti(x, p, "size", b[1]);
        jo_seti(x, p, "tile", be16(b + 2));
        jo_seti(x, p, "x", s8v(b[4]));
        jo_add(pieces, p);
    }
    return pieces;
}

/* A frame: a count and five-byte pieces, or a nametable block (x, y,
 * w-1, h-1, cells). */
static JOut *read_frame(Extract *x, long long at)
{
    u8 first = ex_u8(x, at);
    JOut *f = jo_obj(x);
    if (first != 0 && first < 0x80) {
        jo_set(x, f, "pieces", sprite_pieces(x, ex_read(x, at, 1 + 5 * first)));
        return f;
    }
    const u8 *head = ex_read(x, at, 6);
    int xx = be16(head);
    int y  = be16(head + 2);
    int w  = head[4] + 1;
    int h  = head[5] + 1;
    JOut *cells = jo_arr(x);
    long long p = at + 6;
    for (int r = 0; r < h; r++) {
        JOut *row = jo_arr(x);
        for (int c = 0; c < w; c++) {
            jo_add(row, jo_int(x, ex_u16(x, p)));
            p += 2;
        }
        jo_add(cells, row);
    }
    jo_seti(x, f, "x", (xx >= 0x8000) ? xx - 0x10000 : xx);
    jo_seti(x, f, "y", (y >= 0x8000) ? y - 0x10000 : y);
    jo_seti(x, f, "w", w);
    jo_seti(x, f, "h", h);
    jo_set(x, f, "cells", cells);
    return f;
}

static JOut *read_enemy_maps(Extract *x, const Strs *labels)
{
    JOut *out = jo_obj(x);
    for (int i = 0; i < labels->count; i++) {
        int n = 0;
        long long *frames = frame_table(x, labels->at[i], &n);
        JOut *arr = jo_arr(x);
        for (int k = 0; k < n; k++) {
            jo_add(arr, read_frame(x, frames[k]));
        }
        jo_set(x, out, labels->at[i], arr);
    }
    return out;
}

static JOut *read_sprite_maps(Extract *x, const Strs *labels)
{
    JOut *out = jo_obj(x);
    for (int i = 0; i < labels->count; i++) {
        const char *label = labels->at[i];
        int n = 0;
        long long *frames = frame_table(x, label, &n);
        JOut *arr = jo_arr(x);
        for (int k = 0; k < n; k++) {
            u8 count = ex_u8(x, frames[k]);
            jo_add(arr, sprite_pieces(x, ex_read(x, frames[k], 1 + 5 * count)));
        }
        jo_set(x, out, label, arr);
    }
    return out;
}

/* ---- the weapons' and techniques' effects ---- */

static JOut *read_props(Extract *x, const char *table, const char *prefix,
                        int nbytes, Strs *maps)
{
    int count = 0;
    u32 end = 0;
    const ExLabel **entries = ex_run_of_labels(x, table, ex_part_prefix,
                                               prefix, &count, &end);
    JOut *out = jo_arr(x);
    for (int k = 0; k < count; k++) {
        u32 at = entries[k]->addr;
        const u8 *b = ex_read(x, at, nbytes);
        u32 longs[2] = { ex_u32(x, at + nbytes), ex_u32(x, at + nbytes + 4) };
        JOut *m[2];
        for (int i = 0; i < 2; i++) {
            if (longs[i] & 0xFFFFFF) {
                const char *l = pointer_label(x, longs[i] & 0xFFFFFF, "Map_");
                m[i] = jo_str(x, l);
                strs_add(x, maps, l);
            } else {
                m[i] = jo_null(x);
            }
        }
        u32 a1 = longs[0] >> 24;
        u32 a2 = longs[1] >> 24;
        JOut *p = jo_obj(x);
        jo_sets(x, p, "label", entries[k]->name);
        jo_set(x, p, "bytes", bytes_json(x, b, (size_t)nbytes));
        jo_seti(x, p, "art", a1 & 0x7F);
        jo_seti(x, p, "pal", a1 >> 7);
        jo_set(x, p, "map", m[0]);
        jo_seti(x, p, "art2", a2 & 0x7F);
        jo_seti(x, p, "pal2", a2 >> 7);
        jo_set(x, p, "map2", m[1]);
        jo_add(out, p);
    }
    return out;
}

static JOut *read_effect_art(Extract *x, Strs *labels)
{
    size_t len = 0;
    const u8 *d = ex_blob(x, "WeaponTechArtPtrs", &len);
    JOut *entries = jo_arr(x);
    for (size_t i = 0; i + 3 < len; i += 4) {
        u32 at = be32(d + i);
        const char *lab = pointer_label(x, at, "");
        int count = ex_u16(x, at) + 1;
        JOut *parts = jo_arr(x);
        for (int k = 0; k < count; k++) {
            u32 art_at = ex_u32(x, at + 2 + 6 * (long long)k);
            const char *art = pointer_label(x, art_at, "");
            int vram = ex_u16(x, at + 6 + 6 * (long long)k);
            const char *file = art_label_name(x, art_at);
            JOut *p = jo_obj(x);
            jo_sets(x, p, "art", art);
            jo_sets(x, p, "file", (file != NULL) ? file : art);
            jo_seti(x, p, "vram", vram);
            jo_add(parts, p);
            strs_add(x, labels, art);
        }
        JOut *e = jo_obj(x);
        jo_sets(x, e, "label", lab);
        jo_set(x, e, "parts", parts);
        jo_add(entries, e);
    }
    return entries;
}

static JOut *strs_json(Extract *x, const Strs *s)
{
    JOut *arr = jo_arr(x);
    for (int i = 0; i < s->count; i++) {
        jo_add(arr, jo_str(x, s->at[i]));
    }
    return arr;
}

void ex_step_battle(Extract *x, const ExArgs *a)
{
    ArtFiles files = art_file_names(x);

    Strs map_set;
    memset(&map_set, 0, sizeof map_set);
    JOut *enemies = read_enemies(x, &files, &map_set);
    strs_sort(&map_set);
    JOut *maps = read_enemy_maps(x, &map_set);
    Strs char_maps;
    memset(&char_maps, 0, sizeof char_maps);
    JOut *chars = read_char_sprites(x, &char_maps);
    JOut *sprites = read_sprite_maps(x, &char_maps);

    Strs effect_map_set;
    memset(&effect_map_set, 0, sizeof effect_map_set);
    JOut *weapons = read_props(x, "WeaponProperties", "WeaponProp_", 4,
                               &effect_map_set);
    JOut *techs = read_props(x, "TechniqueProperties", "TechProp_", 2,
                             &effect_map_set);
    Strs art_labels;
    memset(&art_labels, 0, sizeof art_labels);
    JOut *effect_art = read_effect_art(x, &art_labels);
    strs_sort(&effect_map_set);
    JOut *effect_maps = read_enemy_maps(x, &effect_map_set);

    static const char *const TABLES[8][2] = {
        { "idle", "loc_2601A" },
        { "enemy_attack", "loc_26118" },
        { "enemy_tech", "loc_26216" },
        { "enemy_child_a", "loc_26314" },
        { "enemy_child_b", "loc_26414" },
        { "chars", "CharBattle_AnimOffsets" },
        { "effect", "loc_27364" },
        { "effect2", "loc_27226" },
    };
    JOut *anim_tables = jo_obj(x);
    for (int k = 0; k < 8; k++) {
        jo_set(x, anim_tables, TABLES[k][0],
               read_anim_table(x, TABLES[k][1], is_script));
    }

    size_t len = 0;
    const u8 *xs = ex_blob(x, "EnemyXPosArray", &len);
    JOut *enemy_x = jo_arr(x);
    size_t nwords = (len + 1) / 2;
    for (size_t i = 0; i < nwords; i += 5) {
        JOut *row = jo_arr(x);
        for (size_t k = i; k < i + 5 && k < nwords; k++) {
            size_t o = 2 * k;
            jo_add(row, jo_int(x, (o + 1 < len) ? be16(xs + o) : xs[o]));
        }
        jo_add(enemy_x, row);
    }

    Strs inline_effects;
    memset(&inline_effects, 0, sizeof inline_effects);
    for (int i = 0; i < art_labels.count; i++) {
        if (ex_file_at(x, (u32)ex_addr(x, art_labels.at[i])) == NULL) {
            strs_add(x, &inline_effects, art_labels.at[i]);
        }
    }
    strs_sort(&inline_effects);
    strs_sort(&files.inline_);

    JOut *anim = read_anim_table(x, "loc_2601A", is_loc);
    JOut *formations = read_formations(x);
    JOut *groups = read_groups(x);
    JOut *out = jo_obj(x);
    jo_set(x, out, "enemies", enemies);
    jo_set(x, out, "formations", formations);
    jo_set(x, out, "groups", groups);
    jo_set(x, out, "regions", read_regions(x));
    jo_set(x, out, "enemy_x", enemy_x);
    const u8 *cp = ex_blob(x, "Battle_CharCoordinates", &len);
    jo_set(x, out, "char_pos", words_json(x, cp, len));
    jo_set(x, out, "char_sprites", chars);
    jo_set(x, out, "anim", anim);
    jo_set(x, out, "maps", maps);
    jo_set(x, out, "sprites", sprites);
    jo_set(x, out, "inline_art", strs_json(x, &files.inline_));
    jo_set(x, out, "anim_tables", anim_tables);
    jo_set(x, out, "weapon_props", weapons);
    jo_set(x, out, "tech_props", techs);
    jo_set(x, out, "effect_art", effect_art);
    jo_set(x, out, "effect_art_inline", strs_json(x, &inline_effects));
    jo_set(x, out, "effect_maps", effect_maps);
    const u8 *sr = ex_blob(x, "EnemyTechSuccessRate", &len);
    jo_set(x, out, "success_rate", bytes_json(x, sr, len));
    const u8 *tc = ex_blob(x, "Battle_CharTargetChances", &len);
    jo_set(x, out, "target_chances", bytes_json(x, tc, len));
    jo_set(x, out, "hit_anim", bytes_json(x, ex_run_of(x, "loc_34EE", 28), 28));

    ex_mkdirs(x, a->out);
    char *path = ex_path(x, a->out, "battle.json");
    ex_write_text(x, path, jo_dump(x, out, 1, false));
    ex_say(x, "%d enemies, %d formations, %d groups, %d map sets, %d "
              "animation scripts -> %s", jo_len(enemies), jo_len(formations),
           jo_len(groups), jo_len(maps), jo_len(jo_get(anim, "scripts")),
           path);
}
