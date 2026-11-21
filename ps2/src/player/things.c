/* (build 172) play.py's things on the map: talk, search (walk next to a
 * cell, face it, A and C through what follows), the objects
 * LoadSpritesInLevel places (level_sprites.json), the chests and what
 * they hold (chests.json), a chest's flag byte, and make_room's toss
 * when a find has no room. */

#include "pl.h"

#include "../core/json.h"

#include <stdlib.h>

/* talk: A, then C through whatever answers, until the pad is back. The
 * scripts shown go onto `shown`. */
void talk(Session *s, int limit, Ints *shown)
{
    int before = s->shown.n;
    send2(s, "press a", "wait idle");
    for (int i = 0; i < limit; i++) {
        if (st_control(&s->state, "level")) {
            break;
        }
        if (st_waiting(&s->state)) {
            send2(s, "press c", "wait idle");
        } else {
            send1(s, "wait idle");
        }
    }
    if (shown != NULL) {
        for (int i = before; i < s->shown.n; i++) {
            ints_add(shown, s->shown.v[i]);
        }
    }
}

/* neighbours: the open cells next to (cx, cy), each with the side that
 * faces back to it, in the Python's order. */
void neighbours(int level, int cx, int cy, CSet *out)
{
    static const struct {
        int side, dx, dy;
    } D[4] = {
        { SIDE_UP, 0, 1 }, { SIDE_DOWN, 0, -1 }, { SIDE_LEFT, 1, 0 },
        { SIDE_RIGHT, -1, 0 }
    };
    const LevelMap *lm = level_map(level);
    cset_clear(out);
    for (int k = 0; k < 4; k++) {
        int nx = pymod(cx + D[k].dx, lm->w), ny = pymod(cy + D[k].dy, lm->h);
        if (lm_open(lm, nx, ny)) {
            cset_put(out, nx, ny, D[k].side);
        }
    }
}

bool search(Session *s, int level, int cx, int cy, Report *log, bool key,
            const CSet *cells, Ints *shown)
{
    CSet own = { 0 };
    if (cells == NULL) {
        neighbours(level, cx, cy, &own);
        cells = &own;
    }
    bool ok = false;
    if (cells->n == 0 || !travel(s, level, cells, log) || !ready(s)) {
        cset_free(&own);
        return false;
    }
    int d = cset_get(cells, s->state.x, s->state.y, SIDE_NONE);
    if (d != SIDE_NONE) {
        char cmd[16];
        snprintf(cmd, sizeof cmd, "press %s", side_name(d));
        send2(s, cmd, "wait idle");             /* turn to face it */
        if (!st_waiting(&s->state) || !st_wait(&s->state, "control")) {
            ready(s);
        }
        Ints got = { 0 };
        talk(s, 6, &got);
        if (ints_has(&got, 0x1707) && key) {
            /* "It's locked": the Small Key from the menu, facing it, and
             * A again. */
            int h, sl;
            if (find_item(s, "SmallKey", &h, &sl)) {
                use_item(s, h, sl, PL_NONE, &got);
                ready(s);
                talk(s, 6, &got);
            }
        }
        if (shown != NULL) {
            for (int i = 0; i < got.n; i++) {
                ints_add(shown, got.v[i]);
            }
        }
        ints_free(&got);
        ok = true;
    }
    cset_free(&own);
    return ok;
}

/* ---- level_sprites.json and chests.json ---- */

typedef struct {
    int           level;
    LevelObject  *v;
    int           n;
    LevelObject  *raw;          /* every object, in the file's order */
    int           raw_n;
} LevelObjects;

static LevelObjects *g_objs;
static int           g_objs_n;
static char        (*g_chest_item)[24];  /* by index; "" for None */
static int           g_chest_n;

static JsonValue *load_json(const char *path)
{
    char *text = pl_read_file(path, NULL);
    JsonValue *root = (text != NULL) ? json_parse(text, NULL) : NULL;
    free(text);
    if (root == NULL) {
        pl_error("cannot read %s", path);
    }
    return root;
}

static void objects_load(void)
{
    if (g_objs != NULL) {
        return;
    }
    JsonValue *root = load_json("assets/data/level_sprites.json");
    const JsonValue *levels = json_get(root, "levels");
    int n = json_length(levels);
    g_objs = pl_alloc((size_t)(n + 1) * sizeof *g_objs);
    for (int i = 0; i < n; i++) {
        const JsonValue *L = json_at(levels, i);
        int level = (int)json_int(L, "level", -1);
        bool seen = false;
        for (int k = 0; k < g_objs_n; k++) {
            if (g_objs[k].level == level) {
                seen = true;            /* the first of a level answers */
            }
        }
        if (seen) {
            continue;
        }
        LevelObjects *lo = &g_objs[g_objs_n++];
        lo->level = level;
        const JsonValue *objs = json_get(L, "objects");
        int m = json_length(objs);
        lo->v = pl_alloc((size_t)(m + 1) * sizeof *lo->v);
        lo->raw = pl_alloc((size_t)(m + 1) * sizeof *lo->raw);
        for (int j = 0; j < m; j++) {
            const JsonValue *o = json_at(objs, j);
            LevelObject v;
            v.px = (int)json_int(o, "x", 0);
            v.py = (int)json_int(o, "y", 0);
            v.x = pydiv(v.px, 16);
            v.y = pydiv(v.py, 16);
            v.object = (int)json_int(o, "object", 0);
            v.interaction = (int)json_int(o, "interaction", 0);
            v.index = (int)json_int(o, "index", 0);
            lo->raw[lo->raw_n++] = v;
            /* a dict by cell: the first one's place, the last one's
             * value */
            int at = -1;
            for (int k = 0; k < lo->n; k++) {
                if (lo->v[k].x == v.x && lo->v[k].y == v.y) {
                    at = k;
                }
            }
            if (at >= 0) {
                lo->v[at] = v;
            } else {
                lo->v[lo->n++] = v;
            }
        }
    }
    json_free(root);
}

int level_objects(int level, const LevelObject **out)
{
    objects_load();
    for (int k = 0; k < g_objs_n; k++) {
        if (g_objs[k].level == level) {
            *out = g_objs[k].v;
            return g_objs[k].n;
        }
    }
    *out = NULL;
    return 0;
}

static const LevelObject *object_at(int level, int x, int y)
{
    const LevelObject *v;
    int n = level_objects(level, &v);
    for (int i = 0; i < n; i++) {
        if (v[i].x == x && v[i].y == y) {
            return &v[i];
        }
    }
    return NULL;
}

/* loc_7278's rows (npc.c's BOX): left, width, top, height of the box an
 * object's $2C names, from its x, y. Obj_InvisibleBlock ($37) sets $2C to
 * 9 and Obj_InvisibleBlock2 ($38) to 7; a person, chest or door is row
 * 4, one cell. */
const int BOX_ROWS[14][4] = {
    { -8, 15, -24, 15 }, { -8, 15, 8, 15 }, { -24, 15, -8, 15 },
    { 8, 15, -8, 15 }, { -8, 15, -8, 15 }, { -16, 31, -16, 31 },
    { -16, 31, -8, 8 }, { -16, 31, 0, 15 }, { -8, 63, -48, 95 },
    { -32, 63, -64, 63 }, { -88, 176, -24, 40 }, { -32, 56, -8, 16 },
    { -16, 31, -8, 7 }, { -16, 31, -8, 15 }
};

static bool is_block(int obj)
{
    return obj == 0x37 || obj == 0x38;
}

/* object_cells: the cells an object's box covers, from its pixel x, y,
 * in (x, y) order. */
static void object_cells(int obj, int x, int y, CSet *out)
{
    const int *r = BOX_ROWS[(obj == 0x37) ? 9 : (obj == 0x38) ? 7 : 4];
    int l = r[0], w = r[1], t = r[2], h = r[3];
    cset_clear(out);
    for (int cx = pydiv(x + l, 16); cx <= pydiv(x + l + w, 16); cx++) {
        for (int cy = pydiv(y + t, 16); cy <= pydiv(y + t + h, 16); cy++) {
            cset_add(out, cx, cy, 0);
        }
    }
}

bool event_block(int level, int index, CSet *out)
{
    objects_load();
    for (int k = 0; k < g_objs_n; k++) {
        if (g_objs[k].level != level) {
            continue;
        }
        for (int i = 0; i < g_objs[k].raw_n; i++) {
            const LevelObject *o = &g_objs[k].raw[i];
            if (is_block(o->object) && o->index == index) {
                object_cells(o->object, o->px, o->py, out);
                return true;
            }
        }
    }
    return false;
}

void facing_cells(int level, const CSet *cells, CSet *out)
{
    static const struct {
        int side, dx, dy;
    } D[4] = {
        { SIDE_UP, 0, 1 }, { SIDE_DOWN, 0, -1 }, { SIDE_LEFT, 1, 0 },
        { SIDE_RIGHT, -1, 0 }
    };
    const LevelMap *lm = level_map(level);
    cset_clear(out);
    for (int i = 0; i < cells->n; i++) {
        Cell c = cset_cell(cells, i);
        for (int k = 0; k < 4; k++) {
            int nx = pymod(c.x + D[k].dx, lm->w);
            int ny = pymod(c.y + D[k].dy, lm->h);
            if (!cset_has(cells, nx, ny) && lm_open(lm, nx, ny)
                && !cset_has(out, nx, ny)) {
                cset_add(out, nx, ny, D[k].side);
            }
        }
    }
}

bool search_event(Session *s, int level, int index, Report *log,
                  Ints *shown)
{
    char nb[32];
    CSet box = { 0 }, cells = { 0 };
    if (!event_block(level, index, &box) || box.n == 0) {
        say(log, "no event block on %s (index %d)",
            level_name(level, nb, sizeof nb), index);
        cset_free(&box);
        return false;
    }
    Cell low = cset_cell(&box, 0);              /* min(box) */
    for (int i = 1; i < box.n; i++) {
        Cell c = cset_cell(&box, i);
        if (c.x < low.x || (c.x == low.x && c.y < low.y)) {
            low = c;
        }
    }
    facing_cells(level, &box, &cells);
    bool ok = search(s, level, low.x, low.y, log, false, &cells, shown);
    cset_free(&cells);
    cset_free(&box);
    return ok;
}

int chests(int level, Cells *out)
{
    const LevelObject *v;
    int n = level_objects(level, &v);
    out->n = 0;
    for (int i = 0; i < n; i++) {
        if (v[i].object == 57 || v[i].object == 58) {
            cells_add(out, v[i].x, v[i].y);
        }
    }
    return out->n;
}

static void chest_items_load(void)
{
    if (g_chest_item != NULL) {
        return;
    }
    JsonValue *root = load_json("assets/data/chests.json");
    const JsonValue *arr = json_get(root, "contents");
    int n = json_length(arr);
    int top = 0;
    for (int i = 0; i < n; i++) {
        int index = (int)json_int(json_at(arr, i), "index", 0);
        if (index + 1 > top) {
            top = index + 1;
        }
    }
    g_chest_n = top;
    g_chest_item = pl_alloc((size_t)(top + 1) * sizeof *g_chest_item);
    for (int i = 0; i < n; i++) {
        const JsonValue *c = json_at(arr, i);
        int index = (int)json_int(c, "index", 0);
        if (index >= 0) {
            /* {index: item}: the last of an index; None without one */
            snprintf(g_chest_item[index], 24, "%.23s",
                     json_str(c, "item", ""));
        }
    }
    json_free(root);
}

const char *chest_item(int level, int x, int y)
{
    chest_items_load();
    const LevelObject *o = object_at(level, x, y);
    if (o == NULL || (o->object != 57 && o->object != 58)
        || o->interaction != 2) {
        return NULL;
    }
    if (o->index < 0 || o->index >= g_chest_n
        || g_chest_item[o->index][0] == '\0') {
        return NULL;
    }
    return g_chest_item[o->index];
}

static int hexval(char c)
{
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

/* flag_byte: one byte of the $C700 block, read with `expect flag ADDR
 * 0` -- no FAIL is 0; a FAIL's last " is $XX" says what it is
 * (re.search(r"FAIL.* is \$([0-9A-F]{2})")). */
int flag_byte(Session *s, int addr)
{
    char cmd[32];
    snprintf(cmd, sizeof cmd, "expect flag $%04X 0", addr);
    const Lines *out = send1(s, cmd);
    for (int i = 0; i < out->n; i++) {
        const char *ln = out->v[i];
        const char *fail = strstr(ln, "FAIL");
        if (fail == NULL) {
            continue;
        }
        int best = -1;
        for (const char *p = fail + 4; (p = strstr(p, " is $")) != NULL;
             p++) {
            int hi = hexval(p[5]), lo = (hi >= 0) ? hexval(p[6]) : -1;
            if (hi >= 0 && lo >= 0) {
                best = hi * 16 + lo;
            }
        }
        if (best >= 0) {
            return best;
        }
    }
    return 0;
}

bool chest_shut(Session *s, int level, int x, int y)
{
    const LevelObject *o = object_at(level, x, y);
    if (o == NULL || (o->object != 57 && o->object != 58)
        || o->interaction != 2 || o->index == 0) {
        return false;
    }
    int b = flag_byte(s, 0xC700 | ((0x80 + o->index) & 0xFF));
    return b == 0x00 || b == 0x80;
}

/* ---- make_room ---- */

/* What goes when a chest's find has no room ($1704): the least of what a
 * bag commonly holds, first. */
static const char *const TOSS_FIRST[7] = {
    "Monomate", "Dimate", "Antidote", "Telepipe", "Escapipe", "Hidapipe",
    "Trimate"
};

const char *make_room(Session *s, Report *log, int who)
{
    static State snap;
    snap = s->state;                    /* party: before the bags */
    static Bags b;
    bags(s, &b);
    int lim = (b.n < snap.party_n) ? b.n : snap.party_n;
    int order[PL_PARTY_MAX], no = 0;
    for (int h = 0; h < lim; h++) {
        if (!snap.party[h].dead && (who == PL_NONE || h == who)) {
            /* sorted by -len(b[h]), stable */
            int j = no++;
            for (; j > 0 && b.m[order[j - 1]].n < b.m[h].n; j--) {
                order[j] = order[j - 1];
            }
            order[j] = h;
        }
    }
    for (int k = 0; k < 7; k++) {
        const char *name = TOSS_FIRST[k];
        for (int q = 0; q < no; q++) {
            int h = order[q];
            int slot = -1;
            for (int i = b.m[h].n - 1; i >= 0 && slot < 0; i--) {
                if (streq(b.m[h].v[i].name, name) && !b.m[h].v[i].worn) {
                    slot = i;
                }
            }
            if (slot >= 0) {
                Ints shown = { 0 };
                char t[2048];
                toss_item(s, h, slot, &shown);
                say(log, "room: %s tosses %s: %s", snap.party[h].name, name,
                    scripts_text(&shown, t, sizeof t));
                ints_free(&shown);
                return name;
            }
        }
    }
    return NULL;
}

/* ---- (build 173) someone who wanders: `objects`, walk up, face, A ---- */

int objects(Session *s, ObjectSlot *out, int cap)
{
    const Lines *lines = send1(s, "objects");
    for (int i = 0; i < lines->n; i++) {
        const char *p = ok_word(lines->v[i], "objects");
        if (p == NULL) {
            continue;
        }
        /* OBJ_RE.findall: (\d+):\$(\w+):(\d+)/\$(\w+):(\d+),(\d+)(m?) */
        int n = 0;
        char *copy = pl_strdup(p);
        for (char *tok = strtok(copy, " "); tok != NULL && n < cap;
             tok = strtok(NULL, " ")) {
            ObjectSlot o;
            unsigned obj = 0, index = 0;
            int used = 0;
            if (sscanf(tok, "%d:$%x:%d/$%x:%d,%d%n", &o.slot, &obj, &o.type,
                       &index, &o.x, &o.y, &used) != 6
                || o.slot < 0 || o.type < 0 || o.x < 0 || o.y < 0) {
                continue;
            }
            o.object = (int)obj;
            o.index = (int)index;
            o.moving = tok[used] == 'm';
            out[n++] = o;
        }
        free(copy);
        return n;
    }
    return 0;
}

/* person_name: the townsperson whose dialogue number is `index` on
 * `level`, as the kind of person and the cell they are placed at. */
const char *person_name(int level, int index, char *buf, size_t n)
{
    static JsonValue *kinds;
    char nb[32];
    if (kinds == NULL) {
        kinds = load_json("assets/data/npc_types.json");
    }
    objects_load();
    for (int k = 0; k < g_objs_n; k++) {
        if (g_objs[k].level != level) {
            continue;
        }
        for (int i = 0; i < g_objs[k].raw_n; i++) {
            const LevelObject *o = &g_objs[k].raw[i];
            if (o->interaction != 1 || o->index != index) {
                continue;
            }
            const char *kind = "someone";
            for (int t = 0; t < json_length(kinds); t++) {
                const JsonValue *e = json_at(kinds, t);
                if (json_int(e, "object", -1) == o->object) {
                    kind = json_str(e, "name", "someone");
                }
            }
            snprintf(buf, n, "%s at %d,%d", kind, o->x, o->y);
            return buf;
        }
    }
    snprintf(buf, n, "the one with dialogue %d on %s", index,
             level_name(level, nb, sizeof nb));
    return buf;
}

static int person_at(const ObjectSlot *v, int n, int index)
{
    for (int i = 0; i < n; i++) {
        if (v[i].type == 1 && v[i].index == index) {
            return i;
        }
    }
    return -1;
}

bool talk_to(Session *s, int index, const int *want, int nwant, int tries,
             Report *log, Ints *shown)
{
    static ObjectSlot who[128], now[128];
    for (int t = 0; t < tries; t++) {
        if (!ready(s)) {
            return false;
        }
        int nw = objects(s, who, 128);
        int w = person_at(who, nw, index);
        if (w < 0) {
            char pn[128];
            say(log, "talk_to: %s is not here",
                person_name(s->state.level, index, pn, sizeof pn));
            return false;
        }
        if (who[w].moving) {
            send1(s, "wait ticks 16");
            continue;
        }
        int level = s->state.level;
        CSet cells = { 0 };
        neighbours(level, who[w].x, who[w].y, &cells);
        if (!cset_has(&cells, s->state.x, s->state.y)) {
            if (!goto_cells(s, &cells)) {
                cset_free(&cells);
                continue;
            }
            if (!ready(s)) {
                cset_free(&cells);
                return false;
            }
            if (!cset_has(&cells, s->state.x, s->state.y)) {
                cset_free(&cells);
                continue;
            }
        }
        /* still there? */
        int nn = objects(s, now, 128);
        int k = person_at(now, nn, index);
        if (k < 0 || now[k].x != who[w].x || now[k].y != who[w].y
            || now[k].moving) {
            cset_free(&cells);
            continue;
        }
        char cmd[16];
        snprintf(cmd, sizeof cmd, "press %s",
                 side_name(cset_get(&cells, s->state.x, s->state.y,
                                    SIDE_NONE)));
        cset_free(&cells);
        send2(s, cmd, "wait idle");
        if (!st_waiting(&s->state) || !st_wait(&s->state, "control")) {
            ready(s);
        }
        Ints got = { 0 };
        talk(s, 6, &got);
        bool hit = got.n > 0 && want == NULL;
        for (int i = 0; i < got.n && want != NULL && !hit; i++) {
            for (int j = 0; j < nwant; j++) {
                hit = hit || got.v[i] == want[j];
            }
        }
        if (hit) {
            for (int i = 0; i < got.n && shown != NULL; i++) {
                ints_add(shown, got.v[i]);
            }
            ints_free(&got);
            return true;
        }
        ints_free(&got);
    }
    return false;
}
