/* (build 171) play.py's ways: the levels (levels.json), a level's
 * collision as walk.c reads it (LevelMap), the path on one level and the
 * walk along it (goto), doors, the ways off a level and the plan across
 * levels (ways_out, holes, plan, travel). */

#include "pl.h"

#include "../core/json.h"

#include <stdlib.h>

/* ---- levels.json ---- */

typedef struct {
    int  index;
    char name[40];
    int  cols, rows, tileset, collision_flags;
    int  enemy_group, enemy_group_late;
} LevelInfo;

static LevelInfo *g_levels;
static int        g_level_n;

static void levels_load(void)
{
    if (g_levels != NULL) {
        return;
    }
    char *text = pl_read_file("assets/levels/levels.json", NULL);
    JsonValue *root = (text != NULL) ? json_parse(text, NULL) : NULL;
    free(text);
    const JsonValue *arr = json_get(root, "levels");
    if (arr == NULL) {
        pl_error("cannot read assets/levels/levels.json");
    }
    g_level_n = json_length(arr);
    g_levels = pl_alloc((size_t)g_level_n * sizeof *g_levels);
    for (int i = 0; i < g_level_n; i++) {
        const JsonValue *l = json_at(arr, i);
        LevelInfo *li = &g_levels[i];
        li->index = (int)json_int(l, "index", -1);
        snprintf(li->name, sizeof li->name, "%s", json_str(l, "name", ""));
        li->cols = (int)json_int(l, "cols", 0);
        li->rows = (int)json_int(l, "rows", 0);
        li->tileset = (int)json_int(l, "tileset", 0);
        li->collision_flags = (int)json_int(l, "collision_flags", 0);
        li->enemy_group = (int)json_int(l, "enemy_group", 0);
        li->enemy_group_late = (int)json_int(l, "enemy_group_late", 0);
    }
    json_free(root);
}

/* _levels()[index]: the last entry with the index, as the dict keeps. */
static const LevelInfo *level_info(int index)
{
    levels_load();
    const LevelInfo *hit = NULL;
    for (int i = 0; i < g_level_n; i++) {
        if (g_levels[i].index == index) {
            hit = &g_levels[i];
        }
    }
    return hit;
}

/* _levels()[level]'s enemy_group (enemy_group_late), 0 for none. */
int level_enemy_group(int level, bool late)
{
    const LevelInfo *li = level_info(level);
    if (li == NULL) {
        return 0;
    }
    return late ? li->enemy_group_late : li->enemy_group;
}

int levels_count(void)
{
    levels_load();
    return g_level_n;
}

static bool same_nocase(const char *a, const char *b)
{
    for (; *a && *b; a++, b++) {
        char x = *a, y = *b;
        if (x >= 'A' && x <= 'Z') {
            x = (char)(x - 'A' + 'a');
        }
        if (y >= 'A' && y <= 'Z') {
            y = (char)(y - 'A' + 'a');
        }
        if (x != y) {
            return false;
        }
    }
    return *a == *b;
}

/* map_index for a name: the first level of it, any case; PL_NONE. The
 * dict's order is the file's, each index where it first came. */
int map_index_name(const char *name)
{
    levels_load();
    for (int i = 0; i < g_level_n; i++) {
        const LevelInfo *li = level_info(g_levels[i].index);
        if (li == &g_levels[i] && same_nocase(li->name, name)) {
            return li->index;
        }
    }
    return PL_NONE;
}

const char *level_name(int index, char *buf, size_t n)
{
    if (index == PL_NONE) {
        return "no level (off the map)";
    }
    const LevelInfo *li = level_info(index);
    if (li != NULL) {
        return li->name;
    }
    snprintf(buf, n, "level %d", index);
    return buf;
}

static void level_stem(int index, char *out, size_t n)
{
    const LevelInfo *li = level_info(index);
    if (li == NULL) {
        pl_error("KeyError: %d", index);
    }
    snprintf(out, n, "%02x_%s", (unsigned)index, li->name);
}

/* ---- HELD and the item patches ---- */

char  g_held[16][32];
int   g_held_n;

void held_set(const char *const *names, int n)
{
    g_held_n = 0;
    for (int i = 0; i < n && i < 16; i++) {
        snprintf(g_held[g_held_n++], 32, "%s", names[i]);
    }
}

static bool held_has(const char *name)
{
    for (int i = 0; i < g_held_n; i++) {
        if (streq(g_held[i], name)) {
            return true;
        }
    }
    return false;
}

/* (build 176) HELD.add(name) */
void held_add(const char *name)
{
    if (!held_has(name) && g_held_n < 16) {
        snprintf(g_held[g_held_n++], 32, "%s", name);
    }
}

static int str_cmp(const void *a, const void *b)
{
    return strcmp((const char *)a, (const char *)b);
}

typedef struct {
    char plane[16];
    int  block;
    int *at;
    int  at_n;
} Patch;

static JsonValue *g_patches;

/* item_patches(level): [(plane, block, offsets)] whose item is held. */
static int item_patches(int level, Patch *out, int cap)
{
    if (g_patches == NULL) {
        char *text = pl_read_file("assets/data/map_patches.json", NULL);
        g_patches = (text != NULL) ? json_parse(text, NULL) : NULL;
        free(text);
        if (g_patches == NULL) {
            pl_error("cannot read assets/data/map_patches.json");
        }
    }
    const JsonValue *arr = json_get(g_patches, "patches");
    int n = 0;
    for (int i = 0; i < json_length(arr); i++) {
        const JsonValue *p = json_at(arr, i);
        const char *item = json_str(p, "item_name", "");
        if (item == NULL) {
            item = "";
        }
        char name[64];
        snprintf(name, sizeof name, "%s",
                 starts(item, "ItemID_") ? item + 7 : item);
        const JsonValue *lv = json_get(p, "level");
        const JsonValue *set = json_get(p, "set");
        if (lv == NULL || lv->type != JSON_NUMBER || (int)lv->number != level
            || name[0] == '\0' || !held_has(name) || set == NULL) {
            continue;
        }
        if (n < cap && out != NULL) {
            Patch *q = &out[n];
            snprintf(q->plane, sizeof q->plane, "plane%s",
                     json_str(p, "plane", ""));
            q->block = (int)json_int(set, "block", 0);
            const JsonValue *at = json_get(set, "at");
            q->at_n = json_length(at);
            q->at = pl_alloc((size_t)(q->at_n + 1) * sizeof *q->at);
            for (int k = 0; k < q->at_n; k++) {
                q->at[k] = (int)json_at(at, k)->number;
            }
        }
        n++;
    }
    return n;
}

/* The caches' key: the level, and HELD sorted when it patches the map. */
static void map_key(int level, char *out, size_t n)
{
    size_t used = (size_t)snprintf(out, n, "%d", level);
    if (item_patches(level, NULL, 0) == 0) {
        return;
    }
    char sorted[16][32];
    memcpy(sorted, g_held, sizeof sorted);
    qsort(sorted, (size_t)g_held_n, 32, str_cmp);
    for (int i = 0; i < g_held_n && used < n; i++) {
        used += (size_t)snprintf(out + used, n - used, "/%s", sorted[i]);
    }
}

/* ---- LevelMap ---- */

static int plane_of(const LevelMap *lm, const char *name)
{
    for (int i = 0; i < lm->nplanes; i++) {
        if (streq(lm->pname[i], name)) {
            return i;
        }
    }
    return -1;
}

int lm_block(const LevelMap *lm, int plane, int cx, int cy)
{
    int i = (pymod(cy, lm->h) >> 1) * lm->cols + (pymod(cx, lm->w) >> 1);
    if (plane < 0 || i >= lm->plane_len[plane]) {
        pl_error("IndexError: list index out of range (block)");
    }
    return lm->plane[plane][i];
}

void lm_bits(const LevelMap *lm, int cx, int cy, int plane, bool *solid,
             bool *special)
{
    int q = ((cy & 1) << 1) + (cx & 1);
    int b = lm_block(lm, (plane < 0) ? lm->coll : plane, cx, cy);
    if (b < 0) {
        b += 256;
    }
    if (b < 0 || b > 255) {
        pl_error("IndexError: list index out of range (attr)");
    }
    int a = lm->attr[b];
    *solid = (a & (1 << q)) != 0;
    *special = (a & (0x10 << q)) != 0;
}

bool lm_open(const LevelMap *lm, int cx, int cy)
{
    bool solid, special;
    lm_bits(lm, cx, cy, -1, &solid, &special);
    return !solid;
}

static char *tmx_attr(const char *tag, const char *name, char *out,
                      size_t n)
{
    char pat[64];
    snprintf(pat, sizeof pat, " %s=\"", name);
    const char *end = strchr(tag, '>');
    const char *hit = strstr(tag, pat);
    if (hit == NULL || (end != NULL && hit > end)) {
        return NULL;
    }
    hit += strlen(pat);
    const char *q = strchr(hit, '"');
    size_t k = (q != NULL) ? (size_t)(q - hit) : 0;
    if (k >= n) {
        k = n - 1;
    }
    memcpy(out, hit, k);
    out[k] = '\0';
    return out;
}

static LevelMap *lm_new(int index)
{
    const LevelInfo *li = level_info(index);
    if (li == NULL) {
        pl_error("KeyError: %d", index);
    }
    LevelMap *lm = pl_alloc(sizeof *lm);
    lm->index = index;
    char stem[64], path[160];
    level_stem(index, stem, sizeof stem);
    snprintf(path, sizeof path, "assets/levels/%s.tmx", stem);
    char *text = pl_read_file(path, NULL);
    if (text == NULL) {
        pl_error("FileNotFoundError: %s", path);
    }
    /* root.iter("layer"): every <layer>, its name and its csv. */
    for (const char *p = strstr(text, "<layer"); p != NULL;
         p = strstr(p + 1, "<layer")) {
        if (p[6] != ' ' && p[6] != '>') {
            continue;
        }
        char name[32];
        if (tmx_attr(p, "name", name, sizeof name) == NULL) {
            name[0] = '\0';
        }
        const char *d = strstr(p, "<data");
        const char *close = strstr(p, "</layer>");
        if (d == NULL || (close != NULL && d > close)) {
            continue;
        }
        d = strchr(d, '>');
        if (d == NULL) {
            continue;
        }
        d++;
        const char *e = strstr(d, "</data>");
        if (e == NULL) {
            continue;
        }
        int cap = 1024, n = 0;
        int *v = pl_alloc((size_t)cap * sizeof *v);
        const char *c = d;
        while (c < e) {
            const char *comma = memchr(c, ',', (size_t)(e - c));
            const char *stop = (comma != NULL) ? comma : e;
            /* x for x in split(",") if x: after "\n" are taken out (and
             * "\r", which the XML parser reads as "\n") */
            bool any = false;
            for (const char *k = c; k < stop; k++) {
                if (*k != '\n' && *k != '\r') {
                    any = true;
                }
            }
            if (any) {
                if (n == cap) {
                    cap *= 2;
                    v = pl_grow(v, (size_t)cap * sizeof *v);
                }
                v[n++] = (int)strtol(c, NULL, 10) - 1;
            }
            c = stop + 1;
        }
        int slot = plane_of(lm, name);
        if (slot < 0) {
            if (lm->nplanes == LM_PLANES) {
                free(v);
                continue;
            }
            slot = lm->nplanes++;
            snprintf(lm->pname[slot], sizeof lm->pname[slot], "%s", name);
        } else {
            free(lm->plane[slot]);
        }
        lm->plane[slot] = v;
        lm->plane_len[slot] = n;
    }
    free(text);
    lm->cols = li->cols;
    lm->rows = li->rows;
    lm->type = li->tileset;
    int flags = li->collision_flags;
    lm->coll = plane_of(lm, (flags & 0x70) ? "planeB" : "planeA");
    if (lm->coll < 0) {
        lm->coll = plane_of(lm, "planeB");
    }
    snprintf(path, sizeof path, "assets/levels/%s.collision.txt", stem);
    text = pl_read_file(path, NULL);
    if (text == NULL) {
        pl_error("FileNotFoundError: %s", path);
    }
    for (char *line = text; line != NULL && *line; ) {
        char *nl = strchr(line, '\n');
        if (nl != NULL) {
            *nl = '\0';
        }
        if (line[0] != '#') {
            char b[16], solid[16], special[16];
            if (sscanf(line, "%15s %15s %15s", b, solid, special) == 3) {
                int v = 0;
                for (int q = 0; q < 4; q++) {
                    v |= (solid[q] == '#') ? (1 << q) : 0;
                    v |= (special[q] == '#') ? (0x10 << q) : 0;
                }
                lm->attr[strtol(b, NULL, 16) & 0xFF] = (u8)v;
            }
        }
        line = (nl != NULL) ? nl + 1 : NULL;
    }
    free(text);
    lm->w = lm->cols * 2;
    lm->h = lm->rows * 2;
    return lm;
}

/* The grids specials(), damage_floors(), town_edges() and pieces() keep,
 * worked out once the map's patches are in. */
static void lm_finish(LevelMap *lm)
{
    int cells = lm->w * lm->h;
    lm->solid = pl_alloc((size_t)cells);
    lm->special = pl_alloc((size_t)cells);
    lm->spec = pl_alloc((size_t)cells);
    lm->damage = pl_alloc((size_t)cells);
    lm->edge = pl_alloc((size_t)cells);
    lm->piece = pl_alloc((size_t)cells * sizeof *lm->piece);
    for (int cy = 0; cy < lm->h; cy++) {
        for (int cx = 0; cx < lm->w; cx++) {
            bool so, sp;
            lm_bits(lm, cx, cy, -1, &so, &sp);
            int i = cy * lm->w + cx;
            lm->solid[i] = so;
            lm->special[i] = sp;
            lm->spec[i] = !so && sp;
        }
    }
    /* damage_floors: type 3 block $63, type 5 $1A/$46/$77/$78 */
    for (int i = 0; i < cells; i++) {
        if (!lm->spec[i]) {
            continue;
        }
        int b = lm_block(lm, lm->coll, i % lm->w, i / lm->w);
        if ((lm->type == 3 && b == 0x63)
            || (lm->type == 5 && (b == 0x1A || b == 0x46 || b == 0x77
                                  || b == 0x78))) {
            lm->damage[i] = 1;
        }
    }
    int pb = plane_of(lm, "planeB");
    if (lm->type == 2 && pb >= 0) {
        for (int cy = 0; cy < lm->h; cy++) {
            for (int cx = 0; cx < lm->w; cx++) {
                int b = lm_block(lm, pb, cx, cy);
                bool so, sp;
                if (b != 0x53 && b != 0x72) {
                    continue;
                }
                lm_bits(lm, cx, cy, pb, &so, &sp);
                if (sp && !lm->solid[cy * lm->w + cx]) {
                    lm->edge[cy * lm->w + cx] = 1;
                    cset_add(&lm->edges, cx, cy, 0);
                }
            }
        }
    }
    /* pieces: open, not an edge, not special unless a damage floor */
    for (int i = 0; i < cells; i++) {
        lm->piece[i] = -1;
    }
    int *stack = pl_alloc((size_t)cells * sizeof *stack);
    int n = 0;
    for (int cy = 0; cy < lm->h; cy++) {
        for (int cx = 0; cx < lm->w; cx++) {
            int c = cy * lm->w + cx;
            if (lm->piece[c] >= 0 || lm_barrier(lm, c) || lm->solid[c]) {
                continue;
            }
            lm->piece[c] = n;
            int top = 0;
            stack[top++] = c;
            while (top > 0) {
                int k = stack[--top];
                int x = k % lm->w, y = k / lm->w;
                static const int D[4][2] = { {0, -1}, {0, 1}, {-1, 0},
                                             {1, 0} };
                for (int d = 0; d < 4; d++) {
                    int m = pymod(y + D[d][1], lm->h) * lm->w
                          + pymod(x + D[d][0], lm->w);
                    if (lm->piece[m] >= 0 || lm_barrier(lm, m)
                        || lm->solid[m]) {
                        continue;
                    }
                    lm->piece[m] = n;
                    stack[top++] = m;
                }
            }
            n++;
        }
    }
    free(stack);
    lm->cost = pl_alloc((size_t)cells * sizeof *lm->cost);
    lm->prev = pl_alloc((size_t)cells * sizeof *lm->prev);
    lm->stamp = pl_alloc((size_t)cells * sizeof *lm->stamp);
}

typedef struct {
    char      key[256];
    LevelMap *lm;
} MapEntry;

static MapEntry *g_maps;
static int       g_map_n, g_map_cap;

LevelMap *level_map(int level)
{
    char key[256];
    map_key(level, key, sizeof key);
    for (int i = 0; i < g_map_n; i++) {
        if (streq(g_maps[i].key, key)) {
            return g_maps[i].lm;
        }
    }
    LevelMap *lm = lm_new(level);
    Patch p[32];
    int n = item_patches(level, p, 32);
    for (int k = 0; k < n && k < 32; k++) {
        int plane = plane_of(lm, p[k].plane);
        if (plane >= 0) {
            for (int j = 0; j < p[k].at_n; j++) {
                int i = p[k].at[j];
                if (i >= 0 && i < lm->plane_len[plane]) {
                    lm->plane[plane][i] = p[k].block;
                }
            }
        }
        free(p[k].at);
    }
    lm_finish(lm);
    if (g_map_n == g_map_cap) {
        g_map_cap = g_map_cap ? g_map_cap * 2 : 32;
        g_maps = pl_grow(g_maps, (size_t)g_map_cap * sizeof *g_maps);
    }
    snprintf(g_maps[g_map_n].key, sizeof g_maps[g_map_n].key, "%s", key);
    g_maps[g_map_n].lm = lm;
    g_map_n++;
    return lm;
}

/* (town_edges | specials) - damage_floors: what path() only enters as a
 * goal. */
bool lm_barrier(const LevelMap *lm, int c)
{
    return (lm->edge[c] || lm->spec[c]) && !lm->damage[c];
}

static int lm_cell(const LevelMap *lm, int x, int y)
{
    if (x < 0 || y < 0 || x >= lm->w || y >= lm->h) {
        return -1;
    }
    return y * lm->w + x;
}

bool lm_special_open(const LevelMap *lm, int x, int y)
{
    int c = lm_cell(lm, x, y);
    return c >= 0 && lm->spec[c];
}

bool lm_town_edge(const LevelMap *lm, int x, int y)
{
    int c = lm_cell(lm, x, y);
    return c >= 0 && lm->edge[c];
}

bool lm_damage(const LevelMap *lm, int x, int y)
{
    int c = lm_cell(lm, x, y);
    return c >= 0 && lm->damage[c];
}

static int lm_piece_at(const LevelMap *lm, int x, int y)
{
    int c = lm_cell(lm, x, y);
    return (c >= 0) ? lm->piece[c] : -1;
}

static const int DIRS4[4][2] = { {0, -1}, {0, 1}, {-1, 0}, {1, 0} };

bool lm_reaches(const LevelMap *lm, int sx, int sy, const CSet *goals)
{
    if (cset_has(goals, sx, sy)) {
        return true;
    }
    int mine[4], nm = 0;
    int own = lm_piece_at(lm, sx, sy);
    if (own >= 0) {
        mine[nm++] = own;
    } else {
        for (int d = 0; d < 4; d++) {
            int p = lm_piece_at(lm, pymod(sx + DIRS4[d][0], lm->w),
                                pymod(sy + DIRS4[d][1], lm->h));
            if (p >= 0) {
                mine[nm++] = p;
            }
        }
    }
    for (int i = 0; i < goals->n; i++) {
        Cell g = cset_cell(goals, i);
        int p = lm_piece_at(lm, g.x, g.y);
        for (int k = 0; k < nm; k++) {
            if (p == mine[k]) {
                return true;
            }
        }
        for (int d = 0; d < 4; d++) {
            int mx = pymod(g.x + DIRS4[d][0], lm->w);
            int my = pymod(g.y + DIRS4[d][1], lm->h);
            if (mx == sx && my == sy) {
                return true;
            }
            int q = lm_piece_at(lm, mx, my);
            for (int k = 0; k < nm; k++) {
                if (q == mine[k]) {
                    return true;
                }
            }
        }
    }
    return false;
}

/* path()'s heap: (cost, (x, y)) as Python orders the tuples. */
typedef struct {
    int k, x, y;
} HeapItem;

static bool heap_less(const HeapItem *a, const HeapItem *b)
{
    if (a->k != b->k) {
        return a->k < b->k;
    }
    if (a->x != b->x) {
        return a->x < b->x;
    }
    return a->y < b->y;
}

typedef struct {
    HeapItem *v;
    int       n, cap;
} Heap;

static void heap_push(Heap *h, HeapItem it)
{
    if (h->n == h->cap) {
        h->cap = h->cap ? h->cap * 2 : 256;
        h->v = pl_grow(h->v, (size_t)h->cap * sizeof *h->v);
    }
    int i = h->n++;
    h->v[i] = it;
    while (i > 0) {
        int p = (i - 1) / 2;
        if (!heap_less(&h->v[i], &h->v[p])) {
            break;
        }
        HeapItem t = h->v[i];
        h->v[i] = h->v[p];
        h->v[p] = t;
        i = p;
    }
}

static HeapItem heap_pop(Heap *h)
{
    HeapItem top = h->v[0];
    h->v[0] = h->v[--h->n];
    int i = 0;
    for (;;) {
        int l = 2 * i + 1, r = l + 1, m = i;
        if (l < h->n && heap_less(&h->v[l], &h->v[m])) {
            m = l;
        }
        if (r < h->n && heap_less(&h->v[r], &h->v[m])) {
            m = r;
        }
        if (m == i) {
            break;
        }
        HeapItem t = h->v[i];
        h->v[i] = h->v[m];
        h->v[m] = t;
        i = m;
    }
    return top;
}

bool lm_path(LevelMap *lm, int sx, int sy, const CSet *goals,
             const CSet *avoid, const CSet *avoid2, Cells *out)
{
    out->n = 0;
    int start = lm_cell(lm, sx, sy);
    if (start < 0) {
        pl_error("KeyError: path from %d,%d off the map", sx, sy);
    }
    lm->gen++;
    Heap h = { 0 };
    lm->stamp[start] = lm->gen;
    lm->cost[start] = 0;
    lm->prev[start] = -1;
    HeapItem s0 = { 0, sx, sy };
    heap_push(&h, s0);
    while (h.n > 0) {
        HeapItem it = heap_pop(&h);
        int c = it.y * lm->w + it.x;
        if (it.k > lm->cost[c]) {
            continue;
        }
        if (cset_has(goals, it.x, it.y) && c != start) {
            int len = 0;
            for (int k = c; lm->prev[k] != -1; k = lm->prev[k]) {
                len++;
            }
            for (int i = 0; i < len; i++) {
                cells_add(out, 0, 0);
            }
            int i = len;
            for (int k = c; lm->prev[k] != -1; k = lm->prev[k]) {
                out->v[--i].x = k % lm->w;
                out->v[i].y = k / lm->w;
            }
            free(h.v);
            return true;
        }
        for (int d = 0; d < 4; d++) {
            int nx = pymod(it.x + DIRS4[d][0], lm->w);
            int ny = pymod(it.y + DIRS4[d][1], lm->h);
            int n = ny * lm->w + nx;
            if ((avoid != NULL && cset_has(avoid, nx, ny))
                || (avoid2 != NULL && cset_has(avoid2, nx, ny))) {
                continue;
            }
            bool goal = cset_has(goals, nx, ny);
            if (lm_barrier(lm, n) && !goal) {
                continue;
            }
            if (!(!lm->solid[n] || goal)) {
                continue;
            }
            int nk = it.k + (lm->damage[n] ? 5 : 1);
            int was = (lm->stamp[n] == lm->gen) ? lm->cost[n] : (1 << 30);
            if (nk < was) {
                lm->stamp[n] = lm->gen;
                lm->cost[n] = nk;
                lm->prev[n] = c;
                HeapItem ni = { nk, nx, ny };
                heap_push(&h, ni);
            }
        }
    }
    free(h.v);
    return false;
}

/* _runs: the cells as (direction, count) runs. */
int runs_of(int sx, int sy, const Cells *cells, int w, int h, Run *out)
{
    int n = 0, cx = sx, cy = sy;
    for (int i = 0; i < cells->n; i++) {
        Cell c = cells->v[i];
        const char *d = (c.x == pymod(cx + 1, w)) ? "right"
                      : (c.x == pymod(cx - 1, w)) ? "left"
                      : (c.y == pymod(cy + 1, h)) ? "down" : "up";
        if (n > 0 && streq(out[n - 1].d, d)) {
            out[n - 1].n++;
        } else {
            snprintf(out[n].d, sizeof out[n].d, "%s", d);
            out[n].n = 1;
            n++;
        }
        cx = c.x;
        cy = c.y;
    }
    return n;
}

static void step_of(const char *d, int *dx, int *dy)
{
    *dx = streq(d, "left") ? -1 : streq(d, "right") ? 1 : 0;
    *dy = streq(d, "up") ? -1 : streq(d, "down") ? 1 : 0;
}

GotoOpts goto_defaults(void)
{
    GotoOpts o;
    memset(&o, 0, sizeof o);
    o.tries = 30;
    return o;
}

bool goto_cells(Session *s, const CSet *goals)
{
    GotoOpts o = goto_defaults();
    return goto_opts(s, goals, &o);
}

bool goto_opts(Session *s, const CSet *goals, const GotoOpts *o)
{
    char nb[32], a[64], b[256], c[256];
    int start_level = s->state.level;
    if (start_level == PL_NONE) {
        snprintf(s->why, sizeof s->why, "not on a map (%s)",
                 s->state.valid ? s->state.screen : "None");
        s->why_set = true;
        return false;
    }
    LevelMap *lm = level_map(start_level);
    CSet avoid = { 0 };
    s->why_set = false;
    s->why[0] = '\0';
    s->hurt = false;
    Cells cells = { 0 };
    Run *runs = NULL;
    Lines walk_out = { 0 };
    bool result = false, decided = false;
    for (int t = 0; t < o->tries && !decided; t++) {
        const State *st = &s->state;
        if (st->level != start_level) {
            result = decided = true;
            break;
        }
        int curx = st->x, cury = st->y;
        if (cset_has(goals, curx, cury)) {
            result = decided = true;
            break;
        }
        bool keep = o->keep_off != NULL && o->keep_off->n > 0;
        if (!lm_path(lm, curx, cury, goals, &avoid, keep ? o->keep_off : NULL,
                     &cells)) {
            cset_repr(goals, 4, b, sizeof b);
            c[0] = '\0';
            if (avoid.n > 0) {
                char r[200];
                cset_repr(&avoid, avoid.n, r, sizeof r);
                snprintf(c, sizeof c, " avoiding %s", r);
            }
            snprintf(s->why, sizeof s->why, "no path on %s from %s to %s%s",
                     level_name(start_level, nb, sizeof nb),
                     xy_repr(curx, cury, a, sizeof a), b, c);
            s->why_set = true;
            result = false;
            decided = true;
            break;
        }
        runs = pl_grow(runs, (size_t)(cells.n + 1) * sizeof *runs);
        int nr = runs_of(curx, cury, &cells, lm->w, lm->h, runs);
        int px = curx, py = cury;
        for (int r = 0; r < nr; r++) {
            if (!ready(s)) {
                result = false;
                decided = true;
                break;
            }
            int dx, dy;
            step_of(runs[r].d, &dx, &dy);
            px = pymod(px + dx * runs[r].n, lm->w);
            py = pymod(py + dy * runs[r].n, lm->h);
            lines_clear(&walk_out);
            lines_extend(&walk_out, sendf(s, "walk %s %d", runs[r].d,
                                          runs[r].n));
            if (!st_waiting(&s->state) && st_screen(&s->state, "level")) {
                send1(s, "wait idle");
            }
            st = &s->state;
            if (st_screen(st, "battle")) {
                FightOpts fo = s->fight_opts_set ? s->fight_opts
                                                 : fight_defaults();
                Ints shown = { 0 };
                fight_smart(s, &fo, &shown);
                if (o->fights != NULL) {
                    intslist_add(o->fights, &shown);
                }
                ints_free(&shown);
                if (o->after_fight != NULL && st_screen(&s->state, "level")) {
                    o->after_fight(s, o->after_ctx);
                }
                if (s->walk_mend > 0 && st_screen(&s->state, "level")) {
                    revive(s);
                    cure_poison(s);
                    mend(s, s->walk_mend);
                }
                if (needs_healing(s, 0.5) && st_screen(&s->state, "level")) {
                    cure_poison(s);
                    mend(s, 0.6);
                    if (needs_healing(s, 0.5) && s->careful) {
                        s->hurt = true;
                        snprintf(s->why, sizeof s->why,
                                 "hurt with nothing left to mend with");
                        s->why_set = true;
                        result = false;
                        decided = true;
                    }
                }
                break;
            }
            if (st->level != start_level) {
                result = decided = true;
                break;
            }
            if (lines_any2(&walk_out, "FAIL", "blocked")) {
                cset_add(&avoid, pymod(st->x + dx, lm->w),
                         pymod(st->y + dy, lm->h), 0);
                break;
            }
            if (!st_waiting(st) || !st_wait(st, "control")) {
                press_through(s, "level", NULL);
                break;
            }
            if (st->x != px || st->y != py) {
                break;
            }
        }
    }
    if (!decided) {
        const State *st = &s->state;
        result = st->level != start_level || cset_has(goals, st->x, st->y);
        if (!result && !s->why_set) {
            c[0] = '\0';
            if (avoid.n > 0) {
                char r[200];
                cset_repr(&avoid, avoid.n, r, sizeof r);
                snprintf(c, sizeof c, " avoiding %s", r);
            }
            snprintf(s->why, sizeof s->why, "gave up after %d tries at %s%s",
                     o->tries, xy_repr(st->x, st->y, a, sizeof a), c);
            s->why_set = true;
        }
    }
    cset_free(&avoid);
    cells_free(&cells);
    free(runs);
    lines_free(&walk_out);
    return result;
}

/* ---- doors ---- */

typedef struct {
    int   level;
    Door *v;
    int   n;
} DoorList;

static DoorList *g_doors;
static int       g_door_n;

/* level_doors(level).values(): {(y, x): record} -- a later record with a
 * cell already seen takes its place, keeping the first's position. */
const Door *level_doors(int level, int *n)
{
    for (int i = 0; i < g_door_n; i++) {
        if (g_doors[i].level == level) {
            *n = g_doors[i].n;
            return g_doors[i].v;
        }
    }
    char stem[64], path[160];
    level_stem(level, stem, sizeof stem);
    snprintf(path, sizeof path, "assets/levels/%s.doors.json", stem);
    char *text = pl_read_file(path, NULL);
    JsonValue *root = (text != NULL) ? json_parse(text, NULL) : NULL;
    free(text);
    const JsonValue *arr = json_get(root, "entries");
    if (arr == NULL) {
        pl_error("cannot read %s", path);
    }
    int count = json_length(arr), k = 0;
    Door *v = pl_alloc((size_t)(count + 1) * sizeof *v);
    for (int i = 0; i < count; i++) {
        const JsonValue *e = json_at(arr, i);
        Door d;
        memset(&d, 0, sizeof d);
        d.y = (int)json_int(e, "y", 0);
        d.x = (int)json_int(e, "x", 0);
        d.has_to_level = json_get(e, "to_level") != NULL;
        d.to_level = (int)json_int(e, "to_level", PL_NONE);
        d.to_x = (int)json_int(e, "to_x", PL_NONE);
        d.to_y = (int)json_int(e, "to_y", PL_NONE);
        d.building = (int)json_int(e, "building", PL_NONE);
        d.f764 = (int)json_int(e, "f764", 0);
        d.f766 = (int)json_int(e, "f766", 0);
        int at = -1;
        for (int j = 0; j < k; j++) {
            if (v[j].y == d.y && v[j].x == d.x) {
                at = j;
            }
        }
        if (at >= 0) {
            v[at] = d;
        } else {
            v[k++] = d;
        }
    }
    json_free(root);
    g_doors = pl_grow(g_doors, (size_t)(g_door_n + 1) * sizeof *g_doors);
    g_doors[g_door_n].level = level;
    g_doors[g_door_n].v = v;
    g_doors[g_door_n].n = k;
    g_door_n++;
    *n = k;
    return v;
}

/* door_cells: {cell: side} for a record at (ry, rx); side 0 is None. */
void door_cells(int level, int ry, int rx, CSet *out)
{
    LevelMap *lm = level_map(level);
    cset_clear(out);
    for (int cy = ry - 2; cy < ry + 1; cy++) {
        for (int cx = rx - 2; cx < rx + 1; cx++) {
            bool solid, special;
            lm_bits(lm, cx, cy, -1, &solid, &special);
            if (special && !solid) {
                cset_put(out, cx, cy, SIDE_NONE);
            } else if (special && solid) {
                static const struct { int d, dx, dy; } SIDES[4] = {
                    { SIDE_UP, 0, 1 }, { SIDE_DOWN, 0, -1 },
                    { SIDE_RIGHT, -1, 0 }, { SIDE_LEFT, 1, 0 }
                };
                int ns = (lm->type != 2) ? 4 : 1;
                for (int i = 0; i < ns; i++) {
                    int nx = pymod(cx + SIDES[i].dx, lm->w);
                    int ny = pymod(cy + SIDES[i].dy, lm->h);
                    if (lm_open(lm, nx, ny) && !cset_has(out, nx, ny)) {
                        cset_add(out, nx, ny, SIDES[i].d);
                    }
                }
            }
        }
    }
}

const char *side_name(int side)
{
    switch (side) {
    case SIDE_UP:
        return "up";
    case SIDE_DOWN:
        return "down";
    case SIDE_LEFT:
        return "left";
    case SIDE_RIGHT:
        return "right";
    default:
        return NULL;
    }
}

/* ---- the ways off a level ---- */

typedef struct {
    int from_level, to_level, to_x, to_y;
} Exit;

static Exit *g_exits;
static int   g_exit_n = -1;

/* town_exits().get(level): the last entry from the level. */
static const Exit *town_exit_of(int level)
{
    if (g_exit_n < 0) {
        char *text = pl_read_file("assets/levels/exits.json", NULL);
        JsonValue *root = (text != NULL) ? json_parse(text, NULL) : NULL;
        free(text);
        const JsonValue *arr = json_get(root, "entries");
        if (arr == NULL) {
            pl_error("cannot read assets/levels/exits.json");
        }
        g_exit_n = json_length(arr);
        g_exits = pl_alloc((size_t)(g_exit_n + 1) * sizeof *g_exits);
        for (int i = 0; i < g_exit_n; i++) {
            const JsonValue *e = json_at(arr, i);
            g_exits[i].from_level = (int)json_int(e, "from_level", -1);
            g_exits[i].to_level = (int)json_int(e, "to_level", -1);
            g_exits[i].to_x = (int)json_int(e, "to_x", -1);
            g_exits[i].to_y = (int)json_int(e, "to_y", -1);
        }
        json_free(root);
    }
    const Exit *hit = NULL;
    for (int i = 0; i < g_exit_n; i++) {
        if (g_exits[i].from_level == level) {
            hit = &g_exits[i];
        }
    }
    return hit;
}

bool town_exit_to(int level, int *to_x, int *to_y)
{
    const Exit *e = town_exit_of(level);
    if (e == NULL) {
        return false;
    }
    *to_x = e->to_x;
    *to_y = e->to_y;
    return true;
}

/* HOLE_TABLE: loc_718A's level delta, y and x deltas in 32px blocks. */
static const int HOLE_TABLE[23][3] = {
    {-1, 0, 0}, {-1, 12, 0}, {-1, 0, 16}, {-1, 0, 11}, {-1, 2, 0},
    {-1, 0, 0}, {-1, 1, -2}, {-1, 3, 13}, {2, 0, 0}, {2, 0, 0},
    {-3, 0, 0}, {-3, 0, 0}, {-3, 0, -13}, {-1, -2, 16},
    {-1, 3, -3}, {2, 0, 0}, {2, 0, 0}, {-3, 0, 0}, {-3, 0, 0},
    {-3, 0, 0}, {-1, 0, 0}, {-1, 0, 0}, {-1, -16, 0}
};

typedef struct {
    char key[256];
    Hole *v;
    int   n;
} HoleEntry;

static HoleEntry *g_holes;
static int        g_hole_n;

/* _holes(level): one level's holes, each to its first landing, row by
 * row. */
static const Hole *holes_one(int level, int *n)
{
    char key[256];
    map_key(level, key, sizeof key);
    for (int i = 0; i < g_hole_n; i++) {
        if (streq(g_holes[i].key, key)) {
            *n = g_holes[i].n;
            return g_holes[i].v;
        }
    }
    LevelMap *lm = level_map(level);
    Hole *v = NULL;
    int k = 0, cap = 0;
    for (int cy = 0; cy < lm->h; cy++) {
        for (int cx = 0; cx < lm->w; cx++) {
            if (!lm->spec[cy * lm->w + cx]) {
                continue;
            }
            int b = lm_block(lm, lm->coll, cx, cy);
            Hole h;
            bool any = false;
            h.x = cx;
            h.y = cy;
            if ((lm->type == 5 && b == 0x3A) || (lm->type == 6 && b == 0x04)) {
                int i = level - 0x4E;
                i = (i < 0) ? 0 : (i > 22) ? 22 : i;
                h.to_level = level + HOLE_TABLE[i][0];
                h.to_x = cx + HOLE_TABLE[i][2] * 2;
                h.to_y = cy + HOLE_TABLE[i][1] * 2;
                any = true;
            } else if (lm->type == 3 && b >= 0x50 && b <= 0x62) {
                if (level == 0x15) {
                    h.to_level = 0x14;
                    h.to_x = 0x60 / 16;
                    h.to_y = 0xC0 / 16;
                    any = true;
                } else if (level == 0x16) {
                    h.to_level = 0x15;
                    if (cy * 16 >= 0x100) {
                        h.to_x = 0x2C0 / 16;
                        h.to_y = 0x220 / 16;
                    } else {
                        h.to_x = 0x340 / 16;
                        h.to_y = 0xA0 / 16;
                    }
                    any = true;
                }
            }
            if (any) {
                if (k == cap) {
                    cap = cap ? cap * 2 : 16;
                    v = pl_grow(v, (size_t)cap * sizeof *v);
                }
                v[k++] = h;
            }
        }
    }
    g_holes = pl_grow(g_holes, (size_t)(g_hole_n + 1) * sizeof *g_holes);
    snprintf(g_holes[g_hole_n].key, sizeof g_holes[g_hole_n].key, "%s", key);
    g_holes[g_hole_n].v = v;
    g_holes[g_hole_n].n = k;
    g_hole_n++;
    *n = k;
    return v;
}

static const Hole *hole_at(const Hole *v, int n, int x, int y)
{
    for (int i = 0; i < n; i++) {
        if (v[i].x == x && v[i].y == y) {
            return &v[i];
        }
    }
    return NULL;
}

/* holes(level): each hole's chain followed to its last landing. The
 * caller frees *out. */
int holes(int level, Hole **out)
{
    int n;
    const Hole *one = holes_one(level, &n);
    Hole *v = pl_alloc((size_t)(n + 1) * sizeof *v);
    for (int i = 0; i < n; i++) {
        Hole h = one[i];
        for (int k = 0; k < 8; k++) {
            if (h.to_level < 0 || h.to_level >= levels_count()) {
                break;
            }
            int m;
            const Hole *there = holes_one(h.to_level, &m);
            const Hole *nxt = hole_at(there, m, h.to_x, h.to_y);
            if (nxt == NULL) {
                break;
            }
            h.to_level = nxt->to_level;
            h.to_x = nxt->to_x;
            h.to_y = nxt->to_y;
        }
        v[i] = h;
    }
    *out = v;
    return n;
}

bool on_hole(int level, int x, int y)
{
    Hole *v;
    int n = holes(level, &v);
    bool hit = hole_at(v, n, x, y) != NULL;
    free(v);
    return hit;
}

/* FLIGHTS: doors whose record names a building that is a trip. */
static bool flight_of(int level, int y, int x, int *to_level, int *tx,
                      int *ty)
{
    if (level == 0x03 && y == 79 && x == 59) {
        *to_level = 0x04;
        *tx = 31;
        *ty = 16;
        return true;
    }
    return false;
}

typedef struct {
    char key[256];
    Way *v;
    int  n;
} WayEntry;

static WayEntry *g_ways;
static int       g_way_n;

static void way_add(Way **v, int *n, const CSet *cells, int to_level,
                    int ax, int ay)
{
    *v = pl_grow(*v, (size_t)(*n + 1) * sizeof **v);
    Way *w = &(*v)[(*n)++];
    memset(w, 0, sizeof *w);
    cset_copy(&w->cells, cells);
    w->to_level = to_level;
    w->ax = ax;
    w->ay = ay;
}

static void ways_of(int level, Way **out, int *count)
{
    LevelMap *lm = level_map(level);
    Way *v = NULL;
    int n = 0, nd;
    const Door *doors = level_doors(level, &nd);
    CSet cells = { 0 };
    for (int i = 0; i < nd; i++) {
        int tl, tx, ty;
        if (!doors[i].has_to_level
            && flight_of(level, doors[i].y, doors[i].x, &tl, &tx, &ty)) {
            door_cells(level, doors[i].y, doors[i].x, &cells);
            if (cells.n > 0) {
                way_add(&v, &n, &cells, tl, tx, ty);
            }
        }
    }
    Hole *fall;
    int nf = holes(level, &fall);
    CSet kept = { 0 };
    for (int i = 0; i < nd; i++) {
        if (!doors[i].has_to_level) {
            continue;
        }
        door_cells(level, doors[i].y, doors[i].x, &cells);
        cset_clear(&kept);
        for (int k = 0; k < cells.n; k++) {
            Cell c = cset_cell(&cells, k);
            if (hole_at(fall, nf, c.x, c.y) == NULL) {
                cset_add(&kept, c.x, c.y, cells.val[k]);
            }
        }
        if (kept.n > 0) {
            way_add(&v, &n, &kept, doors[i].to_level, doors[i].to_x,
                    doors[i].to_y);
        }
    }
    const Exit *e = town_exit_of(level);
    if (e != NULL && lm->type == 2) {
        way_add(&v, &n, &lm->edges, e->to_level, e->to_x, e->to_y);
    } else if (e != NULL && level >= 0x10) {
        cset_clear(&kept);
        for (int cy = 0; cy < lm->h; cy++) {
            for (int cx = 0; cx < lm->w; cx++) {
                int c = cy * lm->w + cx;
                if (!(lm->solid[c] && lm->special[c])) {
                    continue;
                }
                bool covered = false;
                for (int i = 0; i < nd; i++) {
                    if (doors[i].y - 2 <= cy && cy <= doors[i].y
                        && doors[i].x - 2 <= cx && cx <= doors[i].x) {
                        covered = true;
                        break;
                    }
                }
                if (covered) {
                    continue;
                }
                static const struct { int d, dx, dy; } SIDES[4] = {
                    { SIDE_UP, 0, 1 }, { SIDE_DOWN, 0, -1 },
                    { SIDE_RIGHT, -1, 0 }, { SIDE_LEFT, 1, 0 }
                };
                for (int i = 0; i < 4; i++) {
                    int nx = pymod(cx + SIDES[i].dx, lm->w);
                    int ny = pymod(cy + SIDES[i].dy, lm->h);
                    if (lm_open(lm, nx, ny) && !cset_has(&kept, nx, ny)) {
                        cset_add(&kept, nx, ny, SIDES[i].d);
                    }
                }
            }
        }
        if (kept.n > 0) {
            way_add(&v, &n, &kept, e->to_level, e->to_x, e->to_y);
        }
    }
    /* holes, by where they land, in the order first seen */
    bool *done = pl_alloc((size_t)(nf + 1) * sizeof *done);
    for (int i = 0; i < nf; i++) {
        if (done[i]) {
            continue;
        }
        cset_clear(&kept);
        for (int k = i; k < nf; k++) {
            if (!done[k] && fall[k].to_level == fall[i].to_level
                && fall[k].to_x == fall[i].to_x
                && fall[k].to_y == fall[i].to_y) {
                cset_add(&kept, fall[k].x, fall[k].y, SIDE_NONE);
                done[k] = true;
            }
        }
        way_add(&v, &n, &kept, fall[i].to_level, fall[i].to_x, fall[i].to_y);
    }
    free(done);
    free(fall);
    cset_free(&cells);
    cset_free(&kept);
    *out = v;
    *count = n;
}

const Way *ways_out(int level, int *n)
{
    char key[256];
    map_key(level, key, sizeof key);
    for (int i = 0; i < g_way_n; i++) {
        if (streq(g_ways[i].key, key)) {
            *n = g_ways[i].n;
            return g_ways[i].v;
        }
    }
    g_ways = pl_grow(g_ways, (size_t)(g_way_n + 1) * sizeof *g_ways);
    WayEntry *w = &g_ways[g_way_n++];
    snprintf(w->key, sizeof w->key, "%s", key);
    ways_of(level, &w->v, &w->n);
    *n = w->n;
    return w->v;
}

/* ---- the plan across levels ---- */

static u64 node_key(int level, int x, int y)
{
    return ((u64)(u32)(level + 1) << 32) | cell_key(x, y);
}

void legs_free(Legs *l)
{
    for (int i = 0; i < l->n; i++) {
        if (!l->v[i].is_goals) {
            cset_free(&l->v[i].cells);
        }
    }
    free(l->v);
    memset(l, 0, sizeof *l);
}

static void leg_add(Legs *l, int level, const CSet *cells, bool is_goals)
{
    l->v = pl_grow(l->v, (size_t)(l->n + 1) * sizeof *l->v);
    Leg *g = &l->v[l->n++];
    memset(g, 0, sizeof *g);
    g->level = level;
    g->is_goals = is_goals;
    if (!is_goals) {
        cset_copy(&g->cells, cells);
    }
}

bool plan(int level, int cx, int cy, int target_level, const CSet *goals,
          Legs *out)
{
    typedef struct {
        int level, x, y;
        int prev;           /* node index, -1 for the start */
        int way_level;      /* the leg's level and way, from prev */
        int way_index;
    } Node;
    Node *nodes = NULL;
    int nn = 0;
    CSet seen = { 0 };
    nodes = pl_grow(nodes, sizeof *nodes);
    nodes[0].level = level;
    nodes[0].x = cx;
    nodes[0].y = cy;
    nodes[0].prev = -1;
    nn = 1;
    kset_add(&seen, node_key(level, cx, cy), 0);
    int head = 0;
    memset(out, 0, sizeof *out);
    while (head < nn) {
        Node cur = nodes[head++];
        LevelMap *lm = level_map(cur.level);
        if (cur.level == target_level && lm_reaches(lm, cur.x, cur.y, goals)) {
            /* legs, last first, then turned round */
            Legs back = { 0 };
            leg_add(&back, cur.level, goals, true);
            int k = head - 1;
            while (nodes[k].prev >= 0) {
                int wn;
                const Way *ways = ways_out(nodes[k].way_level, &wn);
                leg_add(&back, nodes[k].way_level,
                        &ways[nodes[k].way_index].cells, false);
                k = nodes[k].prev;
            }
            for (int i = back.n - 1; i >= 0; i--) {
                out->v = pl_grow(out->v, (size_t)(out->n + 1)
                                 * sizeof *out->v);
                out->v[out->n++] = back.v[i];
            }
            free(back.v);
            free(nodes);
            cset_free(&seen);
            return true;
        }
        int wn;
        const Way *ways = ways_out(cur.level, &wn);
        for (int i = 0; i < wn; i++) {
            u64 key = node_key(ways[i].to_level, ways[i].ax, ways[i].ay);
            if (kset_find(&seen, key) >= 0 || ways[i].cells.n == 0) {
                continue;
            }
            if (lm_reaches(lm, cur.x, cur.y, &ways[i].cells)) {
                kset_add(&seen, key, 0);
                nodes = pl_grow(nodes, (size_t)(nn + 1) * sizeof *nodes);
                nodes[nn].level = ways[i].to_level;
                nodes[nn].x = ways[i].ax;
                nodes[nn].y = ways[i].ay;
                nodes[nn].prev = head - 1;
                nodes[nn].way_level = cur.level;
                nodes[nn].way_index = i;
                nn++;
            }
        }
    }
    free(nodes);
    cset_free(&seen);
    return false;
}

void ride_holes(Session *s, Report *log)
{
    char a[64], nb[32];
    for (int i = 0; i < 12; i++) {
        const State *st = &s->state;
        if (!st_screen(st, "level") || st->level == PL_NONE) {
            return;
        }
        if (!on_hole(st->level, st->x, st->y)) {
            return;
        }
        int bl = st->level, bx = st->x, by = st->y;
        send1(s, "walk up 1");
        ready(s);
        st = &s->state;
        if (st->level == bl && st->x == bx && st->y == by) {
            (void)nb;
            say(log, "ride_holes: on a hole at (%d, %s) and nothing fell",
                bl, xy_repr(bx, by, a, sizeof a));
            return;
        }
    }
}

static bool walk_legs(Session *s, const Legs *legs, int target_level,
                      const CSet *goals, Report *log)
{
    char nb[32], a[64], b[256], r[4096];
    for (int i = 0; i < legs->n; i++) {
        const Leg *leg = &legs->v[i];
        const CSet *cells = leg->is_goals ? goals : &leg->cells;
        ride_holes(s, log);
        for (int k = 0; k < 3; k++) {
            if (!ready(s)) {
                state_repr(&s->state, r, sizeof r);
                say(log, "travel: never got control on %s: %s",
                    level_name(leg->level, nb, sizeof nb), r);
                return false;
            }
            if (s->state.level != leg->level) {
                break;
            }
            if (!goto_cells(s, cells)) {
                say(log, "travel: leg on %s failed: %s",
                    level_name(leg->level, nb, sizeof nb),
                    s->why_set ? s->why : "None");
            }
            if (s->hurt) {
                say(log, "travel: stopping, hurt with nothing left");
                return false;
            }
            if (s->state.level != leg->level) {
                break;
            }
            int hx = s->state.x, hy = s->state.y;
            if (cset_has(cells, hx, hy)) {
                if (leg->level == target_level && leg->is_goals) {
                    return true;
                }
                const char *d = side_name(cset_get(cells, hx, hy, SIDE_NONE));
                sendf(s, "walk %s 1", d ? d : "up");
                ready(s);
                if (!cset_has(cells, s->state.x, s->state.y)) {
                    break;
                }
            }
        }
        if (!st_screen(&s->state, "level") && !st_screen(&s->state, "battle")) {
            state_repr(&s->state, r, sizeof r);
            say(log, "travel: off the map: %s", r);
            return false;
        }
    }
    const State *st = &s->state;
    bool ok = st->level == target_level && cset_has(goals, st->x, st->y);
    if (!ok) {
        char menu[16];
        snprintf(menu, sizeof menu, "%d", st->menu);
        cset_repr(goals, 4, b, sizeof b);
        char nb2[32];
        say(log, "travel: ended on %s at %s, wanted %s %s -- screen %s, %s %s, "
            "menu %s, tick %u", level_name(st->level, nb, sizeof nb),
            xy_repr(st->x, st->y, a, sizeof a),
            level_name(target_level, nb2, sizeof nb2), b, st->screen,
            st->kind, st->wait, (st->menu == PL_NONE) ? "None" : menu,
            st->tick);
    }
    return ok;
}

bool travel(Session *s, int target_level, const CSet *goals, Report *log)
{
    char nb[32], nb2[32], a[64];
    if (!ready(s)) {
        return false;
    }
    const State *st = &s->state;
    if (st->riding && st->level == MOTAVIA) {
        /* (build 173) Arrived on the water: plan() walks, so ashore
         * first, somewhere it can walk from. */
        CSet region = { 0 };
        landing_region(target_level, goals, &region);
        bool ok = region.n > 0 && ride_ashore(s, NULL, &region, log);
        cset_free(&region);
        if (!ok) {
            say(log, "travel: riding, and no landing leads to %s",
                level_name(target_level, nb, sizeof nb));
            return false;
        }
        st = &s->state;
    }
    if (cset_has(goals, st->x, st->y) && st->level == target_level) {
        return true;
    }
    for (int attempt = 0; attempt < 4; attempt++) {
        ride_holes(s, log);
        st = &s->state;
        Legs legs;
        if (!plan(st->level, st->x, st->y, target_level, goals, &legs)) {
            say(log, "no way from %s to %s", level_name(st->level, nb,
                                                        sizeof nb),
                level_name(target_level, nb2, sizeof nb2));
            return false;
        }
        bool ok = walk_legs(s, &legs, target_level, goals, log);
        legs_free(&legs);
        if (ok || s->hurt
            || (!st_screen(&s->state, "level")
                && !st_screen(&s->state, "battle"))) {
            return ok;
        }
        if (!ready(s) || !st_screen(&s->state, "level")) {
            return false;
        }
        say(log, "travel: re-planning from %s %s",
            level_name(s->state.level, nb, sizeof nb),
            xy_repr(s->state.x, s->state.y, a, sizeof a));
    }
    return false;
}

/* ---- towns ---- */

typedef struct {
    const char *d;
    int dx, dy;
    double key;
} TownDir;

bool town_exit(Session *s)
{
    int level = s->state.level;
    LevelMap *lm = level_map(level);
    if (!goto_cells(s, &lm->edges)) {
        return false;
    }
    for (int i = 0; i < 4; i++) {
        if (s->state.level != level) {
            return true;
        }
        if (!ready(s)) {
            return false;
        }
        int x = s->state.x, y = s->state.y;
        TownDir dirs[4] = {
            { "down", 0, 1, 0 }, { "up", 0, -1, 0 }, { "right", 1, 0, 0 },
            { "left", -1, 0, 0 }
        };
        for (int k = 0; k < 4; k++) {
            dirs[k].key = -(dirs[k].dx * (x - lm->w / 2.0)
                            + dirs[k].dy * (y - lm->h / 2.0));
        }
        /* list.sort: stable */
        for (int k = 1; k < 4; k++) {
            TownDir t = dirs[k];
            int j = k - 1;
            while (j >= 0 && dirs[j].key > t.key) {
                dirs[j + 1] = dirs[j];
                j--;
            }
            dirs[j + 1] = t;
        }
        for (int k = 0; k < 4; k++) {
            int nx = pymod(x + dirs[k].dx, lm->w);
            int ny = pymod(y + dirs[k].dy, lm->h);
            if (cset_has(&lm->edges, nx, ny) || !lm_open(lm, nx, ny)) {
                sendf(s, "walk %s 1", dirs[k].d);
                if (!st_waiting(&s->state)) {
                    send1(s, "wait idle");
                }
                if (s->state.level != level) {
                    return true;
                }
                break;
            }
        }
    }
    return s->state.level != level;
}

/* town_arrivals: where a town's entrances put the party, from Motavia's
 * and DezolisSkure's records; the town's edges when none. */
void town_arrivals(int level, CSet *out)
{
    cset_clear(out);
    static const int FROM[2] = { MOTAVIA, 0x03 };
    for (int w = 0; w < 2; w++) {
        int n;
        const Door *d = level_doors(FROM[w], &n);
        for (int i = 0; i < n; i++) {
            if (d[i].has_to_level && d[i].to_level == level) {
                cset_add(out, d[i].to_x, d[i].to_y, SIDE_NONE);
            }
        }
    }
    if (out->n == 0) {
        cset_copy(out, &level_map(level)->edges);
    }
}

bool enter_town(Session *s, int level)
{
    if (s->state.level == level) {
        return true;
    }
    CSet arrive = { 0 };
    bool ok;
    if (s->state.level != MOTAVIA) {
        town_arrivals(level, &arrive);
        ok = travel(s, level, &arrive, NULL) || s->state.level == level;
        cset_free(&arrive);
        return ok;
    }
    int n;
    const Door *d = level_doors(MOTAVIA, &n);
    for (int i = 0; i < n; i++) {
        if (!(d[i].has_to_level && d[i].to_level == level)) {
            continue;
        }
        CSet cells = { 0 };
        door_cells(MOTAVIA, d[i].y, d[i].x, &cells);
        if (!goto_cells(s, &cells)) {
            cset_free(&cells);
            town_arrivals(level, &arrive);
            ok = travel(s, level, &arrive, NULL) || s->state.level == level;
            cset_free(&arrive);
            return ok;
        }
        if (s->state.level == MOTAVIA && ready(s)) {
            const char *side = side_name(cset_get(&cells, s->state.x,
                                                  s->state.y, SIDE_NONE));
            sendf(s, "walk %s 1", side ? side : "up");
            ready(s);
        }
        cset_free(&cells);
        return s->state.level == level;
    }
    return false;
}
