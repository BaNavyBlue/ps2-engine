/* (build 173) play.py's jet scooter on Motavia (README, "The jet
 * scooter"): its own collision table (loc_29E16 -- open over water, solid
 * on land, the dam entrances and Uzo's solid and special), the probes of
 * ObjJetSctr_Main, a ride planned over the water, getting off on ground
 * that walks where the party is going, and getting back on. */

#include "pl.h"

#include <stdlib.h>

/* ---- ScooterMap ---- */

enum { D_UP, D_DOWN, D_LEFT, D_RIGHT };

static const char *const DIR_NAME[4] = { "up", "down", "left", "right" };

/* ObjJetSctr_Main's probes as cell offsets from the `state` cell (the
 * scooter's origin >> 4): the pair beyond the edge it is about to cross,
 * the first one's result deciding before the second is looked at. */
static const int PROBES[4][2][2] = {
    { { 0, -2 }, { -1, -2 } }, { { 0, 1 }, { -1, 1 } },
    { { -2, 0 }, { -2, -1 } }, { { 1, 0 }, { 1, -1 } }
};
static const int MOVES[4][2] = { { 0, -1 }, { 0, 1 }, { -1, 0 }, { 1, 0 } };
/* JetScooter_CharOffPosOffsets: where the leader stands after $171D. */
static const int DISMOUNT[4][2] = { { 0, -2 }, { 0, 1 }, { -2, 0 },
                                    { 1, 0 } };

typedef struct {
    LevelMap *lm;
    u8        attr[256];
    bool      loaded;
} ScooterMap;

static ScooterMap g_sm;

static ScooterMap *scooter_map(void)
{
    if (!g_sm.loaded) {
        g_sm.lm = level_map(MOTAVIA);
        char *text = pl_read_file("assets/levels/jet_scooter.collision.txt",
                                  NULL);
        if (text == NULL) {
            pl_error("cannot read assets/levels/jet_scooter.collision.txt");
        }
        for (char *ln = strtok(text, "\r\n"); ln != NULL;
             ln = strtok(NULL, "\r\n")) {
            char solid[8], special[8];
            unsigned b;
            if (ln[0] == '#'
                || sscanf(ln, "%x %7s %7s", &b, solid, special) != 3
                || b > 255) {
                continue;
            }
            u8 v = 0;
            for (int q = 0; q < 4; q++) {
                v |= (u8)((solid[q] == '#') ? (1 << q) : 0);
                v |= (u8)((special[q] == '#') ? (0x10 << q) : 0);
            }
            g_sm.attr[b] = v;
        }
        free(text);
        g_sm.loaded = true;
    }
    return &g_sm;
}

static Cell sm_wrap(const ScooterMap *sm, int x, int y)
{
    Cell c = { pymod(x, sm->lm->w), pymod(y, sm->lm->h) };
    return c;
}

static void sm_bits(const ScooterMap *sm, int x, int y, bool *solid,
                    bool *special)
{
    Cell c = sm_wrap(sm, x, y);
    int q = ((c.y & 1) << 1) + (c.x & 1);
    int b = lm_block(sm->lm, sm->lm->coll, c.x, c.y);
    if (b < 0 || b > 255) {
        pl_error("IndexError: list index out of range (scooter attr)");
    }
    u8 a = sm->attr[b];
    *solid = (a & (1 << q)) != 0;
    *special = (a & (0x10 << q)) != 0;
}

/* walk.c interact(): the first record within two cells down and right
 * of the probed cell. */
static const Door *sm_record(const ScooterMap *sm, int x, int y)
{
    Cell c = sm_wrap(sm, x, y);
    int n;
    const Door *d = level_doors(MOTAVIA, &n);
    for (int i = 0; i < n; i++) {
        if (c.y <= d[i].y && d[i].y <= c.y + 2 && c.x <= d[i].x
            && d[i].x <= c.x + 2) {
            return &d[i];
        }
    }
    return NULL;
}

/* step: ObjJetSctr_Main's one direction -- STEP_MOVE to *to, STEP_DOOR
 * through a record, or STEP_NONE when a probe is solid and nothing
 * matches (the scooter only turns). */
enum { STEP_NONE, STEP_MOVE, STEP_DOOR };

static int sm_step(const ScooterMap *sm, Cell c, int d, Cell *to,
                   const Door **rec)
{
    for (int k = 0; k < 2; k++) {
        int px = c.x + PROBES[d][k][0], py = c.y + PROBES[d][k][1];
        bool solid, special;
        sm_bits(sm, px, py, &solid, &special);
        if (solid) {
            if (special) {
                const Door *r = sm_record(sm, px, py);
                if (rec != NULL) {
                    *rec = r;
                }
                return (r != NULL) ? STEP_DOOR : STEP_NONE;
            }
            return STEP_NONE;
        }
    }
    if (to != NULL) {
        *to = sm_wrap(sm, c.x + MOVES[d][0], c.y + MOVES[d][1]);
    }
    return STEP_MOVE;
}

typedef bool (*RideGoal)(const ScooterMap *sm, Cell c, int d, void *ctx);

/* plan: the moves from `start` until goal(cell, direction) holds, then
 * that direction, into out (how many); -1 for None. BFS. */
static int sm_plan(const ScooterMap *sm, Cell start, RideGoal goal, void *ctx,
                   int *out, int cap)
{
    int w = sm->lm->w, h = sm->lm->h, total = w * h;
    int *prev = pl_alloc((size_t)total * sizeof *prev);     /* cell+1 */
    int *pdir = pl_alloc((size_t)total * sizeof *pdir);
    int *q = pl_alloc((size_t)total * sizeof *q);
    int head = 0, tail = 0, found = -1, fd = -1;
    int s0 = start.y * w + start.x;
    prev[s0] = -1;                      /* the start: no step before it */
    q[tail++] = s0;
    while (head < tail && found < 0) {
        int ci = q[head++];
        Cell c = { ci % w, ci / w };
        for (int d = 0; d < 4; d++) {
            if (goal(sm, c, d, ctx)) {
                found = ci;
                fd = d;
                break;
            }
        }
        if (found >= 0) {
            break;
        }
        for (int d = 0; d < 4; d++) {
            Cell to;
            if (sm_step(sm, c, d, &to, NULL) != STEP_MOVE) {
                continue;
            }
            int ti = to.y * w + to.x;
            if (ti == s0 || prev[ti] != 0) {
                continue;
            }
            prev[ti] = ci + 1;
            pdir[ti] = d;
            q[tail++] = ti;
        }
    }
    int n = -1;
    if (found >= 0) {
        int rev[4096], nr = 0;
        rev[nr++] = fd;
        for (int ci = found; ci != s0 && nr < 4096; ci = prev[ci] - 1) {
            rev[nr++] = pdir[ci];
        }
        n = 0;
        for (int i = nr - 1; i >= 0 && n < cap; i--) {
            out[n++] = rev[i];
        }
    }
    free(prev);
    free(pdir);
    free(q);
    return n;
}

/* ---- the ground on foot ---- */

void walk_region(int level, const CSet *cells, CSet *out)
{
    const LevelMap *lm = level_map(level);
    cset_clear(out);
    int total = lm->w * lm->h;
    int *q = pl_alloc((size_t)(total + cells->n + 1) * sizeof *q);
    int head = 0, tail = 0;
    for (int i = 0; i < cells->n; i++) {
        Cell c = cset_cell(cells, i);
        if (lm_open(lm, c.x, c.y) && cset_add(out, c.x, c.y, 0)) {
            q[tail++] = out->n - 1;
        }
    }
    static const int D4[4][2] = { { 0, -1 }, { 0, 1 }, { -1, 0 }, { 1, 0 } };
    while (head < tail) {
        Cell c = cset_cell(out, q[head++]);
        for (int d = 0; d < 4; d++) {
            int nx = pymod(c.x + D4[d][0], lm->w);
            int ny = pymod(c.y + D4[d][1], lm->h);
            if (cset_has(out, nx, ny) || !lm_open(lm, nx, ny)
                || lm_special_open(lm, nx, ny) || lm_town_edge(lm, nx, ny)) {
                continue;
            }
            cset_add(out, nx, ny, 0);
            q[tail++] = out->n - 1;
        }
    }
    free(q);
}

/* motavia_components: Motavia's ground in pieces connected on foot,
 * largest first. */
static CSet *g_comp;
static int   g_comp_n = -1;

static void motavia_components(void)
{
    if (g_comp_n >= 0) {
        return;
    }
    const LevelMap *lm = level_map(MOTAVIA);
    CSet seen = { 0 };
    int cap = 64;
    g_comp = pl_alloc((size_t)cap * sizeof *g_comp);
    g_comp_n = 0;
    for (int cy = 0; cy < lm->h; cy++) {
        for (int cx = 0; cx < lm->w; cx++) {
            if (cset_has(&seen, cx, cy) || !lm_open(lm, cx, cy)
                || lm_special_open(lm, cx, cy)) {
                continue;
            }
            CSet one = { 0 }, comp = { 0 };
            cset_add(&one, cx, cy, 0);
            walk_region(MOTAVIA, &one, &comp);
            cset_free(&one);
            cset_union(&seen, &comp);
            if (g_comp_n == cap) {
                cap *= 2;
                g_comp = pl_grow(g_comp, (size_t)cap * sizeof *g_comp);
            }
            g_comp[g_comp_n++] = comp;
        }
    }
    cset_free(&seen);
    /* out.sort(key=len, reverse=True): stable */
    for (int i = 1; i < g_comp_n; i++) {
        CSet t = g_comp[i];
        int j = i;
        for (; j > 0 && g_comp[j - 1].n < t.n; j--) {
            g_comp[j] = g_comp[j - 1];
        }
        g_comp[j] = t;
    }
}

void landing_region(int target_level, const CSet *goals, CSet *region)
{
    cset_clear(region);
    if (target_level == MOTAVIA) {
        walk_region(MOTAVIA, goals, region);
        return;
    }
    /* A door on Motavia straight into the target level first. */
    int n;
    const Door *d = level_doors(MOTAVIA, &n);
    for (int i = 0; i < n; i++) {
        if (!(d[i].has_to_level && d[i].to_level == target_level)) {
            continue;
        }
        CSet cells = { 0 }, part = { 0 };
        door_cells(MOTAVIA, d[i].y, d[i].x, &cells);
        if (cells.n > 0) {
            walk_region(MOTAVIA, &cells, &part);
            cset_union(region, &part);
        }
        cset_free(&cells);
        cset_free(&part);
    }
    if (region->n > 0) {
        return;
    }
    motavia_components();
    for (int k = 0; k < g_comp_n; k++) {
        const CSet *comp = &g_comp[k];
        if (comp->n < 4) {
            continue;
        }
        Cell rep = cset_cell(comp, 0);          /* min(comp) */
        for (int i = 1; i < comp->n; i++) {
            Cell c = cset_cell(comp, i);
            if (c.x < rep.x || (c.x == rep.x && c.y < rep.y)) {
                rep = c;
            }
        }
        Legs legs;
        if (plan(MOTAVIA, rep.x, rep.y, target_level, goals, &legs)) {
            legs_free(&legs);
            cset_union(region, comp);
        }
    }
}

/* ---- riding ---- */

/* ride: the scooter ridden until goal holds for the cell it is on and
 * the direction about to be pressed, fighting on the way and planning
 * again after each fight. The last direction (pressed by the caller), or
 * -1 for None. */
static int ride(Session *s, RideGoal goal, void *ctx, Report *log)
{
    char buf[2048], c[32];
    ScooterMap *sm = scooter_map();
    static int moves[4096];
    for (int t = 0; t < 40; t++) {
        if (!ready(s)) {
            return -1;
        }
        const State *st = &s->state;
        if (st->level != MOTAVIA || !st->riding) {
            say(log, "ride: not riding on Motavia: %s",
                state_repr(st, buf, sizeof buf));
            return -1;
        }
        Cell cur = { st->x, st->y };
        int n = sm_plan(sm, cur, goal, ctx, moves, 4096);
        if (n < 0) {
            say(log, "ride: no way on the water from %s",
                xy_repr(cur.x, cur.y, c, sizeof c));
            return -1;
        }
        int last = moves[--n];
        if (n == 0) {
            return last;
        }
        Cells path = { 0 };
        Cell at = cur;
        for (int i = 0; i < n; i++) {
            at = sm_wrap(sm, at.x + MOVES[moves[i]][0],
                         at.y + MOVES[moves[i]][1]);
            cells_add(&path, at.x, at.y);
        }
        static Run runs[4096];
        int nr = runs_of(cur.x, cur.y, &path, sm->lm->w, sm->lm->h, runs);
        cells_free(&path);
        for (int r = 0; r < nr; r++) {
            sendf(s, "walk %s %d", runs[r].d, runs[r].n);
            if (!st_waiting(&s->state)) {
                send1(s, "wait idle");
            }
            if (st_screen(&s->state, "battle")) {
                FightOpts fo = fight_defaults();
                Ints shown = { 0 };
                fight_smart(s, &fo, &shown);
                ints_free(&shown);
                if (needs_healing(s, 0.5) && st_screen(&s->state, "level")) {
                    mend(s, 0.6);
                }
                break;
            }
            if (!st_waiting(&s->state) || !st_wait(&s->state, "control")) {
                break;
            }
        }
    }
    say(log, "ride: gave up");
    return -1;
}

typedef struct {
    const CSet     *region;
    const LevelMap *lm;
} AshoreCtx;

/* The press has to turn, not move, and the leader land in the region. */
static bool ashore_goal(const ScooterMap *sm, Cell c, int d, void *ctx)
{
    const AshoreCtx *a = ctx;
    if (sm_step(sm, c, d, NULL, NULL) != STEP_NONE) {
        return false;
    }
    Cell cell = sm_wrap(sm, c.x + DISMOUNT[d][0], c.y + DISMOUNT[d][1]);
    return cset_has(a->region, cell.x, cell.y)
        && lm_open(a->lm, cell.x, cell.y);
}

bool ride_ashore(Session *s, const CSet *targets, const CSet *region,
                 Report *log)
{
    char t[2048], c[32];
    CSet own = { 0 };
    if (region == NULL) {
        walk_region(MOTAVIA, targets, &own);
        region = &own;
    }
    AshoreCtx a = { region, level_map(MOTAVIA) };
    int d = ride(s, ashore_goal, &a, log);
    cset_free(&own);
    if (d < 0) {
        return false;
    }
    s->scooter_at.x = s->state.x;               /* parked where it stops */
    s->scooter_at.y = s->state.y;
    s->scooter_known = true;
    char cmd[16];
    snprintf(cmd, sizeof cmd, "press %s", DIR_NAME[d]);
    send2(s, cmd, "wait idle");
    Ints shown = { 0 };
    talk(s, 6, &shown);
    send1(s, "wait idle");
    ready(s);
    say(log, "ashore: %s %s", scripts_text(&shown, t, sizeof t),
        xy_repr(s->state.x, s->state.y, c, sizeof c));
    ints_free(&shown);
    return !s->state.riding;
}

typedef struct {
    const CSet *cells;
} FaceCtx;

/* The leader's A box (loc_7278's facing row, from the scooter's corner)
 * overlapping an object's box on `cells`, with a press that only turns. */
static bool face_goal(const ScooterMap *sm, Cell c, int d, void *ctx)
{
    const FaceCtx *f = ctx;
    if (sm_step(sm, c, d, NULL, NULL) != STEP_NONE) {
        return false;
    }
    const int *r = BOX_ROWS[d];         /* up, down, left, right: rows 0-3 */
    int x0 = c.x * 16 + r[0], y0 = c.y * 16 + r[2];
    for (int i = 0; i < f->cells->n; i++) {
        Cell o = cset_cell(f->cells, i);
        int ox = o.x * 16, oy = o.y * 16;
        if (ox <= x0 + r[1] && ox + 15 >= x0 && oy <= y0 + r[3]
            && oy + 15 >= y0) {
            return true;
        }
    }
    return false;
}

bool ride_to_face(Session *s, const CSet *cells, Report *log)
{
    FaceCtx f = { cells };
    int d = ride(s, face_goal, &f, log);
    if (d < 0) {
        return false;
    }
    char cmd[16];
    snprintf(cmd, sizeof cmd, "press %s", DIR_NAME[d]);
    send2(s, cmd, "wait idle");
    return streq(s->state.facing, DIR_NAME[d]);
}

/* (build 174) A push through a door record into `*ctx`'s level. */
static bool door_goal(const ScooterMap *sm, Cell c, int d, void *ctx)
{
    const int *to_level = ctx;
    const Door *rec = NULL;
    return sm_step(sm, c, d, NULL, &rec) == STEP_DOOR && rec != NULL
        && rec->has_to_level && rec->to_level == *to_level;
}

bool ride_into(Session *s, int to_level, Report *log)
{
    int d = ride(s, door_goal, &to_level, log);
    if (s->state.level == to_level) {
        return true;                /* the ride's own last push went in */
    }
    if (d < 0) {
        return false;
    }
    sendf(s, "walk %s 1", DIR_NAME[d]);
    ready(s);
    return s->state.level == to_level;
}

bool board_scooter(Session *s, Report *log)
{
    char nb[32], c[32], t[2048];
    if (!ready(s)) {
        return false;
    }
    if (s->state.riding) {
        return true;
    }
    /* Entering a town with the scooter owned parks it at that town's
     * place, so a cell remembered from before a town is stale: out onto
     * Motavia and read the object instead. */
    bool known = false;
    Cell where = { 0, 0 };
    if (s->state.level != MOTAVIA) {
        int tx, ty;
        bool out = town_exit_to(s->state.level, &tx, &ty);
        if (out) {
            CSet goal = { 0 };
            cset_add(&goal, tx, ty, 0);
            out = travel(s, MOTAVIA, &goal, log);
            cset_free(&goal);
        }
        if (!out) {
            say(log, "board: could not get out onto Motavia from %s",
                level_name(s->state.level, nb, sizeof nb));
            return false;
        }
    }
    if (s->state.level == MOTAVIA) {
        static ObjectSlot ob[128];
        int n = objects(s, ob, 128);
        int k = -1;
        for (int i = 0; i < n && k < 0; i++) {
            if (ob[i].object == 0x22) {
                k = i;
            }
        }
        if (k >= 0) {
            where.x = ob[k].x;
            where.y = ob[k].y;
            known = true;
            s->scooter_at = where;
            s->scooter_known = true;
        } else if (s->scooter_known) {
            where = s->scooter_at;
            known = true;
        }
    }
    if (!known) {
        say(log, "board: where the scooter is parked is not known here");
        return false;
    }
    CSet box = { 0 }, cells = { 0 };
    cset_add(&box, where.x - 1, where.y - 1, 0);
    cset_add(&box, where.x, where.y - 1, 0);
    cset_add(&box, where.x - 1, where.y, 0);
    cset_add(&box, where.x, where.y, 0);
    facing_cells(MOTAVIA, &box, &cells);
    cset_free(&box);
    if (!travel(s, MOTAVIA, &cells, log)) {
        say(log, "board: could not reach the scooter from %s",
            xy_repr(s->state.x, s->state.y, c, sizeof c));
        cset_free(&cells);
        return false;
    }
    int side = cset_get(&cells, s->state.x, s->state.y, SIDE_NONE);
    cset_free(&cells);
    if (side == SIDE_NONE) {
        pl_error("KeyError: %s", xy_repr(s->state.x, s->state.y, c,
                                         sizeof c));
    }
    char cmd[16];
    snprintf(cmd, sizeof cmd, "press %s", side_name(side));
    send2(s, cmd, "wait idle");
    Ints shown = { 0 };
    talk(s, 6, &shown);
    /* The flag is set on the next idle visit and the scooter's init sees
     * it a frame later. */
    send2(s, "wait idle", "wait ticks 4");
    say(log, "board: %s %s", scripts_text(&shown, t, sizeof t),
        xy_repr(s->state.x, s->state.y, c, sizeof c));
    ints_free(&shown);
    return s->state.riding;
}
